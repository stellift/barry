#pragma once

#include "kernel/base/assert.hpp"
#include "kernel/base/error.hpp"
#include "kernel/base/new.hpp"
#include "kernel/base/traits.hpp"
#include "kernel/base/utility.hpp"

namespace barry {

template<typename E>
class Unexpected {
public:
    constexpr explicit Unexpected(E error) : m_error(move(error)) {}
    constexpr E& error() & { return m_error; }
    constexpr E const& error() const& { return m_error; }
    constexpr E&& error() && { return move(m_error); }

private:
    E m_error;
};

template<typename T, typename E = Error>
class [[nodiscard]] Expected {
public:
    using ValueType = T;
    using ErrorType = E;

    Expected(T value) : m_has_value(true) { m_value.construct(move(value)); }

    Expected(Unexpected<E>&& unexpected) : m_has_value(false) {
        m_error.construct(move(unexpected).error());
    }

    Expected(E error) : m_has_value(false) { m_error.construct(move(error)); }

    Expected(Expected&& other) : m_has_value(other.m_has_value) {
        if (m_has_value)
            m_value.construct(move(other.m_value.ref()));
        else
            m_error.construct(move(other.m_error.ref()));
    }

    Expected(Expected const& other)
        requires(is_copy_constructible_v<T> && is_copy_constructible_v<E>)
        : m_has_value(other.m_has_value) {
        if (m_has_value)
            m_value.construct(other.m_value.ref());
        else
            m_error.construct(other.m_error.ref());
    }

    Expected& operator=(Expected&& other) {
        if (this == &other)
            return *this;
        destroy();
        m_has_value = other.m_has_value;
        if (m_has_value)
            m_value.construct(move(other.m_value.ref()));
        else
            m_error.construct(move(other.m_error.ref()));
        return *this;
    }

    ~Expected() { destroy(); }

    bool has_value() const { return m_has_value; }
    bool is_error() const { return !m_has_value; }
    explicit operator bool() const { return m_has_value; }

    T& value() & {
        VERIFY(m_has_value);
        return m_value.ref();
    }

    T const& value() const& {
        VERIFY(m_has_value);
        return m_value.ref();
    }

    E& error() & {
        VERIFY(!m_has_value);
        return m_error.ref();
    }

    E const& error() const& {
        VERIFY(!m_has_value);
        return m_error.ref();
    }

    T& operator*() & { return value(); }
    T const& operator*() const& { return value(); }
    T* operator->() { return &value(); }
    T const* operator->() const { return &value(); }

    T release_value() {
        VERIFY(m_has_value);
        return move(m_value.ref());
    }

    E release_error() {
        VERIFY(!m_has_value);
        return move(m_error.ref());
    }

    template<typename U>
    T value_or(U&& fallback) const& {
        return m_has_value ? m_value.ref() : static_cast<T>(forward<U>(fallback));
    }

private:
    void destroy() {
        if (m_has_value)
            m_value.destroy();
        else
            m_error.destroy();
    }

    union {
        RawStorage<T> m_value;
        RawStorage<E> m_error;
    };
    bool m_has_value;
};

template<typename E>
class [[nodiscard]] Expected<void, E> {
public:
    using ValueType = void;
    using ErrorType = E;

    Expected() = default;

    Expected(Unexpected<E>&& unexpected) : m_has_value(false) {
        m_error.construct(move(unexpected).error());
    }

    Expected(E error) : m_has_value(false) { m_error.construct(move(error)); }

    Expected(Expected&& other) : m_has_value(other.m_has_value) {
        if (!m_has_value)
            m_error.construct(move(other.m_error.ref()));
    }

    Expected(Expected const& other)
        requires(is_copy_constructible_v<E>)
        : m_has_value(other.m_has_value) {
        if (!m_has_value)
            m_error.construct(other.m_error.ref());
    }

    Expected& operator=(Expected&& other) {
        if (this == &other)
            return *this;
        if (!m_has_value)
            m_error.destroy();
        m_has_value = other.m_has_value;
        if (!m_has_value)
            m_error.construct(move(other.m_error.ref()));
        return *this;
    }

    ~Expected() {
        if (!m_has_value)
            m_error.destroy();
    }

    bool has_value() const { return m_has_value; }
    bool is_error() const { return !m_has_value; }
    explicit operator bool() const { return m_has_value; }

    E& error() & {
        VERIFY(!m_has_value);
        return m_error.ref();
    }

    E const& error() const& {
        VERIFY(!m_has_value);
        return m_error.ref();
    }

    void release_value() const { VERIFY(m_has_value); }

    E release_error() {
        VERIFY(!m_has_value);
        return move(m_error.ref());
    }

private:
    union {
        RawStorage<E> m_error;
    };
    bool m_has_value { true };
};

using KResult = Expected<void, Error>;
template<typename T>
using KResultOr = Expected<T, Error>;

}

#define TRY(expression)                                                    \
    ({                                                                     \
        auto&& _barry_try_result = (expression);                           \
        if (_barry_try_result.is_error()) [[unlikely]]                     \
            return ::barry::Unexpected(_barry_try_result.release_error()); \
        _barry_try_result.release_value();                                 \
    })

#define MUST(expression)                                                       \
    ({                                                                         \
        auto&& _barry_must_result = (expression);                              \
        if (_barry_must_result.is_error()) [[unlikely]]                        \
            ::barry::assertion_failed(#expression " must not fail", __FILE__,  \
                                      __LINE__, __PRETTY_FUNCTION__);          \
        _barry_must_result.release_value();                                    \
    })
