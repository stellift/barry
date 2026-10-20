#pragma once

#include "kernel/base/array.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/types.hpp"
#include "kernel/mm/page_region_allocator.hpp"
#include "kernel/sync/spin_lock.hpp"

namespace barry::mm {

struct HeapStatistics {
    u64 mapped_bytes { 0 };
    u64 allocated_bytes { 0 };
    u64 live_allocations { 0 };
};

class KernelHeap {
public:
    static constexpr usize MIN_ALIGNMENT = 16;
    static constexpr usize MAX_ALIGNMENT = PAGE_SIZE;

    KResult initialize();

    KResultOr<void*> allocate(usize size, usize alignment = MIN_ALIGNMENT);
    void deallocate(void* pointer);

    HeapStatistics statistics() const;

private:
    static constexpr usize SIZE_CLASS_COUNT = 8;
    static constexpr u16 LARGE_ALLOCATION = 0xffff;
    static constexpr u32 HEADER_MAGIC = 0x42'52'52'59;
    static constexpr u64 SLAB_PAGES = 4;

    struct [[gnu::packed]] AllocationHeader {
        u32 magic;
        u16 size_class;
        u16 padding_bytes;
        u64 page_count;
    };

    static_assert(sizeof(AllocationHeader) == MIN_ALIGNMENT);

    struct FreeChunk {
        FreeChunk* next { nullptr };
    };

    struct State {
        PageRegionAllocator pages;
        Array<FreeChunk*, SIZE_CLASS_COUNT> free_lists { {} };
        u64 allocated_bytes { 0 };
        u64 live_allocations { 0 };
    };

    static Optional<usize> size_class_for(usize total_bytes);
    static usize chunk_size_of(usize size_class);

    KResultOr<void*> allocate_small(State& state, usize payload_size, usize size_class,
                                    usize alignment);
    KResultOr<void*> allocate_large(State& state, usize payload_size, usize total_bytes,
                                    usize alignment);
    KResult refill_size_class(State& state, usize size_class);

    static void* place_header(VirtRange block, usize payload_size, usize alignment,
                              usize size_class, u64 page_count);
    static AllocationHeader& header_of(void* payload);
    static VirtAddr block_start_of(AllocationHeader const& header);

    mutable sync::Protected<State> m_state;
    bool m_initialized { false };
};

KernelHeap& kernel_heap();

KResult initialize_kernel_heap();

}  // namespace barry::mm
