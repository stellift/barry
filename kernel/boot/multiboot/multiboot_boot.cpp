#include "kernel/boot/multiboot/multiboot1.hpp"

#include "kernel/base/array.hpp"
#include "kernel/base/bytes.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/boot/boot_info_builder.hpp"
#include "kernel/boot/kernel_entry.hpp"

extern "C" char __kernel_virtual_base[];
extern "C" char __kernel_image_end[];
extern "C" char __boot_image_start[];
extern "C" char __boot_image_end[];
extern "C" char kernel_physical_base[];

namespace barry::boot::multiboot {
namespace {

constexpr u64 DIRECT_MAP_BASE = 0xffff800000000000ULL;
constexpr u64 DIRECT_MAP_WINDOW = 4_GiB;
constexpr usize MAX_RESERVATIONS = 8;
constexpr usize COMMAND_LINE_CAPACITY = 256;
constexpr usize LOADER_NAME_CAPACITY = 64;

BootInfoBuilder g_builder;
Array<char, COMMAND_LINE_CAPACITY> g_command_line { {} };
Array<char, LOADER_NAME_CAPACITY> g_loader_name { {} };
Array<PhysRange, MAX_RESERVATIONS> g_reservations { {} };
usize g_reservation_count = 0;

template<typename T>
T const* at_physical(u64 address) {
    return VirtAddr(DIRECT_MAP_BASE + address).as_ptr<T const>();
}

bool has_flag(Info const& info, InfoFlag flag) {
    return (info.flags & to_underlying(flag)) != 0;
}

StringView copy_cstring(Span<char> destination, u64 source_address) {
    if (source_address == 0)
        return StringView();

    char const* source = at_physical<char>(source_address);
    usize const length = min(length_of_cstring(source), destination.size() - 1);
    copy_bytes(as_writable_bytes(destination.first(length)),
               ReadonlyBytes(reinterpret_cast<u8 const*>(source), length));
    destination[length] = '\0';
    return StringView(destination.data(), length);
}

void add_reservation(PhysRange range) {
    if (range.length == 0 || g_reservation_count >= MAX_RESERVATIONS)
        return;
    g_reservations[g_reservation_count++] = range;
}

Span<PhysRange const> reservations() {
    return Span<PhysRange const>(g_reservations.data(), g_reservation_count);
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

Optional<PhysRange> clamp_to_direct_map_window(PhysRange range) {
    PhysAddr const window_end { DIRECT_MAP_WINDOW };
    if (range.base >= window_end)
        return nullopt;

    PhysAddr const end = min(range.end(), window_end);
    return PhysRange { range.base, range.base.distance_to(end) };
}

void emit_region_avoiding_reservations(PhysRange range, MemoryKind kind) {
    if (kind != MemoryKind::Usable) {
        g_builder.add_memory_region(MemoryRegion { range.base, range.length, kind });
        return;
    }

    PhysAddr cursor = range.base;

    while (cursor < range.end()) {
        PhysAddr next_reservation_start = range.end();
        PhysAddr next_reservation_end = range.end();

        for (auto const& reserved : reservations()) {
            if (reserved.end() <= cursor || reserved.base >= range.end())
                continue;
            if (reserved.base <= cursor) {
                cursor = max(cursor, reserved.end());
                next_reservation_start = range.end();
                next_reservation_end = range.end();
                break;
            }
            if (reserved.base < next_reservation_start) {
                next_reservation_start = reserved.base;
                next_reservation_end = reserved.end();
            }
        }

        if (cursor >= range.end())
            break;

        PhysAddr const chunk_end = min(next_reservation_start, range.end());
        if (chunk_end > cursor) {
            g_builder.add_memory_region(
                MemoryRegion { cursor, cursor.distance_to(chunk_end), MemoryKind::Usable });
        }

        cursor = max(chunk_end, next_reservation_end);
    }
}

void import_memory_map(Info const& info) {
    if (!has_flag(info, InfoFlag::MemoryMap))
        boot_failure("the multiboot loader provided no memory map");

    u64 offset = 0;
    while (offset < info.memory_map_length) {
        auto const* entry = at_physical<MemoryMapEntry>(info.memory_map_address + offset);
        if (entry->entry_size == 0)
            break;

        PhysRange const raw { PhysAddr(entry->base), entry->length };
        if (auto const clamped = clamp_to_direct_map_window(raw))
            emit_region_avoiding_reservations(clamped.value(), translate_memory_kind(entry->type));

        offset += entry->entry_size + sizeof(entry->entry_size);
    }
}

void import_reservations(Info const& info, u64 info_address) {
    PhysAddr const boot_start { reinterpret_cast<u64>(__boot_image_start) };
    PhysAddr const boot_end { reinterpret_cast<u64>(__boot_image_end) };
    add_reservation(PhysRange { boot_start, boot_start.distance_to(boot_end) });

    PhysAddr const kernel_start { reinterpret_cast<u64>(kernel_physical_base) };
    u64 const kernel_length = VirtAddr::from_ptr(__kernel_virtual_base)
                                  .distance_to(VirtAddr::from_ptr(__kernel_image_end));
    add_reservation(PhysRange { kernel_start, kernel_length });

    add_reservation(PhysRange { PhysAddr(info_address).page_align_down(), PAGE_SIZE });

    if (has_flag(info, InfoFlag::MemoryMap)) {
        add_reservation(PhysRange { PhysAddr(info.memory_map_address).page_align_down(),
                                    align_up<u64>(info.memory_map_length, PAGE_SIZE) });
    }

    if (has_flag(info, InfoFlag::CommandLine) && info.command_line != 0) {
        add_reservation(
            PhysRange { PhysAddr(info.command_line).page_align_down(), PAGE_SIZE });
    }
}

void import_identity(Info const& info) {
    StringView name = "multiboot"_sv;
    if (has_flag(info, InfoFlag::BootLoaderName))
        name = copy_cstring(g_loader_name.span(), info.bootloader_name);
    g_builder.set_loader_name(name.is_empty() ? "multiboot"_sv : name);

    if (has_flag(info, InfoFlag::CommandLine))
        g_builder.set_command_line(copy_cstring(g_command_line.span(), info.command_line));
}

void import_kernel_image() {
    g_builder.set_kernel_image(KernelImageLocation {
        PhysAddr(reinterpret_cast<u64>(kernel_physical_base)),
        VirtAddr::from_ptr(__kernel_virtual_base),
    });
}

void import_framebuffer(Info const& info) {
    if (!has_flag(info, InfoFlag::FramebufferInfo))
        return;
    if (info.framebuffer_address == 0 || info.framebuffer_width == 0)
        return;

    Framebuffer framebuffer {};
    framebuffer.base = PhysAddr(info.framebuffer_address);
    framebuffer.width = info.framebuffer_width;
    framebuffer.height = info.framebuffer_height;
    framebuffer.pitch = info.framebuffer_pitch;
    framebuffer.bits_per_pixel = info.framebuffer_bits_per_pixel;
    framebuffer.layout = PixelLayout::Unknown;
    g_builder.set_framebuffer(framebuffer);
}

void import_modules(Info const& info) {
    if (!has_flag(info, InfoFlag::Modules))
        return;

    for (u32 index = 0; index < info.module_count; ++index) {
        auto const* module
            = at_physical<Module>(info.module_address + index * sizeof(Module));
        g_builder.add_module(BootModule { PhysAddr(module->start),
                                          module->end - module->start, StringView() });
    }
}

}  // namespace
}  // namespace barry::boot::multiboot

extern "C" [[noreturn]] void barry_multiboot_main(barry::u32 magic, barry::u32 info_address) {
    using namespace barry;
    using namespace barry::boot;
    using namespace barry::boot::multiboot;

    if (magic != BOOTLOADER_MAGIC)
        boot_failure("the loader did not identify itself as multiboot compliant");

    auto const& info = *at_physical<Info>(info_address);

    g_builder.set_direct_map_base(DIRECT_MAP_BASE);
    import_identity(info);
    import_kernel_image();
    import_reservations(info, info_address);
    import_memory_map(info);
    import_framebuffer(info);
    import_modules(info);
    g_builder.add_processor(ProcessorDescriptor { 0, 0 });

    kernel_main(g_builder.finish());
}
