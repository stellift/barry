#include "kernel/arch/x86_64/smp.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/arch/x86_64/interrupts.hpp"
#include "kernel/arch/x86_64/local_apic.hpp"
#include "kernel/arch/x86_64/pit.hpp"
#include "kernel/arch/x86_64/processor_local.hpp"
#include "kernel/arch/x86_64/syscall.hpp"
#include "kernel/arch/x86_64/user_mode.hpp"
#include "kernel/base/array.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/base/atomic.hpp"
#include "kernel/base/bytes.hpp"
#include "kernel/log.hpp"
#include "kernel/mm/direct_map.hpp"
#include "kernel/mm/kernel_address_space.hpp"
#include "kernel/mm/kernel_stacks.hpp"
#include "kernel/mm/physical_memory.hpp"
#include "kernel/sched/scheduler.hpp"

extern "C" barry::u8 const barry_ap_trampoline_start[];
extern "C" barry::u8 const barry_ap_trampoline_end[];
extern "C" barry::u64 const barry_ap_trampoline_offsets[];

namespace barry::arch {
namespace {

enum class TrampolineParameter : usize {
    PageTableRoot = 0,
    StackTop,
    EntryPoint,
    Argument,
    ReadyFlag,
};

constexpr PhysAddr REAL_MODE_LIMIT { 1_MiB };
constexpr u32 INIT_SETTLE_MICROSECONDS = 10000;
constexpr u32 STARTUP_SETTLE_MICROSECONDS = 200;
constexpr u32 READY_POLL_MICROSECONDS = 500;
constexpr u32 READY_POLL_ATTEMPTS = 400;
constexpr u32 STARTUP_ATTEMPTS = 2;

struct StartupRecord {
    u32 hardware_id { 0 };
    VirtAddr kernel_stack_top {};
    InterruptStackSet interrupt_stacks {};
};

Array<StartupRecord, MAX_PROCESSORS> g_startup_records { {} };
Atomic<u32> g_started_processors { 1 };
u32 g_reported_processors = 1;
u32 g_failed_processors = 0;
PhysAddr g_trampoline_page {};

VirtAddr trampoline_window() {
    return VirtAddr(g_trampoline_page.raw());
}

u64 volatile* parameter_slot(TrampolineParameter which) {
    u64 const offset = barry_ap_trampoline_offsets[to_underlying(which)];
    return trampoline_window().offset(offset).as_ptr<u64 volatile>();
}

usize trampoline_size() {
    return static_cast<usize>(barry_ap_trampoline_end - barry_ap_trampoline_start);
}

KResult reserve_trampoline_page() {
    g_trampoline_page = TRY(mm::physical_memory().allocate_page_below(REAL_MODE_LIMIT));

    auto& space = mm::kernel_address_space();
    VirtAddr const identity { g_trampoline_page.raw() };
    TRY(space.map_page(identity, g_trampoline_page, mm::BOOT_TRAMPOLINE_FLAGS));

    copy_bytes(Bytes(identity.as_ptr<u8>(), PAGE_SIZE),
               ReadonlyBytes(barry_ap_trampoline_start, trampoline_size()));
    return {};
}

void wait_for_processors_to_leave_trampoline(u32 expected_online) {
    for (u32 attempt = 0; attempt < READY_POLL_ATTEMPTS; ++attempt) {
        if (g_started_processors.load(Order::Acquire) >= expected_online)
            return;
        pit_busy_wait(READY_POLL_MICROSECONDS);
    }
}

void release_trampoline_page() {
    if (g_trampoline_page.is_null())
        return;

    VirtAddr const identity { g_trampoline_page.raw() };
    if (mm::kernel_address_space().unmap_page(identity).has_value())
        mm::physical_memory().free_page(g_trampoline_page);

    g_trampoline_page = PhysAddr {};
}

KResultOr<StartupRecord> prepare_startup_record(boot::ProcessorDescriptor const& processor) {
    StartupRecord record {};
    record.hardware_id = processor.hardware_id;

    auto const startup_stack = TRY(mm::kernel_stacks().allocate());
    record.kernel_stack_top = startup_stack.top();

    for (usize index = 0; index < INTERRUPT_STACK_COUNT; ++index) {
        auto const stack = TRY(mm::kernel_stacks().allocate());
        record.interrupt_stacks.tops[index] = stack.top();
    }

    return record;
}

void publish_startup_parameters(u32 index) {
    *parameter_slot(TrampolineParameter::ReadyFlag) = 0;
    *parameter_slot(TrampolineParameter::PageTableRoot)
        = mm::kernel_address_space().root().raw();
    *parameter_slot(TrampolineParameter::StackTop)
        = g_startup_records[index].kernel_stack_top.raw();
    *parameter_slot(TrampolineParameter::EntryPoint)
        = reinterpret_cast<u64>(&barry_ap_entry);
    *parameter_slot(TrampolineParameter::Argument) = index;
    atomic_thread_fence(Order::Release);
}

bool wait_until_ready() {
    u64 volatile* const flag = parameter_slot(TrampolineParameter::ReadyFlag);

    for (u32 attempt = 0; attempt < READY_POLL_ATTEMPTS; ++attempt) {
        if (*flag != 0) {
            atomic_thread_fence(Order::Acquire);
            return true;
        }
        pit_busy_wait(READY_POLL_MICROSECONDS);
    }

    return false;
}

bool wake_processor(u32 hardware_id) {
    LocalApic::send_init_assert(hardware_id);
    pit_busy_wait(INIT_SETTLE_MICROSECONDS);

    for (u32 attempt = 0; attempt < STARTUP_ATTEMPTS; ++attempt) {
        LocalApic::send_startup(hardware_id, g_trampoline_page);
        pit_busy_wait(STARTUP_SETTLE_MICROSECONDS);
        if (wait_until_ready())
            return true;
    }

    return false;
}

}  // namespace

KResult start_application_processors(Span<boot::ProcessorDescriptor const> processors,
                                    u32 boot_processor_hardware_id) {
    g_reported_processors = static_cast<u32>(processors.size());
    if (g_reported_processors <= 1)
        return {};

    TRY(reserve_trampoline_page());

    u32 const boot_hardware_id = boot_processor_hardware_id;
    u32 next_index = 1;

    for (auto const& processor : processors) {
        if (processor.hardware_id == boot_hardware_id)
            continue;
        if (next_index >= MAX_PROCESSORS) {
            ++g_failed_processors;
            continue;
        }

        u32 const index = next_index++;
        auto record = prepare_startup_record(processor);
        if (record.is_error()) {
            dbgln("  processor {} could not be prepared: {}", processor.hardware_id,
                  record.error());
            ++g_failed_processors;
            continue;
        }

        g_startup_records[index] = record.release_value();
        publish_startup_parameters(index);

        if (!wake_processor(processor.hardware_id)) {
            dbgln("  processor {} did not answer the startup sequence", processor.hardware_id);
            ++g_failed_processors;
        }
    }

    u32 const expected_online = next_index - g_failed_processors;
    wait_for_processors_to_leave_trampoline(expected_online);
    release_trampoline_page();
    return {};
}

SmpStatistics smp_statistics() {
    SmpStatistics statistics {};
    statistics.reported_processors = g_reported_processors;
    statistics.started_processors = g_started_processors.load(Order::Acquire);
    statistics.failed_processors = g_failed_processors;
    return statistics;
}

u32 online_processor_count() {
    return g_started_processors.load(Order::Acquire);
}

}  // namespace barry::arch

extern "C" [[noreturn]] void barry_ap_entry(barry::u64 processor_index) {
    using namespace barry;
    using namespace barry::arch;

    auto const index = static_cast<u32>(processor_index);
    StartupRecord const& record = g_startup_records[index];

    MUST(initialize_descriptor_tables_for_processor(index, record.interrupt_stacks));
    load_interrupt_descriptor_table();

    set_kernel_stack_for_current_processor(index, record.kernel_stack_top);
    MUST(initialize_processor_local_data(index, record.hardware_id, record.kernel_stack_top));

    write_cr4(read_cr4() | CR4_PAGE_GLOBAL_ENABLE);
    enable_user_floating_point();
    MUST(LocalApic::initialize_for_current_processor());
    enable_fast_syscalls_on_current_processor();

    g_started_processors.fetch_add(1, Order::Release);

    sched::scheduler().run_application_processor();
}
