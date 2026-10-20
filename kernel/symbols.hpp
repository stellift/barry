#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/base/types.hpp"

namespace barry {

struct KernelSymbol {
    u64 address;
    u32 size;
    u32 name_offset;
};

extern KernelSymbol const KERNEL_SYMBOLS[];
extern u32 const KERNEL_SYMBOL_COUNT;
extern char const KERNEL_SYMBOL_NAMES[];

StringView kernel_symbol_name(KernelSymbol const& symbol);

Optional<KernelSymbol> find_kernel_symbol(VirtAddr address);

}  // namespace barry
