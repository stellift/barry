#pragma once

#include "kernel/boot/boot_info.hpp"
#include "kernel/base/array.hpp"
#include "kernel/base/types.hpp"

namespace barry::boot {

inline constexpr usize MAX_MEMORY_REGIONS = 256;
inline constexpr usize MAX_BOOT_MODULES = 32;
inline constexpr usize MAX_PROCESSORS = 256;

class BootInfoBuilder {
public:
    bool add_memory_region(MemoryRegion region);
    bool add_module(BootModule module);
    bool add_processor(ProcessorDescriptor processor);

    void set_loader_name(StringView name) { m_info.loader_name = name; }
    void set_command_line(StringView command_line) { m_info.command_line = command_line; }
    void set_direct_map_base(u64 base) { m_info.direct_map_base = base; }
    void set_kernel_image(KernelImageLocation location) { m_info.kernel_image = location; }
    void set_framebuffer(Framebuffer framebuffer) { m_info.framebuffer = framebuffer; }
    void set_acpi_rsdp(PhysAddr address) { m_info.acpi_rsdp = address; }
    void set_boot_processor_hardware_id(u32 id) { m_info.boot_processor_hardware_id = id; }

    bool dropped_entries() const { return m_dropped_entries; }

    BootInfo const& finish();

private:
    BootInfo m_info {};
    Array<MemoryRegion, MAX_MEMORY_REGIONS> m_memory_regions { {} };
    Array<BootModule, MAX_BOOT_MODULES> m_modules { {} };
    Array<ProcessorDescriptor, MAX_PROCESSORS> m_processors { {} };
    usize m_memory_region_count { 0 };
    usize m_module_count { 0 };
    usize m_processor_count { 0 };
    bool m_dropped_entries { false };
};

}  // namespace barry::boot
