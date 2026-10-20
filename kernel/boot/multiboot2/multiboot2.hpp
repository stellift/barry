#pragma once

#include "kernel/base/types.hpp"

namespace barry::boot::multiboot2 {

inline constexpr u32 BOOTLOADER_MAGIC = 0x36d76289;

enum class TagType : u32 {
    End = 0,
    CommandLine = 1,
    BootLoaderName = 2,
    Module = 3,
    BasicMemoryInfo = 4,
    MemoryMap = 6,
    Framebuffer = 8,
    AcpiOldRsdp = 14,
    AcpiNewRsdp = 15,
};

enum class MemoryMapType : u32 {
    Available = 1,
    Reserved = 2,
    AcpiReclaimable = 3,
    AcpiNonVolatile = 4,
    Defective = 5,
};

enum class FramebufferType : u8 {
    Indexed = 0,
    DirectRgb = 1,
    EgaText = 2,
};

struct [[gnu::packed]] InfoHeader {
    u32 total_size;
    u32 reserved;
};

struct [[gnu::packed]] Tag {
    u32 type;
    u32 size;
};

struct [[gnu::packed]] StringTag {
    Tag tag;
    char text[1];
};

struct [[gnu::packed]] MemoryMapTag {
    Tag tag;
    u32 entry_size;
    u32 entry_version;
};

struct [[gnu::packed]] MemoryMapEntry {
    u64 base;
    u64 length;
    u32 type;
    u32 reserved;
};

struct [[gnu::packed]] ModuleTag {
    Tag tag;
    u32 start;
    u32 end;
    char name[1];
};

struct [[gnu::packed]] FramebufferTag {
    Tag tag;
    u64 address;
    u32 pitch;
    u32 width;
    u32 height;
    u8 bits_per_pixel;
    u8 framebuffer_type;
    u8 reserved;
    u8 red_field_position;
    u8 red_mask_size;
    u8 green_field_position;
    u8 green_mask_size;
    u8 blue_field_position;
    u8 blue_mask_size;
};

struct [[gnu::packed]] AcpiRsdpTag {
    Tag tag;
    u8 rsdp[1];
};

}  // namespace barry::boot::multiboot2
