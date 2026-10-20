#pragma once

#include "kernel/base/assert.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/types.hpp"
#include "kernel/base/utility.hpp"

namespace barry {

template<typename T>
class Span {
public:
    using ValueType = T;

    constexpr Span() = default;
    constexpr Span(Span const&) = default;
    constexpr Span& operator=(Span const&) = default;

    constexpr Span(T* data, usize count) : m_data(data), m_size(count) {}

    template<usize N>
    constexpr Span(T (&array)[N]) : m_data(array), m_size(N) {}

    template<typename U>
        requires(is_same_v<T, U const>)
    constexpr Span(Span<U> const& other) : m_data(other.data()), m_size(other.size()) {}

    constexpr T* data() const { return m_data; }
    constexpr usize size() const { return m_size; }
    constexpr usize size_in_bytes() const { return m_size * sizeof(T); }
    constexpr bool is_empty() const { return m_size == 0; }

    constexpr T* begin() const { return m_data; }
    constexpr T* end() const { return m_data + m_size; }

    constexpr T& operator[](usize index) const {
        VERIFY(index < m_size);
        return m_data[index];
    }

    constexpr Optional<T> at(usize index) const {
        if (index >= m_size)
            return nullopt;
        return m_data[index];
    }

    constexpr T& first() const {
        VERIFY(m_size > 0);
        return m_data[0];
    }

    constexpr T& last() const {
        VERIFY(m_size > 0);
        return m_data[m_size - 1];
    }

    constexpr Span slice(usize offset) const {
        VERIFY(offset <= m_size);
        return Span(m_data + offset, m_size - offset);
    }

    constexpr Span slice(usize offset, usize count) const {
        VERIFY(offset <= m_size);
        VERIFY(count <= m_size - offset);
        return Span(m_data + offset, count);
    }

    constexpr Optional<Span> try_slice(usize offset, usize count) const {
        if (offset > m_size || count > m_size - offset)
            return nullopt;
        return Span(m_data + offset, count);
    }

    constexpr Span first(usize count) const { return slice(0, count); }
    constexpr Span last(usize count) const {
        VERIFY(count <= m_size);
        return slice(m_size - count, count);
    }

private:
    T* m_data { nullptr };
    usize m_size { 0 };
};

using Bytes = Span<u8>;
using ReadonlyBytes = Span<u8 const>;

template<typename T>
Bytes as_writable_bytes(Span<T> span) {
    return Bytes(reinterpret_cast<u8*>(span.data()), span.size_in_bytes());
}

template<typename T>
ReadonlyBytes as_bytes(Span<T> span) {
    return ReadonlyBytes(reinterpret_cast<u8 const*>(span.data()), span.size_in_bytes());
}

}  // namespace barry
