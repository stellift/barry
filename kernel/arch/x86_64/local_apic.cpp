#include "kernel/arch/x86_64/local_apic.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/arch/x86_64/pit.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/base/atomic.hpp"
#include "kernel/mm/kernel_address_space.hpp"

namespace barry::arch {
namespace {

enum class Register : u32 {
    Id = 0x020,
    Version = 0x030,
    TaskPriority = 0x080,
    EndOfInterrupt = 0x0b0,
    SpuriousInterruptVector = 0x0f0,
    InterruptCommandLow = 0x300,
    InterruptCommandHigh = 0x310,
    TimerLocalVector = 0x320,
    TimerInitialCount = 0x380,
    TimerCurrentCount = 0x390,
    TimerDivideConfiguration = 0x3e0,
};

constexpr u64 APIC_BASE_ADDRESS_MASK = 0x000ffffffffff000ULL;
constexpr u64 APIC_GLOBAL_ENABLE = 1ULL << 11;

constexpr u32 SPURIOUS_APIC_SOFTWARE_ENABLE = 1u << 8;
constexpr u32 TIMER_MODE_PERIODIC = 1u << 17;
constexpr u32 LOCAL_VECTOR_MASKED = 1u << 16;
constexpr u32 DIVIDE_BY_16 = 0b0011;
constexpr u32 TIMER_DIVIDER = 16;

constexpr u32 DELIVERY_MODE_FIXED = 0u << 8;
constexpr u32 DELIVERY_MODE_INIT = 5u << 8;
constexpr u32 DELIVERY_MODE_STARTUP = 6u << 8;
constexpr u32 LEVEL_ASSERT = 1u << 14;
constexpr u32 DELIVERY_STATUS_PENDING = 1u << 12;

constexpr u32 CALIBRATION_MICROSECONDS = 20000;
constexpr u32 MICROSECONDS_PER_SECOND = 1000000;

constexpr u32 CPUID_FEATURE_LEAF = 1;
constexpr u32 CPUID_EDX_APIC_BIT = 1u << 9;

VirtAddr g_registers;
u64 g_timer_frequency_hz = 0;
u64 g_timestamp_frequency_hz = 0;

PhysAddr base_address_from_msr() {
    return PhysAddr(read_msr(MSR_APIC_BASE) & APIC_BASE_ADDRESS_MASK);
}

u32 read_register(Register which) {
    VERIFY(!g_registers.is_null());
    auto const* slot = g_registers.offset(to_underlying(which)).as_ptr<u32 volatile const>();
    return *slot;
}

void write_register(Register which, u32 value) {
    VERIFY(!g_registers.is_null());
    auto* slot = g_registers.offset(to_underlying(which)).as_ptr<u32 volatile>();
    *slot = value;
}

void software_enable() {
    write_msr(MSR_APIC_BASE, read_msr(MSR_APIC_BASE) | APIC_GLOBAL_ENABLE);
    write_register(Register::SpuriousInterruptVector,
                   SPURIOUS_APIC_SOFTWARE_ENABLE | SPURIOUS_INTERRUPT_VECTOR);
    write_register(Register::TaskPriority, 0);
}

void wait_for_delivery() {
    while ((read_register(Register::InterruptCommandLow) & DELIVERY_STATUS_PENDING) != 0)
        cpu_relax();
}

void send_interrupt_command(u32 target_id, u32 low) {
    write_register(Register::InterruptCommandHigh, target_id << 24);
    write_register(Register::InterruptCommandLow, low);
    wait_for_delivery();
}

void calibrate_against_pit() {
    write_register(Register::TimerDivideConfiguration, DIVIDE_BY_16);
    write_register(Register::TimerLocalVector, LOCAL_VECTOR_MASKED);
    write_register(Register::TimerInitialCount, 0xffffffff);

    u64 const timestamp_before = read_timestamp_counter();
    pit_busy_wait(CALIBRATION_MICROSECONDS);
    u64 const timestamp_after = read_timestamp_counter();

    u32 const remaining = read_register(Register::TimerCurrentCount);
    write_register(Register::TimerInitialCount, 0);

    u64 const elapsed_ticks = 0xffffffffULL - remaining;
    g_timer_frequency_hz = (elapsed_ticks * TIMER_DIVIDER * MICROSECONDS_PER_SECOND)
                           / CALIBRATION_MICROSECONDS;
    g_timestamp_frequency_hz = ((timestamp_after - timestamp_before) * MICROSECONDS_PER_SECOND)
                               / CALIBRATION_MICROSECONDS;
}

}  // namespace

bool LocalApic::is_available() {
    return (cpuid(CPUID_FEATURE_LEAF).edx & CPUID_EDX_APIC_BIT) != 0;
}

KResult LocalApic::initialize_for_boot_processor() {
    if (!is_available())
        return Error(Errno::ENODEV, "this processor reports no local APIC");

    PhysAddr const base = base_address_from_msr();
    if (base.is_null())
        return Error(Errno::ENODEV, "the local APIC base address is zero");

    g_registers = TRY(mm::map_device_memory(PhysRange { base, PAGE_SIZE }));

    software_enable();
    calibrate_against_pit();
    return {};
}

KResult LocalApic::initialize_for_current_processor() {
    if (g_registers.is_null())
        return Error(Errno::EINVAL, "the boot processor must initialize the APIC first");
    software_enable();
    return {};
}

u32 LocalApic::current_id() {
    return read_register(Register::Id) >> 24;
}

void LocalApic::end_of_interrupt() {
    write_register(Register::EndOfInterrupt, 0);
}

KResult LocalApic::start_periodic_timer(u32 frequency_hz, u8 vector) {
    if (frequency_hz == 0)
        return Error(Errno::EINVAL, "the timer frequency must be positive");
    if (g_timer_frequency_hz == 0)
        return Error(Errno::ENODEV, "the local APIC timer has not been calibrated");

    u64 const count = g_timer_frequency_hz / (static_cast<u64>(frequency_hz) * TIMER_DIVIDER);
    if (count == 0 || count > 0xffffffffULL)
        return Error(Errno::ERANGE, "the requested timer frequency is out of range");

    write_register(Register::TimerDivideConfiguration, DIVIDE_BY_16);
    write_register(Register::TimerLocalVector, TIMER_MODE_PERIODIC | vector);
    write_register(Register::TimerInitialCount, static_cast<u32>(count));
    return {};
}

void LocalApic::stop_timer() {
    write_register(Register::TimerLocalVector, LOCAL_VECTOR_MASKED);
    write_register(Register::TimerInitialCount, 0);
}

u64 LocalApic::calibrated_timer_frequency_hz() {
    return g_timer_frequency_hz;
}

u64 LocalApic::calibrated_timestamp_frequency_hz() {
    return g_timestamp_frequency_hz;
}

void LocalApic::send_reschedule_interrupt(u32 target_id) {
    send_interrupt_command(target_id, DELIVERY_MODE_FIXED | RESCHEDULE_INTERRUPT_VECTOR);
}

void LocalApic::send_init_assert(u32 target_id) {
    send_interrupt_command(target_id, DELIVERY_MODE_INIT | LEVEL_ASSERT);
}

void LocalApic::send_startup(u32 target_id, PhysAddr trampoline) {
    auto const vector = static_cast<u32>(trampoline.raw() >> 12) & 0xff;
    send_interrupt_command(target_id, DELIVERY_MODE_STARTUP | LEVEL_ASSERT | vector);
}

}  // namespace barry::arch
