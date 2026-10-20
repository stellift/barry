#pragma once

#include "kernel/arch/x86_64/processor_local.hpp"
#include "kernel/base/array.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/intrusive_list.hpp"
#include "kernel/base/own_ptr.hpp"
#include "kernel/sched/thread.hpp"
#include "kernel/sync/spin_lock.hpp"

namespace barry::sched {

struct SchedulerStatistics {
    u64 context_switches { 0 };
    u64 timer_ticks { 0 };
    u64 live_threads { 0 };
    u64 ready_threads { 0 };
    u64 finished_threads { 0 };
    u32 preempting_processors { 0 };
};

class Scheduler {
public:
    static constexpr usize MAX_THREADS = 256;
    static constexpr u32 TIMER_FREQUENCY_HZ = 250;

    KResult initialize();
    KResult start_preemption_on_current_processor();

    [[noreturn]] void run_application_processor();

    KResultOr<ThreadId> spawn(StringView name, ThreadEntry entry, void* argument);
    KResultOr<ThreadId> spawn_user_thread(StringView name, ThreadEntry entry, void* argument,
                                          PhysAddr address_space_root);

    void yield();
    void on_timer_tick();

    [[noreturn]] void exit_current_thread();
    [[noreturn]] void enter_current_thread();

    Thread* current_thread();

    SchedulerStatistics statistics() const;

private:
    struct ProcessorState {
        Thread* current { nullptr };
        Thread* idle { nullptr };
        Thread* pending_requeue { nullptr };
        u64 context_switches { 0 };
        bool preemption_enabled { false };
    };

    struct State {
        IntrusiveList<Thread, ReadyQueueTag> ready;
        IntrusiveList<Thread, AllThreadsTag> all;
        u64 timer_ticks { 0 };
        u64 finished_threads { 0 };
    };

    ProcessorState& processor_state();
    ProcessorState const& processor_state() const;

    KResultOr<ThreadId> install_thread(OwnPtr<Thread> thread);
    void reschedule();
    void finish_handoff(ProcessorState& processor);
    Thread* take_next_ready();
    void requeue(Thread& thread);
    void reap_finished();
    Thread* find_reapable_thread(State& state) const;

    KResultOr<Thread*> adopt_current_context(StringView name, bool as_idle);
    KResultOr<Thread*> create_idle_thread();

    mutable sync::Protected<State> m_state;
    Array<ProcessorState, arch::MAX_PROCESSORS> m_processors { {} };
};

Scheduler& scheduler();

KResult initialize_scheduler();

}  // namespace barry::sched
