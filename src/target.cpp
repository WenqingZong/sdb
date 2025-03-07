#include <csignal>
#include <libsdb/bit.hpp>
#include <libsdb/disassembler.hpp>
#include <libsdb/target.hpp>
#include <libsdb/types.hpp>
#include <optional>

namespace {
std::unique_ptr<sdb::elf> create_loaded_elf(const sdb::process& proc,
                                            const std::filesystem::path& path) {
    auto auxv = proc.get_auxv();
    auto obj = std::make_unique<sdb::elf>(path);
    obj->notify_loaded(
        sdb::virt_addr(auxv[AT_ENTRY] - obj->get_header().e_entry));
    return obj;
}
} // namespace

std::unique_ptr<sdb::target>
sdb::target::launch(std::filesystem::path path,
                    std::optional<int> stdout_replacement) {
    auto proc = process::launch(path, true, stdout_replacement);
    auto obj = create_loaded_elf(*proc, path);
    auto tgt =
        std::unique_ptr<target>(new target(std::move(proc), std::move(obj)));
    tgt->get_process().set_target(tgt.get());
    return tgt;
}

std::unique_ptr<sdb::target> sdb::target::attach(pid_t pid) {
    auto elf_path =
        std::filesystem::path("/proc") / std::to_string(pid) / "exe";
    auto proc = process::attach(pid);
    auto obj = create_loaded_elf(*proc, elf_path);
    auto tgt =
        std::unique_ptr<target>(new target(std::move(proc), std::move(obj)));
    tgt->get_process().set_target(tgt.get());
    return tgt;
}

sdb::file_addr sdb::target::get_pc_file_address() const {
    return process_->get_pc().to_file_addr(*elf_);
}

void sdb::target::notify_stop(const sdb::stop_reason& reason) {
    stack_.reset_inline_height();
}

sdb::stop_reason sdb::target::step_in() {
    auto& stack = get_stack();
    if (stack.inline_height() > 0) {
        stack.simulate_inlined_step_in();
        return stop_reason(process_state::stopped, SIGTRAP,
                           trap_type::single_step);
    }

    auto orig_line = line_entry_at_pc();
    do {
        auto reason = process_->step_instruction();
        if (!reason.is_step()) {
            return reason;
        }
    } while ((line_entry_at_pc() == orig_line or
              line_entry_at_pc()->end_sequence) and
             line_entry_at_pc() != line_table::iterator{});

    auto pc = get_pc_file_address();
    if (pc.elf_file() != nullptr) {
        auto& dwarf = pc.elf_file()->get_dwarf();
        auto func = dwarf.function_containing_address(pc);
        if (func and func->low_pc() == pc) {
            auto line = line_entry_at_pc();
            if (line != line_table::iterator{}) {
                line++;
                return run_until_address(line->address.to_virt_addr());
            }
        }
    }

    return stop_reason(process_state::stopped, SIGTRAP, trap_type::single_step);
}

sdb::line_table::iterator sdb::target::line_entry_at_pc() const {
    auto pc = get_pc_file_address();
    if (!pc.elf_file()) {
        return line_table::iterator();
    }
    auto cu = pc.elf_file()->get_dwarf().compile_unit_containing_address(pc);
    if (!cu) {
        return line_table::iterator();
    }
    return cu->lines().get_entry_by_address(pc);
}

sdb::stop_reason sdb::target::run_until_address(virt_addr address) {
    breakpoint_site* breakpoint_to_remove = nullptr;
    if (!process_->breakpoint_sites().contains_address(address)) {
        breakpoint_to_remove =
            &process_->create_breakpoint_site(address, false, true);
        breakpoint_to_remove->enable();
    }

    process_->resume();
    auto reason = process_->wait_on_signal();
    if (reason.is_breakpoint() and process_->get_pc() == address) {
        reason.trap_reason = trap_type::single_step;
    }

    if (breakpoint_to_remove) {
        process_->breakpoint_sites().remove_by_address(
            breakpoint_to_remove->address());
    }

    return reason;
}

sdb::stop_reason sdb::target::step_over() {
    auto orig_line = line_entry_at_pc();
    disassembler disas(*process_);
    sdb::stop_reason reason;
    auto& stack = get_stack();
    do {
        auto inline_stack = stack.inline_stack_at_pc();
        auto at_start_of_line_frame = stack.inline_height() > 0;
        if (at_start_of_line_frame) {
            auto frame_to_skip =
                inline_stack[inline_stack.size() - stack.inline_height()];
            auto return_address = frame_to_skip.high_pc().to_virt_addr();
            reason = run_until_address(return_address);
            if (!reason.is_step() or process_->get_pc() != return_address) {
                return reason;
            }
        } else if (auto instructions = disas.disassemble(2, process_->get_pc());
                   instructions[0].text.rfind("call") == 0) {
            reason = run_until_address(instructions[1].address);
            if (!reason.is_step() or
                process_->get_pc() != instructions[1].address) {
                return reason;
            }
        } else {
            reason = process_->step_instruction();
            if (!reason.is_step()) {
                return reason;
            }
        }
    } while ((line_entry_at_pc() == orig_line or
              line_entry_at_pc()->end_sequence) and
             line_entry_at_pc() != line_table::iterator{});
    return reason;
}

sdb::stop_reason sdb::target::step_out() {
    auto& stack = get_stack();
    auto inline_stack = stack.inline_stack_at_pc();
    auto has_inline_frames = inline_stack.size() > 1;
    auto at_inline_frame = stack.inline_height() < inline_stack.size() - 1;

    if (has_inline_frames and at_inline_frame) {
        auto current_frame =
            inline_stack[inline_stack.size() - stack.inline_height() - 1];
        auto return_address = current_frame.high_pc().to_virt_addr();
        return run_until_address(return_address);
    }

    auto frame_pointer = process_->get_registers().read_by_id_as<std::uint64_t>(
        register_id::rbp);

    auto return_address =
        process_->read_memory_as<std::uint64_t>(virt_addr{frame_pointer + 8});

    return run_until_address(virt_addr{return_address});
}

sdb::target::find_functions_result
sdb::target::find_functions(std::string name) const {
    find_functions_result result;

    auto dwarf_found = elf_->get_dwarf().find_functions(name);
    if (dwarf_found.empty()) {
        auto elf_found = elf_->get_symbols_by_name(name);
        for (auto sym : elf_found) {
            result.elf_functions.push_back(std::pair{elf_.get(), sym});
        }
    } else {
        result.dwarf_functions.insert(result.dwarf_functions.end(),
                                      dwarf_found.begin(), dwarf_found.end());
    }
    return result;
}
