#pragma once

#include "kernel/base/allocator.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/base/atomic.hpp"
#include "kernel/base/new.hpp"
#include "kernel/base/traits.hpp"
#include "kernel/base/utility.hpp"

namespace barry {

template<typename T>
class RefCounted {
public:
    void acquire_reference() const { m_reference_count.fetch_add(1, Order::Relaxed); }

    void release_reference() const {
        u32 const previous = m_reference_count.fetch_sub(1, Order::AcqRel);
        VERIFY(previous > 0);
        if (previous != 1)
            return;

        auto const* self = static_cast<T const*>(this);
        destroy_at(const_cast<T*>(self));
        deallocate_raw(const_cast<void*>(static_cast<void const*>(self)));
    }

    u32 reference_count() const { return m_reference_count.load(Order::Relaxed); }

protected:
    RefCounted() = default;
    ~RefCounted() = default;

private:
    mutable Atomic<u32> m_reference_count { 1 };
};

struct AdoptTag {
    explicit constexpr AdoptTag() = default;
};

inline constexpr AdoptTag adopt {};

template<typename T>
class [[nodiscard]] RefPtr {
public:
    RefPtr() = default;
    RefPtr(nullptr_t) {}

    RefPtr(AdoptTag, T& object) : m_pointer(&object) {}

    RefPtr(T& object) : m_pointer(&object) { m_pointer->acquire_reference(); }

    RefPtr(RefPtr const& other) : m_pointer(other.m_pointer) {
        if (m_pointer != nullptr)
            m_pointer->acquire_reference();
    }

    RefPtr(RefPtr&& other) : m_pointer(other.leak()) {}

    template<typename U>
        requires(DerivedFrom<U, T>)
    RefPtr(RefPtr<U> const& other) : m_pointer(other.get()) {
        if (m_pointer != nullptr)
            m_pointer->acquire_reference();
    }

    ~RefPtr() { clear(); }

    RefPtr& operator=(RefPtr const& other) {
        if (this != &other) {
            T* const replacement = other.m_pointer;
            if (replacement != nullptr)
                replacement->acquire_reference();
            clear();
            m_pointer = replacement;
        }
        return *this;
    }

    RefPtr& operator=(RefPtr&& other) {
        if (this != &other) {
            clear();
            m_pointer = other.leak();
        }
        return *this;
    }

    RefPtr& operator=(nullptr_t) {
        clear();
        return *this;
    }

    T* get() const { return m_pointer; }
    explicit operator bool() const { return m_pointer != nullptr; }
    bool is_null() const { return m_pointer == nullptr; }

    T& operator*() const {
        VERIFY(m_pointer != nullptr);
        return *m_pointer;
    }

    T* operator->() const {
        VERIFY(m_pointer != nullptr);
        return m_pointer;
    }

    [[nodiscard]] T* leak() { return exchange(m_pointer, nullptr); }

    void clear() {
        T* const previous = exchange(m_pointer, nullptr);
        if (previous != nullptr)
            previous->release_reference();
    }

    bool operator==(RefPtr const& other) const { return m_pointer == other.m_pointer; }

private:
    T* m_pointer { nullptr };
};

template<typename T, typename... Args>
KResultOr<RefPtr<T>> make_ref(Args&&... args) {
    void* storage = TRY(allocate_raw(sizeof(T), alignof(T)));
    T* object = construct_at(static_cast<T*>(storage), forward<Args>(args)...);
    return RefPtr<T>(adopt, *object);
}

}  // namespace barry
