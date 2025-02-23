#ifndef SDB_DWARF_HPP
#define SDB_DWARF_HPP

#include <cstdint>
#include <libsdb/detail/dwarf.h>
#include <unordered_map>
#include <vector>

namespace sdb {

struct attr_spec {
    std::uint64_t attr;
    std::uint64_t form;
};

struct abbrev {
    std::uint64_t code;
    std::uint64_t tag;
    bool has_children;
    std::vector<attr_spec> attr_specs;
};

class elf;
class dwarf {
  public:
    dwarf(const elf& parent);
    const elf* elf_file() const { return elf_; }

    const std::unordered_map<std::uint64_t, abbrev>&
    get_abbrev_table(std::size_t offset);

  private:
    const elf* elf_;
    std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, abbrev>>
        abbrev_tables_;
};
} // namespace sdb

#endif
