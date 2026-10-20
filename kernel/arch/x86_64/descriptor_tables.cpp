#include "kernel/arch/x86_64/descriptor_tables.hpp"

#include "kernel/arch/x86_64/processor_local.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/base/utility.hpp"

namespace barry::arch {
namespace {

constexpr u8 ACCESS_PRESENT = 1 << 7;
constexpr u8 ACCESS_DESCRIPTOR_TYPE = 1 << 4;
constexpr u8 ACCESS_EXECUTABLE = 1 << 3;
constexpr u8 ACCESS_READ_WRITE = 1 << 1;
constexpr u8 ACCESS_TSS_AVAILABLE = 0x09;

constexpr u8 FLAG_LONG_MODE = 1 << 5;
constexpr u8 FLAG_GRANULARITY_4K = 1 << 7;
constexpr u8 FLAG_SIZE_32BIT = 1 << 6;

constexpr usize TSS_LOW_INDEX = 5;

constexpr u8 privilege_bits(u8 privilege_level) {
    return static_cast<u8>((privilege_level & 0x3) << 5);
}

alignas(16) Array<u8, INTERRUPT_STACK_SIZE> g_boot_double_fault_stack { {} };
alignas(16) Array<u8, INTERRUPT_STACK_SIZE> g_boot_non_maskable_stack { {} };
alignas(16) Array<u8, INTERRUPT_STACK_SIZE> g_boot_debug_stack { {} };

Array<TaskStateSegment, MAX_PROCESSORS> g_task_states { {} };
Array<GlobalDescriptorTable, MAX_PROCESSORS> g_descriptor_tables { {} };

VirtAddr top_of(Array<u8, INTERRUPT_STACK_SIZE>& stack) {
    return VirtAddr::from_ptr(stack.data() + stack.size());
}

void install_interrupt_stacks(TaskStateSegment& task_state, InterruptStackSet const& stacks) {
    for (usize index = 0; index < INTERRUPT_STACK_COUNT; ++index)
        task_state.interrupt_stacks[index] = stacks.tops[index].raw();
    task_state.io_map_base = sizeof(TaskStateSegment);
}

}  // namespace

constexpr SegmentDescriptor SegmentDescriptor::code_segment(u8 privilege_level) {
    SegmentDescriptor descriptor {};
    descriptor.access = static_cast<u8>(ACCESS_PRESENT | ACCESS_DESCRIPTOR_TYPE
                                        | ACCESS_EXECUTABLE | ACCESS_READ_WRITE
                                        | privilege_bits(privilege_level));
    descriptor.flags_and_limit_high = FLAG_LONG_MODE;
    return descriptor;
}

constexpr SegmentDescriptor SegmentDescriptor::data_segment(u8 privilege_level) {
    SegmentDescriptor descriptor {};
    descriptor.access = static_cast<u8>(ACCESS_PRESENT | ACCESS_DESCRIPTOR_TYPE
                                        | ACCESS_READ_WRITE | privilege_bits(privilege_level));
    descriptor.flags_and_limit_high = static_cast<u8>(FLAG_GRANULARITY_4K | FLAG_SIZE_32BIT);
    return descriptor;
}

void GlobalDescriptorTable::initialize(TaskStateSegment& task_state) {
    m_entries[0] = SegmentDescriptor::null();
    m_entries[1] = SegmentDescriptor::code_segment(0);
    m_entries[2] = SegmentDescriptor::data_segment(0);
    m_entries[3] = SegmentDescriptor::data_segment(3);
    m_entries[4] = SegmentDescriptor::code_segment(3);

    auto const base = reinterpret_cast<u64>(&task_state);
    auto const limit = static_cast<u16>(sizeof(TaskStateSegment) - 1);

    SegmentDescriptor low {};
    low.limit_low = limit;
    low.base_low = static_cast<u16>(base & 0xffff);
    low.base_middle = static_cast<u8>((base >> 16) & 0xff);
    low.access = ACCESS_TSS_AVAILABLE | ACCESS_PRESENT;
    low.base_high = static_cast<u8>((base >> 24) & 0xff);

    SegmentDescriptor high {};
    high.limit_low = static_cast<u16>((base >> 32) & 0xffff);
    high.base_low = static_cast<u16>((base >> 48) & 0xffff);

    m_entries[TSS_LOW_INDEX] = low;
    m_entries[TSS_LOW_INDEX + 1] = high;
}

void GlobalDescriptorTable::load() const {
    DescriptorTablePointer pointer {};
    pointer.limit = static_cast<u16>(sizeof(m_entries) - 1);
    pointer.base = reinterpret_cast<u64>(m_entries.data());

    __asm__ volatile("lgdt %0" ::"m"(pointer) : "memory");

    barry_reload_segments(to_underlying(Selector::KernelCode), to_underlying(Selector::KernelData));

    __asm__ volatile("ltr %0" ::"r"(to_underlying(Selector::TaskState)));
}

TaskStateSegment& task_state_segment_of(u32 index) {
    VERIFY(index < MAX_PROCESSORS);
    return g_task_states[index];
}

void set_kernel_stack_for_current_processor(u32 index, VirtAddr stack_top) {
    task_state_segment_of(index).privilege_stacks[0] = stack_top.raw();
}

KResult initialize_descriptor_tables_for_processor(u32 index, InterruptStackSet const& stacks) {
    if (index >= MAX_PROCESSORS)
        return Error(Errno::ERANGE, "processor index exceeds the supported maximum");

    TaskStateSegment& task_state = g_task_states[index];
    install_interrupt_stacks(task_state, stacks);

    GlobalDescriptorTable& table = g_descriptor_tables[index];
    table.initialize(task_state);
    table.load();
    return {};
}

KResult initialize_descriptor_tables_for_boot_processor() {
    InterruptStackSet stacks {};
    stacks.tops[to_underlying(InterruptStack::DoubleFault) - 1]
        = top_of(g_boot_double_fault_stack);
    stacks.tops[to_underlying(InterruptStack::NonMaskable) - 1]
        = top_of(g_boot_non_maskable_stack);
    stacks.tops[to_underlying(InterruptStack::Debug) - 1] = top_of(g_boot_debug_stack);

    return initialize_descriptor_tables_for_processor(0, stacks);
}

}  // namespace barry::arch
