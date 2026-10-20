#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/array.hpp"
#include "kernel/base/types.hpp"

namespace barry::arch {

enum class Selector : u16 {
    Null = 0x00,
    KernelCode = 0x08,
    KernelData = 0x10,
    UserData = 0x1b,
    UserCode = 0x23,
    TaskState = 0x28,
};

enum class InterruptStack : u8 {
    None = 0,
    DoubleFault = 1,
    NonMaskable = 2,
    Debug = 3,
};

struct [[gnu::packed]] SegmentDescriptor {
    u16 limit_low { 0 };
    u16 base_low { 0 };
    u8 base_middle { 0 };
    u8 access { 0 };
    u8 flags_and_limit_high { 0 };
    u8 base_high { 0 };

    static constexpr SegmentDescriptor null() { return SegmentDescriptor {}; }
    static constexpr SegmentDescriptor code_segment(u8 privilege_level);
    static constexpr SegmentDescriptor data_segment(u8 privilege_level);
};

struct [[gnu::packed]] SystemSegmentDescriptor {
    SegmentDescriptor low {};
    u32 base_highest { 0 };
    u32 reserved { 0 };
};

struct [[gnu::packed]] TaskStateSegment {
    u32 reserved0 { 0 };
    Array<u64, 3> privilege_stacks { {} };
    u64 reserved1 { 0 };
    Array<u64, 7> interrupt_stacks { {} };
    u64 reserved2 { 0 };
    u16 reserved3 { 0 };
    u16 io_map_base { 0 };
};

struct [[gnu::packed]] DescriptorTablePointer {
    u16 limit { 0 };
    u64 base { 0 };
};

class GlobalDescriptorTable {
public:
    void initialize(TaskStateSegment& task_state);
    void load() const;

private:
    static constexpr usize ENTRY_COUNT = 7;

    Array<SegmentDescriptor, ENTRY_COUNT> m_entries { {} };
};

void initialize_descriptor_tables();

TaskStateSegment& boot_task_state_segment();

void set_kernel_stack_for_current_cpu(VirtAddr stack_top);

}  // namespace barry::arch

extern "C" void barry_reload_segments(barry::u16 code_selector, barry::u16 data_selector);
