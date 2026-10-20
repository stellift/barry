#pragma once

#include "kernel/base/intrusive_list.hpp"
#include "kernel/base/own_ptr.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/base/types.hpp"
#include "kernel/arch/x86_64/syscall.hpp"
#include "kernel/arch/x86_64/user_mode.hpp"
#include "kernel/base/array.hpp"
#include "kernel/mm/kernel_stacks.hpp"

namespace barry::sched {

class Thread;

enum class ThreadState : u8 {
    Ready,
    Running,
    Blocked,
    Finished,
};

char const* thread_state_name(ThreadState state);

using ThreadEntry = void (*)(void* argument);

using ThreadId = u64;

struct ReadyQueueTag {};
struct AllThreadsTag {};

class Thread
    : public IntrusiveListNode<Thread, ReadyQueueTag>
    , public IntrusiveListNode<Thread, AllThreadsTag> {
public:
    using ReadyQueueNode = IntrusiveListNode<Thread, ReadyQueueTag>;
    using AllThreadsNode = IntrusiveListNode<Thread, AllThreadsTag>;

    static KResultOr<OwnPtr<Thread>> create(StringView name, ThreadEntry entry, void* argument);
    static KResultOr<OwnPtr<Thread>> create_user_thread(StringView name, ThreadEntry entry,
                                                        void* argument,
                                                        PhysAddr address_space_root);
    static KResultOr<OwnPtr<Thread>> adopt_running_context(StringView name);

    Thread(StringView name, ThreadId id, mm::KernelStack stack, ThreadEntry entry,
           void* argument);
    explicit Thread(StringView name, ThreadId id);
    ~Thread();

    Thread(Thread const&) = delete;
    Thread& operator=(Thread const&) = delete;

    ThreadId id() const { return m_id; }
    StringView name() const { return m_name; }
    ThreadState state() const { return m_state; }
    void set_state(ThreadState state) { m_state = state; }

    u64 stack_pointer() const { return m_stack_pointer; }
    u64* stack_pointer_slot() { return &m_stack_pointer; }
    VirtAddr stack_top() const { return m_stack.top(); }

    u64 scheduled_count() const { return m_scheduled_count; }
    void note_scheduled() { ++m_scheduled_count; }

    bool is_idle() const { return m_is_idle; }
    void mark_as_idle() { m_is_idle = true; }

    bool is_user_thread() const { return m_process != nullptr; }
    void* process() const { return m_process; }
    void set_process(void* process) { m_process = process; }

    PhysAddr address_space_root() const { return m_address_space_root; }
    void set_address_space_root(PhysAddr root) { m_address_space_root = root; }

    Span<u8> floating_point_state() { return m_floating_point_state.span(); }

    VirtAddr thread_pointer() const { return m_thread_pointer; }
    void set_thread_pointer(VirtAddr address) { m_thread_pointer = address; }

    arch::SyscallFrame* syscall_frame() const { return m_syscall_frame; }
    void set_syscall_frame(arch::SyscallFrame* frame) { m_syscall_frame = frame; }

    void run_entry();

private:
    void prepare_initial_stack();

    StringView m_name;
    ThreadId m_id { 0 };
    ThreadState m_state { ThreadState::Ready };
    mm::KernelStack m_stack {};
    ThreadEntry m_entry { nullptr };
    void* m_argument { nullptr };
    u64 m_stack_pointer { 0 };
    u64 m_scheduled_count { 0 };
    bool m_is_idle { false };
    void* m_process { nullptr };
    PhysAddr m_address_space_root {};
    VirtAddr m_thread_pointer {};
    arch::SyscallFrame* m_syscall_frame { nullptr };
    alignas(arch::FPU_STATE_ALIGNMENT) Array<u8, arch::FPU_STATE_SIZE> m_floating_point_state {
        {}
    };
};

}  // namespace barry::sched

extern "C" void barry_switch_context(barry::u64* outgoing_stack_pointer,
                                     barry::u64 incoming_stack_pointer);
extern "C" void barry_thread_trampoline();
