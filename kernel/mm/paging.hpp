#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/array.hpp"
#include "kernel/base/enum_flags.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/types.hpp"

namespace barry::mm {

enum class PageFlags : u64 {
    None = 0,
    Present = 1ULL << 0,
    Writable = 1ULL << 1,
    UserAccessible = 1ULL << 2,
    WriteThrough = 1ULL << 3,
    CacheDisabled = 1ULL << 4,
    Accessed = 1ULL << 5,
    Dirty = 1ULL << 6,
    PageSize = 1ULL << 7,
    Global = 1ULL << 8,
    NoExecute = 1ULL << 63,
};

enum class PageSizeClass : u8 {
    Small,
    Large,
};

}  // namespace barry::mm

namespace barry {
template<>
struct EnableEnumFlags<mm::PageFlags> : TrueType {};
}

namespace barry::mm {

inline constexpr PageFlags KERNEL_CODE_FLAGS = PageFlags::Present | PageFlags::Global;
inline constexpr PageFlags KERNEL_READONLY_FLAGS
    = PageFlags::Present | PageFlags::Global | PageFlags::NoExecute;
inline constexpr PageFlags KERNEL_DATA_FLAGS
    = PageFlags::Present | PageFlags::Writable | PageFlags::Global | PageFlags::NoExecute;
inline constexpr PageFlags DEVICE_MEMORY_FLAGS = PageFlags::Present | PageFlags::Writable
                                                 | PageFlags::NoExecute
                                                 | PageFlags::CacheDisabled;

class PageTableEntry {
public:
    static constexpr u64 ADDRESS_MASK = 0x000ffffffffff000ULL;

    bool is_present() const { return has_all_flags(flags(), PageFlags::Present); }
    bool is_large_page() const { return has_all_flags(flags(), PageFlags::PageSize); }

    PhysAddr frame() const { return PhysAddr(m_raw & ADDRESS_MASK); }
    PageFlags flags() const { return static_cast<PageFlags>(m_raw & ~ADDRESS_MASK); }

    void set(PhysAddr frame, PageFlags flags) {
        m_raw = (frame.raw() & ADDRESS_MASK) | to_underlying(flags);
    }

    void clear() { m_raw = 0; }

private:
    u64 m_raw { 0 };
};

static_assert(sizeof(PageTableEntry) == sizeof(u64));

inline constexpr usize PAGE_TABLE_ENTRY_COUNT = 512;

struct PageTable {
    Array<PageTableEntry, PAGE_TABLE_ENTRY_COUNT> entries;
};

static_assert(sizeof(PageTable) == PAGE_SIZE);

class AddressSpace {
public:
    AddressSpace() = default;
    explicit AddressSpace(PhysAddr root) : m_root(root) {}

    static KResultOr<AddressSpace> create_empty();

    PhysAddr root() const { return m_root; }
    bool is_valid() const { return !m_root.is_null(); }

    KResult map_page(VirtAddr virtual_address, PhysAddr physical_address, PageFlags flags);
    KResult map_large_page(VirtAddr virtual_address, PhysAddr physical_address, PageFlags flags);
    KResult map_range(VirtRange range, PhysAddr physical_base, PageFlags flags);
    KResult unmap_page(VirtAddr virtual_address);

    Optional<PhysAddr> translate(VirtAddr virtual_address) const;

    void activate() const;

private:
    KResultOr<PageTable*> next_level(PageTableEntry& entry, bool create);
    PageTable& table_at(PhysAddr address) const;

    PhysAddr m_root;
};

}  // namespace barry::mm
