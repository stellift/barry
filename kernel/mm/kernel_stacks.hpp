#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/mm/layout.hpp"
#include "kernel/sync/spin_lock.hpp"

namespace barry::mm {

struct KernelStack {
    VirtRange usable {};
    VirtAddr guard_page {};

    bool is_valid() const { return usable.length != 0; }
    VirtAddr top() const { return usable.end(); }
    VirtAddr bottom() const { return usable.base; }
};

class KernelStackAllocator {
public:
    static constexpr u64 SLOT_SIZE = KERNEL_STACK_SIZE + KERNEL_STACK_GUARD_SIZE;

    KResult initialize();

    KResultOr<KernelStack> allocate();
    void free(KernelStack stack);

    u64 live_stack_count() const;

private:
    struct FreeSlot {
        FreeSlot* next { nullptr };
    };

    struct State {
        VirtRange area {};
        u64 next_slot_index { 0 };
        FreeSlot* free_list { nullptr };
        u64 live_stacks { 0 };
    };

    static KernelStack stack_from_slot(VirtAddr slot_base);

    KResultOr<KernelStack> carve_new_slot(State& state);

    mutable sync::Protected<State> m_state;
    bool m_initialized { false };
};

KernelStackAllocator& kernel_stacks();

KResult initialize_kernel_stacks();

}  // namespace barry::mm
