#pragma once

#include "kernel/boot/boot_info.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/mm/paging.hpp"

namespace barry::mm {

struct KernelImageRanges {
    VirtRange code;
    VirtRange readonly;
    VirtRange writable;
};

KernelImageRanges kernel_image_ranges();

KResult build_and_activate_kernel_address_space(boot::BootInfo const& boot_info);

AddressSpace& kernel_address_space();

KResultOr<VirtAddr> map_device_memory(PhysRange range);

}  // namespace barry::mm
