#pragma once

#include "kernel/base/allocator.hpp"
#include "kernel/base/assert.hpp"
#include "kernel/base/new.hpp"
#include "kernel/base/traits.hpp"
#include "kernel/base/utility.hpp"

namespace barry {

template<typename T>
class [[nodiscard]] OwnPtr {
public:
    OwnPtr() = default;
    OwnPtr(nullptr_t) {}

    explicit OwnPtr(T* pointer) : m_pointer(pointer) {}

    OwnPtr(OwnPtr&& other) : m_pointer(other.leak()) {}

    template<typename U>
        requires(DerivedFrom<U, T>)
    OwnPtr(OwnPtr<U>&& other) : m_pointer(other.leak()) {}

    OwnPtr(OwnPtr const&) = delete;
    OwnPtr& operator=(OwnPtr const&) = delete;

    OwnPtr& operator=(OwnPtr&& other) {
        if (this != &other)
            reset(other.leak());
        return *this;
    }

    OwnPtr& operator=(nullptr_t) {
        reset(nullptr);
        return *this;
    }

    ~OwnPtr() { reset(nullptr); }

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

    [[nodiscard]] T* leak() {
        return exchange(m_pointer, nullptr);
    }

    void reset(T* replacement) {
        T* const previous = exchange(m_pointer, replacement);
        if (previous == nullptr)
            return;
        destroy_at(previous);
        deallocate_raw(previous);
    }

private:
    T* m_pointer { nullptr };
};

template<typename T, typename... Args>
KResultOr<OwnPtr<T>> make_own(Args&&... args) {
    void* storage = TRY(allocate_raw(sizeof(T), alignof(T)));
    return OwnPtr<T>(construct_at(static_cast<T*>(storage), forward<Args>(args)...));
}

}  // namespace barry
