#include "kernel/mm/heap.hpp"

#include "kernel/base/allocator.hpp"

#include "kernel/base/assert.hpp"
#include "kernel/base/bytes.hpp"
#include "kernel/mm/kernel_address_space.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/mm/paging.hpp"
#include "kernel/mm/physical_memory.hpp"

namespace barry::mm {
namespace {

constexpr Array<usize, 8> SIZE_CLASSES { { 16, 32, 64, 128, 256, 512, 1024, 2048 } };

class KernelPageBackend final : public PageBackend {
public:
    KResult map_pages(VirtRange range) override {
        u64 mapped = 0;

        while (mapped < range.length) {
            VirtAddr const virtual_address = range.base.offset(mapped);
            auto frame = physical_memory().allocate_zeroed_page();
            if (frame.is_error()) {
                unmap_prefix(range, mapped);
                return Unexpected(frame.release_error());
            }

            auto mapping = kernel_address_space().map_page(virtual_address, frame.value(),
                                                           KERNEL_DATA_FLAGS);
            if (mapping.is_error()) {
                physical_memory().free_page(frame.value());
                unmap_prefix(range, mapped);
                return Unexpected(mapping.release_error());
            }

            mapped += PAGE_SIZE;
        }

        return {};
    }

private:
    static void unmap_prefix(VirtRange range, u64 length) {
        for (u64 offset = 0; offset < length; offset += PAGE_SIZE) {
            VirtAddr const virtual_address = range.base.offset(offset);
            auto const frame = kernel_address_space().translate(virtual_address);
            if (kernel_address_space().unmap_page(virtual_address).has_value()
                && frame.has_value())
                physical_memory().free_page(frame.value());
        }
    }
};

KernelPageBackend g_page_backend;
KernelHeap g_kernel_heap;

}  // namespace

usize KernelHeap::chunk_size_of(usize size_class) {
    VERIFY(size_class < SIZE_CLASS_COUNT);
    return SIZE_CLASSES[size_class];
}

Optional<usize> KernelHeap::size_class_for(usize total_bytes) {
    for (usize index = 0; index < SIZE_CLASS_COUNT; ++index) {
        if (total_bytes <= SIZE_CLASSES[index])
            return index;
    }
    return nullopt;
}

KResult KernelHeap::initialize() {
    if (m_initialized)
        return Error(Errno::EBUSY, "the kernel heap is already initialized");

    auto guard = m_state.lock();
    TRY(guard->pages.initialize(kernel_heap_range(), g_page_backend));
    m_initialized = true;
    return {};
}

void* KernelHeap::place_header(VirtRange block, usize payload_size, usize alignment,
                               usize size_class, u64 page_count) {
    VirtAddr const payload
        = VirtAddr(align_up<u64>(block.base.raw() + sizeof(AllocationHeader), alignment));
    VirtAddr const header_address = payload.subtract(sizeof(AllocationHeader));

    VERIFY(header_address >= block.base);
    VERIFY(payload.offset(payload_size) <= block.end());

    auto* header = header_address.as_ptr<AllocationHeader>();
    header->magic = HEADER_MAGIC;
    header->size_class = static_cast<u16>(size_class);
    header->padding_bytes = static_cast<u16>(block.base.distance_to(header_address));
    header->page_count = page_count;

    return payload.as_ptr<void>();
}

KernelHeap::AllocationHeader& KernelHeap::header_of(void* payload) {
    VirtAddr const address = VirtAddr::from_ptr(payload);
    return *address.subtract(sizeof(AllocationHeader)).as_ptr<AllocationHeader>();
}

VirtAddr KernelHeap::block_start_of(AllocationHeader const& header) {
    return VirtAddr::from_ptr(&header).subtract(header.padding_bytes);
}

KResult KernelHeap::refill_size_class(State& state, usize size_class) {
    auto const slab = TRY(state.pages.allocate_pages(SLAB_PAGES));
    usize const chunk_size = chunk_size_of(size_class);
    u64 const chunk_count = slab.length / chunk_size;

    for (u64 index = 0; index < chunk_count; ++index) {
        auto* chunk = slab.base.offset(index * chunk_size).as_ptr<FreeChunk>();
        chunk->next = state.free_lists[size_class];
        state.free_lists[size_class] = chunk;
    }

    return {};
}

KResultOr<void*> KernelHeap::allocate_small(State& state, usize payload_size, usize size_class,
                                            usize alignment) {
    if (state.free_lists[size_class] == nullptr)
        TRY(refill_size_class(state, size_class));

    FreeChunk* chunk = state.free_lists[size_class];
    VERIFY(chunk != nullptr);
    state.free_lists[size_class] = chunk->next;

    usize const chunk_size = chunk_size_of(size_class);
    VirtRange const block { VirtAddr::from_ptr(chunk), chunk_size };

    state.allocated_bytes += chunk_size;
    ++state.live_allocations;
    return place_header(block, payload_size, alignment, size_class, 0);
}

KResultOr<void*> KernelHeap::allocate_large(State& state, usize payload_size, usize total_bytes,
                                            usize alignment) {
    u64 const page_count = div_ceil<u64>(total_bytes, PAGE_SIZE);
    auto const block = TRY(state.pages.allocate_pages(page_count));

    state.allocated_bytes += block.length;
    ++state.live_allocations;
    return place_header(block, payload_size, alignment, LARGE_ALLOCATION, page_count);
}

KResultOr<void*> KernelHeap::allocate(usize size, usize alignment) {
    if (!m_initialized)
        return Error(Errno::ENOMEM, "the kernel heap is not initialized yet");
    if (!is_power_of_two(alignment) || alignment > MAX_ALIGNMENT)
        return Error(Errno::EINVAL, "alignment must be a power of two no larger than a page");

    usize const effective_alignment = max(alignment, MIN_ALIGNMENT);
    usize const requested = max<usize>(size, 1);
    usize const total_bytes
        = requested + sizeof(AllocationHeader) + (effective_alignment - MIN_ALIGNMENT);

    auto guard = m_state.lock();

    if (auto const size_class = size_class_for(total_bytes))
        return allocate_small(*guard, requested, size_class.value(), effective_alignment);

    return allocate_large(*guard, requested, total_bytes, effective_alignment);
}

void KernelHeap::deallocate(void* pointer) {
    if (pointer == nullptr)
        return;

    AllocationHeader& header = header_of(pointer);
    VERIFY(header.magic == HEADER_MAGIC);

    u16 const size_class = header.size_class;
    u16 const padding = header.padding_bytes;
    u64 const page_count = header.page_count;
    VirtAddr const block = block_start_of(header);

    header.magic = 0;

    auto guard = m_state.lock();
    VERIFY(guard->live_allocations > 0);
    --guard->live_allocations;

    if (size_class == LARGE_ALLOCATION) {
        u64 const length = page_count * PAGE_SIZE;
        VERIFY(guard->allocated_bytes >= length);
        guard->allocated_bytes -= length;
        guard->pages.free_pages(VirtRange { block, length });
        return;
    }

    VERIFY(size_class < SIZE_CLASS_COUNT);
    usize const chunk_size = chunk_size_of(size_class);
    VERIFY(padding < chunk_size);
    VERIFY(guard->allocated_bytes >= chunk_size);
    guard->allocated_bytes -= chunk_size;

    auto* chunk = block.as_ptr<FreeChunk>();
    chunk->next = guard->free_lists[size_class];
    guard->free_lists[size_class] = chunk;
}

HeapStatistics KernelHeap::statistics() const {
    auto guard = m_state.lock();
    auto const pages = guard->pages.statistics();

    HeapStatistics result {};
    result.mapped_bytes = pages.mapped_pages * PAGE_SIZE;
    result.allocated_bytes = guard->allocated_bytes;
    result.live_allocations = guard->live_allocations;
    return result;
}

KernelHeap& kernel_heap() {
    return g_kernel_heap;
}

KResult initialize_kernel_heap() {
    return g_kernel_heap.initialize();
}

}  // namespace barry::mm

namespace barry {

KResultOr<void*> allocate_raw(usize size, usize alignment) {
    return mm::kernel_heap().allocate(size, alignment);
}

void deallocate_raw(void* pointer) {
    mm::kernel_heap().deallocate(pointer);
}

}  // namespace barry
