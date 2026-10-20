#include "kernel/sched/thread.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/base/atomic.hpp"
#include "kernel/mm/kernel_address_space.hpp"
#include "kernel/sched/scheduler.hpp"

namespace barry::sched {
namespace {

Atomic<ThreadId> g_next_thread_id { 1 };

enum class ContextSlot : usize {
    Rflags = 0,
    R15,
    R14,
    R13,
    R12,
    Rbx,
    Rbp,
    ReturnAddress,
    Count,
};

constexpr usize CONTEXT_SLOT_COUNT = to_underlying(ContextSlot::Count);
constexpr u64 INITIAL_RFLAGS = arch::RFLAGS_INTERRUPT_ENABLE | 0x2;

u64& slot_of(u64* context, ContextSlot which) {
    return context[to_underlying(which)];
}

}  // namespace

char const* thread_state_name(ThreadState state) {
    switch (state) {
    case ThreadState::Ready:
        return "ready";
    case ThreadState::Running:
        return "running";
    case ThreadState::Blocked:
        return "blocked";
    case ThreadState::Finished:
        return "finished";
    }
    return "unknown";
}

Thread::Thread(StringView name, ThreadId id, mm::KernelStack stack, ThreadEntry entry,
               void* argument)
    : m_name(name), m_id(id), m_stack(stack), m_entry(entry), m_argument(argument) {
    m_address_space_root = mm::kernel_address_space().root();
    arch::initialize_fpu_state(floating_point_state());
    prepare_initial_stack();
}

Thread::Thread(StringView name, ThreadId id) : m_name(name), m_id(id) {
    m_state = ThreadState::Running;
    m_address_space_root = mm::kernel_address_space().root();
    arch::initialize_fpu_state(floating_point_state());
}

Thread::~Thread() {
    VERIFY(!ReadyQueueNode::is_linked());
    VERIFY(!AllThreadsNode::is_linked());

    if (m_stack.is_valid())
        mm::kernel_stacks().free(m_stack);
}

void Thread::prepare_initial_stack() {
    VERIFY(m_stack.is_valid());
    VERIFY(is_aligned(m_stack.top().raw(), 16ULL));

    u64* const context
        = m_stack.top().subtract(CONTEXT_SLOT_COUNT * sizeof(u64)).as_ptr<u64>();

    slot_of(context, ContextSlot::Rflags) = INITIAL_RFLAGS;
    slot_of(context, ContextSlot::R15) = 0;
    slot_of(context, ContextSlot::R14) = 0;
    slot_of(context, ContextSlot::R13) = 0;
    slot_of(context, ContextSlot::R12) = 0;
    slot_of(context, ContextSlot::Rbx) = 0;
    slot_of(context, ContextSlot::Rbp) = 0;
    slot_of(context, ContextSlot::ReturnAddress)
        = reinterpret_cast<u64>(&barry_thread_trampoline);

    m_stack_pointer = reinterpret_cast<u64>(context);
}

void Thread::run_entry() {
    VERIFY(m_entry != nullptr);
    m_entry(m_argument);
}

KResultOr<OwnPtr<Thread>> Thread::adopt_running_context(StringView name) {
    ThreadId const id = g_next_thread_id.fetch_add(1, Order::Relaxed);
    return TRY(make_own<Thread>(name, id));
}

KResultOr<OwnPtr<Thread>> Thread::create_user_thread(StringView name, ThreadEntry entry,
                                                     void* argument,
                                                     PhysAddr address_space_root) {
    auto thread = TRY(create(name, entry, argument));
    thread->set_process(argument);
    thread->set_address_space_root(address_space_root);
    arch::initialize_fpu_state(thread->floating_point_state());
    return thread;
}

KResultOr<OwnPtr<Thread>> Thread::create(StringView name, ThreadEntry entry, void* argument) {
    if (entry == nullptr)
        return Error(Errno::EINVAL, "a thread needs an entry point");

    auto stack = TRY(mm::kernel_stacks().allocate());
    ThreadId const id = g_next_thread_id.fetch_add(1, Order::Relaxed);

    auto thread = make_own<Thread>(name, id, stack, entry, argument);
    if (thread.is_error()) {
        mm::kernel_stacks().free(stack);
        return Unexpected(thread.release_error());
    }

    return thread.release_value();
}

}  // namespace barry::sched

extern "C" void barry_enter_thread() {
    barry::sched::scheduler().enter_current_thread();
}
