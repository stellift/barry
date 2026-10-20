#pragma once

#include "kernel/base/expected.hpp"
#include "kernel/base/types.hpp"

namespace barry {

inline constexpr usize DEFAULT_ALLOCATION_ALIGNMENT = 16;

KResultOr<void*> allocate_raw(usize size, usize alignment = DEFAULT_ALLOCATION_ALIGNMENT);
void deallocate_raw(void* pointer);

}  // namespace barry
