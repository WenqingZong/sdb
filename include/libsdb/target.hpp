#ifndef SDB_TARGET_HPP
#define SDB_TARGET_HPP

#include <libsdb/dwarf.hpp>
#include <libsdb/elf.hpp>
#include <libsdb/process.hpp>
#include <libsdb/stack.hpp>
#include <memory>

namespace sdb {
class target {
  public:
    target() = delete;
    target(const target&) = delete;
    target& operator=(const target&) = delete;

    static std::unique_ptr<target>
    launch(std::filesystem::path path,
           std::optional<int> stdout_replacement = std::nullopt);
    static std::unique_ptr<target> attach(pid_t pid);

    process& get_process() { return *process_; }
    const process& get_process() const { return *process_; }
    elf& get_elf() { return *elf_; }
    const elf& get_elf() const { return *elf_; }

    void notify_stop(const sdb::stop_reason& reason);

    file_addr get_pc_file_address() const;

    stack& get_stack() { return stack_; }
    const stack& get_stack() const { return stack_; }

    sdb::stop_reason step_in();
    sdb::stop_reason step_out();
    sdb::stop_reason step_over();

    sdb::line_table::iterator line_entry_at_pc() const;
    sdb::stop_reason run_until_address(virt_addr address);

  private:
    target(std::unique_ptr<process> proc, std::unique_ptr<elf> obj)
        : process_(std::move(proc)), elf_(std::move(obj)), stack_(this) {}
    std::unique_ptr<process> process_;
    std::unique_ptr<elf> elf_;

    stack stack_;
};
} // namespace sdb

#endif