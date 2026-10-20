#include "kernel/mm/direct_map.hpp"

namespace barry::mm {
namespace {

u64 g_direct_map_base = 0;
bool g_initialized = false;

}  // namespace

void initialize_direct_map(u64 base) {
    VERIFY(!g_initialized);
    VERIFY(base != 0);
    VERIFY(VirtAddr(base).is_canonical());
    g_direct_map_base = base;
    g_initialized = true;
}

u64 direct_map_base() {
    VERIFY(g_initialized);
    return g_direct_map_base;
}

VirtAddr direct_map_of(PhysAddr address) {
    VERIFY(g_initialized);
    return VirtAddr(g_direct_map_base + address.raw());
}

PhysAddr physical_address_of(VirtAddr address) {
    VERIFY(g_initialized);
    VERIFY(address.raw() >= g_direct_map_base);
    return PhysAddr(address.raw() - g_direct_map_base);
}

bool is_direct_mapped(VirtAddr address) {
    return g_initialized && address.raw() >= g_direct_map_base;
}

}  // namespace barry::mm
