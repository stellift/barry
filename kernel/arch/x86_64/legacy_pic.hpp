#pragma once

#include "kernel/base/types.hpp"

namespace barry::arch {

inline constexpr u8 PIC_PRIMARY_VECTOR_BASE = 0x20;
inline constexpr u8 PIC_SECONDARY_VECTOR_BASE = 0x28;

void remap_and_mask_legacy_pic();

}  // namespace barry::arch
