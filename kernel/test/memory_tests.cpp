#include "kernel/base/own_ptr.hpp"
#include "kernel/base/ref_ptr.hpp"
#include "kernel/mm/direct_map.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/mm/kernel_address_space.hpp"
#include "kernel/mm/paging.hpp"
#include "kernel/mm/physical_memory.hpp"
#include "kernel/test/framework.hpp"

namespace barry {
namespace {

struct Tracked {
    explicit Tracked(u32 value) : id(value) { ++live_count; }
    ~Tracked() { --live_count; }

    Tracked(Tracked const&) = delete;
    Tracked& operator=(Tracked const&) = delete;

    u32 id;
    static u32 live_count;
};

u32 Tracked::live_count = 0;

struct Shared : RefCounted<Shared> {
    explicit Shared(u32 value) : id(value) { ++live_count; }
    ~Shared() { --live_count; }

    u32 id;
    static u32 live_count;
};

u32 Shared::live_count = 0;

}  // namespace

KERNEL_TEST(physical_allocator_round_trips_a_page) {
    auto& pmm = mm::physical_memory();
    auto const before = pmm.statistics();

    auto page = pmm.allocate_page();
    EXPECT(page.has_value());
    EXPECT(!page.value().is_null());
    EXPECT(page.value().is_page_aligned());
    EXPECT(pmm.is_managed(page.value()));

    EXPECT_EQ(pmm.statistics().allocated_pages, before.allocated_pages + 1);

    pmm.free_page(page.value());
    EXPECT_EQ(pmm.statistics().allocated_pages, before.allocated_pages);
}

KERNEL_TEST(physical_allocator_serves_contiguous_runs) {
    auto& pmm = mm::physical_memory();
    auto const before = pmm.statistics();

    auto range = pmm.allocate_contiguous(8);
    EXPECT(range.has_value());
    EXPECT_EQ(range.value().page_count(), 8u);
    EXPECT(range.value().base.is_page_aligned());

    EXPECT_EQ(pmm.statistics().allocated_pages, before.allocated_pages + 8);

    pmm.free_contiguous(range.value());
    EXPECT_EQ(pmm.statistics().allocated_pages, before.allocated_pages);

    EXPECT_ERROR(pmm.allocate_contiguous(0));
}

KERNEL_TEST(zeroed_pages_really_are_zero) {
    auto& pmm = mm::physical_memory();
    auto page = pmm.allocate_zeroed_page();
    EXPECT(page.has_value());

    auto const bytes = mm::direct_map_bytes(PhysRange { page.value(), PAGE_SIZE });
    bool all_zero = true;
    for (u8 const value : bytes) {
        if (value != 0)
            all_zero = false;
    }
    EXPECT(all_zero);

    pmm.free_page(page.value());
}

KERNEL_TEST(direct_map_converts_both_ways) {
    PhysAddr const physical { 0x12345000 };
    VirtAddr const mapped = mm::direct_map_of(physical);

    EXPECT(mm::is_direct_mapped(mapped));
    EXPECT_EQ(mm::physical_address_of(mapped), physical);
    EXPECT_EQ(mapped.raw() - mm::direct_map_base(), physical.raw());
}

KERNEL_TEST(kernel_address_space_translates_its_own_code) {
    auto& space = mm::kernel_address_space();
    EXPECT(space.is_valid());

    auto const ranges = mm::kernel_image_ranges();
    auto const code = space.translate(ranges.code.base);
    EXPECT(code.has_value());

    auto const writable = space.translate(ranges.writable.base);
    EXPECT(writable.has_value());

    EXPECT(!space.translate(VirtAddr(0xffff'0000'0000'0000)).has_value());
}

KERNEL_TEST(mapping_a_page_makes_it_readable_and_writable) {
    auto& pmm = mm::physical_memory();
    auto& space = mm::kernel_address_space();

    auto frame = pmm.allocate_zeroed_page();
    EXPECT(frame.has_value());

    VirtAddr const probe { 0xffff'fe00'0000'0000 };
    EXPECT_OK(space.map_page(probe, frame.value(), mm::KERNEL_DATA_FLAGS));

    auto const translated = space.translate(probe);
    EXPECT(translated.has_value());
    EXPECT_EQ(translated.value(), frame.value());

    auto* word = probe.as_ptr<u64>();
    *word = 0xcafebabedeadbeef;
    EXPECT_EQ(*word, 0xcafebabedeadbeefu);

    EXPECT_ERROR(space.map_page(probe, frame.value(), mm::KERNEL_DATA_FLAGS));
    EXPECT_OK(space.unmap_page(probe));
    EXPECT(!space.translate(probe).has_value());

    pmm.free_page(frame.value());
}

KERNEL_TEST(mapping_rejects_unaligned_and_noncanonical_addresses) {
    auto& space = mm::kernel_address_space();
    EXPECT_ERROR(space.map_page(VirtAddr(0xffff'fe00'0000'0001), PhysAddr(0x1000),
                                mm::KERNEL_DATA_FLAGS));
    EXPECT_ERROR(space.map_page(VirtAddr(0x0000'8000'0000'0000), PhysAddr(0x1000),
                                mm::KERNEL_DATA_FLAGS));
}

KERNEL_TEST(heap_serves_small_allocations_and_reuses_them) {
    auto& heap = mm::kernel_heap();
    auto const before = heap.statistics();

    auto first = heap.allocate(64);
    EXPECT(first.has_value());
    void* const first_pointer = first.value();

    auto* bytes = static_cast<u8*>(first_pointer);
    for (usize index = 0; index < 64; ++index)
        bytes[index] = static_cast<u8>(index);
    EXPECT_EQ(bytes[63], 63u);

    heap.deallocate(first_pointer);

    auto second = heap.allocate(64);
    EXPECT(second.has_value());
    EXPECT_EQ(second.value(), first_pointer);
    heap.deallocate(second.value());

    EXPECT_EQ(heap.statistics().live_allocations, before.live_allocations);
}

KERNEL_TEST(heap_honours_alignment_requests) {
    auto& heap = mm::kernel_heap();

    for (usize alignment = 16; alignment <= PAGE_SIZE; alignment *= 2) {
        auto allocation = heap.allocate(100, alignment);
        EXPECT(allocation.has_value());
        EXPECT(is_aligned(reinterpret_cast<u64>(allocation.value()),
                          static_cast<u64>(alignment)));
        heap.deallocate(allocation.value());
    }

    EXPECT_ERROR(heap.allocate(16, 24));
    EXPECT_ERROR(heap.allocate(16, PAGE_SIZE * 2));
}

KERNEL_TEST(heap_serves_allocations_larger_than_a_page) {
    auto& heap = mm::kernel_heap();
    auto const before = heap.statistics();

    auto allocation = heap.allocate(32_KiB);
    EXPECT(allocation.has_value());

    auto* bytes = static_cast<u8*>(allocation.value());
    bytes[0] = 1;
    bytes[32_KiB - 1] = 2;
    EXPECT_EQ(bytes[0], 1u);
    EXPECT_EQ(bytes[32_KiB - 1], 2u);

    heap.deallocate(allocation.value());
    EXPECT_EQ(heap.statistics().live_allocations, before.live_allocations);
}

KERNEL_TEST(own_ptr_destroys_exactly_once) {
    u32 const before = Tracked::live_count;

    {
        auto owned = make_own<Tracked>(7u);
        EXPECT(owned.has_value());
        EXPECT_EQ(owned.value()->id, 7u);
        EXPECT_EQ(Tracked::live_count, before + 1);

        auto moved = move(owned.value());
        EXPECT(owned.value().is_null());
        EXPECT_EQ(moved->id, 7u);
    }

    EXPECT_EQ(Tracked::live_count, before);
}

KERNEL_TEST(ref_ptr_counts_references) {
    u32 const before = Shared::live_count;

    {
        auto created = make_ref<Shared>(9u);
        EXPECT(created.has_value());

        RefPtr<Shared> first = created.release_value();
        EXPECT_EQ(first->reference_count(), 1u);

        {
            RefPtr<Shared> second = first;
            EXPECT_EQ(first->reference_count(), 2u);
            EXPECT_EQ(second->id, 9u);
        }

        EXPECT_EQ(first->reference_count(), 1u);
        EXPECT_EQ(Shared::live_count, before + 1);
    }

    EXPECT_EQ(Shared::live_count, before);
}

}  // namespace barry
