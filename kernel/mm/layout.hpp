#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/types.hpp"

namespace barry::mm {

inline constexpr VirtAddr KERNEL_HEAP_BASE { 0xffffffffc0000000ULL };
inline constexpr u64 KERNEL_HEAP_MAX_SIZE = 512_MiB;

inline constexpr VirtAddr KERNEL_STACK_AREA_BASE { 0xffffffffa0000000ULL };
inline constexpr u64 KERNEL_STACK_AREA_SIZE = 256_MiB;

inline constexpr u64 KERNEL_STACK_SIZE = 64_KiB;
inline constexpr u64 KERNEL_STACK_GUARD_SIZE = PAGE_SIZE;

inline constexpr VirtRange kernel_heap_range() {
    return VirtRange { KERNEL_HEAP_BASE, KERNEL_HEAP_MAX_SIZE };
}

inline constexpr VirtRange kernel_stack_area_range() {
    return VirtRange { KERNEL_STACK_AREA_BASE, KERNEL_STACK_AREA_SIZE };
}

}  // namespace barry::mm
