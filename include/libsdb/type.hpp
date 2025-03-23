#ifndef SDB_TYPE_HPP
#define SDB_TYPE_HPP

#include <libsdb/dwarf.hpp>
#include <optional>
#include <string_view>

namespace sdb {

class type {
  public:
    type(die die) : die_(std::move(die)) {}
    die get_die() const { return die_; }
    std::size_t byte_size() const;
    bool is_char_type() const;

    //  This template parameter list indicates that the strip function template
    //  takes any number of integer template arguments
    template <int... Tags> type strip() const {
        auto ret = *this;
        auto tag = ret.get_die().abbrev_entry()->tag;

        // fold expression. It runs some expression across every element in a
        // parameter pack and collects the result using a binary operator.
        while (((tag == Tags) or ...)) {
            ret = ret.get_die()[DW_AT_type].as_type().get_die();
            tag = ret.get_die().abbrev_entry()->tag;
        }
        return ret;
    }

    type strip_cv_typedef() const {
        return strip<DW_TAG_const_type, DW_TAG_volatile_type, DW_TAG_typedef>();
    }
    type strip_cvref_typedef() const {
        return strip<DW_TAG_const_type, DW_TAG_volatile_type, DW_TAG_typedef,
                     DW_TAG_reference_type, DW_TAG_rvalue_reference_type>();
    }
    type strip_all() const {
        return strip<DW_TAG_const_type, DW_TAG_volatile_type, DW_TAG_typedef,
                     DW_TAG_reference_type, DW_TAG_rvalue_reference_type,
                     DW_TAG_pointer_type>();
    }

  private:
    std::size_t compute_byte_size() const;
    die die_;
    mutable std::optional<std::size_t> byte_size_;
};

} // namespace sdb

#endif
