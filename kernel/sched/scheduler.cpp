#include "kernel/sched/scheduler.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/arch/x86_64/interrupts.hpp"
#include "kernel/arch/x86_64/local_apic.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/arch/x86_64/processor_local.hpp"
#include "kernel/arch/x86_64/user_mode.hpp"
#include "kernel/log.hpp"
#include "kernel/panic.hpp"

namespace barry::sched {
namespace {

Scheduler g_scheduler;

void on_timer_interrupt(arch::TrapFrame&) {
    arch::LocalApic::end_of_interrupt();
    g_scheduler.on_timer_tick();
}

void on_reschedule_interrupt(arch::TrapFrame&) {
    arch::LocalApic::end_of_interrupt();
    g_scheduler.yield();
}

void on_spurious_interrupt(arch::TrapFrame&) {}

void adopt_thread_on_current_processor(Thread& incoming) {
    PhysAddr const root = incoming.address_space_root();
    if (!root.is_null() && arch::read_cr3() != root)
        arch::write_cr3(root);

    if (!incoming.stack_top().is_null())
        arch::set_current_task_stacks(incoming.stack_top());

    if (incoming.is_user_thread())
        arch::write_msr(arch::MSR_FS_BASE, incoming.thread_pointer().raw());
}

void idle_loop(void*) {
    for (;;) {
        g_scheduler.yield();
        arch::enable_interrupts();
        arch::halt();
    }
}

KResult install_interrupt_handlers() {
    arch::unregister_interrupt_handler(arch::SPURIOUS_INTERRUPT_VECTOR);

    if (!arch::register_interrupt_handler(arch::TIMER_INTERRUPT_VECTOR, &on_timer_interrupt))
        return Error(Errno::EBUSY, "the timer vector is already taken");
    if (!arch::register_interrupt_handler(arch::RESCHEDULE_INTERRUPT_VECTOR,
                                          &on_reschedule_interrupt))
        return Error(Errno::EBUSY, "the reschedule vector is already taken");
    if (!arch::register_interrupt_handler(arch::SPURIOUS_INTERRUPT_VECTOR,
                                          &on_spurious_interrupt))
        return Error(Errno::EBUSY, "the spurious vector is already taken");

    return {};
}

}  // namespace

Scheduler::ProcessorState& Scheduler::processor_state() {
    return m_processors[arch::current_processor_index()];
}

Scheduler::ProcessorState const& Scheduler::processor_state() const {
    return m_processors[arch::current_processor_index()];
}

KResultOr<Thread*> Scheduler::adopt_current_context(StringView name, bool as_idle) {
    auto adopted = TRY(Thread::adopt_running_context(name));
    Thread& thread = *adopted.leak();

    if (as_idle)
        thread.mark_as_idle();

    auto guard = m_state.lock();
    guard->all.append(thread);
    return &thread;
}

KResultOr<Thread*> Scheduler::create_idle_thread() {
    auto created = TRY(Thread::create("kernel-idle"_sv, &idle_loop, nullptr));
    Thread& thread = *created.leak();
    thread.mark_as_idle();

    auto guard = m_state.lock();
    guard->all.append(thread);
    return &thread;
}

KResult Scheduler::initialize() {
    ProcessorState& processor = processor_state();
    if (processor.current != nullptr)
        return Error(Errno::EBUSY, "the scheduler is already initialized");

    processor.current = TRY(adopt_current_context("kernel-bootstrap"_sv, false));
    processor.idle = TRY(create_idle_thread());

    return install_interrupt_handlers();
}

KResult Scheduler::start_preemption_on_current_processor() {
    ProcessorState& processor = processor_state();
    if (processor.current == nullptr)
        return Error(Errno::EINVAL, "this processor has no scheduler state yet");

    TRY(arch::LocalApic::start_periodic_timer(TIMER_FREQUENCY_HZ,
                                              arch::TIMER_INTERRUPT_VECTOR));
    processor.preemption_enabled = true;
    arch::enable_interrupts();
    return {};
}

void Scheduler::run_application_processor() {
    auto adopted = adopt_current_context("kernel-idle"_sv, true);
    if (adopted.is_error())
        PANIC("an application processor could not join the scheduler: {}", adopted.error());

    ProcessorState& processor = processor_state();
    processor.current = adopted.value();
    processor.idle = adopted.value();

    auto started = start_preemption_on_current_processor();
    if (started.is_error())
        PANIC("an application processor could not start its timer: {}", started.error());

    idle_loop(nullptr);
    VERIFY_NOT_REACHED();
}

KResultOr<ThreadId> Scheduler::install_thread(OwnPtr<Thread> thread) {
    Thread& spawned = *thread.leak();
    ThreadId const id = spawned.id();

    auto guard = m_state.lock();
    guard->all.append(spawned);
    spawned.set_state(ThreadState::Ready);
    guard->ready.append(spawned);
    return id;
}

KResultOr<ThreadId> Scheduler::spawn(StringView name, ThreadEntry entry, void* argument) {
    {
        auto guard = m_state.lock();
        if (guard->all.size() >= MAX_THREADS)
            return Error(Errno::EAGAIN, "the scheduler thread table is full");
    }

    return install_thread(TRY(Thread::create(name, entry, argument)));
}

KResultOr<ThreadId> Scheduler::spawn_user_thread(StringView name, ThreadEntry entry,
                                                 void* argument,
                                                 PhysAddr address_space_root) {
    {
        auto guard = m_state.lock();
        if (guard->all.size() >= MAX_THREADS)
            return Error(Errno::EAGAIN, "the scheduler thread table is full");
    }

    return install_thread(
        TRY(Thread::create_user_thread(name, entry, argument, address_space_root)));
}

Thread* Scheduler::take_next_ready() {
    auto guard = m_state.lock();
    return guard->ready.take_first();
}

void Scheduler::requeue(Thread& thread) {
    auto guard = m_state.lock();
    thread.set_state(ThreadState::Ready);
    guard->ready.append(thread);
}

Thread* Scheduler::find_reapable_thread(State& state) const {
    Thread const* const running = processor_state().current;

    for (auto& thread : state.all) {
        if (&thread == running || thread.is_idle())
            continue;
        if (thread.state() != ThreadState::Finished)
            continue;
        return &thread;
    }
    return nullptr;
}

void Scheduler::reap_finished() {
    for (;;) {
        Thread* victim = nullptr;

        {
            auto guard = m_state.lock();
            victim = find_reapable_thread(*guard);
            if (victim == nullptr)
                return;
            guard->all.remove(*victim);
            ++guard->finished_threads;
        }

        OwnPtr<Thread> reclaimed { victim };
        reclaimed = nullptr;
    }
}

void Scheduler::finish_handoff(ProcessorState& processor) {
    Thread* const previous = exchange(processor.pending_requeue, nullptr);
    if (previous == nullptr || previous->is_idle())
        return;
    if (previous->state() == ThreadState::Running)
        requeue(*previous);
}

void Scheduler::reschedule() {
    VERIFY(!arch::interrupts_enabled());

    ProcessorState& processor = processor_state();
    Thread* const outgoing = processor.current;
    VERIFY(outgoing != nullptr);

    Thread* incoming = take_next_ready();

    if (incoming == nullptr) {
        if (outgoing->state() == ThreadState::Running)
            return;
        incoming = processor.idle;
        VERIFY(incoming != nullptr);
    }

    if (incoming == outgoing) {
        incoming->set_state(ThreadState::Running);
        return;
    }

    incoming->set_state(ThreadState::Running);
    incoming->note_scheduled();
    processor.current = incoming;
    processor.pending_requeue = outgoing;
    ++processor.context_switches;

    arch::save_fpu_state(outgoing->floating_point_state());
    arch::restore_fpu_state(incoming->floating_point_state());
    adopt_thread_on_current_processor(*incoming);

    barry_switch_context(outgoing->stack_pointer_slot(), incoming->stack_pointer());

    finish_handoff(processor_state());
}

void Scheduler::yield() {
    arch::InterruptGuard const guard;
    if (processor_state().current == nullptr)
        return;
    reschedule();
}

void Scheduler::on_timer_tick() {
    {
        auto guard = m_state.lock();
        ++guard->timer_ticks;
    }

    ProcessorState& processor = processor_state();
    if (!processor.preemption_enabled || processor.current == nullptr)
        return;

    reschedule();
}

void Scheduler::enter_current_thread() {
    finish_handoff(processor_state());
    reap_finished();

    arch::enable_interrupts();

    Thread* const current = processor_state().current;
    VERIFY(current != nullptr);
    current->run_entry();

    exit_current_thread();
}

void Scheduler::exit_current_thread() {
    arch::disable_interrupts();

    Thread* const current = processor_state().current;
    VERIFY(current != nullptr);
    current->set_state(ThreadState::Finished);

    for (;;) {
        reschedule();
        arch::enable_interrupts();
        arch::halt();
        arch::disable_interrupts();
    }
}

Thread* Scheduler::current_thread() {
    return processor_state().current;
}

SchedulerStatistics Scheduler::statistics() const {
    SchedulerStatistics result {};

    {
        auto guard = m_state.lock();
        result.timer_ticks = guard->timer_ticks;
        result.finished_threads = guard->finished_threads;
        result.live_threads = guard->all.size();
        result.ready_threads = guard->ready.size();
    }

    for (usize index = 0; index < arch::MAX_PROCESSORS; ++index) {
        result.context_switches += m_processors[index].context_switches;
        if (m_processors[index].preemption_enabled)
            ++result.preempting_processors;
    }

    return result;
}

Scheduler& scheduler() {
    return g_scheduler;
}

KResult initialize_scheduler() {
    return g_scheduler.initialize();
}

}  // namespace barry::sched
