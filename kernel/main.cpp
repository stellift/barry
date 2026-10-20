#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/arch/x86_64/descriptor_tables.hpp"
#include "kernel/arch/x86_64/interrupts.hpp"
#include "kernel/arch/x86_64/legacy_pic.hpp"
#include "kernel/base/array.hpp"
#include "kernel/base/types.hpp"
#include "kernel/boot/kernel_entry.hpp"
#include "kernel/dev/serial.hpp"
#include "kernel/log.hpp"
#include "kernel/mm/direct_map.hpp"
#include "kernel/mm/kernel_address_space.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/mm/physical_memory.hpp"
#include "kernel/panic.hpp"
#include "kernel/test/framework.hpp"

namespace barry {
namespace {

constexpr StringView BARRY_VERSION = "0.1.0-phase1"_sv;

void print_banner(boot::BootInfo const& boot_info) {
    dbgln("");
    dbgln("Barry {} on x86_64", BARRY_VERSION);
    dbgln("  loader        : {}", boot_info.loader_name);
    dbgln("  command line  : {}", boot_info.command_line);
    dbgln("  kernel phys   : {}", boot_info.kernel_image.physical_base);
    dbgln("  kernel virt   : {}", boot_info.kernel_image.virtual_base);
    dbgln("  direct map    : {}", VirtAddr(boot_info.direct_map_base));
    dbgln("  processors    : {}", boot_info.processors.size());
    dbgln("  memory regions: {}", boot_info.memory_map.size());
    dbgln("  usable memory : {} KiB", boot_info.total_usable_bytes() / 1024);

    if (boot_info.framebuffer.has_value()) {
        auto const& framebuffer = boot_info.framebuffer.value();
        dbgln("  framebuffer   : {}x{} at {} ({} bpp)", framebuffer.width, framebuffer.height,
              framebuffer.base, framebuffer.bits_per_pixel);
    }
}

KResult set_up_descriptor_tables(boot::BootInfo const&) {
    arch::initialize_descriptor_tables();
    return {};
}

KResult set_up_interrupts(boot::BootInfo const&) {
    arch::remap_and_mask_legacy_pic();
    arch::initialize_interrupts();
    return {};
}

KResult set_up_direct_map(boot::BootInfo const& boot_info) {
    mm::initialize_direct_map(boot_info.direct_map_base);
    return {};
}

KResult set_up_physical_memory(boot::BootInfo const& boot_info) {
    return mm::physical_memory().initialize(boot_info);
}

KResult set_up_kernel_address_space(boot::BootInfo const& boot_info) {
    return mm::build_and_activate_kernel_address_space(boot_info);
}

KResult set_up_kernel_heap(boot::BootInfo const&) {
    return mm::initialize_kernel_heap();
}

struct BringUpStep {
    StringView name;
    KResult (*run)(boot::BootInfo const&);
};

constexpr Array<BringUpStep, 6> BRING_UP_SEQUENCE { {
    { "descriptor tables"_sv, &set_up_descriptor_tables },
    { "interrupts"_sv, &set_up_interrupts },
    { "direct map"_sv, &set_up_direct_map },
    { "physical memory"_sv, &set_up_physical_memory },
    { "kernel address space"_sv, &set_up_kernel_address_space },
    { "kernel heap"_sv, &set_up_kernel_heap },
} };

void run_bring_up_sequence(boot::BootInfo const& boot_info) {
    for (auto const& step : BRING_UP_SEQUENCE) {
        auto result = step.run(boot_info);
        if (result.is_error())
            PANIC("bring-up step '{}' failed: {}", step.name, result.error());
        dbgln("  [ok] {}", step.name);
    }
}

void report_memory_state() {
    auto const statistics = mm::physical_memory().statistics();
    dbgln("");
    dbgln("Physical memory:");
    dbgln("  managed   : {} KiB", statistics.managed_bytes() / 1024);
    dbgln("  free      : {} KiB", statistics.free_bytes() / 1024);
    dbgln("  allocated : {} KiB", statistics.allocated_bytes() / 1024);

    auto const ranges = mm::kernel_image_ranges();
    dbgln("Kernel image mapping:");
    dbgln("  code      : {} .. {}", ranges.code.base, ranges.code.end());
    dbgln("  readonly  : {} .. {}", ranges.readonly.base, ranges.readonly.end());
    dbgln("  writable  : {} .. {}", ranges.writable.base, ranges.writable.end());

    auto const heap = mm::kernel_heap().statistics();
    dbgln("Kernel heap:");
    dbgln("  mapped    : {} KiB", heap.mapped_bytes / 1024);
    dbgln("  allocated : {} bytes", heap.allocated_bytes);
}

}  // namespace

void boot_failure(StringView reason) {
    dev::debug_port().initialize();
    dbgln("");
    dbgln("Barry failed to start: {}", reason);
    arch::halt_forever();
}

void kernel_main(boot::BootInfo const& boot_info) {
    dev::debug_port().initialize();

    print_banner(boot_info);

    if (!boot_info.is_valid())
        PANIC("the boot backend handed the kernel an invalid BootInfo");

    dbgln("");
    dbgln("Bring-up:");
    run_bring_up_sequence(boot_info);
    report_memory_state();

    auto const tests = test::run_all_tests();
    if (!tests.all_passed())
        PANIC("{} in-kernel tests failed", tests.failed);

    dbgln("");
    dbgln("Barry reached the end of phase-1 bring-up; halting.");
    arch::halt_forever();
}

}  // namespace barry
