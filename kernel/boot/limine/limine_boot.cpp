#define LIMINE_API_REVISION 3

#include "limine.h"

#include "kernel/boot/boot_info_builder.hpp"
#include "kernel/boot/kernel_entry.hpp"
#include "kernel/base/types.hpp"

namespace barry::boot::limine {
namespace {

constexpr u64 REQUESTED_STACK_SIZE = 64_KiB;

#define LIMINE_REQUEST_SECTION __attribute__((used, section(".limine_requests")))
#define LIMINE_MARKER_SECTION(name) __attribute__((used, section(name)))

LIMINE_MARKER_SECTION(".limine_requests_start") LIMINE_REQUESTS_START_MARKER

LIMINE_REQUEST_SECTION volatile LIMINE_BASE_REVISION(3)

LIMINE_REQUEST_SECTION volatile limine_memmap_request g_memmap_request {
    .id = LIMINE_MEMMAP_REQUEST, .revision = 0, .response = nullptr
};

LIMINE_REQUEST_SECTION volatile limine_hhdm_request g_hhdm_request {
    .id = LIMINE_HHDM_REQUEST, .revision = 0, .response = nullptr
};

LIMINE_REQUEST_SECTION volatile limine_executable_address_request g_executable_address_request {
    .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST, .revision = 0, .response = nullptr
};

LIMINE_REQUEST_SECTION volatile limine_framebuffer_request g_framebuffer_request {
    .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0, .response = nullptr
};

LIMINE_REQUEST_SECTION volatile limine_rsdp_request g_rsdp_request {
    .id = LIMINE_RSDP_REQUEST, .revision = 0, .response = nullptr
};

LIMINE_REQUEST_SECTION volatile limine_mp_request g_mp_request {
    .id = LIMINE_MP_REQUEST, .revision = 0, .response = nullptr, .flags = 0
};

LIMINE_REQUEST_SECTION volatile limine_module_request g_module_request {
    .id = LIMINE_MODULE_REQUEST,
    .revision = 0,
    .response = nullptr,
    .internal_module_count = 0,
    .internal_modules = nullptr
};

LIMINE_REQUEST_SECTION volatile limine_executable_cmdline_request g_cmdline_request {
    .id = LIMINE_EXECUTABLE_CMDLINE_REQUEST, .revision = 0, .response = nullptr
};

LIMINE_REQUEST_SECTION volatile limine_bootloader_info_request g_bootloader_info_request {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST, .revision = 0, .response = nullptr
};

LIMINE_REQUEST_SECTION volatile limine_stack_size_request g_stack_size_request {
    .id = LIMINE_STACK_SIZE_REQUEST,
    .revision = 0,
    .response = nullptr,
    .stack_size = REQUESTED_STACK_SIZE
};

LIMINE_REQUEST_SECTION volatile limine_paging_mode_request g_paging_mode_request {
    .id = LIMINE_PAGING_MODE_REQUEST,
    .revision = 0,
    .response = nullptr,
    .mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .max_mode = LIMINE_PAGING_MODE_X86_64_4LVL,
    .min_mode = LIMINE_PAGING_MODE_X86_64_4LVL
};

LIMINE_MARKER_SECTION(".limine_requests_end") LIMINE_REQUESTS_END_MARKER

BootInfoBuilder g_builder;

MemoryKind translate_memory_kind(u64 limine_type) {
    switch (limine_type) {
    case LIMINE_MEMMAP_USABLE:
        return MemoryKind::Usable;
    case LIMINE_MEMMAP_ACPI_RECLAIMABLE:
        return MemoryKind::AcpiReclaimable;
    case LIMINE_MEMMAP_ACPI_NVS:
        return MemoryKind::AcpiNonVolatile;
    case LIMINE_MEMMAP_BAD_MEMORY:
        return MemoryKind::Defective;
    case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
        return MemoryKind::LoaderReclaimable;
    case LIMINE_MEMMAP_EXECUTABLE_AND_MODULES:
        return MemoryKind::KernelImage;
    case LIMINE_MEMMAP_FRAMEBUFFER:
        return MemoryKind::Framebuffer;
    case LIMINE_MEMMAP_RESERVED:
    default:
        return MemoryKind::Reserved;
    }
}

PixelLayout translate_pixel_layout(u8 memory_model) {
    return memory_model == LIMINE_FRAMEBUFFER_RGB ? PixelLayout::Rgb : PixelLayout::Unknown;
}

StringView to_string_view(char const* text) {
    return text != nullptr ? StringView(text) : StringView();
}

void import_memory_map() {
    auto const* response = g_memmap_request.response;
    if (response == nullptr)
        boot_failure("Limine did not provide a memory map"_sv);

    for (u64 index = 0; index < response->entry_count; ++index) {
        auto const& entry = *response->entries[index];
        g_builder.add_memory_region(MemoryRegion { PhysAddr(entry.base), entry.length,
                                                   translate_memory_kind(entry.type) });
    }
}

u64 import_direct_map() {
    auto const* response = g_hhdm_request.response;
    if (response == nullptr)
        boot_failure("Limine did not provide a direct map offset"_sv);
    g_builder.set_direct_map_base(response->offset);
    return response->offset;
}

void import_kernel_image() {
    auto const* response = g_executable_address_request.response;
    if (response == nullptr)
        boot_failure("Limine did not report the kernel load address"_sv);
    g_builder.set_kernel_image(
        KernelImageLocation { PhysAddr(response->physical_base), VirtAddr(response->virtual_base) });
}

void import_framebuffer(u64 direct_map_base) {
    auto const* response = g_framebuffer_request.response;
    if (response == nullptr || response->framebuffer_count == 0)
        return;

    auto const& source = *response->framebuffers[0];
    auto const virtual_address = reinterpret_cast<u64>(source.address);
    if (virtual_address < direct_map_base)
        return;

    Framebuffer framebuffer {};
    framebuffer.base = PhysAddr(virtual_address - direct_map_base);
    framebuffer.width = source.width;
    framebuffer.height = source.height;
    framebuffer.pitch = source.pitch;
    framebuffer.bits_per_pixel = source.bpp;
    framebuffer.layout = translate_pixel_layout(source.memory_model);
    framebuffer.red = ChannelMask { source.red_mask_size, source.red_mask_shift };
    framebuffer.green = ChannelMask { source.green_mask_size, source.green_mask_shift };
    framebuffer.blue = ChannelMask { source.blue_mask_size, source.blue_mask_shift };
    g_builder.set_framebuffer(framebuffer);
}

void import_processors() {
    auto const* response = g_mp_request.response;
    if (response == nullptr) {
        g_builder.add_processor(ProcessorDescriptor { 0, 0 });
        return;
    }

    g_builder.set_boot_processor_hardware_id(response->bsp_lapic_id);
    for (u64 index = 0; index < response->cpu_count; ++index) {
        auto const& cpu = *response->cpus[index];
        g_builder.add_processor(ProcessorDescriptor { cpu.processor_id, cpu.lapic_id });
    }
}

void import_modules() {
    auto const* response = g_module_request.response;
    if (response == nullptr)
        return;

    for (u64 index = 0; index < response->module_count; ++index) {
        auto const& file = *response->modules[index];
        g_builder.add_module(BootModule { PhysAddr(reinterpret_cast<u64>(file.address)), file.size,
                                          to_string_view(file.path) });
    }
}

void import_acpi() {
    auto const* response = g_rsdp_request.response;
    if (response != nullptr && response->address != 0)
        g_builder.set_acpi_rsdp(PhysAddr(response->address));
}

void import_identity() {
    auto const* info = g_bootloader_info_request.response;
    g_builder.set_loader_name(info != nullptr ? to_string_view(info->name) : "limine"_sv);

    auto const* cmdline = g_cmdline_request.response;
    g_builder.set_command_line(cmdline != nullptr ? to_string_view(cmdline->cmdline)
                                                  : StringView());
}

}  // namespace
}  // namespace barry::boot::limine

extern "C" [[noreturn]] void barry_limine_entry() {
    using namespace barry;
    using namespace barry::boot::limine;

    if (!LIMINE_BASE_REVISION_SUPPORTED)
        boot_failure("Limine does not support base revision 3"_sv);

    import_identity();
    u64 const direct_map_base = import_direct_map();
    import_kernel_image();
    import_memory_map();
    import_framebuffer(direct_map_base);
    import_processors();
    import_modules();
    import_acpi();

    kernel_main(g_builder.finish());
}
