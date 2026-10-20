#include "kernel/mm/paging.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/base/bytes.hpp"
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

KResultOr<PageTable*> AddressSpace::next_level(PageTableEntry& entry, PageFlags parent_flags) {
    if (entry.is_present()) {
        if (entry.is_large_page())
            return Error(Errno::EEXIST, "a large page already covers this range");

        PageFlags const merged = entry.flags() | parent_flags;
        if (merged != entry.flags())
            entry.set(entry.frame(), merged);

        return &table_at(entry.frame());
    }

    PhysAddr const page = TRY(physical_memory().allocate_zeroed_page());
    entry.set(page, parent_flags);
    return &table_at(page);
}

KResultOr<PageTable*> AddressSpace::existing_next_level(PageTableEntry const& entry) const {
    if (!entry.is_present())
        return Error(Errno::ENOENT, "page table level is not present");
    if (entry.is_large_page())
        return Error(Errno::EEXIST, "a large page already covers this range");
    return &table_at(entry.frame());
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
    PageTable* level3 = TRY(next_level(level4.entries[virtual_address.pml4_index()], parent_flags));
    PageTable* level2
        = TRY(next_level(level3->entries[virtual_address.pdpt_index()], parent_flags));
    PageTable* level1 = TRY(next_level(level2->entries[virtual_address.pd_index()], parent_flags));

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
    PageTable* level3 = TRY(next_level(level4.entries[virtual_address.pml4_index()], parent_flags));
    PageTable* level2
        = TRY(next_level(level3->entries[virtual_address.pdpt_index()], parent_flags));

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

    PageTable const& level4 = table_at(m_root);
    PageTable* level3 = TRY(existing_next_level(level4.entries[virtual_address.pml4_index()]));
    PageTable* level2 = TRY(existing_next_level(level3->entries[virtual_address.pdpt_index()]));
    PageTable* level1 = TRY(existing_next_level(level2->entries[virtual_address.pd_index()]));

    PageTableEntry& leaf = level1->entries[virtual_address.pt_index()];
    if (!leaf.is_present())
        return Error(Errno::ENOENT, "virtual page is not mapped");

    leaf.clear();
    arch::invalidate_page(virtual_address);
    return {};
}

KResultOr<AddressSpace> AddressSpace::create_user_space(AddressSpace const& kernel_space) {
    VERIFY(kernel_space.is_valid());

    PhysAddr const root = TRY(physical_memory().allocate_zeroed_page());
    AddressSpace space(root);

    PageTable const& kernel_table = kernel_space.table_at(kernel_space.m_root);
    PageTable& user_table = space.table_at(root);

    for (usize index = FIRST_KERNEL_TOP_LEVEL_INDEX; index < PAGE_TABLE_ENTRY_COUNT; ++index)
        user_table.entries[index] = kernel_table.entries[index];

    return space;
}

void AddressSpace::release_user_half() {
    if (!is_valid())
        return;

    PageTable& level4 = table_at(m_root);

    for (usize level4_index = 0; level4_index < FIRST_KERNEL_TOP_LEVEL_INDEX; ++level4_index) {
        PageTableEntry& level4_entry = level4.entries[level4_index];
        if (!level4_entry.is_present())
            continue;

        PageTable& level3 = table_at(level4_entry.frame());
        for (usize level3_index = 0; level3_index < PAGE_TABLE_ENTRY_COUNT; ++level3_index) {
            PageTableEntry& level3_entry = level3.entries[level3_index];
            if (!level3_entry.is_present())
                continue;
            if (level3_entry.is_large_page()) {
                physical_memory().free_contiguous(PhysRange { level3_entry.frame(), HUGE_PAGE_SIZE });
                level3_entry.clear();
                continue;
            }

            PageTable& level2 = table_at(level3_entry.frame());
            for (usize level2_index = 0; level2_index < PAGE_TABLE_ENTRY_COUNT; ++level2_index) {
                PageTableEntry& level2_entry = level2.entries[level2_index];
                if (!level2_entry.is_present())
                    continue;
                if (level2_entry.is_large_page()) {
                    physical_memory().free_contiguous(
                        PhysRange { level2_entry.frame(), LARGE_PAGE_SIZE });
                    level2_entry.clear();
                    continue;
                }

                PageTable& level1 = table_at(level2_entry.frame());
                for (usize level1_index = 0; level1_index < PAGE_TABLE_ENTRY_COUNT;
                     ++level1_index) {
                    PageTableEntry& leaf = level1.entries[level1_index];
                    if (!leaf.is_present())
                        continue;
                    physical_memory().free_page(leaf.frame());
                    leaf.clear();
                }

                physical_memory().free_page(level2_entry.frame());
                level2_entry.clear();
            }

            physical_memory().free_page(level3_entry.frame());
            level3_entry.clear();
        }

        physical_memory().free_page(level4_entry.frame());
        level4_entry.clear();
    }

    physical_memory().free_page(m_root);
    m_root = PhysAddr {};
}

namespace {

VirtAddr address_from_indices(usize level4, usize level3, usize level2, usize level1) {
    return VirtAddr((static_cast<u64>(level4) << 39) | (static_cast<u64>(level3) << 30)
                    | (static_cast<u64>(level2) << 21) | (static_cast<u64>(level1) << 12));
}

}  // namespace

KResult AddressSpace::clone_user_half(AddressSpace& destination) const {
    VERIFY(is_valid());
    VERIFY(destination.is_valid());

    PageTable const& level4 = table_at(m_root);

    for (usize level4_index = 0; level4_index < FIRST_KERNEL_TOP_LEVEL_INDEX; ++level4_index) {
        PageTableEntry const& level4_entry = level4.entries[level4_index];
        if (!level4_entry.is_present())
            continue;
        if (level4_entry.is_large_page())
            return Error(Errno::ENOSYS, "cloning gigabyte pages is not supported");

        PageTable const& level3 = table_at(level4_entry.frame());
        for (usize level3_index = 0; level3_index < PAGE_TABLE_ENTRY_COUNT; ++level3_index) {
            PageTableEntry const& level3_entry = level3.entries[level3_index];
            if (!level3_entry.is_present())
                continue;
            if (level3_entry.is_large_page())
                return Error(Errno::ENOSYS, "cloning gigabyte pages is not supported");

            PageTable const& level2 = table_at(level3_entry.frame());
            for (usize level2_index = 0; level2_index < PAGE_TABLE_ENTRY_COUNT; ++level2_index) {
                PageTableEntry const& level2_entry = level2.entries[level2_index];
                if (!level2_entry.is_present())
                    continue;
                if (level2_entry.is_large_page())
                    return Error(Errno::ENOSYS, "cloning large pages is not supported");

                PageTable const& level1 = table_at(level2_entry.frame());
                for (usize level1_index = 0; level1_index < PAGE_TABLE_ENTRY_COUNT;
                     ++level1_index) {
                    PageTableEntry const& leaf = level1.entries[level1_index];
                    if (!leaf.is_present())
                        continue;

                    VirtAddr const address = address_from_indices(level4_index, level3_index,
                                                                  level2_index, level1_index);
                    PhysAddr const copy = TRY(physical_memory().allocate_page());

                    copy_bytes(Bytes(direct_map_pointer<u8>(copy), PAGE_SIZE),
                               ReadonlyBytes(direct_map_pointer<u8>(leaf.frame()), PAGE_SIZE));

                    auto mapped = destination.map_page(address, copy, leaf.flags());
                    if (mapped.is_error()) {
                        physical_memory().free_page(copy);
                        return Unexpected(mapped.release_error());
                    }
                }
            }
        }
    }

    return {};
}

KResult AddressSpace::unmap_and_release_range(VirtRange range) {
    VERIFY(range.base.is_page_aligned());
    VERIFY(is_aligned(range.length, PAGE_SIZE));

    for (u64 offset = 0; offset < range.length; offset += PAGE_SIZE) {
        VirtAddr const address = range.base.offset(offset);
        auto const frame = translate(address);
        if (!frame.has_value())
            continue;

        TRY(unmap_page(address));
        physical_memory().free_page(frame.value());
    }

    return {};
}

Optional<PageTableEntry> AddressSpace::lookup(VirtAddr virtual_address) const {
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
        return level3_entry;

    PageTable const& level2 = table_at(level3_entry.frame());
    PageTableEntry const& level2_entry = level2.entries[virtual_address.pd_index()];
    if (!level2_entry.is_present())
        return nullopt;
    if (level2_entry.is_large_page())
        return level2_entry;

    PageTable const& level1 = table_at(level2_entry.frame());
    PageTableEntry const& leaf = level1.entries[virtual_address.pt_index()];
    if (!leaf.is_present())
        return nullopt;

    return leaf;
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
