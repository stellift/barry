#include "kernel/mm/physical_memory.hpp"

#include "kernel/base/bytes.hpp"
#include "kernel/mm/direct_map.hpp"

namespace barry::mm {
namespace {

PhysicalMemoryManager g_physical_memory;

constexpr u64 NULL_FRAME_INDEX = 0;

u64 page_index_of(PhysAddr address) {
    return address.page_frame_number();
}

void reserve_null_frame(Bitmap& frames) {
    frames.set(NULL_FRAME_INDEX);
}

Optional<PhysRange> aligned_slice_of(boot::MemoryRegion const& region, u64 required_bytes) {
    if (!region.is_allocatable())
        return nullopt;

    PhysAddr const base = region.base.page_align_up();
    if (base >= region.end())
        return nullopt;
    if (base.distance_to(region.end()) < required_bytes)
        return nullopt;

    return PhysRange { base, required_bytes };
}

}  // namespace

KResultOr<PhysRange> PhysicalMemoryManager::reserve_bitmap_storage(
    boot::BootInfo const& boot_info, u64 required_bytes) {
    u64 const required_pages = div_ceil(required_bytes, PAGE_SIZE);

    for (auto const& region : boot_info.memory_map) {
        if (auto slice = aligned_slice_of(region, required_pages * PAGE_SIZE))
            return slice.release_value();
    }

    return Error(Errno::ENOMEM, "no usable region large enough for the frame bitmap");
}

void PhysicalMemoryManager::mark_usable_regions(State& state, boot::BootInfo const& boot_info) {
    for (auto const& region : boot_info.memory_map) {
        if (!region.is_allocatable())
            continue;

        u64 const first = region.base.page_align_up().page_frame_number();
        u64 const last = region.end().page_align_down().page_frame_number();
        if (last <= first || first >= state.managed_page_count)
            continue;

        u64 const count = min(last - first, state.managed_page_count - first);
        state.frames.set_range(first, count, false);
    }
}

KResult PhysicalMemoryManager::initialize(boot::BootInfo const& boot_info) {
    if (m_initialized)
        return Error(Errno::EBUSY, "the physical memory manager is already initialized");

    PhysAddr const highest = boot_info.highest_usable_address();
    if (highest.is_null())
        return Error(Errno::ENOMEM, "the boot memory map reports no usable memory");

    u64 const page_count = highest.page_frame_number();
    u64 const bitmap_bytes = Bitmap::bytes_needed_for(page_count);
    PhysRange const storage = TRY(reserve_bitmap_storage(boot_info, bitmap_bytes));

    auto words = direct_map_span<Bitmap::Word>(storage.base, storage.length / sizeof(Bitmap::Word));

    auto guard = m_state.lock();
    guard->frames = Bitmap(words, page_count);
    guard->managed_page_count = page_count;
    guard->frames.fill(true);

    mark_usable_regions(*guard, boot_info);

    u64 const bitmap_first_page = storage.base.page_frame_number();
    guard->frames.set_range(bitmap_first_page, storage.page_count(), true);

    reserve_null_frame(guard->frames);

    u64 free_pages = 0;
    for (u64 index = 0; index < page_count; ++index) {
        if (!guard->frames.get(index))
            ++free_pages;
    }
    guard->allocated_page_count = page_count - free_pages;
    guard->search_cursor = 0;

    m_initialized = true;
    return {};
}

KResultOr<u64> PhysicalMemoryManager::claim_run(State& state, u64 page_count) {
    auto found = state.frames.find_clear_run(page_count, state.search_cursor);
    if (!found.has_value() && state.search_cursor != 0)
        found = state.frames.find_clear_run(page_count, 0);

    if (!found.has_value())
        return Error(Errno::ENOMEM, "no contiguous run of free physical pages");

    u64 const start = found.value();
    state.frames.set_range(start, page_count, true);
    state.allocated_page_count += page_count;
    state.search_cursor = start + page_count;
    if (state.search_cursor >= state.managed_page_count)
        state.search_cursor = 0;

    return start;
}

KResultOr<u64> PhysicalMemoryManager::claim_frame_below(State& state, u64 frame_limit) {
    u64 const highest = min(frame_limit, state.managed_page_count);

    for (u64 frame = 1; frame < highest; ++frame) {
        if (state.frames.get(frame))
            continue;

        state.frames.set(frame);
        ++state.allocated_page_count;
        return frame;
    }

    return Error(Errno::ENOMEM, "no free physical page below the requested limit");
}

KResultOr<PhysAddr> PhysicalMemoryManager::allocate_page_below(PhysAddr limit) {
    auto guard = m_state.lock();
    u64 const frame = TRY(claim_frame_below(*guard, limit.page_frame_number()));
    return PhysAddr::from_page_frame_number(frame);
}

KResultOr<PhysAddr> PhysicalMemoryManager::allocate_page() {
    auto range = TRY(allocate_contiguous(1));
    return range.base;
}

KResultOr<PhysAddr> PhysicalMemoryManager::allocate_zeroed_page() {
    PhysAddr const page = TRY(allocate_page());
    zero_bytes(direct_map_bytes(PhysRange { page, PAGE_SIZE }));
    return page;
}

KResultOr<PhysRange> PhysicalMemoryManager::allocate_contiguous(u64 page_count) {
    if (page_count == 0)
        return Error(Errno::EINVAL, "cannot allocate zero physical pages");

    auto guard = m_state.lock();
    u64 const start = TRY(claim_run(*guard, page_count));
    return PhysRange { PhysAddr::from_page_frame_number(start), page_count * PAGE_SIZE };
}

void PhysicalMemoryManager::free_page(PhysAddr address) {
    free_contiguous(PhysRange { address, PAGE_SIZE });
}

void PhysicalMemoryManager::free_contiguous(PhysRange range) {
    VERIFY(!range.base.is_null());
    VERIFY(range.base.is_page_aligned());
    VERIFY(range.length != 0 && (range.length % PAGE_SIZE) == 0);

    auto guard = m_state.lock();
    u64 const first = page_index_of(range.base);
    u64 const count = range.page_count();
    VERIFY(first + count <= guard->managed_page_count);
    VERIFY(!guard->frames.is_range_clear(first, count));

    guard->frames.set_range(first, count, false);
    VERIFY(guard->allocated_page_count >= count);
    guard->allocated_page_count -= count;
}

PhysicalMemoryStatistics PhysicalMemoryManager::statistics() const {
    auto guard = m_state.lock();
    PhysicalMemoryStatistics result {};
    result.managed_pages = guard->managed_page_count;
    result.allocated_pages = guard->allocated_page_count;
    result.free_pages = guard->managed_page_count - guard->allocated_page_count;
    return result;
}

bool PhysicalMemoryManager::is_managed(PhysAddr address) const {
    auto guard = m_state.lock();
    return page_index_of(address) < guard->managed_page_count;
}

PhysicalMemoryManager& physical_memory() {
    return g_physical_memory;
}

}  // namespace barry::mm
