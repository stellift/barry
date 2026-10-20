#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/arch/x86_64/processor_local.hpp"
#include "kernel/arch/x86_64/smp.hpp"
#include "kernel/base/atomic.hpp"
#include "kernel/sched/scheduler.hpp"
#include "kernel/test/framework.hpp"

namespace barry {
namespace {

constexpr u32 PARALLEL_WORKERS = 8;
constexpr u32 ITERATIONS_PER_WORKER = 20000;
constexpr u64 TICK_BUDGET = 2000;

Atomic<u64> g_parallel_counter { 0 };
Atomic<u64> g_observed_processor_mask { 0 };
Atomic<u32> g_parallel_finished { 0 };

void record_current_processor() {
    u32 const index = arch::current_processor_index();
    g_observed_processor_mask.fetch_or(u64 { 1 } << (index & 63), Order::Relaxed);
}

void parallel_worker(void*) {
    for (u32 iteration = 0; iteration < ITERATIONS_PER_WORKER; ++iteration) {
        g_parallel_counter.fetch_add(1, Order::Relaxed);
        record_current_processor();
    }
    g_parallel_finished.fetch_add(1, Order::Release);
}

bool wait_for_workers(u32 expected) {
    u64 const deadline = sched::scheduler().statistics().timer_ticks + TICK_BUDGET;

    while (g_parallel_finished.load(Order::Acquire) < expected) {
        if (sched::scheduler().statistics().timer_ticks > deadline)
            return false;
        sched::scheduler().yield();
    }
    return true;
}

u32 distinct_processors(u64 mask) {
    return static_cast<u32>(__builtin_popcountll(mask));
}

}  // namespace

KERNEL_TEST(every_reported_processor_comes_online) {
    auto const statistics = arch::smp_statistics();

    EXPECT_EQ(statistics.failed_processors, 0u);
    EXPECT_EQ(statistics.started_processors, statistics.reported_processors);
    EXPECT(statistics.every_processor_started());
    EXPECT_EQ(arch::online_processor_count(), statistics.started_processors);
}

KERNEL_TEST(every_online_processor_preempts) {
    auto const schedule = sched::scheduler().statistics();
    EXPECT_EQ(schedule.preempting_processors, arch::online_processor_count());
}

KERNEL_TEST(parallel_threads_lose_no_updates) {
    g_parallel_counter.store(0, Order::Relaxed);
    g_observed_processor_mask.store(0, Order::Relaxed);
    g_parallel_finished.store(0, Order::Relaxed);

    for (u32 index = 0; index < PARALLEL_WORKERS; ++index) {
        auto id = sched::scheduler().spawn("parallel-worker"_sv, &parallel_worker, nullptr);
        EXPECT(id.has_value());
    }

    EXPECT(wait_for_workers(PARALLEL_WORKERS));
    EXPECT_EQ(g_parallel_counter.load(Order::Acquire),
              u64 { PARALLEL_WORKERS } * ITERATIONS_PER_WORKER);
}

KERNEL_TEST(work_is_spread_across_the_online_processors) {
    u32 const online = arch::online_processor_count();
    u32 const observed = distinct_processors(g_observed_processor_mask.load(Order::Acquire));

    EXPECT(observed >= 1u);
    EXPECT(observed <= online);

    if (online > 1)
        EXPECT(observed > 1u);
}

KERNEL_TEST(each_processor_sees_its_own_local_data) {
    u32 const index = arch::current_processor_index();
    auto const& processor = arch::current_processor();

    EXPECT_EQ(processor.index, index);
    EXPECT_EQ(processor.self, reinterpret_cast<u64>(&processor));
    EXPECT(index < arch::MAX_PROCESSORS);
}

}  // namespace barry
