#ifndef SDB_ELF_HPP
#define SDB_ELF_HPP

#include <elf.h>
#include <filesystem>
#include <libsdb/types.hpp>
#include <optional>
#include <unordered_map>
#include <vector>

namespace sdb {

class elf {
  public:
    elf(const std::filesystem::path& path);
    ~elf();

    // Because elf objects should be unique
    elf(const elf&) = delete;
    elf& operator=(const elf&) = delete;

    std::filesystem::path path() const { return path_; }
    const Elf64_Ehdr& get_header() const { return header_; }

    std::string_view get_section_name(std::size_t index) const;

    std::optional<const Elf64_Shdr*> get_section(std::string_view name) const;
    span<const std::byte> get_section_contents(std::string_view name) const;

    std::string_view get_string(std::size_t index) const;

    virt_addr load_bias() const { return load_bias_; }
    void notify_loaded(virt_addr address) { load_bias_ = address; }

    const Elf64_Shdr* get_section_containing_address(file_addr addr) const;
    const Elf64_Shdr* get_section_containing_address(virt_addr addr) const;

    std::optional<file_addr>
    get_section_start_address(std::string_view name) const;

  private:
    int fd_;
    std::filesystem::path path_;
    std::size_t file_size_;
    std::byte* data_;
    Elf64_Ehdr header_;

    void parse_section_headers();
    std::vector<Elf64_Shdr> section_headers_;

    void build_section_map();
    std::unordered_map<std::string_view, Elf64_Shdr*> section_map_;

    virt_addr load_bias_;

    void parse_symbol_table();

    std::vector<Elf64_Sym> symbol_table_;
};

} // namespace sdb

#endif