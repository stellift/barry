#pragma once

#include "kernel/base/assert.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/types.hpp"

namespace barry {

template<typename T, usize N>
struct Array {
    static_assert(N > 0, "Array<T, 0> is not useful; use Span");

    using ValueType = T;

    constexpr usize size() const { return N; }

    constexpr T* data() { return m_elements; }
    constexpr T const* data() const { return m_elements; }

    constexpr T* begin() { return m_elements; }
    constexpr T* end() { return m_elements + N; }
    constexpr T const* begin() const { return m_elements; }
    constexpr T const* end() const { return m_elements + N; }

    constexpr T& operator[](usize index) {
        VERIFY(index < N);
        return m_elements[index];
    }

    constexpr T const& operator[](usize index) const {
        VERIFY(index < N);
        return m_elements[index];
    }

    constexpr Span<T> span() { return Span<T>(m_elements, N); }
    constexpr Span<T const> span() const { return Span<T const>(m_elements, N); }

    constexpr void fill(T const& value) {
        for (usize i = 0; i < N; ++i)
            m_elements[i] = value;
    }

    T m_elements[N];
};

}  // namespace barry
