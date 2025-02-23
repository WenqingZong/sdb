#ifndef SDB_DWARF_HPP
#define SDB_DWARF_HPP

#include <cstdint>
#include <libsdb/detail/dwarf.h>
#include <memory>
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
class compile_unit;

class dwarf {
  public:
    dwarf(const elf& parent);
    const elf* elf_file() const { return elf_; }

    const std::unordered_map<std::uint64_t, abbrev>&
    get_abbrev_table(std::size_t offset);

    const std::vector<std::unique_ptr<compile_unit>>& compile_units() const {
        return compile_units_;
    }

  private:
    const elf* elf_;
    std::unordered_map<std::size_t, std::unordered_map<std::uint64_t, abbrev>>
        abbrev_tables_;
    std::vector<std::unique_ptr<compile_unit>> compile_units_;
};

class compile_unit {
  public:
    compile_unit(dwarf& parent, span<const std::byte> data,
                 std::size_t abbrev_offset)
        : parent_(&parent), data_(data), abbrev_offset_(abbrev_offset) {}
    const dwarf* dwarf_info() const { return parent_; }
    span<const std::byte> data() const { return data_; }
    const std::unordered_map<std::uint64_t, sdb::abbrev>& abbrev_table() const;

  private:
    dwarf* parent_;
    span<const std::byte> data_;
    std::size_t abbrev_offset_;
};

} // namespace sdb

#endif
