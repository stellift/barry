#include "kernel/acpi/tables.hpp"
#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/arch/x86_64/descriptor_tables.hpp"
#include "kernel/arch/x86_64/interrupts.hpp"
#include "kernel/arch/x86_64/legacy_pic.hpp"
#include "kernel/arch/x86_64/local_apic.hpp"
#include "kernel/arch/x86_64/processor_local.hpp"
#include "kernel/arch/x86_64/smp.hpp"
#include "kernel/arch/x86_64/syscall.hpp"
#include "kernel/base/array.hpp"
#include "kernel/base/types.hpp"
#include "kernel/boot/command_line.hpp"
#include "kernel/boot/kernel_entry.hpp"
#include "kernel/arch/x86_64/user_mode.hpp"
#include "kernel/dev/serial.hpp"
#include "kernel/boot/initramfs.hpp"
#include "kernel/fs/console.hpp"
#include "kernel/fs/virtual_file_system.hpp"
#include "kernel/syscall/handlers.hpp"
#include "kernel/log.hpp"
#include "kernel/mm/direct_map.hpp"
#include "kernel/mm/kernel_address_space.hpp"
#include "kernel/mm/heap.hpp"
#include "kernel/mm/kernel_stacks.hpp"
#include "kernel/mm/physical_memory.hpp"
#include "kernel/sched/scheduler.hpp"
#include "kernel/panic.hpp"
#include "kernel/test/automation.hpp"
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
    return arch::initialize_descriptor_tables_for_boot_processor();
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

KResult set_up_kernel_stacks(boot::BootInfo const&) {
    return mm::initialize_kernel_stacks();
}

KResult set_up_processor_local_data(boot::BootInfo const& boot_info) {
    auto const stack = TRY(mm::kernel_stacks().allocate());
    arch::set_kernel_stack_for_current_processor(0, stack.top());
    return arch::initialize_processor_local_data(0, boot_info.boot_processor_hardware_id,
                                                 stack.top());
}

KResult set_up_local_apic(boot::BootInfo const&) {
    return arch::LocalApic::initialize_for_boot_processor();
}

KResult set_up_fast_syscalls(boot::BootInfo const&) {
    arch::enable_user_floating_point();
    fs::initialize_console();
    TRY(arch::initialize_fast_syscalls());
    return syscall::install_default_handlers();
}

KResult set_up_root_filesystem(boot::BootInfo const&) {
    TRY(fs::initialize_root_filesystem());

    auto const summary = TRY(boot::populate_from_cpio_archive(boot::embedded_initramfs()));
    dbgln("       initramfs: {} directories, {} files, {} bytes", summary.directories,
          summary.files, summary.bytes);
    return {};
}

KResult set_up_scheduler(boot::BootInfo const&) {
    TRY(sched::initialize_scheduler());
    return sched::scheduler().start_preemption_on_current_processor();
}

Span<boot::ProcessorDescriptor const> discover_processors(
    boot::BootInfo const& boot_info, Span<boot::ProcessorDescriptor> scratch) {
    if (!boot_info.acpi_rsdp.has_value())
        return boot_info.processors;

    auto const initialized = acpi::initialize(boot_info.acpi_rsdp.value());
    if (initialized.is_error()) {
        dbgln("  acpi tables unusable: {}", initialized.error());
        return boot_info.processors;
    }

    usize const discovered = acpi::enumerate_processors(scratch);
    if (discovered <= boot_info.processors.size())
        return boot_info.processors;

    dbgln("  acpi reports {} processors; the loader reported {}", discovered,
          boot_info.processors.size());
    return Span<boot::ProcessorDescriptor const>(scratch.data(), discovered);
}

KResult set_up_application_processors(boot::BootInfo const& boot_info) {
    Array<boot::ProcessorDescriptor, arch::MAX_PROCESSORS> scratch { {} };
    auto const processors = discover_processors(boot_info, scratch.span());
    return arch::start_application_processors(processors, arch::LocalApic::current_id());
}

struct BringUpStep {
    StringView name;
    KResult (*run)(boot::BootInfo const&);
};

constexpr Array<BringUpStep, 13> BRING_UP_SEQUENCE { {
    { "descriptor tables"_sv, &set_up_descriptor_tables },
    { "interrupts"_sv, &set_up_interrupts },
    { "direct map"_sv, &set_up_direct_map },
    { "physical memory"_sv, &set_up_physical_memory },
    { "kernel address space"_sv, &set_up_kernel_address_space },
    { "kernel heap"_sv, &set_up_kernel_heap },
    { "kernel stacks"_sv, &set_up_kernel_stacks },
    { "processor local data"_sv, &set_up_processor_local_data },
    { "local apic"_sv, &set_up_local_apic },
    { "fast syscalls"_sv, &set_up_fast_syscalls },
    { "root filesystem"_sv, &set_up_root_filesystem },
    { "scheduler"_sv, &set_up_scheduler },
    { "application processors"_sv, &set_up_application_processors },
} };

void run_bring_up_sequence(boot::BootInfo const& boot_info) {
    for (auto const& step : BRING_UP_SEQUENCE) {
        auto result = step.run(boot_info);
        if (result.is_error())
            PANIC("bring-up step '{}' failed: {}", step.name, result.error());
        dbgln("  [ok] {}", step.name);
    }
}

void report_scheduler_state() {
    auto const schedule = sched::scheduler().statistics();
    auto const smp = arch::smp_statistics();

    dbgln("Scheduler:");
    dbgln("  threads   : {}", schedule.live_threads);
    dbgln("  switches  : {}", schedule.context_switches);
    dbgln("  ticks     : {}", schedule.timer_ticks);
    dbgln("Processors:");
    dbgln("  reported  : {}", smp.reported_processors);
    dbgln("  online    : {}", smp.started_processors);
    dbgln("  failed    : {}", smp.failed_processors);
    dbgln("  preempting: {}", schedule.preempting_processors);
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

    dbgln("Timing:");
    dbgln("  apic timer: {} Hz", arch::LocalApic::calibrated_timer_frequency_hz());
    dbgln("  timestamp : {} Hz", arch::LocalApic::calibrated_timestamp_frequency_hz());
    dbgln("  preemption: {} Hz", sched::Scheduler::TIMER_FREQUENCY_HZ);
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
    report_scheduler_state();

    boot::CommandLine const command_line { boot_info.command_line };
    auto const tests = test::run_all_tests();

    if (test::automation_requested(command_line))
        test::finish_automated_run(tests);

    if (!tests.all_passed())
        PANIC("{} in-kernel tests failed", tests.failed);

    dbgln("");
    dbgln("Barry reached the end of phase-1 bring-up; halting.");
    arch::halt_forever();
}

}  // namespace barry
