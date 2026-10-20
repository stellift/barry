#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/types.hpp"

namespace barry::mm {

void initialize_direct_map(u64 base);

u64 direct_map_base();

VirtAddr direct_map_of(PhysAddr address);

PhysAddr physical_address_of(VirtAddr direct_mapped_address);

bool is_direct_mapped(VirtAddr address);

template<typename T>
T* direct_map_pointer(PhysAddr address) {
    return direct_map_of(address).as_ptr<T>();
}

template<typename T>
Span<T> direct_map_span(PhysAddr address, usize count) {
    return Span<T>(direct_map_pointer<T>(address), count);
}

inline Bytes direct_map_bytes(PhysRange range) {
    return Bytes(direct_map_pointer<u8>(range.base), range.length);
}

}  // namespace barry::mm
