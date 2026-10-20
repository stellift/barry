#include "kernel/base/array.hpp"
#include "kernel/boot/command_line.hpp"
#include "kernel/boot/boot_info_builder.hpp"
#include "kernel/boot/memory_reservations.hpp"
#include "kernel/test/framework.hpp"

namespace barry {
namespace {

PhysRange range_of(u64 base, u64 length) {
    return PhysRange { PhysAddr(base), length };
}

}  // namespace

KERNEL_TEST(command_line_finds_flags_and_values) {
    boot::CommandLine const command_line { "barry.autotest root=/dev/sda1  loglevel=debug"_sv };

    EXPECT(command_line.has_flag("barry.autotest"_sv));
    EXPECT(command_line.has_flag("root"_sv));
    EXPECT(!command_line.has_flag("nosuchflag"_sv));

    auto const root = command_line.value_of("root"_sv);
    EXPECT(root.has_value());
    EXPECT_EQ(root.value(), "/dev/sda1"_sv);

    auto const level = command_line.value_of("loglevel"_sv);
    EXPECT(level.has_value());
    EXPECT_EQ(level.value(), "debug"_sv);

    EXPECT(!command_line.value_of("barry.autotest"_sv).has_value());
}

KERNEL_TEST(command_line_handles_an_empty_string) {
    boot::CommandLine const command_line {};
    EXPECT(!command_line.has_flag("anything"_sv));
    EXPECT(!command_line.value_of("anything"_sv).has_value());
}

KERNEL_TEST(reservations_split_a_region_around_holes) {
    boot::MemoryReservations reservations;
    EXPECT(reservations.add(range_of(0x2000, 0x1000)));
    EXPECT(reservations.add(range_of(0x5000, 0x2000)));
    EXPECT_EQ(reservations.count(), 2u);

    Array<PhysRange, boot::MemoryReservations::MAX_FRAGMENTS> fragments { {} };
    usize const count = reservations.split(range_of(0x1000, 0x8000), fragments.span());

    EXPECT_EQ(count, 3u);
    EXPECT_EQ(fragments[0].base, PhysAddr(0x1000));
    EXPECT_EQ(fragments[0].length, 0x1000u);
    EXPECT_EQ(fragments[1].base, PhysAddr(0x3000));
    EXPECT_EQ(fragments[1].length, 0x2000u);
    EXPECT_EQ(fragments[2].base, PhysAddr(0x7000));
    EXPECT_EQ(fragments[2].length, 0x2000u);
}

KERNEL_TEST(reservations_sort_themselves_and_merge_overlaps) {
    boot::MemoryReservations reservations;
    EXPECT(reservations.add(range_of(0x8000, 0x1000)));
    EXPECT(reservations.add(range_of(0x1000, 0x1000)));
    EXPECT(reservations.add(range_of(0x1800, 0x1000)));

    Array<PhysRange, boot::MemoryReservations::MAX_FRAGMENTS> fragments { {} };
    usize const count = reservations.split(range_of(0x0, 0x10000), fragments.span());

    EXPECT_EQ(count, 3u);
    EXPECT_EQ(fragments[0].base, PhysAddr(0x0));
    EXPECT_EQ(fragments[0].length, 0x1000u);
    EXPECT_EQ(fragments[1].base, PhysAddr(0x2800));
    EXPECT_EQ(fragments[1].length, 0x5800u);
    EXPECT_EQ(fragments[2].base, PhysAddr(0x9000));
    EXPECT_EQ(fragments[2].length, 0x7000u);
}

KERNEL_TEST(reservations_covering_everything_leave_nothing) {
    boot::MemoryReservations reservations;
    EXPECT(reservations.add(range_of(0x0, 0x10000)));
    EXPECT(!reservations.add(range_of(0x0, 0)));

    Array<PhysRange, boot::MemoryReservations::MAX_FRAGMENTS> fragments { {} };
    EXPECT_EQ(reservations.split(range_of(0x1000, 0x2000), fragments.span()), 0u);
}

KERNEL_TEST(boot_info_builder_sorts_the_memory_map) {
    boot::BootInfoBuilder builder;

    builder.set_direct_map_base(0xffff800000000000);
    builder.set_kernel_image(
        boot::KernelImageLocation { PhysAddr(0x200000), VirtAddr(0xffffffff80000000) });
    builder.add_processor(boot::ProcessorDescriptor { 0, 0 });

    EXPECT(builder.add_memory_region(
        boot::MemoryRegion { PhysAddr(0x200000), 0x100000, boot::MemoryKind::KernelImage }));
    EXPECT(builder.add_memory_region(
        boot::MemoryRegion { PhysAddr(0x0), 0x1000, boot::MemoryKind::Usable }));
    EXPECT(builder.add_memory_region(
        boot::MemoryRegion { PhysAddr(0x100000), 0x100000, boot::MemoryKind::Usable }));

    auto const& info = builder.finish();

    EXPECT_EQ(info.memory_map.size(), 3u);
    EXPECT_EQ(info.memory_map[0].base, PhysAddr(0x0));
    EXPECT_EQ(info.memory_map[1].base, PhysAddr(0x100000));
    EXPECT_EQ(info.memory_map[2].base, PhysAddr(0x200000));
    EXPECT(info.is_valid());
    EXPECT_EQ(info.total_usable_bytes(), 0x101000u);
}

KERNEL_TEST(boot_info_rejects_an_overlapping_memory_map) {
    boot::BootInfoBuilder builder;

    builder.set_direct_map_base(0xffff800000000000);
    builder.set_kernel_image(
        boot::KernelImageLocation { PhysAddr(0x200000), VirtAddr(0xffffffff80000000) });
    builder.add_processor(boot::ProcessorDescriptor { 0, 0 });

    EXPECT(builder.add_memory_region(
        boot::MemoryRegion { PhysAddr(0x1000), 0x4000, boot::MemoryKind::Usable }));
    EXPECT(builder.add_memory_region(
        boot::MemoryRegion { PhysAddr(0x3000), 0x4000, boot::MemoryKind::Usable }));

    EXPECT(!builder.finish().is_valid());
}

KERNEL_TEST(boot_info_rejects_a_missing_direct_map) {
    boot::BootInfoBuilder builder;
    builder.set_kernel_image(
        boot::KernelImageLocation { PhysAddr(0x200000), VirtAddr(0xffffffff80000000) });
    builder.add_processor(boot::ProcessorDescriptor { 0, 0 });
    EXPECT(builder.add_memory_region(
        boot::MemoryRegion { PhysAddr(0x1000), 0x1000, boot::MemoryKind::Usable }));

    EXPECT(!builder.finish().is_valid());
}

}  // namespace barry
