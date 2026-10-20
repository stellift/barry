#include "kernel/arch/x86_64/legacy_pic.hpp"

#include "kernel/arch/x86_64/io.hpp"

namespace barry::arch {
namespace {

constexpr u16 PRIMARY_COMMAND = 0x20;
constexpr u16 PRIMARY_DATA = 0x21;
constexpr u16 SECONDARY_COMMAND = 0xa0;
constexpr u16 SECONDARY_DATA = 0xa1;

constexpr u8 INIT_REQUIRE_FOURTH_WORD = 0x01;
constexpr u8 INIT_BEGIN = 0x10;
constexpr u8 MODE_8086 = 0x01;
constexpr u8 MASK_ALL = 0xff;
constexpr u8 SECONDARY_CASCADE_LINE = 0x04;
constexpr u8 SECONDARY_CASCADE_IDENTITY = 0x02;

void write_command(u16 port, u8 value) {
    out8(port, value);
    io_delay();
}

}  // namespace

void remap_and_mask_legacy_pic() {
    write_command(PRIMARY_COMMAND, INIT_BEGIN | INIT_REQUIRE_FOURTH_WORD);
    write_command(SECONDARY_COMMAND, INIT_BEGIN | INIT_REQUIRE_FOURTH_WORD);

    write_command(PRIMARY_DATA, PIC_PRIMARY_VECTOR_BASE);
    write_command(SECONDARY_DATA, PIC_SECONDARY_VECTOR_BASE);

    write_command(PRIMARY_DATA, SECONDARY_CASCADE_LINE);
    write_command(SECONDARY_DATA, SECONDARY_CASCADE_IDENTITY);

    write_command(PRIMARY_DATA, MODE_8086);
    write_command(SECONDARY_DATA, MODE_8086);

    write_command(PRIMARY_DATA, MASK_ALL);
    write_command(SECONDARY_DATA, MASK_ALL);
}

}  // namespace barry::arch
