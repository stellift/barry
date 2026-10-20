#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/base/types.hpp"

namespace barry::boot {

enum class MemoryKind : u8 {
    Usable,
    Reserved,
    AcpiReclaimable,
    AcpiNonVolatile,
    Defective,
    LoaderReclaimable,
    KernelImage,
    Framebuffer,
};

char const* memory_kind_name(MemoryKind);

struct MemoryRegion {
    PhysAddr base;
    u64 length { 0 };
    MemoryKind kind { MemoryKind::Reserved };

    PhysAddr end() const { return base.offset(length); }
    bool is_allocatable() const { return kind == MemoryKind::Usable; }
};

enum class PixelLayout : u8 {
    Unknown,
    Rgb,
};

struct ChannelMask {
    u8 size { 0 };
    u8 shift { 0 };
};

struct Framebuffer {
    PhysAddr base;
    u64 width { 0 };
    u64 height { 0 };
    u64 pitch { 0 };
    u16 bits_per_pixel { 0 };
    PixelLayout layout { PixelLayout::Unknown };
    ChannelMask red;
    ChannelMask green;
    ChannelMask blue;
};

struct KernelImageLocation {
    PhysAddr physical_base;
    VirtAddr virtual_base;
};

struct BootModule {
    PhysAddr base;
    u64 size { 0 };
    StringView name;
};

struct ProcessorDescriptor {
    u32 index { 0 };
    u32 hardware_id { 0 };
};

struct BootInfo {
    StringView loader_name;
    StringView command_line;

    u64 direct_map_base { 0 };

    KernelImageLocation kernel_image;
    Span<MemoryRegion const> memory_map;
    Span<BootModule const> modules;
    Span<ProcessorDescriptor const> processors;

    Optional<Framebuffer> framebuffer;
    Optional<PhysAddr> acpi_rsdp;

    u32 boot_processor_hardware_id { 0 };

    bool is_valid() const;
    u64 total_usable_bytes() const;
    PhysAddr highest_usable_address() const;
};

}  // namespace barry::boot
