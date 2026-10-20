#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/types.hpp"

namespace barry::mm {

class PageBackend {
public:
    virtual KResult map_pages(VirtRange range) = 0;

protected:
    ~PageBackend() = default;
};

struct PageRegionStatistics {
    u64 mapped_pages { 0 };
    u64 allocated_pages { 0 };
    u64 free_run_count { 0 };
};

class PageRegionAllocator {
public:
    KResult initialize(VirtRange region, PageBackend& backend);

    KResultOr<VirtRange> allocate_pages(u64 page_count);
    void free_pages(VirtRange range);

    PageRegionStatistics statistics() const;

    bool owns(VirtAddr address) const;

private:
    struct FreeRun {
        u64 page_count { 0 };
        FreeRun* next { nullptr };
    };

    KResult grow_mapped_area(u64 page_count);
    Optional<VirtRange> take_from_free_list(u64 page_count);
    void insert_into_free_list(VirtRange range);

    static FreeRun* run_at(VirtAddr address) { return address.as_ptr<FreeRun>(); }
    static VirtAddr address_of(FreeRun const* run) { return VirtAddr::from_ptr(run); }
    static VirtAddr end_of(FreeRun const* run) {
        return address_of(run).offset(run->page_count * PAGE_SIZE);
    }

    VirtRange m_region {};
    PageBackend* m_backend { nullptr };
    FreeRun* m_free_list { nullptr };
    u64 m_mapped_pages { 0 };
    u64 m_allocated_pages { 0 };
};

}  // namespace barry::mm
