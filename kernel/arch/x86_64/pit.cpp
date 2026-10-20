#include "kernel/arch/x86_64/pit.hpp"

#include "kernel/arch/x86_64/io.hpp"
#include "kernel/base/utility.hpp"

namespace barry::arch {
namespace {

constexpr u16 CHANNEL_2_DATA = 0x42;
constexpr u16 MODE_COMMAND = 0x43;
constexpr u16 SPEAKER_CONTROL = 0x61;

constexpr u8 CHANNEL_2_ONE_SHOT = 0xb0;
constexpr u8 SPEAKER_GATE_ENABLE = 0x01;
constexpr u8 SPEAKER_OUTPUT_DISABLE = 0x02;
constexpr u8 SPEAKER_CHANNEL_2_OUTPUT = 0x20;

constexpr u32 MICROSECONDS_PER_SECOND = 1000000;
constexpr u32 MAX_COUNT = 0xffff;

u16 count_for(u32 microseconds) {
    u64 const ticks = (static_cast<u64>(PIT_INPUT_FREQUENCY_HZ) * microseconds)
                      / MICROSECONDS_PER_SECOND;
    return static_cast<u16>(min<u64>(ticks, MAX_COUNT));
}

void start_one_shot(u16 count) {
    u8 const control = in8(SPEAKER_CONTROL);
    out8(SPEAKER_CONTROL, static_cast<u8>((control & ~SPEAKER_OUTPUT_DISABLE)
                                          & ~SPEAKER_GATE_ENABLE));

    out8(MODE_COMMAND, CHANNEL_2_ONE_SHOT);
    out8(CHANNEL_2_DATA, static_cast<u8>(count & 0xff));
    out8(CHANNEL_2_DATA, static_cast<u8>(count >> 8));

    u8 const gated = in8(SPEAKER_CONTROL);
    out8(SPEAKER_CONTROL, static_cast<u8>(gated | SPEAKER_GATE_ENABLE));
}

bool one_shot_expired() {
    return (in8(SPEAKER_CONTROL) & SPEAKER_CHANNEL_2_OUTPUT) != 0;
}

void stop_one_shot() {
    u8 const control = in8(SPEAKER_CONTROL);
    out8(SPEAKER_CONTROL, static_cast<u8>(control & ~SPEAKER_GATE_ENABLE));
}

}  // namespace

void pit_busy_wait(u32 microseconds) {
    u32 remaining = microseconds;

    while (remaining > 0) {
        u32 const slice = min<u32>(remaining, 50000);
        start_one_shot(count_for(slice));
        while (!one_shot_expired())
            io_delay();
        stop_one_shot();
        remaining -= slice;
    }
}

}  // namespace barry::arch
