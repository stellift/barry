#pragma once

#include "kernel/base/types.hpp"

namespace barry::boot::multiboot {

inline constexpr u32 BOOTLOADER_MAGIC = 0x2badb002;

enum class InfoFlag : u32 {
    BasicMemory = 1u << 0,
    BootDevice = 1u << 1,
    CommandLine = 1u << 2,
    Modules = 1u << 3,
    MemoryMap = 1u << 6,
    BootLoaderName = 1u << 9,
    FramebufferInfo = 1u << 12,
};

enum class MemoryMapType : u32 {
    Available = 1,
    Reserved = 2,
    AcpiReclaimable = 3,
    AcpiNonVolatile = 4,
    Defective = 5,
};

struct [[gnu::packed]] Info {
    u32 flags;
    u32 memory_lower;
    u32 memory_upper;
    u32 boot_device;
    u32 command_line;
    u32 module_count;
    u32 module_address;
    u32 symbol_table[4];
    u32 memory_map_length;
    u32 memory_map_address;
    u32 drives_length;
    u32 drives_address;
    u32 config_table;
    u32 bootloader_name;
    u32 apm_table;
    u32 vbe_control_info;
    u32 vbe_mode_info;
    u16 vbe_mode;
    u16 vbe_interface_segment;
    u16 vbe_interface_offset;
    u16 vbe_interface_length;
    u64 framebuffer_address;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8 framebuffer_bits_per_pixel;
    u8 framebuffer_type;
    u8 framebuffer_colour_info[6];
};

struct [[gnu::packed]] MemoryMapEntry {
    u32 entry_size;
    u64 base;
    u64 length;
    u32 type;
};

struct [[gnu::packed]] Module {
    u32 start;
    u32 end;
    u32 name;
    u32 reserved;
};

}  // namespace barry::boot::multiboot
