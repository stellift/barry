#include "kernel/arch/x86_64/processor_local.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/arch/x86_64/descriptor_tables.hpp"
#include "kernel/base/array.hpp"
#include "kernel/base/assert.hpp"

namespace barry::arch {
namespace {

Array<ProcessorLocalData, MAX_PROCESSORS> g_processor_data { {} };

ProcessorLocalData* read_gs_self() {
    ProcessorLocalData* self = nullptr;
    __asm__ volatile("movq %%gs:0, %0" : "=r"(self));
    return self;
}

}  // namespace

KResult initialize_processor_local_data(u32 index, u32 hardware_id, VirtAddr kernel_stack_top) {
    if (index >= MAX_PROCESSORS)
        return Error(Errno::ERANGE, "processor index exceeds the supported maximum");

    ProcessorLocalData& data = g_processor_data[index];
    data.self = reinterpret_cast<u64>(&data);
    data.kernel_stack_top = kernel_stack_top.raw();
    data.user_stack_scratch = 0;
    data.index = index;
    data.hardware_id = hardware_id;

    write_msr(MSR_GS_BASE, data.self);
    write_msr(MSR_KERNEL_GS_BASE, 0);
    return {};
}

ProcessorLocalData& current_processor() {
    ProcessorLocalData* self = read_gs_self();
    VERIFY(self != nullptr);
    return *self;
}

u32 current_processor_index() {
    return current_processor().index;
}

void set_current_kernel_stack(VirtAddr stack_top) {
    current_processor().kernel_stack_top = stack_top.raw();
}

void set_current_task_stacks(VirtAddr stack_top) {
    ProcessorLocalData& processor = current_processor();
    processor.kernel_stack_top = stack_top.raw();
    set_kernel_stack_for_current_processor(processor.index, stack_top);
}

}  // namespace barry::arch
