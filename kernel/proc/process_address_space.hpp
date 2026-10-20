#pragma once

#include "kernel/base/expected.hpp"
#include "kernel/base/own_ptr.hpp"
#include "kernel/base/span.hpp"
#include "kernel/mm/paging.hpp"
#include "kernel/proc/user_layout.hpp"

namespace barry::proc {

class ProcessAddressSpace {
public:
    static KResultOr<OwnPtr<ProcessAddressSpace>> create();

    KResultOr<OwnPtr<ProcessAddressSpace>> clone() const;

    explicit ProcessAddressSpace(mm::AddressSpace space) : m_space(space) {}
    ~ProcessAddressSpace();

    ProcessAddressSpace(ProcessAddressSpace const&) = delete;
    ProcessAddressSpace& operator=(ProcessAddressSpace const&) = delete;

    mm::AddressSpace& page_tables() { return m_space; }
    mm::AddressSpace const& page_tables() const { return m_space; }

    PhysAddr root() const { return m_space.root(); }

    KResult map_anonymous(VirtRange range, mm::PageFlags flags);
    KResult unmap(VirtRange range);

    KResultOr<VirtAddr> reserve_mapping(u64 length, mm::PageFlags flags);

    KResult copy_in(VirtAddr destination, ReadonlyBytes source);

    void set_program_break_start(VirtAddr start);
    VirtAddr program_break() const { return m_break_current; }
    KResultOr<VirtAddr> move_program_break(VirtAddr requested);

    void activate() const { m_space.activate(); }

    u64 mapped_bytes() const { return m_mapped_bytes; }

private:
    mm::AddressSpace m_space;
    VirtAddr m_break_start {};
    VirtAddr m_break_current {};
    VirtAddr m_mapping_cursor { USER_MMAP_BASE };
    u64 m_mapped_bytes { 0 };
};

}  // namespace barry::proc
