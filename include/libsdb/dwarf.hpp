#ifndef SDB_DWARF_HPP
#define SDB_DWARF_HPP

#include <cstdint>
#include <libsdb/detail/dwarf.h>
#include <memory>
#include <optional>
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

class compile_unit;
class die;
class elf;

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
    die root() const;

  private:
    dwarf* parent_;
    span<const std::byte> data_;
    std::size_t abbrev_offset_;
};

class die {

  public:
    explicit die(const std::byte* next) : next_(next) {}
    die(const std::byte* pos, const compile_unit* cu, const abbrev* abbrev,
        std::vector<const std::byte*> attr_locs, const std::byte* next)
        : pos_(pos), cu_(cu), abbrev_(abbrev), attr_locs_(std::move(attr_locs)),
          next_(next) {}
    const compile_unit* cu() const { return cu_; }
    const abbrev* abbrev_entry() const { return abbrev_; }
    const std::byte* position() const { return pos_; }
    const std::byte* next() const { return next_; }

    class children_range;
    children_range children() const;

  private:
    const std::byte* pos_ = nullptr;
    const compile_unit* cu_ = nullptr;
    const abbrev* abbrev_ = nullptr;
    const std::byte* next_ = nullptr;
    std::vector<const std::byte*> attr_locs_;
};

class die::children_range {

  public:
    children_range(die die) : die_(std::move(die)) {}

    class iterator {
      public:
        using value_type = die;
        using reference = const die&;
        using pointer = const die*;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;
        iterator() = default;
        iterator(const iterator&) = default;
        iterator& operator=(const iterator&) = default;
        explicit iterator(const die& die);
        const die& operator*() const { return *die_; }
        const die* operator->() const { return &die_.value(); }
        iterator& operator++();
        iterator operator++(int);
        bool operator==(const iterator& rhs) const;
        bool operator!=(const iterator& rhs) const { return !(*this == rhs); }

      private:
        std::optional<die> die_;
    };

    iterator begin() const {
        if (die_.abbrev_->has_children) {
            return iterator{die_};
        }
        return end();
    }
    iterator end() const { return iterator{}; }

  private:
    die die_;
};

} // namespace sdb

#endif
