#include "kernel/mm/page_region_allocator.hpp"

#include "kernel/base/assert.hpp"
#include "kernel/base/utility.hpp"

namespace barry::mm {
namespace {

u64 pages_in(VirtRange range) {
    return range.length / PAGE_SIZE;
}

}  // namespace

KResult PageRegionAllocator::initialize(VirtRange region, PageBackend& backend) {
    if (m_backend != nullptr)
        return Error(Errno::EBUSY, "the page region allocator is already initialized");
    if (!region.base.is_page_aligned() || !is_aligned(region.length, PAGE_SIZE))
        return Error(Errno::EINVAL, "the heap region must be page aligned");
    if (region.length < PAGE_SIZE)
        return Error(Errno::EINVAL, "the heap region is too small");

    m_region = region;
    m_backend = &backend;
    m_free_list = nullptr;
    m_mapped_pages = 0;
    m_allocated_pages = 0;
    return {};
}

bool PageRegionAllocator::owns(VirtAddr address) const {
    return m_region.contains(address);
}

KResult PageRegionAllocator::grow_mapped_area(u64 page_count) {
    VERIFY(m_backend != nullptr);

    u64 const available_pages = pages_in(m_region) - m_mapped_pages;
    if (page_count > available_pages)
        return Error(Errno::ENOMEM, "the kernel heap region is exhausted");

    VirtRange const fresh { m_region.base.offset(m_mapped_pages * PAGE_SIZE),
                            page_count * PAGE_SIZE };
    TRY(m_backend->map_pages(fresh));
    m_mapped_pages += page_count;
    insert_into_free_list(fresh);
    return {};
}

Optional<VirtRange> PageRegionAllocator::take_from_free_list(u64 page_count) {
    FreeRun** link = &m_free_list;

    while (*link != nullptr) {
        FreeRun* run = *link;
        if (run->page_count < page_count) {
            link = &run->next;
            continue;
        }

        VirtAddr const base = address_of(run);
        u64 const remaining = run->page_count - page_count;

        if (remaining == 0) {
            *link = run->next;
        } else {
            FreeRun* const tail = run_at(base.offset(page_count * PAGE_SIZE));
            tail->page_count = remaining;
            tail->next = run->next;
            *link = tail;
        }

        return VirtRange { base, page_count * PAGE_SIZE };
    }

    return nullopt;
}

void PageRegionAllocator::insert_into_free_list(VirtRange range) {
    VERIFY(range.base.is_page_aligned());
    VERIFY(is_aligned(range.length, PAGE_SIZE));

    FreeRun* previous = nullptr;
    FreeRun* next = m_free_list;
    while (next != nullptr && address_of(next) < range.base) {
        previous = next;
        next = next->next;
    }

    FreeRun* inserted = run_at(range.base);
    inserted->page_count = pages_in(range);
    inserted->next = next;

    if (previous != nullptr)
        previous->next = inserted;
    else
        m_free_list = inserted;

    if (next != nullptr && address_of(next) == end_of(inserted)) {
        inserted->page_count += next->page_count;
        inserted->next = next->next;
    }

    if (previous != nullptr && end_of(previous) == range.base) {
        previous->page_count += inserted->page_count;
        previous->next = inserted->next;
    }
}

KResultOr<VirtRange> PageRegionAllocator::allocate_pages(u64 page_count) {
    if (page_count == 0)
        return Error(Errno::EINVAL, "cannot allocate zero pages");
    VERIFY(m_backend != nullptr);

    if (auto range = take_from_free_list(page_count)) {
        m_allocated_pages += page_count;
        return range.release_value();
    }

    u64 const growth_pages = max<u64>(page_count, 16);
    TRY(grow_mapped_area(growth_pages));

    auto range = take_from_free_list(page_count);
    if (!range.has_value())
        return Error(Errno::ENOMEM, "growing the heap did not satisfy the request");

    m_allocated_pages += page_count;
    return range.release_value();
}

void PageRegionAllocator::free_pages(VirtRange range) {
    VERIFY(owns(range.base));
    VERIFY(is_aligned(range.length, PAGE_SIZE));
    VERIFY(m_allocated_pages >= pages_in(range));

    m_allocated_pages -= pages_in(range);
    insert_into_free_list(range);
}

PageRegionStatistics PageRegionAllocator::statistics() const {
    PageRegionStatistics result {};
    result.mapped_pages = m_mapped_pages;
    result.allocated_pages = m_allocated_pages;
    for (FreeRun const* run = m_free_list; run != nullptr; run = run->next)
        ++result.free_run_count;
    return result;
}

}  // namespace barry::mm
