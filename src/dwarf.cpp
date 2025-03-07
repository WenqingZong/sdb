#include <algorithm>
#include <libsdb/bit.hpp>
#include <libsdb/dwarf.hpp>
#include <libsdb/elf.hpp>
#include <libsdb/error.hpp>
#include <libsdb/types.hpp>
#include <string_view>

namespace {

bool path_ends_in(const std::filesystem::path& lhs,
                  const std::filesystem::path& rhs) {
    auto lhs_size = std::distance(lhs.begin(), lhs.end());
    auto rhs_size = std::distance(rhs.begin(), rhs.end());
    if (rhs_size > lhs_size) {
        return false;
    }
    auto start = std::next(lhs.begin(), lhs_size - rhs_size);
    return std::equal(start, lhs.end(), rhs.begin());
}

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

    void skip_form(std::uint64_t form) {
        switch (form) {
        case DW_FORM_flag_present:
            break;
        case DW_FORM_data1:
        case DW_FORM_ref1:
        case DW_FORM_flag:
            pos_ += 1;
            break;
        case DW_FORM_data2:
        case DW_FORM_ref2:
            pos_ += 2;
            break;
        case DW_FORM_data4:
        case DW_FORM_ref4:
        case DW_FORM_ref_addr:
        case DW_FORM_sec_offset:
        case DW_FORM_strp:
            pos_ += 4;
            break;
        case DW_FORM_data8:
        case DW_FORM_addr:
            pos_ += 8;
            break;

        case DW_FORM_sdata:
            sleb128();
            break;
        case DW_FORM_udata:
        case DW_FORM_ref_udata:
            uleb128();
            break;

        case DW_FORM_block1:
            pos_ += u8();
            break;

        case DW_FORM_block2:
            pos_ += u16();
            break;
        case DW_FORM_block4:
            pos_ += u32();
            break;
        case DW_FORM_block:
        case DW_FORM_exprloc:
            pos_ += uleb128();
            break;

        case DW_FORM_string:
            while (!finished() && *pos_ != std::byte(0)) {
                pos_++;
            }
            pos_++;
            break;

        case DW_FORM_indirect:
            skip_form(uleb128());
            break;

        default:
            sdb::error::send("Unrecognized DWARF form");
        }
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

sdb::line_table::file parse_line_table_file(
    cursor& cur, std::filesystem::path compilation_dir,
    const std::vector<std::filesystem::path>& include_directories) {
    auto file = cur.string();
    auto dir_index = cur.uleb128();
    auto modification_time = cur.uleb128();
    auto file_length = cur.uleb128();

    std::filesystem::path path = file;
    if (file[0] != '/') {
        if (dir_index == 0) {
            path = compilation_dir / std::string(file);
        } else {
            path = include_directories[dir_index - 1] / std::string(file);
        }
    }
    return {path.string(), modification_time, file_length};
}

std::unique_ptr<sdb::line_table> parse_line_table(const sdb::compile_unit& cu) {
    auto section =
        cu.dwarf_info()->elf_file()->get_section_contents(".debug_line");
    if (!cu.root().contains(DW_AT_stmt_list)) {
        return nullptr;
    }
    auto offset = cu.root()[DW_AT_stmt_list].as_section_offset();
    cursor cur({section.begin() + offset, section.end()});

    auto size = cur.u32();
    auto end = cur.position() + size;

    auto version = cur.u16();
    if (version != 4) {
        sdb::error::send("Only DWARF 4 is supported");
    }

    // Cast to void to avoid compiler warnings.
    (void)cur.u32(); // Header length

    auto minimum_instruction_length = cur.u8();
    if (minimum_instruction_length != 1) {
        sdb::error::send("Invalid minimum instruction length");
    }
    auto maximum_operations_per_instruction = cur.u8();
    if (maximum_operations_per_instruction != 1) {
        sdb::error::send("Invalid maximum operations per instruction");
    }

    auto default_is_stmt = cur.u8();
    auto line_base = cur.s8();
    auto line_range = cur.u8();
    auto opcode_base = cur.u8();

    std::array<std::uint8_t, 12> expected_opcode_lengths{0, 1, 1, 1, 1, 0,
                                                         0, 0, 1, 0, 0, 1};
    for (auto i = 0; i < opcode_base - 1; i++) {
        if (cur.u8() != expected_opcode_lengths[i]) {
            sdb::error::send("Unexpected opcode length");
        }
    }

    std::vector<std::filesystem::path> include_directories;
    std::filesystem::path compilation_dir(
        cu.root()[DW_AT_comp_dir].as_string());
    for (auto dir = cur.string(); !dir.empty(); dir = cur.string()) {
        if (dir[0] == '/') {
            include_directories.push_back(std::string(dir));
        } else {
            include_directories.push_back(compilation_dir / std::string(dir));
        }
    }

    std::vector<sdb::line_table::file> file_names;
    while (*cur.position() != std::byte(0)) {
        file_names.push_back(
            parse_line_table_file(cur, compilation_dir, include_directories));
    }
    cur += 1;

    sdb::span<const std::byte> data{cur.position(), end};
    return std::make_unique<sdb::line_table>(
        data, &cu, default_is_stmt, line_base, line_range, opcode_base,
        std::move(include_directories), std::move(file_names));
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

sdb::die parse_die(const sdb::compile_unit& cu, cursor cur) {
    auto pos = cur.position();
    auto abbrev_code = cur.uleb128();

    if (abbrev_code == 0) {
        auto next = cur.position();
        return sdb::die{next};
    }

    auto& abbrev_table = cu.abbrev_table();
    auto& abbrev = abbrev_table.at(abbrev_code);

    std::vector<const std::byte*> attr_locs;
    attr_locs.reserve(abbrev.attr_specs.size());
    for (auto& attr : abbrev.attr_specs) {
        attr_locs.push_back(cur.position());
        cur.skip_form(attr.form);
    }
    auto next = cur.position();
    return sdb::die(pos, &cu, &abbrev, std::move(attr_locs), next);
}

sdb::die sdb::compile_unit::root() const {
    std::size_t header_size = 11;
    cursor cur({data_.begin() + header_size, data_.end()});
    return parse_die(*this, cur);
}

sdb::die::children_range::iterator::iterator(const sdb::die& d) {
    cursor next_cur({d.next_, d.cu_->data().end()});
    die_ = parse_die(*d.cu_, next_cur);
}

bool sdb::die::children_range::iterator::operator==(const iterator& rhs) const {
    auto lhs_null = !die_.has_value() or !die_->abbrev_entry();
    auto rhs_null = !rhs.die_.has_value() or !rhs.die_->abbrev_entry();
    if (lhs_null and rhs_null) {
        return true;
    }
    if (lhs_null or rhs_null) {
        return false;
    }

    return die_->abbrev_ == rhs->abbrev_ and die_->next() == rhs->next();
}

sdb::die::children_range::iterator&
sdb::die::children_range::iterator::operator++() {
    if (!die_.has_value() or !die_->abbrev_) {
        return *this;
    }

    if (!die_->abbrev_->has_children) {
        cursor next_cur({die_->next_, die_->cu_->data().end()});
        die_ = parse_die(*die_->cu_, next_cur);
    } else if (die_->contains(DW_AT_sibling)) {
        die_ = die_.value()[DW_AT_sibling].as_reference();
    } else {
        iterator sub_children(*die_);
        while (sub_children->abbrev_) {
            sub_children++;
        }
        cursor next_cur({sub_children->next_, die_->cu_->data().end()});
        die_ = parse_die(*die_->cu_, next_cur);
    }
    // 'this' is a pointer, and *this is dereference of the pointer, its result
    // is a reference.
    return *this;
}

sdb::die::children_range::iterator
sdb::die::children_range::iterator::operator++(int) {
    auto tmp = *this;
    ++(*this);
    return tmp;
}

sdb::die::children_range sdb::die::children() const {
    return children_range(*this);
}

bool sdb::die::contains(std::uint64_t attribute) const {
    auto& specs = abbrev_->attr_specs;
    return std::find_if(begin(specs), end(specs), [=](auto spec) {
               return spec.attr == attribute;
           }) != end(specs);
}

sdb::attr sdb::die::operator[](std::uint64_t attribute) const {
    auto& specs = abbrev_->attr_specs;
    for (std::size_t i = 0; i < specs.size(); i++) {
        if (specs[i].attr == attribute) {
            return {cu_, specs[i].attr, specs[i].form, attr_locs_[i]};
        }
    }
    error::send("Attribute not found");
}

sdb::file_addr sdb::attr::as_address() const {
    cursor cur({location_, cu_->data().end()});
    if (form_ != DW_FORM_addr) {
        error::send("Invalid address type");
    }
    auto elf = cu_->dwarf_info()->elf_file();
    return file_addr{*elf, cur.u64()};
}

std::uint32_t sdb::attr::as_section_offset() const {
    cursor cur({location_, cu_->data().end()});
    if (form_ != DW_FORM_sec_offset) {
        error::send("Invalid offset type");
    }
    return cur.u32();
}

std::uint64_t sdb::attr::as_int() const {
    cursor cur({location_, cu_->data().end()});
    switch (form_) {
    case DW_FORM_data1:
        return cur.u8();
    case DW_FORM_data2:
        return cur.u16();
    case DW_FORM_data4:
        return cur.u32();
    case DW_FORM_data8:
        return cur.u64();
    case DW_FORM_udata:
        return cur.uleb128();
    default:
        error::send("Invalid integer type");
    }
}

sdb::span<const std::byte> sdb::attr::as_block() const {
    std::size_t size;
    cursor cur({location_, cu_->data().end()});
    switch (form_) {
    case DW_FORM_block1:
        size = cur.u8();
        break;
    case DW_FORM_block2:
        size = cur.u16();
        break;
    case DW_FORM_block4:
        size = cur.u32();
        break;
    case DW_FORM_block:
        size = cur.uleb128();
        break;
    default:
        error::send("Invalid block type");
    }
    return {cur.position(), size};
}

sdb::die sdb::attr::as_reference() const {
    cursor cur({location_, cu_->data().end()});
    std::size_t offset;
    switch (form_) {
    case DW_FORM_ref1:
        offset = cur.u8();
        break;
    case DW_FORM_ref2:
        offset = cur.u16();
        break;
    case DW_FORM_ref4:
        offset = cur.u32();
        break;
    case DW_FORM_ref8:
        offset = cur.u64();
        break;
    case DW_FORM_ref_udata:
        offset = cur.uleb128();
        break;
    case DW_FORM_ref_addr: {
        offset = cur.u32();
        auto section =
            cu_->dwarf_info()->elf_file()->get_section_contents(".debug_info");
        auto die_pos = section.begin() + offset;
        auto& cus = cu_->dwarf_info()->compile_units();
        auto cu_finder = [=](auto& cu) {
            return cu->data().begin() <= die_pos and cu->data().end() > die_pos;
        };
        auto cu_for_offset = std::find_if(begin(cus), end(cus), cu_finder);
        cursor ref_cur({die_pos, cu_for_offset->get()->data().end()});
        return parse_die(**cu_for_offset, ref_cur);
    }
    default:
        error::send("Invalid reference type");
    }
    cursor ref_cur({cu_->data().begin() + offset, cu_->data().end()});
    return parse_die(*cu_, ref_cur);
}

std::string_view sdb::attr::as_string() const {
    cursor cur({location_, cu_->data().end()});
    switch (form_) {
    case DW_FORM_string:
        return cur.string();
    case DW_FORM_strp: {
        auto offset = cur.u32();
        auto stab =
            cu_->dwarf_info()->elf_file()->get_section_contents(".debug_str");
        cursor stab_cur({stab.begin() + offset, stab.end()});
        return stab_cur.string();
    }
    default:
        error::send("Invalid string type");
    }
}

sdb::file_addr sdb::die::low_pc() const {
    if (contains(DW_AT_ranges)) {
        auto first_entry = (*this)[DW_AT_ranges].as_range_list().begin();
        return first_entry->low;
    } else if (contains(DW_AT_low_pc)) {
        return (*this)[DW_AT_low_pc].as_address();
    }
    error::send("DIW does not have low PC");
}

sdb::file_addr sdb::die::high_pc() const {
    if (contains(DW_AT_ranges)) {
        auto ranges = (*this)[DW_AT_ranges].as_range_list();
        auto it = ranges.begin();
        while (std::next(it) != ranges.end()) {
            it++;
        }
        return it->high;
    } else if (contains(DW_AT_high_pc)) {
        auto attr = (*this)[DW_AT_high_pc];
        file_addr addr;
        if (attr.form() == DW_FORM_addr) {
            return attr.as_address();
        } else {
            return low_pc() + attr.as_int();
        }
    }
    error::send("DIW does not have high PC");
}

sdb::range_list::iterator::iterator(const compile_unit* cu,
                                    sdb::span<const std::byte> data,
                                    file_addr base_address)
    : cu_(cu), data_(data), base_address_(base_address), pos_(data.begin()) {
    ++(*this);
}

sdb::range_list::iterator& sdb::range_list::iterator::operator++() {
    auto elf = cu_->dwarf_info()->elf_file();
    // ~ is the bit-wise complement operator.
    constexpr auto base_address_flag = ~static_cast<std::uint64_t>(0);

    cursor cur({pos_, data_.end()});
    while (true) {
        current_.low = file_addr{*elf, cur.u64()};
        current_.high = file_addr{*elf, cur.u64()};

        if (current_.low.addr() == base_address_flag) {
            base_address_ = current_.high;
        } else if (current_.low.addr() == 0 and current_.high.addr() == 0) {
            pos_ = nullptr;
            break;
        } else {
            pos_ = cur.position();
            current_.low += base_address_.addr();
            current_.high += base_address_.addr();
            break;
        }
    }
    return *this;
}

sdb::range_list::iterator sdb::range_list::iterator::operator++(int) {
    auto tmp = *this;
    ++(*this);
    return tmp;
}

sdb::range_list sdb::attr::as_range_list() const {
    auto section =
        cu_->dwarf_info()->elf_file()->get_section_contents(".debug_ranges");
    auto offset = as_section_offset();
    span<const std::byte> data(section.begin() + offset, section.end());

    auto root = cu_->root();
    file_addr base_address = root.contains(DW_AT_low_pc)
                                 ? root[DW_AT_low_pc].as_address()
                                 : file_addr{};
    return {cu_, data, base_address};
}

sdb::range_list::iterator sdb::range_list::begin() const {
    return {cu_, data_, base_address_};
}

sdb::range_list::iterator sdb::range_list::end() const { return {}; }

bool sdb::range_list::contains(file_addr address) const {
    return std::any_of(begin(), end(),
                       [=](auto& e) { return e.contains(address); });
}

bool sdb::die::contains_address(file_addr address) const {
    if (address.elf_file() != this->cu_->dwarf_info()->elf_file()) {
        return false;
    }

    if (contains(DW_AT_ranges)) {
        return (*this)[DW_AT_ranges].as_range_list().contains(address);
    } else if (contains(DW_AT_low_pc)) {
        return low_pc() <= address and high_pc() > address;
    }

    return false;
}

const sdb::compile_unit*
sdb::dwarf::compile_unit_containing_address(file_addr address) const {
    for (auto& cu : compile_units_) {
        if (cu->root().contains_address(address)) {
            return cu.get();
        }
    }
    return nullptr;
}

std::optional<sdb::die>
sdb::dwarf::function_containing_address(file_addr address) const {
    index();
    for (auto& [name, entry] : function_index_) {
        cursor cur({entry.pos, entry.cu->data().end()});
        auto d = parse_die(*entry.cu, cur);
        if (d.contains_address(address) and
            d.abbrev_entry()->tag == DW_TAG_subprogram) {
            return d;
        }
    }
    return std::nullopt;
}

std::vector<sdb::die> sdb::dwarf::find_functions(std::string name) const {
    index();

    std::vector<die> found;
    auto [begin, end] = function_index_.equal_range(name);
    std::transform(begin, end, std::back_inserter(found), [](auto& pair) {
        auto [name, entry] = pair;
        cursor cur({entry.pos, entry.cu->data().end()});
        return parse_die(*entry.cu, cur);
    });
    return found;
}

void sdb::dwarf::index() const {
    if (!function_index_.empty()) {
        return;
    }
    for (auto& cu : compile_units_) {
        index_die(cu->root());
    }
}

std::optional<std::string_view> sdb::die::name() const {
    if (contains(DW_AT_name)) {
        return (*this)[DW_AT_name].as_string();
    }
    if (contains(DW_AT_specification)) {
        return (*this)[DW_AT_specification].as_reference().name();
    }
    if (contains(DW_AT_abstract_origin)) {
        return (*this)[DW_AT_abstract_origin].as_reference().name();
    }
    return std::nullopt;
}

void sdb::dwarf::index_die(const die& current) const {
    bool has_range =
        current.contains(DW_AT_low_pc) or current.contains(DW_AT_ranges);
    bool is_function = current.abbrev_entry()->tag == DW_TAG_subprogram or
                       current.abbrev_entry()->tag == DW_TAG_inlined_subroutine;
    if (has_range and is_function) {
        if (auto name = current.name(); name) {
            index_entry entry{current.cu(), current.position()};
            function_index_.emplace(*name, entry);
        }
    }
    for (auto child : current.children()) {
        index_die(child);
    }
}

sdb::compile_unit::compile_unit(dwarf& parent, span<const std::byte> data,
                                std::size_t abbrev_offset)
    : parent_(&parent), data_(data), abbrev_offset_(abbrev_offset) {
    line_table_ = parse_line_table(*this);
}

sdb::line_table::iterator::iterator(const sdb::line_table* table)
    : table_(table), pos_(table->data_.begin()) {
    registers_.is_stmt = table->default_is_stmt_;
    ++(*this);
}

sdb::line_table::iterator sdb::line_table::begin() const {
    return iterator(this);
}

sdb::line_table::iterator sdb::line_table::end() const { return {}; }

sdb::line_table::iterator& sdb::line_table::iterator::operator++() {
    if (pos_ == table_->data_.end()) {
        pos_ = nullptr;
        return *this;
    }

    bool emitted = false;
    do {
        emitted = execute_instruction();
    } while (!emitted);

    current_.file_entry = &table_->file_names_[current_.file_index - 1];
    return *this;
}

sdb::line_table::iterator sdb::line_table::iterator::operator++(int) {
    auto tmp = *this;
    ++(*this);
    return tmp;
}

bool sdb::line_table::iterator::execute_instruction() {
    auto elf = table_->cu_->dwarf_info()->elf_file();
    cursor cur({pos_, table_->data_.end()});
    auto opcode = cur.u8();
    bool emitted = false;

    if (opcode > 0 and opcode < table_->opcode_base_) {
        switch (opcode) {
        case DW_LNS_copy:
            current_ = registers_;
            registers_.basic_block_start = false;
            registers_.prologue_end = false;
            registers_.epilogue_begin = false;
            registers_.discriminator = 0;
            emitted = true;
            break;
        case DW_LNS_advance_pc:
            registers_.address += cur.uleb128();
            break;
        case DW_LNS_advance_line:
            registers_.line += cur.sleb128();
            break;
        case DW_LNS_set_file:
            registers_.file_index = cur.uleb128();
            break;
        case DW_LNS_set_column:
            registers_.column = cur.uleb128();
            break;
        case DW_LNS_negate_stmt:
            registers_.is_stmt = !registers_.is_stmt;
            break;
        case DW_LNS_set_basic_block:
            registers_.basic_block_start = true;
            break;
        case DW_LNS_const_add_pc:
            registers_.address +=
                (255 - table_->opcode_base_) / table_->line_range_;
            break;
        case DW_LNS_fixed_advance_pc:
            registers_.address += cur.u16();
            break;
        case DW_LNS_set_prologue_end:
            registers_.prologue_end = true;
            break;
        case DW_LNS_set_epilogue_begin:
            registers_.epilogue_begin = true;
            break;
        case DW_LNS_set_isa:
            break;
        default:
            error::send("Unexpected standard opcode");
        }
    } else if (opcode == 0) {
        auto length = cur.uleb128();
        auto extended_opcode = cur.u8();

        switch (extended_opcode) {
        case DW_LNE_end_sequence:
            registers_.end_sequence = true;
            current_ = registers_;
            registers_ = entry{};
            registers_.is_stmt = table_->default_is_stmt_;
            emitted = true;
            break;
        case DW_LNE_set_address:
            registers_.address = file_addr(*elf, cur.u64());
            break;
        case DW_LNE_define_file: {
            auto compilation_dir =
                table_->cu_->root()[DW_AT_comp_dir].as_string();
            auto file = parse_line_table_file(cur, std::string(compilation_dir),
                                              table_->include_directories_);
            table_->file_names_.push_back(file);
            break;
        }
        case DW_LNE_set_discriminator:
            registers_.discriminator = cur.uleb128();
            break;
        default:
            error::send("Unexpected extended opcode");
        }
    } else {
        auto adjusted_opcode = opcode - table_->opcode_base_;
        registers_.address += adjusted_opcode / table_->line_range_;
        registers_.line +=
            table_->line_base_ + (adjusted_opcode % table_->line_range_);
        current_ = registers_;
        registers_.basic_block_start = false;
        registers_.prologue_end = false;
        registers_.epilogue_begin = false;
        registers_.discriminator = 0;
        emitted = true;
    }

    pos_ = cur.position();
    return emitted;
}

sdb::line_table::iterator
sdb::line_table::get_entry_by_address(file_addr address) const {
    auto prev = begin();
    if (prev == end()) {
        return prev;
    }

    auto it = prev;
    for (it++; it != end(); prev = it++) {
        if (prev->address <= address and it->address > address and
            !prev->end_sequence) {
            return prev;
        }
    }
    return end();
}

std::vector<sdb::line_table::iterator>
sdb::line_table::get_entries_by_line(std::filesystem::path path,
                                     std::size_t line) const {
    std::vector<iterator> entries;

    for (auto it = begin(); it != end(); it++) {
        auto& entry_path = it->file_entry->path;
        if (it->line == line) {
            if ((path.is_absolute() and entry_path == path) or
                (path.is_relative() and path_ends_in(entry_path, path))) {
                entries.push_back(it);
            }
        }
    }

    return entries;
}

sdb::source_location sdb::die::location() const { return {&file(), line()}; }

const sdb::line_table::file& sdb::die::file() const {
    std::uint64_t idx;
    if (abbrev_->tag == DW_TAG_inlined_subroutine) {
        idx = (*this)[DW_AT_call_file].as_int();
    } else {
        idx = (*this)[DW_AT_decl_file].as_int();
    }
    return this->cu_->lines().file_names()[idx - 1];
}

std::uint64_t sdb::die::line() const {
    if (abbrev_->tag == DW_TAG_inlined_subroutine) {
        return (*this)[DW_AT_call_line].as_int();
    }
    return (*this)[DW_AT_decl_line].as_int();
}

std::vector<sdb::die>
sdb::dwarf::inline_stack_at_address(file_addr address) const {
    auto func = function_containing_address(address);
    std::vector<sdb::die> stack;

    if (func) {
        stack.push_back(*func);
        while (true) {
            const auto& children = stack.back().children();
            auto found = std::find_if(
                children.begin(), children.end(), [=](auto& child) {
                    return child.abbrev_entry()->tag ==
                               DW_TAG_inlined_subroutine and
                           child.contains_address(address);
                });

            if (found == children.end()) {
                break;
            } else {
                stack.push_back(*found);
            }
        }
    }
    return stack;
}
