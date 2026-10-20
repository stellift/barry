#pragma once

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/base/atomic.hpp"
#include "kernel/base/traits.hpp"
#include "kernel/base/types.hpp"
#include "kernel/base/utility.hpp"

namespace barry::sync {

class SpinLock {
public:
    constexpr SpinLock() = default;
    SpinLock(SpinLock const&) = delete;
    SpinLock& operator=(SpinLock const&) = delete;

    void acquire() {
        while (m_locked.exchange(true, Order::Acquire))
            while (m_locked.load(Order::Relaxed))
                cpu_relax();
    }

    bool try_acquire() { return !m_locked.exchange(true, Order::Acquire); }

    void release() { m_locked.store(false, Order::Release); }

    bool is_locked() const { return m_locked.load(Order::Relaxed); }

private:
    Atomic<bool> m_locked { false };
};

template<typename T>
class Protected {
public:
    class Guard {
    public:
        explicit Guard(Protected& owner)
            : m_owner(&owner), m_interrupts_were_enabled(arch::interrupts_enabled()) {
            arch::disable_interrupts();
            m_owner->m_lock.acquire();
        }

        ~Guard() {
            if (m_owner == nullptr)
                return;
            m_owner->m_lock.release();
            if (m_interrupts_were_enabled)
                arch::enable_interrupts();
        }

        Guard(Guard const&) = delete;
        Guard& operator=(Guard const&) = delete;

        Guard(Guard&& other)
            : m_owner(other.m_owner),
              m_interrupts_were_enabled(other.m_interrupts_were_enabled) {
            other.m_owner = nullptr;
        }

        T& operator*() const { return m_owner->m_value; }
        T* operator->() const { return &m_owner->m_value; }

    private:
        Protected* m_owner;
        bool m_interrupts_were_enabled;
    };

    constexpr Protected() = default;

    template<typename... Args>
    constexpr explicit Protected(Args&&... args) : m_value(forward<Args>(args)...) {}

    Protected(Protected const&) = delete;
    Protected& operator=(Protected const&) = delete;

    [[nodiscard]] Guard lock() { return Guard(*this); }

    template<typename Callback>
    decltype(auto) with(Callback&& callback) {
        Guard guard(*this);
        return callback(*guard);
    }

    bool is_locked() const { return m_lock.is_locked(); }

private:
    T m_value {};
    SpinLock m_lock;
};

}  // namespace barry::sync
