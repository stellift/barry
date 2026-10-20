#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/array.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/types.hpp"

namespace barry::boot {

class MemoryReservations {
public:
    static constexpr usize CAPACITY = 16;
    static constexpr usize MAX_FRAGMENTS = CAPACITY + 1;

    bool add(PhysRange range);

    Span<PhysRange const> all() const {
        return Span<PhysRange const>(m_ranges.data(), m_count);
    }

    usize count() const { return m_count; }
    bool is_full() const { return m_count == CAPACITY; }

    usize split(PhysRange region, Span<PhysRange> fragments) const;

private:
    Array<PhysRange, CAPACITY> m_ranges { {} };
    usize m_count { 0 };
};

}  // namespace barry::boot
