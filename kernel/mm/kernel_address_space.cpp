#include "kernel/mm/kernel_address_space.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/log.hpp"
#include "kernel/mm/direct_map.hpp"

extern "C" char __kernel_virtual_base[];
extern "C" char __kernel_text_start[];
extern "C" char __kernel_text_end[];
extern "C" char __kernel_readonly_start[];
extern "C" char __kernel_readonly_end[];
extern "C" char __kernel_writable_start[];
extern "C" char __kernel_writable_end[];

namespace barry::mm {
namespace {

AddressSpace g_kernel_address_space;

VirtRange range_between(char const* start, char const* end) {
    VirtAddr const base = VirtAddr::from_ptr(start);
    VirtAddr const limit = VirtAddr::from_ptr(end);
    VERIFY(limit >= base);
    return VirtRange { base, base.distance_to(limit) };
}

bool region_is_system_memory(boot::MemoryKind kind) {
    switch (kind) {
    case boot::MemoryKind::Usable:
    case boot::MemoryKind::LoaderReclaimable:
    case boot::MemoryKind::AcpiReclaimable:
    case boot::MemoryKind::AcpiNonVolatile:
    case boot::MemoryKind::KernelImage:
        return true;
    case boot::MemoryKind::Reserved:
    case boot::MemoryKind::Defective:
    case boot::MemoryKind::Framebuffer:
        return false;
    }
    return false;
}

void enable_paging_features() {
    arch::write_msr(arch::MSR_EFER, arch::read_msr(arch::MSR_EFER) | arch::EFER_NO_EXECUTE_ENABLE);
    arch::write_cr4(arch::read_cr4() | arch::CR4_PAGE_GLOBAL_ENABLE);
}

KResult map_direct_window(AddressSpace& space, PhysRange range) {
    PhysAddr const base = range.base.page_align_down();
    PhysAddr const limit = range.end().page_align_up();
    VirtRange const window { direct_map_of(base), base.distance_to(limit) };
    return space.map_range(window, base, KERNEL_DATA_FLAGS);
}

KResult map_system_memory(AddressSpace& space, boot::BootInfo const& boot_info) {
    Optional<PhysRange> pending;

    for (auto const& region : boot_info.memory_map) {
        if (!region_is_system_memory(region.kind))
            continue;

        PhysRange const current { region.base.page_align_down(),
                                  region.base.page_align_down().distance_to(
                                      region.end().page_align_up()) };

        if (!pending.has_value()) {
            pending = current;
            continue;
        }

        if (current.base <= pending->end()) {
            PhysAddr const merged_end = max(pending->end(), current.end());
            pending = PhysRange { pending->base, pending->base.distance_to(merged_end) };
            continue;
        }

        TRY(map_direct_window(space, pending.value()));
        pending = current;
    }

    if (pending.has_value())
        TRY(map_direct_window(space, pending.value()));

    return {};
}

KResult map_kernel_image(AddressSpace& space, boot::BootInfo const& boot_info) {
    auto const ranges = kernel_image_ranges();
    VirtAddr const virtual_base = boot_info.kernel_image.virtual_base;
    PhysAddr const physical_base = boot_info.kernel_image.physical_base;

    auto physical_for = [&](VirtAddr address) {
        return physical_base.offset(virtual_base.distance_to(address));
    };

    TRY(space.map_range(ranges.code, physical_for(ranges.code.base), KERNEL_CODE_FLAGS));
    TRY(space.map_range(ranges.readonly, physical_for(ranges.readonly.base),
                        KERNEL_READONLY_FLAGS));
    TRY(space.map_range(ranges.writable, physical_for(ranges.writable.base), KERNEL_DATA_FLAGS));
    return {};
}

KResult map_framebuffer(AddressSpace& space, boot::BootInfo const& boot_info) {
    if (!boot_info.framebuffer.has_value())
        return {};

    auto const& framebuffer = boot_info.framebuffer.value();
    u64 const length = align_up<u64>(framebuffer.pitch * framebuffer.height, PAGE_SIZE);
    VirtRange const window { direct_map_of(framebuffer.base), length };
    return space.map_range(window, framebuffer.base, DEVICE_MEMORY_FLAGS);
}

}  // namespace

KernelImageRanges kernel_image_ranges() {
    return KernelImageRanges {
        range_between(__kernel_text_start, __kernel_text_end),
        range_between(__kernel_readonly_start, __kernel_readonly_end),
        range_between(__kernel_writable_start, __kernel_writable_end),
    };
}

KResult build_and_activate_kernel_address_space(boot::BootInfo const& boot_info) {
    enable_paging_features();

    auto space = TRY(AddressSpace::create_empty());

    TRY(map_system_memory(space, boot_info));
    TRY(map_kernel_image(space, boot_info));
    TRY(map_framebuffer(space, boot_info));

    g_kernel_address_space = space;
    g_kernel_address_space.activate();
    return {};
}

AddressSpace& kernel_address_space() {
    return g_kernel_address_space;
}

KResultOr<VirtAddr> map_device_memory(PhysRange range) {
    if (range.length == 0)
        return Error(Errno::EINVAL, "cannot map an empty device range");

    PhysAddr const base = range.base.page_align_down();
    PhysAddr const limit = range.end().page_align_up();
    VirtAddr const window = direct_map_of(base);

    for (u64 offset = 0; offset < base.distance_to(limit); offset += PAGE_SIZE) {
        VirtAddr const virtual_address = window.offset(offset);
        if (g_kernel_address_space.translate(virtual_address).has_value())
            continue;
        TRY(g_kernel_address_space.map_page(virtual_address, base.offset(offset),
                                            DEVICE_MEMORY_FLAGS));
    }

    return direct_map_of(range.base);
}

}  // namespace barry::mm
