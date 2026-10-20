#pragma once

#include "kernel/arch/x86_64/interrupts.hpp"
#include "kernel/arch/x86_64/user_mode.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/types.hpp"

namespace barry::arch {

struct SyscallFrame {
    GeneralPurposeRegisters registers;
    u64 return_address;
    u64 saved_rflags;
    u64 user_stack_pointer;

    UserContext to_user_context() const;
};

static_assert(sizeof(SyscallFrame) == 18 * sizeof(u64));

void enable_fast_syscalls_on_current_processor();

KResult initialize_fast_syscalls();

bool fast_syscalls_enabled();

}  // namespace barry::arch

extern "C" void barry_syscall_entry();
extern "C" void barry_handle_syscall(barry::arch::SyscallFrame* frame);
