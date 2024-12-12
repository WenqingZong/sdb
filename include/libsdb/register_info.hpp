#ifndef SDB_REGISTER_INFO_HPP
#define SDB_REGISTER_INFO_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <sys/user.h>

#define DR_OFFSET(number) (offsetof(user, u_debugreg) + number * 8)
#define DEFINE_DR(number)                                                      \
    DEFINE_REGISTER(dr##number, -1, 8, DR_OFFSET(number), register_type::dr,   \
                    register_format::uint)

namespace sdb {
enum class register_id {
#define DEFINE_REGISTER(name, dwarf_id, size, offset, type, format) name
#include <libsdb/detail/registers.inc>
#undef DEFINE_REGISTER
};

enum class register_type { gpr, sub_gpr, fpr, dr };

enum class register_format { uint, double_float, long_double, vector };

struct register_info {
    register_id id;
    std::string_view name;
    std::int32_t dwarf_id;
    std::size_t size;
    std::size_t offset;
    register_type type;
    register_format format;
};

inline constexpr const register_info g_register_infos[] = {
#define DEFINE_REGISTER(name, dwarf_id, size, offset, type, format)            \
    { register_id::name, #name, dwarf_id, size, offset, type, format }
#include <libsdb/detail/registers.inc>
#undef DEFINE_REGISTER // book page 79
};
} // namespace sdb

#endif