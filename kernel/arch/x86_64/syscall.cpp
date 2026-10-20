#include "kernel/arch/x86_64/syscall.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/arch/x86_64/descriptor_tables.hpp"
#include "kernel/sched/scheduler.hpp"
#include "kernel/syscall/dispatcher.hpp"

namespace barry::arch {
namespace {

constexpr u64 SYSCALL_FLAG_MASK = RFLAGS_INTERRUPT_ENABLE | (1ULL << 8) | (1ULL << 18);

bool g_enabled = false;

u64 compose_star() {
    auto const kernel_code = static_cast<u64>(to_underlying(Selector::KernelCode));
    auto const user_base = static_cast<u64>(to_underlying(Selector::UserData)) - 8;
    return (kernel_code << 32) | (user_base << 48);
}

}  // namespace

void enable_fast_syscalls_on_current_processor() {
    write_msr(MSR_STAR, compose_star());
    write_msr(MSR_LSTAR, reinterpret_cast<u64>(&barry_syscall_entry));
    write_msr(MSR_SFMASK, SYSCALL_FLAG_MASK);
    write_msr(MSR_EFER, read_msr(MSR_EFER) | EFER_SYSCALL_ENABLE);
}

UserContext SyscallFrame::to_user_context() const {
    UserContext context {};
    context.rip = return_address;
    context.rsp = user_stack_pointer;
    context.rflags = saved_rflags;
    context.rdi = registers.rdi;
    context.rsi = registers.rsi;
    context.rdx = registers.rdx;
    context.rcx = return_address;
    context.r8 = registers.r8;
    context.r9 = registers.r9;
    context.rax = registers.rax;
    context.rbx = registers.rbx;
    context.rbp = registers.rbp;
    context.r10 = registers.r10;
    context.r11 = saved_rflags;
    context.r12 = registers.r12;
    context.r13 = registers.r13;
    context.r14 = registers.r14;
    context.r15 = registers.r15;
    return context;
}

KResult initialize_fast_syscalls() {
    enable_fast_syscalls_on_current_processor();
    g_enabled = true;
    return {};
}

bool fast_syscalls_enabled() {
    return g_enabled;
}

}  // namespace barry::arch

extern "C" void barry_handle_syscall(barry::arch::SyscallFrame* frame) {
    using namespace barry;

    auto* thread = sched::scheduler().current_thread();
    if (thread != nullptr)
        thread->set_syscall_frame(frame);

    arch::enable_interrupts();

    auto const& registers = frame->registers;
    syscall::Arguments const arguments {
        registers.rax, registers.rdi, registers.rsi, registers.rdx,
        registers.r10, registers.r8,  registers.r9,
    };

    frame->registers.rax = static_cast<u64>(syscall::dispatch(arguments));

    arch::disable_interrupts();

    if (thread != nullptr)
        thread->set_syscall_frame(nullptr);
}
