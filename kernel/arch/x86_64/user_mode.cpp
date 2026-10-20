#include "kernel/arch/x86_64/user_mode.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/base/bytes.hpp"

namespace barry::arch {
namespace {

constexpr u64 CR4_OS_FXSAVE = 1ULL << 9;
constexpr u64 CR4_OS_XMM_EXCEPTION = 1ULL << 10;
constexpr u64 CR0_EMULATE_COPROCESSOR = 1ULL << 2;
constexpr u64 CR0_TASK_SWITCHED = 1ULL << 3;

constexpr u64 USER_RFLAGS = RFLAGS_INTERRUPT_ENABLE | 0x2;

}  // namespace

UserContext UserContext::for_fresh_process(VirtAddr entry_point, VirtAddr stack_pointer) {
    UserContext context {};
    context.rip = entry_point.raw();
    context.rsp = stack_pointer.raw();
    context.rflags = USER_RFLAGS;
    return context;
}

void enable_user_floating_point() {
    write_cr4(read_cr4() | CR4_OS_FXSAVE | CR4_OS_XMM_EXCEPTION);
    write_cr0((read_cr0() & ~CR0_EMULATE_COPROCESSOR) & ~CR0_TASK_SWITCHED);
}

void initialize_fpu_state(Span<u8> state) {
    VERIFY(state.size() >= FPU_STATE_SIZE);
    zero_bytes(state.first(FPU_STATE_SIZE));

    u32 const mxcsr = DEFAULT_MXCSR;
    __builtin_memcpy(state.data() + MXCSR_OFFSET, &mxcsr, sizeof(mxcsr));
}

void save_fpu_state(Span<u8> state) {
    VERIFY(state.size() >= FPU_STATE_SIZE);
    __asm__ volatile("fxsave64 (%0)" ::"r"(state.data()) : "memory");
}

void restore_fpu_state(Span<u8 const> state) {
    VERIFY(state.size() >= FPU_STATE_SIZE);
    __asm__ volatile("fxrstor64 (%0)" ::"r"(state.data()) : "memory");
}

}  // namespace barry::arch
