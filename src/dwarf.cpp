#include <algorithm>
#include <libsdb/bit.hpp>
#include <libsdb/dwarf.hpp>
#include <libsdb/elf.hpp>
#include <libsdb/error.hpp>
#include <libsdb/types.hpp>
#include <string_view>

namespace {
class cursor {
  public:
    // 在C++中，explicit
    // 关键字用于修饰类的构造函数，表示该构造函数不能用于隐式转换，只能用于显式地创建对象。它的主要作用是防止编译器进行意外
    // 的隐式类型转换，从而避免潜在的歧义或错误。
    explicit cursor(sdb::span<const std::byte> data)
        : data_(data), pos_(data.begin()) {}
    cursor& operator++() {
        pos_++;
        return *this;
    }
    cursor& operator+=(std::size_t size) {
        pos_ += size;
        return *this;
    }
    const std::byte* position() const { return pos_; }
    bool finished() const { return pos_ >= data_.end(); }

    template <class T> T fixed_int() {
        auto t = sdb::from_bytes<T>(pos_);
        pos_ += sizeof(T);
        return t;
    }
    std::uint8_t u8() { return fixed_int<std::uint8_t>(); }
    std::uint16_t u16() { return fixed_int<std::uint16_t>(); }
    std::uint32_t u32() { return fixed_int<std::uint32_t>(); }
    std::uint64_t u64() { return fixed_int<std::uint64_t>(); }
    std::int8_t s8() { return fixed_int<std::int8_t>(); }
    std::int16_t s16() { return fixed_int<std::int16_t>(); }
    std::int32_t s32() { return fixed_int<std::int32_t>(); }
    std::int64_t s64() { return fixed_int<std::int64_t>(); }

    std::string_view string() {
        auto null_terminator = std::find(pos_, data_.end(), std::byte{0});
        std::string_view ret(reinterpret_cast<const char*>(pos_),
                             null_terminator - pos_);
        pos_ = null_terminator + 1;
        return ret;
    }

    std::uint64_t uleb128() {
        std::uint64_t res = 0;
        int shift = 0;
        std::uint8_t byte = 0;
        do {
            byte = u8();
            auto masked = static_cast<uint64_t>(byte & 0x7f);
            res |= masked << shift;
            shift += 7;
        } while ((byte & 0x80) != 0);
        return res;
    }

    std::int64_t sleb128() {
        std::uint64_t res = 0;
        int shift = 0;
        std::uint8_t byte = 0;
        do {
            byte = u8();
            auto masked = static_cast<uint64_t>(byte & 0x7f);
            res |= masked << shift;
            shift += 7;
        } while ((byte & 0x80) != 0);
        if ((shift < sizeof(res) * 8) and (byte & 0x40)) {
            res |= (~static_cast<std::uint64_t>(0) << shift);
        }
        return res;
    }

  private:
    sdb::span<const std::byte> data_;
    const std::byte* pos_;
};

std::unordered_map<std::uint64_t, sdb::abbrev>
parse_abbrev_table(const sdb::elf& obj, std::size_t offset) {
    cursor cur(obj.get_section_contents(".debug_abbrev"));
    cur += offset;

    std::unordered_map<std::uint64_t, sdb::abbrev> table;
    std::uint64_t code = 0;
    do {
        code = cur.uleb128();
        auto tag = cur.uleb128();
        auto has_children = static_cast<bool>(cur.u8());

        std::vector<sdb::attr_spec> attr_specs;
        std::uint64_t attr = 0;
        do {
            attr = cur.uleb128();
            auto form = cur.uleb128();
            if (attr != 0) {
                attr_specs.push_back(sdb::attr_spec{attr, form});
            }
        } while (attr != 0);

        if (code != 0) {
            table.emplace(code, sdb::abbrev{code, tag, has_children,
                                            std::move(attr_specs)});
        }
    } while (code != 0);

    return table;
}

std::unique_ptr<sdb::compile_unit>
parse_compile_unit(sdb::dwarf& dwarf, const sdb::elf& obj, cursor cur) {
    auto start = cur.position();
    auto size = cur.u32();
    auto version = cur.u16();
    auto abbrev = cur.u32();
    auto address_size = cur.u8();
    if (size == 0xffffffff) {
        sdb::error::send("Only DWARF32 is supported");
    }
    if (version != 4) {
        sdb::error::send("Only DWARF version 4 is supported");
    }
    if (address_size != 8) {
        sdb::error::send("Invalid address size for DWARF");
    }
    size += sizeof(std::uint32_t);
    sdb::span<const std::byte> data = {start, size};
    return std::make_unique<sdb::compile_unit>(dwarf, data, abbrev);
}

std::vector<std::unique_ptr<sdb::compile_unit>>
parse_compile_units(sdb::dwarf& dwarf, const sdb::elf& obj) {
    auto debug_info = obj.get_section_contents(".debug_info");
    cursor cur(debug_info);

    std::vector<std::unique_ptr<sdb::compile_unit>> units;

    while (!cur.finished()) {
        auto unit = parse_compile_unit(dwarf, obj, cur);
        cur += unit->data().size();
        units.push_back(std::move(unit));
    }

    return units;
}

} // namespace

sdb::dwarf::dwarf(const sdb::elf& parent) : elf_(&parent) {
    compile_units_ = parse_compile_units(*this, parent);
}

const std::unordered_map<std::uint64_t, sdb::abbrev>&
sdb::dwarf::get_abbrev_table(std::size_t offset) {
    if (!abbrev_tables_.count(offset)) {
        abbrev_tables_.emplace(offset, parse_abbrev_table(*elf_, offset));
    }
    return abbrev_tables_.at(offset);
}

const std::unordered_map<std::uint64_t, sdb::abbrev>&
sdb::compile_unit::abbrev_table() const {
    return parent_->get_abbrev_table(abbrev_offset_);
}
