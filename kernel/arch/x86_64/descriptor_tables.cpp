#include "kernel/arch/x86_64/descriptor_tables.hpp"

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

constexpr u8 privilege_bits(u8 privilege_level) {
    return static_cast<u8>((privilege_level & 0x3) << 5);
}

constexpr usize TSS_LOW_INDEX = 5;

constexpr u64 DOUBLE_FAULT_STACK_SIZE = 16_KiB;
constexpr u64 NON_MASKABLE_STACK_SIZE = 16_KiB;
constexpr u64 DEBUG_STACK_SIZE = 16_KiB;

alignas(16) u8 g_double_fault_stack[DOUBLE_FAULT_STACK_SIZE];
alignas(16) u8 g_non_maskable_stack[NON_MASKABLE_STACK_SIZE];
alignas(16) u8 g_debug_stack[DEBUG_STACK_SIZE];

TaskStateSegment g_boot_task_state;
GlobalDescriptorTable g_boot_gdt;

VirtAddr stack_top_of(u8* stack, u64 size) {
    return VirtAddr::from_ptr(stack + size);
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
    low.flags_and_limit_high = 0;
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

    barry_reload_segments(static_cast<u16>(Selector::KernelCode),
                          static_cast<u16>(Selector::KernelData));

    __asm__ volatile("ltr %0" ::"r"(static_cast<u16>(Selector::TaskState)));
}

TaskStateSegment& boot_task_state_segment() {
    return g_boot_task_state;
}

void set_kernel_stack_for_current_cpu(VirtAddr stack_top) {
    g_boot_task_state.privilege_stacks[0] = stack_top.raw();
}

void initialize_descriptor_tables() {
    auto& task_state = g_boot_task_state;

    task_state.interrupt_stacks[to_underlying(InterruptStack::DoubleFault) - 1]
        = stack_top_of(g_double_fault_stack, DOUBLE_FAULT_STACK_SIZE).raw();
    task_state.interrupt_stacks[to_underlying(InterruptStack::NonMaskable) - 1]
        = stack_top_of(g_non_maskable_stack, NON_MASKABLE_STACK_SIZE).raw();
    task_state.interrupt_stacks[to_underlying(InterruptStack::Debug) - 1]
        = stack_top_of(g_debug_stack, DEBUG_STACK_SIZE).raw();
    task_state.io_map_base = sizeof(TaskStateSegment);

    g_boot_gdt.initialize(task_state);
    g_boot_gdt.load();
}

}  // namespace barry::arch
