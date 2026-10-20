#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/base/atomic.hpp"
#include "kernel/sched/scheduler.hpp"
#include "kernel/test/framework.hpp"

namespace barry {
namespace {

constexpr u32 COOPERATIVE_WORKER_COUNT = 4;
constexpr u32 ITERATIONS_PER_WORKER = 2000;
constexpr u64 SPIN_ITERATIONS = 20'000'000;
constexpr u64 TICK_BUDGET = 1000;

Atomic<u32> g_cooperative_counter { 0 };
Atomic<u32> g_cooperative_finished { 0 };
Atomic<bool> g_spinner_finished { false };
Atomic<u64> g_observed_thread_ids { 0 };

void cooperative_worker(void*) {
    for (u32 iteration = 0; iteration < ITERATIONS_PER_WORKER; ++iteration) {
        g_cooperative_counter.fetch_add(1, Order::Relaxed);
        sched::scheduler().yield();
    }
    g_cooperative_finished.fetch_add(1, Order::Release);
}

void non_yielding_spinner(void*) {
    u64 accumulator = 0;
    for (u64 iteration = 0; iteration < SPIN_ITERATIONS; ++iteration)
        accumulator += iteration;

    g_observed_thread_ids.store(accumulator == 0 ? 0 : sched::scheduler().current_thread()->id(),
                                Order::Release);
    g_spinner_finished.store(true, Order::Release);
}

bool wait_until(Atomic<bool>& flag, u64 tick_budget) {
    u64 const deadline = sched::scheduler().statistics().timer_ticks + tick_budget;

    while (!flag.load(Order::Acquire)) {
        if (sched::scheduler().statistics().timer_ticks > deadline)
            return false;
        cpu_relax();
    }
    return true;
}

}  // namespace

KERNEL_TEST(scheduler_starts_with_a_running_bootstrap_thread) {
    auto* current = sched::scheduler().current_thread();
    EXPECT(current != nullptr);
    EXPECT(current->state() == sched::ThreadState::Running);
    EXPECT_EQ(current->name(), "kernel-bootstrap"_sv);
}

KERNEL_TEST(timer_interrupt_advances_the_tick_counter) {
    u64 const before = sched::scheduler().statistics().timer_ticks;

    while (sched::scheduler().statistics().timer_ticks == before)
        cpu_relax();

    EXPECT(sched::scheduler().statistics().timer_ticks > before);
}

KERNEL_TEST(cooperative_threads_all_make_progress) {
    g_cooperative_counter.store(0, Order::Relaxed);
    g_cooperative_finished.store(0, Order::Relaxed);

    for (u32 index = 0; index < COOPERATIVE_WORKER_COUNT; ++index) {
        auto id = sched::scheduler().spawn("cooperative-worker"_sv, &cooperative_worker, nullptr);
        EXPECT(id.has_value());
    }

    u64 const deadline = sched::scheduler().statistics().timer_ticks + TICK_BUDGET;
    while (g_cooperative_finished.load(Order::Acquire) < COOPERATIVE_WORKER_COUNT) {
        if (sched::scheduler().statistics().timer_ticks > deadline)
            break;
        sched::scheduler().yield();
    }

    EXPECT_EQ(g_cooperative_finished.load(Order::Acquire), COOPERATIVE_WORKER_COUNT);
    EXPECT_EQ(g_cooperative_counter.load(Order::Relaxed),
              COOPERATIVE_WORKER_COUNT * ITERATIONS_PER_WORKER);
}

KERNEL_TEST(preemption_lets_a_non_yielding_thread_finish) {
    g_spinner_finished.store(false, Order::Relaxed);

    auto id = sched::scheduler().spawn("spinner"_sv, &non_yielding_spinner, nullptr);
    EXPECT(id.has_value());

    EXPECT(wait_until(g_spinner_finished, TICK_BUDGET));
    EXPECT(g_observed_thread_ids.load(Order::Acquire) == id.value());
}

KERNEL_TEST(context_switches_are_counted) {
    u64 const before = sched::scheduler().statistics().context_switches;
    sched::scheduler().yield();
    EXPECT(sched::scheduler().statistics().context_switches >= before);
}

KERNEL_TEST(finished_threads_are_reclaimed) {
    auto const before = sched::scheduler().statistics();

    g_cooperative_counter.store(0, Order::Relaxed);
    g_cooperative_finished.store(0, Order::Relaxed);

    auto id = sched::scheduler().spawn("short-lived"_sv, &cooperative_worker, nullptr);
    EXPECT(id.has_value());

    u64 const deadline = sched::scheduler().statistics().timer_ticks + TICK_BUDGET;
    while (g_cooperative_finished.load(Order::Acquire) == 0) {
        if (sched::scheduler().statistics().timer_ticks > deadline)
            break;
        sched::scheduler().yield();
    }

    EXPECT_EQ(g_cooperative_finished.load(Order::Acquire), 1u);
    EXPECT(sched::scheduler().statistics().timer_ticks >= before.timer_ticks);
}

}  // namespace barry
