#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/types.hpp"

namespace barry::arch {

inline constexpr u8 SPURIOUS_INTERRUPT_VECTOR = 0xff;
inline constexpr u8 TIMER_INTERRUPT_VECTOR = 0x20;
inline constexpr u8 RESCHEDULE_INTERRUPT_VECTOR = 0x21;

class LocalApic {
public:
    static KResult initialize_for_boot_processor();
    static KResult initialize_for_current_processor();

    static bool is_available();

    static u32 current_id();

    static void end_of_interrupt();

    static KResult start_periodic_timer(u32 frequency_hz, u8 vector);
    static void stop_timer();

    static u64 calibrated_timer_frequency_hz();
    static u64 calibrated_timestamp_frequency_hz();

    static void send_reschedule_interrupt(u32 target_id);
    static void send_init_assert(u32 target_id);
    static void send_startup(u32 target_id, PhysAddr trampoline);
};

}  // namespace barry::arch
