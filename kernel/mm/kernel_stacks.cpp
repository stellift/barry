#include "kernel/mm/kernel_stacks.hpp"

#include "kernel/base/assert.hpp"
#include "kernel/mm/kernel_address_space.hpp"
#include "kernel/mm/paging.hpp"
#include "kernel/mm/physical_memory.hpp"

namespace barry::mm {
namespace {

KernelStackAllocator g_kernel_stacks;

KResult map_stack_pages(VirtRange range) {
    u64 mapped = 0;

    while (mapped < range.length) {
        VirtAddr const virtual_address = range.base.offset(mapped);
        auto frame = physical_memory().allocate_zeroed_page();
        if (frame.is_error())
            return Unexpected(frame.release_error());

        auto mapping = kernel_address_space().map_page(virtual_address, frame.value(),
                                                       KERNEL_DATA_FLAGS);
        if (mapping.is_error()) {
            physical_memory().free_page(frame.value());
            return Unexpected(mapping.release_error());
        }

        mapped += PAGE_SIZE;
    }

    return {};
}

}  // namespace

KernelStack KernelStackAllocator::stack_from_slot(VirtAddr slot_base) {
    KernelStack stack {};
    stack.guard_page = slot_base;
    stack.usable = VirtRange { slot_base.offset(KERNEL_STACK_GUARD_SIZE), KERNEL_STACK_SIZE };
    return stack;
}

KResult KernelStackAllocator::initialize() {
    if (m_initialized)
        return Error(Errno::EBUSY, "the kernel stack allocator is already initialized");

    auto guard = m_state.lock();
    guard->area = kernel_stack_area_range();
    guard->next_slot_index = 0;
    guard->free_list = nullptr;
    guard->live_stacks = 0;
    m_initialized = true;
    return {};
}

KResultOr<KernelStack> KernelStackAllocator::carve_new_slot(State& state) {
    u64 const offset = state.next_slot_index * SLOT_SIZE;
    if (offset + SLOT_SIZE > state.area.length)
        return Error(Errno::ENOMEM, "the kernel stack area is exhausted");

    KernelStack const stack = stack_from_slot(state.area.base.offset(offset));
    TRY(map_stack_pages(stack.usable));

    ++state.next_slot_index;
    return stack;
}

KResultOr<KernelStack> KernelStackAllocator::allocate() {
    if (!m_initialized)
        return Error(Errno::EINVAL, "the kernel stack allocator is not initialized");

    auto guard = m_state.lock();

    if (guard->free_list != nullptr) {
        FreeSlot* const slot = guard->free_list;
        guard->free_list = slot->next;
        ++guard->live_stacks;
        return stack_from_slot(VirtAddr::from_ptr(slot).subtract(KERNEL_STACK_GUARD_SIZE));
    }

    auto stack = TRY(carve_new_slot(*guard));
    ++guard->live_stacks;
    return stack;
}

void KernelStackAllocator::free(KernelStack stack) {
    VERIFY(stack.is_valid());

    auto guard = m_state.lock();
    VERIFY(guard->live_stacks > 0);
    --guard->live_stacks;

    auto* slot = stack.bottom().as_ptr<FreeSlot>();
    slot->next = guard->free_list;
    guard->free_list = slot;
}

u64 KernelStackAllocator::live_stack_count() const {
    auto guard = m_state.lock();
    return guard->live_stacks;
}

KernelStackAllocator& kernel_stacks() {
    return g_kernel_stacks;
}

KResult initialize_kernel_stacks() {
    return g_kernel_stacks.initialize();
}

}  // namespace barry::mm
