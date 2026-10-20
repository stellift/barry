#include "kernel/arch/x86_64/interrupts.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/base/array.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/log.hpp"
#include "kernel/panic.hpp"

extern "C" barry::u8 const barry_isr_stub_table[];
extern "C" barry::u64 const barry_isr_stub_stride;

namespace barry::arch {
namespace {

constexpr u8 GATE_PRESENT = 1 << 7;
constexpr u8 GATE_TYPE_INTERRUPT = 0x0e;
constexpr u8 GATE_TYPE_TRAP = 0x0f;

struct [[gnu::packed]] InterruptDescriptor {
    u16 offset_low { 0 };
    u16 selector { 0 };
    u8 interrupt_stack { 0 };
    u8 type_attributes { 0 };
    u16 offset_middle { 0 };
    u32 offset_high { 0 };
    u32 reserved { 0 };

    void set_offset(VirtAddr handler) {
        u64 const raw = handler.raw();
        offset_low = static_cast<u16>(raw & 0xffff);
        offset_middle = static_cast<u16>((raw >> 16) & 0xffff);
        offset_high = static_cast<u32>(raw >> 32);
    }
};

static_assert(sizeof(InterruptDescriptor) == 16);

alignas(16) Array<InterruptDescriptor, VECTOR_COUNT> g_idt { {} };
Array<InterruptHandler, VECTOR_COUNT> g_handlers { {} };

VirtAddr stub_address_for(u8 vector) {
    return VirtAddr::from_ptr(barry_isr_stub_table + vector * barry_isr_stub_stride);
}

void load_interrupt_descriptor_table() {
    DescriptorTablePointer pointer {};
    pointer.limit = static_cast<u16>(sizeof(g_idt) - 1);
    pointer.base = reinterpret_cast<u64>(g_idt.data());
    __asm__ volatile("lidt %0" ::"m"(pointer) : "memory");
}

void report_page_fault(TrapFrame const& frame) {
    u64 const code = frame.error_code;
    dbgln("  faulting address: {}", read_cr2());
    dbgln("  cause           : {} {} {}", (code & 1) != 0 ? "protection" : "not-present",
          (code & 2) != 0 ? "write" : "read", (code & 4) != 0 ? "user" : "kernel");
    dbgln("  instruction fetch: {}", (code & 16) != 0);
}

void report_registers(TrapFrame const& frame) {
    auto const& registers = frame.registers;
    dbgln("  rip={} rsp={} rflags={#x}", frame.instruction_pointer(), frame.stack_pointer(),
          frame.rflags);
    dbgln("  cs={#x} ss={#x} error={#x}", frame.code_selector, frame.stack_selector,
          frame.error_code);
    dbgln("  rax={#016x} rbx={#016x} rcx={#016x} rdx={#016x}", registers.rax, registers.rbx,
          registers.rcx, registers.rdx);
    dbgln("  rsi={#016x} rdi={#016x} rbp={#016x} r8 ={#016x}", registers.rsi, registers.rdi,
          registers.rbp, registers.r8);
    dbgln("  r9 ={#016x} r10={#016x} r11={#016x} r12={#016x}", registers.r9, registers.r10,
          registers.r11, registers.r12);
    dbgln("  r13={#016x} r14={#016x} r15={#016x}", registers.r13, registers.r14, registers.r15);
}

[[noreturn]] void fatal_exception(TrapFrame& frame) {
    auto const vector = static_cast<u8>(frame.vector);
    dbgln("");
    dbgln("Unhandled CPU exception {} ({})", vector, exception_name(vector));
    report_registers(frame);
    if (vector == to_underlying(Exception::PageFault))
        report_page_fault(frame);

    if (auto const symbol = resolve_symbol(frame.instruction_pointer()))
        dbgln("  in {}+{#x}", symbol->name, symbol->offset);

    PANIC("unrecoverable exception {} ({})", vector, exception_name(vector));
}

void ignore_interrupt(TrapFrame&) {}

}  // namespace

char const* exception_name(u8 vector) {
    switch (static_cast<Exception>(vector)) {
    case Exception::DivideError:
        return "divide error";
    case Exception::Debug:
        return "debug";
    case Exception::NonMaskableInterrupt:
        return "non-maskable interrupt";
    case Exception::Breakpoint:
        return "breakpoint";
    case Exception::Overflow:
        return "overflow";
    case Exception::BoundRangeExceeded:
        return "bound range exceeded";
    case Exception::InvalidOpcode:
        return "invalid opcode";
    case Exception::DeviceNotAvailable:
        return "device not available";
    case Exception::DoubleFault:
        return "double fault";
    case Exception::InvalidTaskStateSegment:
        return "invalid task state segment";
    case Exception::SegmentNotPresent:
        return "segment not present";
    case Exception::StackSegmentFault:
        return "stack segment fault";
    case Exception::GeneralProtectionFault:
        return "general protection fault";
    case Exception::PageFault:
        return "page fault";
    case Exception::FloatingPointError:
        return "floating point error";
    case Exception::AlignmentCheck:
        return "alignment check";
    case Exception::MachineCheck:
        return "machine check";
    case Exception::SimdFloatingPointError:
        return "simd floating point error";
    case Exception::VirtualizationError:
        return "virtualization error";
    case Exception::ControlProtection:
        return "control protection";
    default:
        return "reserved";
    }
}

bool vector_is_exception(u8 vector) {
    return vector <= LAST_EXCEPTION_VECTOR;
}

bool register_interrupt_handler(u8 vector, InterruptHandler handler) {
    if (handler == nullptr)
        return false;
    if (g_handlers[vector] != nullptr && g_handlers[vector] != &ignore_interrupt)
        return false;
    g_handlers[vector] = handler;
    return true;
}

void unregister_interrupt_handler(u8 vector) {
    g_handlers[vector] = &ignore_interrupt;
}

void set_interrupt_stack(u8 vector, InterruptStack stack) {
    g_idt[vector].interrupt_stack = to_underlying(stack);
}

void initialize_interrupts() {
    for (u16 vector = 0; vector < VECTOR_COUNT; ++vector) {
        auto& descriptor = g_idt[vector];
        descriptor.set_offset(stub_address_for(static_cast<u8>(vector)));
        descriptor.selector = to_underlying(Selector::KernelCode);
        descriptor.interrupt_stack = to_underlying(InterruptStack::None);
        descriptor.type_attributes = GATE_PRESENT
                                     | (vector == to_underlying(Exception::Breakpoint)
                                            ? GATE_TYPE_TRAP
                                            : GATE_TYPE_INTERRUPT);
        g_handlers[vector] = vector_is_exception(static_cast<u8>(vector)) ? nullptr
                                                                         : &ignore_interrupt;
    }

    set_interrupt_stack(to_underlying(Exception::DoubleFault), InterruptStack::DoubleFault);
    set_interrupt_stack(to_underlying(Exception::NonMaskableInterrupt), InterruptStack::NonMaskable);
    set_interrupt_stack(to_underlying(Exception::Debug), InterruptStack::Debug);

    load_interrupt_descriptor_table();
}

}  // namespace barry::arch

extern "C" void barry_dispatch_interrupt(barry::arch::TrapFrame* frame) {
    using namespace barry;
    using namespace barry::arch;

    VERIFY(frame != nullptr);
    auto const vector = static_cast<u8>(frame->vector);
    InterruptHandler const handler = g_handlers[vector];

    if (handler == nullptr)
        fatal_exception(*frame);

    handler(*frame);
}
