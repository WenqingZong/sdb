#ifndef SDB_PROCESS_HPP
#define SDB_PROCESS_HPP

#include <cstdint>
#include <filesystem>
#include <libsdb/registers.hpp>
#include <libsdb/types.hpp>
#include <memory>
#include <optional>
#include <sys/types.h>

namespace sdb {
enum class process_state { stopped, running, exited, terminated };

struct stop_reason {
    stop_reason(int wait_status);

    process_state reason;
    std::uint8_t info;
};

class process {
  public:
    void write_fprs(const user_fpregs_struct& fprs);
    void write_gprs(const user_regs_struct& gprs);
    registers& get_registers() { return *registers_; }
    const registers& get_registers() const { return *registers_; }
    void write_user_area(std::size_t offset, std::uint64_t data);
    static std::unique_ptr<process>
    launch(std::filesystem::path path, bool debug = true,
           std::optional<int> stdout_replacement = std::nullopt);
    static std::unique_ptr<process> attach(pid_t pid);

    void resume();
    stop_reason wait_on_signal();
    ~process();

    pid_t pid() const { return pid_; }
    process_state state() const { return state_; }

    virt_addr get_pc() const {
        return virt_addr{
            get_registers().read_by_id_as<std::uint64_t>(register_id::rip)};
    }

  private:
    pid_t pid_ = 0;
    bool terminate_on_end_ = true;
    bool is_attached_ = true;

    process_state state_ = process_state::stopped;
    process(pid_t pid, bool terminate_on_end, bool is_attached)
        : pid_(pid), terminate_on_end_(terminate_on_end),
          is_attached_(is_attached), registers_(new registers(*this)) {}

    void read_all_registers();
    process() = delete;
    process(const process&) = delete;
    process& operator=(const process&) = delete;
    std::unique_ptr<registers> registers_;
};
} // namespace sdb

#endif
