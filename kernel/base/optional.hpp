#pragma once

#include "kernel/base/assert.hpp"
#include "kernel/base/new.hpp"
#include "kernel/base/traits.hpp"
#include "kernel/base/utility.hpp"

namespace barry {

struct NullOpt {
    explicit constexpr NullOpt() = default;
};

inline constexpr NullOpt nullopt {};

template<typename T>
class [[nodiscard]] Optional {
public:
    using ValueType = T;

    Optional() = default;
    Optional(NullOpt) {}

    Optional(T value) : m_has_value(true) { m_storage.construct(move(value)); }

    Optional(Optional const& other)
        requires(is_copy_constructible_v<T>)
        : m_has_value(other.m_has_value) {
        if (m_has_value)
            m_storage.construct(other.m_storage.ref());
    }

    Optional(Optional&& other) : m_has_value(other.m_has_value) {
        if (m_has_value) {
            m_storage.construct(move(other.m_storage.ref()));
            other.clear();
        }
    }

    Optional& operator=(Optional const& other)
        requires(is_copy_constructible_v<T>)
    {
        if (this != &other) {
            clear();
            m_has_value = other.m_has_value;
            if (m_has_value)
                m_storage.construct(other.m_storage.ref());
        }
        return *this;
    }

    Optional& operator=(Optional&& other) {
        if (this != &other) {
            clear();
            m_has_value = other.m_has_value;
            if (m_has_value) {
                m_storage.construct(move(other.m_storage.ref()));
                other.clear();
            }
        }
        return *this;
    }

    Optional& operator=(NullOpt) {
        clear();
        return *this;
    }

    ~Optional()
        requires(is_trivially_destructible_v<T>)
    = default;

    ~Optional()
        requires(!is_trivially_destructible_v<T>)
    {
        clear();
    }

    bool has_value() const { return m_has_value; }
    explicit operator bool() const { return m_has_value; }

    void clear() {
        if (m_has_value) {
            m_storage.destroy();
            m_has_value = false;
        }
    }

    template<typename... Args>
    T& emplace(Args&&... args) {
        clear();
        m_storage.construct(forward<Args>(args)...);
        m_has_value = true;
        return m_storage.ref();
    }

    T& value() & {
        VERIFY(m_has_value);
        return m_storage.ref();
    }

    T const& value() const& {
        VERIFY(m_has_value);
        return m_storage.ref();
    }

    T& operator*() & { return value(); }
    T const& operator*() const& { return value(); }
    T* operator->() { return &value(); }
    T const* operator->() const { return &value(); }

    T release_value() {
        VERIFY(m_has_value);
        T taken = move(m_storage.ref());
        clear();
        return taken;
    }

    template<typename U>
    T value_or(U&& fallback) const& {
        return m_has_value ? m_storage.ref() : static_cast<T>(forward<U>(fallback));
    }

    bool operator==(Optional const& other) const {
        if (m_has_value != other.m_has_value)
            return false;
        if (!m_has_value)
            return true;
        return m_storage.ref() == other.m_storage.ref();
    }

    bool operator==(NullOpt) const { return !m_has_value; }

private:
    union {
        RawStorage<T> m_storage;
    };
    bool m_has_value { false };
};

}  // namespace barry
