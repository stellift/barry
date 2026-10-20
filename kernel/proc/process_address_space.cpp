#include "kernel/proc/process_address_space.hpp"

#include "kernel/base/assert.hpp"
#include "kernel/mm/kernel_address_space.hpp"
#include "kernel/base/bytes.hpp"
#include "kernel/mm/direct_map.hpp"
#include "kernel/mm/physical_memory.hpp"

namespace barry::proc {

KResultOr<OwnPtr<ProcessAddressSpace>> ProcessAddressSpace::create() {
    auto space = TRY(mm::AddressSpace::create_user_space(mm::kernel_address_space()));

    auto owned = make_own<ProcessAddressSpace>(space);
    if (owned.is_error()) {
        space.release_user_half();
        return Unexpected(owned.release_error());
    }

    return owned.release_value();
}

KResultOr<OwnPtr<ProcessAddressSpace>> ProcessAddressSpace::clone() const {
    auto copy = TRY(create());

    TRY(m_space.clone_user_half(copy->m_space));

    copy->m_break_start = m_break_start;
    copy->m_break_current = m_break_current;
    copy->m_mapping_cursor = m_mapping_cursor;
    copy->m_mapped_bytes = m_mapped_bytes;

    return copy;
}

ProcessAddressSpace::~ProcessAddressSpace() {
    m_space.release_user_half();
}

KResult ProcessAddressSpace::map_anonymous(VirtRange range, mm::PageFlags flags) {
    if (!is_user_range(range))
        return Error(Errno::EINVAL, "the requested range is outside user address space");
    if (!range.base.is_page_aligned() || !is_aligned(range.length, PAGE_SIZE))
        return Error(Errno::EINVAL, "anonymous mappings must be page aligned");

    u64 mapped = 0;
    while (mapped < range.length) {
        VirtAddr const address = range.base.offset(mapped);

        auto frame = mm::physical_memory().allocate_zeroed_page();
        if (frame.is_error()) {
            auto const failure = frame.release_error();
            (void)unmap(VirtRange { range.base, mapped });
            return Unexpected(failure);
        }

        auto mapping = m_space.map_page(address, frame.value(), flags);
        if (mapping.is_error()) {
            auto const failure = mapping.release_error();
            mm::physical_memory().free_page(frame.value());
            (void)unmap(VirtRange { range.base, mapped });
            return Unexpected(failure);
        }

        mapped += PAGE_SIZE;
    }

    m_mapped_bytes += range.length;
    return {};
}

KResult ProcessAddressSpace::unmap(VirtRange range) {
    if (range.length == 0)
        return {};
    if (!is_user_range(range))
        return Error(Errno::EINVAL, "the requested range is outside user address space");

    TRY(m_space.unmap_and_release_range(range));
    m_mapped_bytes -= min(m_mapped_bytes, range.length);
    return {};
}

KResult ProcessAddressSpace::copy_in(VirtAddr destination, ReadonlyBytes source) {
    u64 copied = 0;

    while (copied < source.size()) {
        VirtAddr const address = destination.offset(copied);
        u64 const chunk = min<u64>(PAGE_SIZE - address.page_offset(), source.size() - copied);

        auto const frame = m_space.translate(address);
        if (!frame.has_value())
            return Error(Errno::EFAULT, "a destination page is not mapped");

        auto* target = mm::direct_map_pointer<u8>(frame.value());
        copy_bytes(Bytes(target, chunk), source.slice(copied, chunk));
        copied += chunk;
    }

    return {};
}

KResultOr<VirtAddr> ProcessAddressSpace::reserve_mapping(u64 length, mm::PageFlags flags) {
    if (length == 0)
        return Error(Errno::EINVAL, "a mapping needs a positive length");

    u64 const rounded = align_up<u64>(length, PAGE_SIZE);
    VirtAddr const base = m_mapping_cursor;

    if (base.offset(rounded) > USER_MMAP_BASE.offset(USER_MMAP_AREA_SIZE))
        return Error(Errno::ENOMEM, "the mapping area is exhausted");

    TRY(map_anonymous(VirtRange { base, rounded }, flags));
    m_mapping_cursor = base.offset(rounded);
    return base;
}

void ProcessAddressSpace::set_program_break_start(VirtAddr start) {
    m_break_start = start.page_align_up();
    m_break_current = m_break_start;
}

KResultOr<VirtAddr> ProcessAddressSpace::move_program_break(VirtAddr requested) {
    if (requested.is_null() || requested < m_break_start)
        return m_break_current;

    if (requested > m_break_start.offset(USER_BRK_MAX_SIZE))
        return Error(Errno::ENOMEM, "the program break would exceed its limit");

    VirtAddr const current_page = m_break_current.page_align_up();
    VirtAddr const requested_page = requested.page_align_up();

    if (requested_page > current_page) {
        VirtRange const growth { current_page, current_page.distance_to(requested_page) };
        TRY(map_anonymous(growth, mm::USER_DATA_FLAGS));
    } else if (requested_page < current_page) {
        VirtRange const shrink { requested_page, requested_page.distance_to(current_page) };
        TRY(unmap(shrink));
    }

    m_break_current = requested;
    return m_break_current;
}

}  // namespace barry::proc
