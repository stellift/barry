#pragma once

#include "kernel/base/types.hpp"

namespace barry::arch {

inline constexpr u32 PIT_INPUT_FREQUENCY_HZ = 1193182;

void pit_busy_wait(u32 microseconds);

}  // namespace barry::arch
