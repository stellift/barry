#pragma once

#include "kernel/base/traits.hpp"

namespace barry {

enum class Order : int {
    Relaxed = __ATOMIC_RELAXED,
    Consume = __ATOMIC_CONSUME,
    Acquire = __ATOMIC_ACQUIRE,
    Release = __ATOMIC_RELEASE,
    AcqRel = __ATOMIC_ACQ_REL,
    SeqCst = __ATOMIC_SEQ_CST,
};

inline void atomic_thread_fence(Order order = Order::SeqCst) {
    __atomic_thread_fence(static_cast<int>(order));
}

inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__)
    __asm__ volatile("yield" ::: "memory");
#else
    __atomic_signal_fence(__ATOMIC_SEQ_CST);
#endif
}

template<typename T>
class Atomic {
    static_assert(is_integral_v<T> || is_enum_v<T> || is_pointer_v<T>,
                  "Atomic<T> supports integers, scoped enums and pointers");

public:
    Atomic() = default;
    constexpr explicit Atomic(T value) : m_value(value) {}

    Atomic(Atomic const&) = delete;
    Atomic& operator=(Atomic const&) = delete;

    T load(Order order = Order::SeqCst) const {
        return __atomic_load_n(&m_value, static_cast<int>(order));
    }

    void store(T value, Order order = Order::SeqCst) {
        __atomic_store_n(&m_value, value, static_cast<int>(order));
    }

    T exchange(T value, Order order = Order::SeqCst) {
        return __atomic_exchange_n(&m_value, value, static_cast<int>(order));
    }

    bool compare_exchange_strong(T& expected, T desired, Order success = Order::SeqCst,
                                 Order failure = Order::SeqCst) {
        return __atomic_compare_exchange_n(&m_value, &expected, desired, false,
                                           static_cast<int>(success), static_cast<int>(failure));
    }

    bool compare_exchange_weak(T& expected, T desired, Order success = Order::SeqCst,
                               Order failure = Order::SeqCst) {
        return __atomic_compare_exchange_n(&m_value, &expected, desired, true,
                                           static_cast<int>(success), static_cast<int>(failure));
    }

    T fetch_add(T delta, Order order = Order::SeqCst)
        requires(is_integral_v<T>)
    {
        return __atomic_fetch_add(&m_value, delta, static_cast<int>(order));
    }

    T fetch_sub(T delta, Order order = Order::SeqCst)
        requires(is_integral_v<T>)
    {
        return __atomic_fetch_sub(&m_value, delta, static_cast<int>(order));
    }

    T fetch_or(T bits, Order order = Order::SeqCst)
        requires(is_integral_v<T>)
    {
        return __atomic_fetch_or(&m_value, bits, static_cast<int>(order));
    }

    T fetch_and(T bits, Order order = Order::SeqCst)
        requires(is_integral_v<T>)
    {
        return __atomic_fetch_and(&m_value, bits, static_cast<int>(order));
    }

private:
    T m_value {};
};

}
