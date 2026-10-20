#include "kernel/boot/multiboot2/multiboot2.hpp"

#include "kernel/base/array.hpp"
#include "kernel/base/bytes.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/boot/boot_info_builder.hpp"
#include "kernel/boot/kernel_entry.hpp"
#include "kernel/boot/memory_reservations.hpp"

extern "C" char __kernel_virtual_base[];
extern "C" char __kernel_image_end[];
extern "C" char __boot_image_start[];
extern "C" char __boot_image_end[];
extern "C" char kernel_physical_base[];

namespace barry::boot::multiboot2 {
namespace {

constexpr u64 DIRECT_MAP_BASE = 0xffff800000000000ULL;
constexpr u64 DIRECT_MAP_WINDOW = 4_GiB;
constexpr usize COMMAND_LINE_CAPACITY = 256;
constexpr usize LOADER_NAME_CAPACITY = 64;
constexpr u32 TAG_ALIGNMENT = 8;

BootInfoBuilder g_builder;
MemoryReservations g_reservations;
Array<char, COMMAND_LINE_CAPACITY> g_command_line { {} };
Array<char, LOADER_NAME_CAPACITY> g_loader_name { {} };

template<typename T>
T const& at_physical(u64 address) {
    return *VirtAddr(DIRECT_MAP_BASE + address).as_ptr<T const>();
}

StringView copy_cstring(Span<char> destination, char const* source) {
    if (source == nullptr || destination.size() < 2)
        return StringView();

    usize const length = min(length_of_cstring(source), destination.size() - 1);
    copy_bytes(as_writable_bytes(destination.first(length)),
               ReadonlyBytes(reinterpret_cast<u8 const*>(source), length));
    destination[length] = '\0';
    return StringView(destination.data(), length);
}

MemoryKind translate_memory_kind(u32 type) {
    switch (static_cast<MemoryMapType>(type)) {
    case MemoryMapType::Available:
        return MemoryKind::Usable;
    case MemoryMapType::AcpiReclaimable:
        return MemoryKind::AcpiReclaimable;
    case MemoryMapType::AcpiNonVolatile:
        return MemoryKind::AcpiNonVolatile;
    case MemoryMapType::Defective:
        return MemoryKind::Defective;
    case MemoryMapType::Reserved:
        break;
    }
    return MemoryKind::Reserved;
}

PixelLayout translate_pixel_layout(u8 framebuffer_type) {
    return static_cast<FramebufferType>(framebuffer_type) == FramebufferType::DirectRgb
               ? PixelLayout::Rgb
               : PixelLayout::Unknown;
}

Optional<PhysRange> clamp_to_direct_map_window(PhysRange range) {
    PhysAddr const window_end { DIRECT_MAP_WINDOW };
    if (range.base >= window_end || range.length == 0)
        return nullopt;

    PhysAddr const end = min(range.end(), window_end);
    return PhysRange { range.base, range.base.distance_to(end) };
}

void collect_reservations(u64 info_address, u32 info_size) {
    PhysAddr const boot_start { reinterpret_cast<u64>(__boot_image_start) };
    PhysAddr const boot_end { reinterpret_cast<u64>(__boot_image_end) };
    g_reservations.add(PhysRange { boot_start, boot_start.distance_to(boot_end) });

    PhysAddr const kernel_start { reinterpret_cast<u64>(kernel_physical_base) };
    u64 const kernel_length = VirtAddr::from_ptr(__kernel_virtual_base)
                                  .distance_to(VirtAddr::from_ptr(__kernel_image_end));
    g_reservations.add(PhysRange { kernel_start, kernel_length });

    PhysAddr const info_start = PhysAddr(info_address).page_align_down();
    u64 const info_length = align_up<u64>(info_size + PhysAddr(info_address).page_offset(),
                                          PAGE_SIZE);
    g_reservations.add(PhysRange { info_start, info_length });
}

void emit_reserved_regions() {
    for (auto const& reserved : g_reservations.all())
        g_builder.add_memory_region(MemoryRegion { reserved.base, reserved.length,
                                                   MemoryKind::KernelImage });
}

void emit_memory_region(PhysRange range, MemoryKind kind) {
    if (kind != MemoryKind::Usable) {
        g_builder.add_memory_region(MemoryRegion { range.base, range.length, kind });
        return;
    }

    Array<PhysRange, MemoryReservations::MAX_FRAGMENTS> fragments { {} };
    usize const count = g_reservations.split(range, fragments.span());

    for (usize index = 0; index < count; ++index) {
        g_builder.add_memory_region(
            MemoryRegion { fragments[index].base, fragments[index].length, MemoryKind::Usable });
    }
}

void import_memory_map(MemoryMapTag const& tag) {
    if (tag.entry_size < sizeof(MemoryMapEntry))
        boot_failure("the multiboot2 memory map entries are too small");

    u32 offset = sizeof(MemoryMapTag);
    while (offset + tag.entry_size <= tag.tag.size) {
        auto const* entry = reinterpret_cast<MemoryMapEntry const*>(
            reinterpret_cast<u8 const*>(&tag) + offset);

        PhysRange const raw { PhysAddr(entry->base), entry->length };
        if (auto const clamped = clamp_to_direct_map_window(raw))
            emit_memory_region(clamped.value(), translate_memory_kind(entry->type));

        offset += tag.entry_size;
    }
}

void import_framebuffer(FramebufferTag const& tag) {
    if (tag.address == 0 || tag.width == 0 || tag.height == 0)
        return;

    Framebuffer framebuffer {};
    framebuffer.base = PhysAddr(tag.address);
    framebuffer.width = tag.width;
    framebuffer.height = tag.height;
    framebuffer.pitch = tag.pitch;
    framebuffer.bits_per_pixel = tag.bits_per_pixel;
    framebuffer.layout = translate_pixel_layout(tag.framebuffer_type);
    framebuffer.red = ChannelMask { tag.red_mask_size, tag.red_field_position };
    framebuffer.green = ChannelMask { tag.green_mask_size, tag.green_field_position };
    framebuffer.blue = ChannelMask { tag.blue_mask_size, tag.blue_field_position };
    g_builder.set_framebuffer(framebuffer);
}

void import_module(ModuleTag const& tag) {
    g_builder.add_module(BootModule { PhysAddr(tag.start), tag.end - tag.start,
                                      StringView(tag.name) });
}

void import_acpi_rsdp(AcpiRsdpTag const& tag) {
    PhysAddr const address { reinterpret_cast<u64>(&tag.rsdp[0]) - DIRECT_MAP_BASE };
    g_builder.set_acpi_rsdp(address);
}

void walk_tags(u64 info_address) {
    auto const& header = at_physical<InfoHeader>(info_address);
    u64 offset = sizeof(InfoHeader);

    while (offset + sizeof(Tag) <= header.total_size) {
        auto const& tag = at_physical<Tag>(info_address + offset);
        if (static_cast<TagType>(tag.type) == TagType::End)
            break;
        if (tag.size < sizeof(Tag))
            boot_failure("the multiboot2 information structure is malformed");

        switch (static_cast<TagType>(tag.type)) {
        case TagType::CommandLine:
            g_builder.set_command_line(
                copy_cstring(g_command_line.span(),
                             reinterpret_cast<StringTag const&>(tag).text));
            break;
        case TagType::BootLoaderName:
            g_builder.set_loader_name(
                copy_cstring(g_loader_name.span(),
                             reinterpret_cast<StringTag const&>(tag).text));
            break;
        case TagType::MemoryMap:
            import_memory_map(reinterpret_cast<MemoryMapTag const&>(tag));
            break;
        case TagType::Framebuffer:
            import_framebuffer(reinterpret_cast<FramebufferTag const&>(tag));
            break;
        case TagType::Module:
            import_module(reinterpret_cast<ModuleTag const&>(tag));
            break;
        case TagType::AcpiOldRsdp:
        case TagType::AcpiNewRsdp:
            import_acpi_rsdp(reinterpret_cast<AcpiRsdpTag const&>(tag));
            break;
        case TagType::BasicMemoryInfo:
        case TagType::End:
            break;
        }

        offset += align_up<u64>(tag.size, TAG_ALIGNMENT);
    }
}

void import_kernel_image() {
    g_builder.set_kernel_image(KernelImageLocation {
        PhysAddr(reinterpret_cast<u64>(kernel_physical_base)),
        VirtAddr::from_ptr(__kernel_virtual_base),
    });
}

}  // namespace
}  // namespace barry::boot::multiboot2

extern "C" [[noreturn]] void barry_multiboot2_main(barry::u32 magic, barry::u32 info_address) {
    using namespace barry;
    using namespace barry::boot;
    using namespace barry::boot::multiboot2;

    if (magic != BOOTLOADER_MAGIC)
        boot_failure("the loader did not identify itself as multiboot2 compliant");

    auto const& header = at_physical<InfoHeader>(info_address);

    g_builder.set_direct_map_base(DIRECT_MAP_BASE);
    g_builder.set_loader_name("multiboot2"_sv);
    import_kernel_image();
    collect_reservations(info_address, header.total_size);
    emit_reserved_regions();
    walk_tags(info_address);
    g_builder.add_processor(ProcessorDescriptor { 0, 0 });

    kernel_main(g_builder.finish());
}
