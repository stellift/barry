#include "kernel/symbols.hpp"

namespace barry {
namespace {

bool symbol_contains(KernelSymbol const& symbol, u64 address) {
    u64 const span = symbol.size != 0 ? symbol.size : 1;
    return address >= symbol.address && address < symbol.address + span;
}

}  // namespace

StringView kernel_symbol_name(KernelSymbol const& symbol) {
    return StringView(KERNEL_SYMBOL_NAMES + symbol.name_offset);
}

Optional<KernelSymbol> find_kernel_symbol(VirtAddr address) {
    if (KERNEL_SYMBOL_COUNT == 0)
        return nullopt;

    u64 const target = address.raw();
    u32 low = 0;
    u32 high = KERNEL_SYMBOL_COUNT;

    while (low < high) {
        u32 const middle = low + (high - low) / 2;
        if (KERNEL_SYMBOLS[middle].address <= target)
            low = middle + 1;
        else
            high = middle;
    }

    if (low == 0)
        return nullopt;

    KernelSymbol const& candidate = KERNEL_SYMBOLS[low - 1];
    if (!symbol_contains(candidate, target))
        return nullopt;
    return candidate;
}

}  // namespace barry
