#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/types.hpp"

namespace barry::proc {

inline constexpr VirtAddr USER_SPACE_BEGIN { 0x0000000000001000ULL };
inline constexpr VirtAddr USER_SPACE_END { 0x0000800000000000ULL };

inline constexpr VirtAddr USER_STACK_TOP { 0x00007ffffffff000ULL };
inline constexpr u64 USER_STACK_SIZE = 8_MiB;

inline constexpr VirtAddr USER_MMAP_BASE { 0x0000700000000000ULL };
inline constexpr u64 USER_MMAP_AREA_SIZE = 64_GiB;

inline constexpr u64 USER_BRK_MAX_SIZE = 1_GiB;

inline constexpr bool is_user_range(VirtRange range) {
    if (range.length == 0)
        return false;
    if (range.end() < range.base)
        return false;
    return range.base >= USER_SPACE_BEGIN && range.end() <= USER_SPACE_END;
}

inline constexpr VirtRange user_stack_range() {
    return VirtRange { USER_STACK_TOP.subtract(USER_STACK_SIZE), USER_STACK_SIZE };
}

}  // namespace barry::proc
