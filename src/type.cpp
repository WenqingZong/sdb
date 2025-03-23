#include <libsdb/type.hpp>

std::size_t sdb::type::byte_size() const {
    if (!byte_size_.has_value()) {
        byte_size_ = compute_byte_size();
    }
    return *byte_size_;
}

std::size_t sdb::type::compute_byte_size() const {
    auto tag = die_.abbrev_entry()->tag;

    if (tag == DW_TAG_pointer_type) {
        return 8;
    }
    if (tag == DW_TAG_ptr_to_member_type) {
        auto member_type = die_[DW_AT_type].as_type();
        if (member_type.get_die().abbrev_entry()->tag == DW_TAG_subrange_type) {
            return 16;
        }
        return 8;
    }
    if (tag == DW_TAG_array_type) {
        auto value_size = die_[DW_AT_type].as_type().byte_size();
        for (auto& child : die_.children()) {
            if (child.abbrev_entry()->tag == DW_TAG_subrange_type) {
                value_size *= child[DW_AT_upper_bound].as_int() + 1;
            }
        }
        return value_size;
    }
    if (die_.contains(DW_AT_byte_size)) {
        return die_[DW_AT_byte_size].as_int();
    }
    if (die_.contains(DW_AT_type)) {
        return die_[DW_AT_type].as_type().byte_size();
    }
    return 0;
}

bool sdb::type::is_char_type() const {
    auto stripped = strip_cv_typedef().get_die();
    if (!stripped.contains(DW_AT_encoding)) {
        return false;
    }
    auto encoding = stripped[DW_AT_encoding].as_int();
    return stripped.abbrev_entry()->tag == DW_TAG_base_type and
               encoding == DW_ATE_signed_char or
           encoding == DW_ATE_unsigned_char;
}
