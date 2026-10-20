#include "kernel/boot/memory_reservations.hpp"

#include "kernel/base/assert.hpp"
#include "kernel/base/utility.hpp"

namespace barry::boot {

bool MemoryReservations::add(PhysRange range) {
    if (range.length == 0)
        return false;
    if (m_count == CAPACITY)
        return false;

    usize position = m_count;
    while (position > 0 && range.base < m_ranges[position - 1].base) {
        m_ranges[position] = m_ranges[position - 1];
        --position;
    }

    m_ranges[position] = range;
    ++m_count;
    return true;
}

usize MemoryReservations::split(PhysRange region, Span<PhysRange> fragments) const {
    usize written = 0;
    PhysAddr cursor = region.base;

    auto emit = [&](PhysAddr start, PhysAddr end) {
        if (end <= start || written >= fragments.size())
            return;
        fragments[written++] = PhysRange { start, start.distance_to(end) };
    };

    for (usize index = 0; index < m_count && cursor < region.end(); ++index) {
        PhysRange const& reserved = m_ranges[index];
        if (reserved.end() <= cursor)
            continue;
        if (reserved.base >= region.end())
            break;

        emit(cursor, min(reserved.base, region.end()));
        cursor = max(cursor, reserved.end());
    }

    emit(cursor, region.end());
    return written;
}

}  // namespace barry::boot
