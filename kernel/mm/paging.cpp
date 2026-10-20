#include "kernel/mm/paging.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/mm/direct_map.hpp"
#include "kernel/mm/physical_memory.hpp"

namespace barry::mm {
namespace {

constexpr PageFlags INTERMEDIATE_FLAGS = PageFlags::Present | PageFlags::Writable;

PageFlags intermediate_flags_for(PageFlags leaf_flags) {
    PageFlags flags = INTERMEDIATE_FLAGS;
    if (has_all_flags(leaf_flags, PageFlags::UserAccessible))
        flags |= PageFlags::UserAccessible;
    return flags;
}

}  // namespace

KResultOr<AddressSpace> AddressSpace::create_empty() {
    PhysAddr const root = TRY(physical_memory().allocate_zeroed_page());
    return AddressSpace(root);
}

PageTable& AddressSpace::table_at(PhysAddr address) const {
    return *direct_map_pointer<PageTable>(address);
}

KResultOr<PageTable*> AddressSpace::next_level(PageTableEntry& entry, bool create) {
    if (entry.is_present()) {
        if (entry.is_large_page())
            return Error(Errno::EEXIST, "a large page already covers this range");
        return &table_at(entry.frame());
    }

    if (!create)
        return Error(Errno::ENOENT, "page table level is not present");

    PhysAddr const page = TRY(physical_memory().allocate_zeroed_page());
    entry.set(page, INTERMEDIATE_FLAGS);
    return &table_at(page);
}

KResult AddressSpace::map_page(VirtAddr virtual_address, PhysAddr physical_address,
                               PageFlags flags) {
    VERIFY(is_valid());
    if (!virtual_address.is_page_aligned() || !physical_address.is_page_aligned())
        return Error(Errno::EINVAL, "map_page requires page-aligned addresses");
    if (!virtual_address.is_canonical())
        return Error(Errno::EFAULT, "virtual address is not canonical");

    PageFlags const parent_flags = intermediate_flags_for(flags);

    PageTable& level4 = table_at(m_root);
    PageTableEntry& level4_entry = level4.entries[virtual_address.pml4_index()];
    if (!level4_entry.is_present()) {
        PhysAddr const page = TRY(physical_memory().allocate_zeroed_page());
        level4_entry.set(page, parent_flags);
    }

    PageTable* level3 = TRY(next_level(level4_entry, true));
    PageTableEntry& level3_entry = level3->entries[virtual_address.pdpt_index()];
    PageTable* level2 = TRY(next_level(level3_entry, true));
    PageTableEntry& level2_entry = level2->entries[virtual_address.pd_index()];
    PageTable* level1 = TRY(next_level(level2_entry, true));

    PageTableEntry& leaf = level1->entries[virtual_address.pt_index()];
    if (leaf.is_present())
        return Error(Errno::EEXIST, "virtual page is already mapped");

    leaf.set(physical_address, flags | PageFlags::Present);
    arch::invalidate_page(virtual_address);
    return {};
}

KResult AddressSpace::map_large_page(VirtAddr virtual_address, PhysAddr physical_address,
                                     PageFlags flags) {
    VERIFY(is_valid());
    if (!is_aligned(virtual_address.raw(), LARGE_PAGE_SIZE)
        || !is_aligned(physical_address.raw(), LARGE_PAGE_SIZE))
        return Error(Errno::EINVAL, "map_large_page requires 2 MiB alignment");

    PageFlags const parent_flags = intermediate_flags_for(flags);

    PageTable& level4 = table_at(m_root);
    PageTableEntry& level4_entry = level4.entries[virtual_address.pml4_index()];
    if (!level4_entry.is_present()) {
        PhysAddr const page = TRY(physical_memory().allocate_zeroed_page());
        level4_entry.set(page, parent_flags);
    }

    PageTable* level3 = TRY(next_level(level4_entry, true));
    PageTableEntry& level3_entry = level3->entries[virtual_address.pdpt_index()];
    PageTable* level2 = TRY(next_level(level3_entry, true));

    PageTableEntry& leaf = level2->entries[virtual_address.pd_index()];
    if (leaf.is_present())
        return Error(Errno::EEXIST, "virtual range is already mapped");

    leaf.set(physical_address, flags | PageFlags::Present | PageFlags::PageSize);
    arch::invalidate_page(virtual_address);
    return {};
}

KResult AddressSpace::map_range(VirtRange range, PhysAddr physical_base, PageFlags flags) {
    VERIFY(range.base.is_page_aligned());
    VERIFY(is_aligned(range.length, PAGE_SIZE));

    u64 offset = 0;
    while (offset < range.length) {
        VirtAddr const virtual_address = range.base.offset(offset);
        PhysAddr const physical_address = physical_base.offset(offset);
        u64 const remaining = range.length - offset;

        bool const can_use_large_page = remaining >= LARGE_PAGE_SIZE
                                        && is_aligned(virtual_address.raw(), LARGE_PAGE_SIZE)
                                        && is_aligned(physical_address.raw(), LARGE_PAGE_SIZE);

        if (can_use_large_page) {
            TRY(map_large_page(virtual_address, physical_address, flags));
            offset += LARGE_PAGE_SIZE;
            continue;
        }

        TRY(map_page(virtual_address, physical_address, flags));
        offset += PAGE_SIZE;
    }
    return {};
}

KResult AddressSpace::unmap_page(VirtAddr virtual_address) {
    VERIFY(is_valid());

    PageTable& level4 = table_at(m_root);
    PageTableEntry& level4_entry = level4.entries[virtual_address.pml4_index()];
    PageTable* level3 = TRY(next_level(level4_entry, false));
    PageTableEntry& level3_entry = level3->entries[virtual_address.pdpt_index()];
    PageTable* level2 = TRY(next_level(level3_entry, false));
    PageTableEntry& level2_entry = level2->entries[virtual_address.pd_index()];
    PageTable* level1 = TRY(next_level(level2_entry, false));

    PageTableEntry& leaf = level1->entries[virtual_address.pt_index()];
    if (!leaf.is_present())
        return Error(Errno::ENOENT, "virtual page is not mapped");

    leaf.clear();
    arch::invalidate_page(virtual_address);
    return {};
}

Optional<PhysAddr> AddressSpace::translate(VirtAddr virtual_address) const {
    if (!is_valid())
        return nullopt;

    PageTable const& level4 = table_at(m_root);
    PageTableEntry const& level4_entry = level4.entries[virtual_address.pml4_index()];
    if (!level4_entry.is_present())
        return nullopt;

    PageTable const& level3 = table_at(level4_entry.frame());
    PageTableEntry const& level3_entry = level3.entries[virtual_address.pdpt_index()];
    if (!level3_entry.is_present())
        return nullopt;
    if (level3_entry.is_large_page())
        return level3_entry.frame().offset(virtual_address.raw() & (HUGE_PAGE_SIZE - 1));

    PageTable const& level2 = table_at(level3_entry.frame());
    PageTableEntry const& level2_entry = level2.entries[virtual_address.pd_index()];
    if (!level2_entry.is_present())
        return nullopt;
    if (level2_entry.is_large_page())
        return level2_entry.frame().offset(virtual_address.raw() & (LARGE_PAGE_SIZE - 1));

    PageTable const& level1 = table_at(level2_entry.frame());
    PageTableEntry const& leaf = level1.entries[virtual_address.pt_index()];
    if (!leaf.is_present())
        return nullopt;

    return leaf.frame().offset(virtual_address.page_offset());
}

void AddressSpace::activate() const {
    VERIFY(is_valid());
    arch::write_cr3(m_root);
}

}  // namespace barry::mm
