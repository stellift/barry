#pragma once

#include "kernel/arch/x86_64/descriptor_tables.hpp"
#include "kernel/base/addr.hpp"
#include "kernel/base/types.hpp"

namespace barry::arch {

struct GeneralPurposeRegisters {
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 r11;
    u64 r10;
    u64 r9;
    u64 r8;
    u64 rbp;
    u64 rdi;
    u64 rsi;
    u64 rdx;
    u64 rcx;
    u64 rbx;
    u64 rax;
};

struct TrapFrame {
    GeneralPurposeRegisters registers;
    u64 vector;
    u64 error_code;
    u64 rip;
    u64 code_selector;
    u64 rflags;
    u64 rsp;
    u64 stack_selector;

    bool came_from_user_mode() const { return (code_selector & 0x3) != 0; }
    VirtAddr instruction_pointer() const { return VirtAddr(rip); }
    VirtAddr stack_pointer() const { return VirtAddr(rsp); }
};

static_assert(sizeof(GeneralPurposeRegisters) == 15 * sizeof(u64));
static_assert(sizeof(TrapFrame) == 22 * sizeof(u64));
static_assert(__builtin_offsetof(TrapFrame, code_selector) == 144,
              "the interrupt stubs test the privilege level at this offset");

enum class Exception : u8 {
    DivideError = 0,
    Debug = 1,
    NonMaskableInterrupt = 2,
    Breakpoint = 3,
    Overflow = 4,
    BoundRangeExceeded = 5,
    InvalidOpcode = 6,
    DeviceNotAvailable = 7,
    DoubleFault = 8,
    InvalidTaskStateSegment = 10,
    SegmentNotPresent = 11,
    StackSegmentFault = 12,
    GeneralProtectionFault = 13,
    PageFault = 14,
    FloatingPointError = 16,
    AlignmentCheck = 17,
    MachineCheck = 18,
    SimdFloatingPointError = 19,
    VirtualizationError = 20,
    ControlProtection = 21,
};

inline constexpr u8 FIRST_EXCEPTION_VECTOR = 0;
inline constexpr u8 LAST_EXCEPTION_VECTOR = 31;
inline constexpr u16 VECTOR_COUNT = 256;

char const* exception_name(u8 vector);
bool vector_is_exception(u8 vector);

using InterruptHandler = void (*)(TrapFrame&);

void initialize_interrupts();

void load_interrupt_descriptor_table();

bool register_interrupt_handler(u8 vector, InterruptHandler handler);
void unregister_interrupt_handler(u8 vector);

void set_interrupt_stack(u8 vector, InterruptStack stack);

}  // namespace barry::arch

extern "C" void barry_reload_segments(barry::u16 code_selector, barry::u16 data_selector);
extern "C" void barry_dispatch_interrupt(barry::arch::TrapFrame* frame);
