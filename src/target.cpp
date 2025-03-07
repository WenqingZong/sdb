#include <libsdb/target.hpp>
#include <libsdb/types.hpp>

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
