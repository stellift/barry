#pragma once

#include "kernel/base/fixed_string_buffer.hpp"
#include "kernel/base/format.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/source_location.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/base/types.hpp"

namespace barry {

struct ResolvedSymbol {
    StringView name;
    VirtAddr base;
    u64 offset { 0 };
};

Optional<ResolvedSymbol> resolve_symbol(VirtAddr address);

void print_backtrace();

[[noreturn]] void enter_panic(StringView message, Optional<SourceLocation> location);

inline constexpr usize PANIC_MESSAGE_CAPACITY = 512;

template<typename... Args>
[[noreturn]] void panic_at(SourceLocation location, StringView format, Args const&... args) {
    FixedStringBuffer<PANIC_MESSAGE_CAPACITY> message;
    format_to(message, format, args...);
    enter_panic(message.view(), location);
}

}  // namespace barry

#define PANIC(...) ::barry::panic_at(BARRY_SOURCE_LOCATION, __VA_ARGS__)
