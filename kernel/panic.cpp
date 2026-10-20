#include "kernel/panic.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/arch/x86_64/stack_walk.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/base/atomic.hpp"
#include "kernel/log.hpp"
#include "kernel/symbols.hpp"

namespace barry {
namespace {

Atomic<bool> g_panic_in_progress { false };

void print_panic_banner() {
    log_write("\n"_sv);
    log_write("================ BARRY KERNEL PANIC ================\n"_sv);
}

void print_location(SourceLocation const& location) {
    dbgln("  at {}:{} in {}", location.file, location.line, location.function);
}

}  // namespace

Optional<ResolvedSymbol> resolve_symbol(VirtAddr address) {
    auto const symbol = find_kernel_symbol(address);
    if (!symbol.has_value())
        return nullopt;
    return ResolvedSymbol { kernel_symbol_name(symbol.value()), VirtAddr(symbol->address),
                            address.raw() - symbol->address };
}

void print_backtrace() {
    log_write("  backtrace:\n"_sv);

    auto walker = arch::StackFrameWalker::from_current_frame();
    usize frame_index = 0;

    while (auto return_address = walker.next_return_address()) {
        VirtAddr const address = return_address.value();
        if (auto symbol = resolve_symbol(address))
            dbgln("    #{} {} <{}+{#x}>", frame_index, address, symbol->name, symbol->offset);
        else
            dbgln("    #{} {} <unknown>", frame_index, address);
        ++frame_index;
    }

    if (frame_index == 0)
        log_write("    <no frames; was the kernel built with frame pointers?>\n"_sv);
}

void enter_panic(StringView message, Optional<SourceLocation> location) {
    arch::disable_interrupts();

    bool expected = false;
    if (!g_panic_in_progress.compare_exchange_strong(expected, true))
        arch::halt_forever();

    print_panic_banner();
    dbgln("  {}", message);
    if (location.has_value())
        print_location(location.value());
    print_backtrace();
    log_write("====================================================\n"_sv);

    arch::halt_forever();
}

void assertion_failed(char const* expression, char const* file, u32 line, char const* function) {
    FixedStringBuffer<PANIC_MESSAGE_CAPACITY> message;
    format_to(message, "assertion failed: {}"_sv, expression);
    enter_panic(message.view(),
                SourceLocation { StringView(file), StringView(function), line });
}

}  // namespace barry
