#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/types.hpp"

namespace barry::arch {

struct [[gnu::packed]] ProcessorLocalData {
    u64 self;
    u64 kernel_stack_top;
    u64 user_stack_scratch;
    u32 index;
    u32 hardware_id;
};

inline constexpr usize PROCESSOR_LOCAL_SELF_OFFSET = 0;
inline constexpr usize PROCESSOR_LOCAL_KERNEL_STACK_OFFSET = 8;
inline constexpr usize PROCESSOR_LOCAL_USER_STACK_OFFSET = 16;

static_assert(__builtin_offsetof(ProcessorLocalData, self) == PROCESSOR_LOCAL_SELF_OFFSET);
static_assert(__builtin_offsetof(ProcessorLocalData, kernel_stack_top)
              == PROCESSOR_LOCAL_KERNEL_STACK_OFFSET);
static_assert(__builtin_offsetof(ProcessorLocalData, user_stack_scratch)
              == PROCESSOR_LOCAL_USER_STACK_OFFSET);

inline constexpr usize MAX_PROCESSORS = 64;

KResult initialize_processor_local_data(u32 index, u32 hardware_id, VirtAddr kernel_stack_top);

ProcessorLocalData& current_processor();

u32 current_processor_index();

void set_current_kernel_stack(VirtAddr stack_top);

void set_current_task_stacks(VirtAddr stack_top);

}  // namespace barry::arch
