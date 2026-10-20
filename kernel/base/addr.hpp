#pragma once

#include "kernel/base/assert.hpp"
#include "kernel/base/types.hpp"
#include "kernel/base/utility.hpp"

namespace barry {

inline constexpr u64 PAGE_SIZE = 4096;
inline constexpr u64 PAGE_SHIFT = 12;
inline constexpr u64 PAGE_MASK = PAGE_SIZE - 1;

inline constexpr u64 LARGE_PAGE_SIZE = 2_MiB;
inline constexpr u64 HUGE_PAGE_SIZE = 1_GiB;

class PhysAddr {
public:
    constexpr PhysAddr() = default;
    constexpr explicit PhysAddr(u64 raw) : m_raw(raw) {}

    constexpr u64 raw() const { return m_raw; }
    constexpr bool is_null() const { return m_raw == 0; }
    constexpr bool is_page_aligned() const { return (m_raw & PAGE_MASK) == 0; }
    constexpr u64 page_offset() const { return m_raw & PAGE_MASK; }
    constexpr u64 page_frame_number() const { return m_raw >> PAGE_SHIFT; }

    constexpr PhysAddr offset(u64 delta) const { return PhysAddr(m_raw + delta); }
    constexpr PhysAddr page_align_down() const { return PhysAddr(m_raw & ~PAGE_MASK); }
    constexpr PhysAddr page_align_up() const {
        return PhysAddr(align_up<u64>(m_raw, PAGE_SIZE));
    }
    constexpr PhysAddr align_down_to(u64 alignment) const {
        return PhysAddr(align_down<u64>(m_raw, alignment));
    }
    constexpr PhysAddr align_up_to(u64 alignment) const {
        return PhysAddr(align_up<u64>(m_raw, alignment));
    }

    static constexpr PhysAddr from_page_frame_number(u64 pfn) {
        return PhysAddr(pfn << PAGE_SHIFT);
    }

    constexpr u64 distance_to(PhysAddr other) const {
        VERIFY(other.m_raw >= m_raw);
        return other.m_raw - m_raw;
    }

    constexpr bool operator==(PhysAddr other) const { return m_raw == other.m_raw; }
    constexpr bool operator<(PhysAddr other) const { return m_raw < other.m_raw; }
    constexpr bool operator<=(PhysAddr other) const { return m_raw <= other.m_raw; }
    constexpr bool operator>(PhysAddr other) const { return m_raw > other.m_raw; }
    constexpr bool operator>=(PhysAddr other) const { return m_raw >= other.m_raw; }

private:
    u64 m_raw { 0 };
};

class VirtAddr {
public:
    constexpr VirtAddr() = default;
    constexpr explicit VirtAddr(u64 raw) : m_raw(raw) {}

    template<typename T>
    static VirtAddr from_ptr(T* pointer) {
        return VirtAddr(reinterpret_cast<u64>(pointer));
    }

    constexpr u64 raw() const { return m_raw; }
    constexpr bool is_null() const { return m_raw == 0; }
    constexpr bool is_page_aligned() const { return (m_raw & PAGE_MASK) == 0; }
    constexpr u64 page_offset() const { return m_raw & PAGE_MASK; }

    constexpr VirtAddr offset(u64 delta) const { return VirtAddr(m_raw + delta); }
    constexpr VirtAddr subtract(u64 delta) const {
        VERIFY(m_raw >= delta);
        return VirtAddr(m_raw - delta);
    }
    constexpr VirtAddr page_align_down() const { return VirtAddr(m_raw & ~PAGE_MASK); }
    constexpr VirtAddr page_align_up() const {
        return VirtAddr(align_up<u64>(m_raw, PAGE_SIZE));
    }

    constexpr bool is_kernel_space() const { return (m_raw >> 63) != 0; }
    constexpr bool is_user_space() const { return (m_raw >> 63) == 0; }

    constexpr bool is_canonical() const {
        u64 const high = m_raw >> 47;
        return high == 0 || high == 0x1ffff;
    }

    constexpr u64 pml4_index() const { return (m_raw >> 39) & 0x1ff; }
    constexpr u64 pdpt_index() const { return (m_raw >> 30) & 0x1ff; }
    constexpr u64 pd_index() const { return (m_raw >> 21) & 0x1ff; }
    constexpr u64 pt_index() const { return (m_raw >> 12) & 0x1ff; }

    template<typename T>
    T* as_ptr() const {
        return reinterpret_cast<T*>(m_raw);
    }

    constexpr u64 distance_to(VirtAddr other) const {
        VERIFY(other.m_raw >= m_raw);
        return other.m_raw - m_raw;
    }

    constexpr bool operator==(VirtAddr other) const { return m_raw == other.m_raw; }
    constexpr bool operator<(VirtAddr other) const { return m_raw < other.m_raw; }
    constexpr bool operator<=(VirtAddr other) const { return m_raw <= other.m_raw; }
    constexpr bool operator>(VirtAddr other) const { return m_raw > other.m_raw; }
    constexpr bool operator>=(VirtAddr other) const { return m_raw >= other.m_raw; }

private:
    u64 m_raw { 0 };
};

template<typename T>
class UserPtr {
public:
    using PointeeType = T;

    constexpr UserPtr() = default;
    constexpr explicit UserPtr(VirtAddr address) : m_address(address) {}
    constexpr explicit UserPtr(u64 raw) : m_address(raw) {}

    constexpr VirtAddr address() const { return m_address; }
    constexpr bool is_null() const { return m_address.is_null(); }

    constexpr UserPtr offset(u64 count) const {
        return UserPtr(m_address.offset(count * sizeof(T)));
    }

    constexpr UserPtr<u8 const> as_bytes() const { return UserPtr<u8 const>(m_address); }

    template<typename U>
    constexpr UserPtr<U> cast() const {
        return UserPtr<U>(m_address);
    }

    constexpr bool operator==(UserPtr other) const { return m_address == other.m_address; }

private:
    VirtAddr m_address;
};

struct PhysRange {
    PhysAddr base;
    u64 length { 0 };

    constexpr PhysAddr end() const { return base.offset(length); }
    constexpr bool is_empty() const { return length == 0; }
    constexpr bool contains(PhysAddr address) const {
        return address >= base && address < end();
    }
    constexpr u64 page_count() const { return length / PAGE_SIZE; }
};

struct VirtRange {
    VirtAddr base;
    u64 length { 0 };

    constexpr VirtAddr end() const { return base.offset(length); }
    constexpr bool is_empty() const { return length == 0; }
    constexpr bool contains(VirtAddr address) const {
        return address >= base && address < end();
    }
    constexpr bool overlaps(VirtRange const& other) const {
        return base < other.end() && other.base < end();
    }
    constexpr u64 page_count() const { return length / PAGE_SIZE; }
};

}  // namespace barry
