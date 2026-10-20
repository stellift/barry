#pragma once

#include "kernel/boot/boot_info.hpp"
#include "kernel/base/addr.hpp"
#include "kernel/base/bitmap.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/sync/spin_lock.hpp"

namespace barry::mm {

struct PhysicalMemoryStatistics {
    u64 managed_pages { 0 };
    u64 free_pages { 0 };
    u64 allocated_pages { 0 };

    u64 managed_bytes() const { return managed_pages * PAGE_SIZE; }
    u64 free_bytes() const { return free_pages * PAGE_SIZE; }
    u64 allocated_bytes() const { return allocated_pages * PAGE_SIZE; }
};

class PhysicalMemoryManager {
public:
    KResult initialize(boot::BootInfo const& boot_info);

    KResultOr<PhysAddr> allocate_page();
    KResultOr<PhysAddr> allocate_zeroed_page();
    KResultOr<PhysAddr> allocate_page_below(PhysAddr limit);
    KResultOr<PhysRange> allocate_contiguous(u64 page_count);

    void free_page(PhysAddr address);
    void free_contiguous(PhysRange range);

    PhysicalMemoryStatistics statistics() const;

    bool is_managed(PhysAddr address) const;

private:
    struct State {
        Bitmap frames;
        u64 managed_page_count { 0 };
        u64 allocated_page_count { 0 };
        u64 search_cursor { 0 };
    };

    static KResultOr<PhysRange> reserve_bitmap_storage(boot::BootInfo const& boot_info,
                                                       u64 required_bytes);

    static void mark_usable_regions(State& state, boot::BootInfo const& boot_info);

    KResultOr<u64> claim_run(State& state, u64 page_count);
    KResultOr<u64> claim_frame_below(State& state, u64 frame_limit);

    mutable sync::Protected<State> m_state;
    bool m_initialized { false };
};

PhysicalMemoryManager& physical_memory();

}  // namespace barry::mm
