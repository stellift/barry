#include "kernel/boot/boot_info.hpp"

namespace barry::boot {

char const* memory_kind_name(MemoryKind kind) {
    switch (kind) {
    case MemoryKind::Usable:
        return "usable";
    case MemoryKind::Reserved:
        return "reserved";
    case MemoryKind::AcpiReclaimable:
        return "acpi-reclaimable";
    case MemoryKind::AcpiNonVolatile:
        return "acpi-nvs";
    case MemoryKind::Defective:
        return "defective";
    case MemoryKind::LoaderReclaimable:
        return "loader-reclaimable";
    case MemoryKind::KernelImage:
        return "kernel-image";
    case MemoryKind::Framebuffer:
        return "framebuffer";
    }
    return "unknown";
}

bool BootInfo::is_valid() const {
    if (memory_map.is_empty())
        return false;
    if (direct_map_base == 0)
        return false;
    if (!VirtAddr(direct_map_base).is_canonical())
        return false;
    if (kernel_image.virtual_base.is_null())
        return false;
    if (processors.is_empty())
        return false;

    PhysAddr previous_end {};
    for (auto const& region : memory_map) {
        if (region.length == 0)
            return false;
        if (region.end() < region.base)
            return false;
        if (region.base < previous_end)
            return false;
        previous_end = region.end();
    }
    return true;
}

u64 BootInfo::total_usable_bytes() const {
    u64 total = 0;
    for (auto const& region : memory_map) {
        if (region.is_allocatable())
            total += region.length;
    }
    return total;
}

PhysAddr BootInfo::highest_usable_address() const {
    PhysAddr highest {};
    for (auto const& region : memory_map) {
        if (region.is_allocatable() && region.end() > highest)
            highest = region.end();
    }
    return highest;
}

}  // namespace barry::boot
