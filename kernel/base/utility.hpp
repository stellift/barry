#pragma once

#include "kernel/base/traits.hpp"

namespace barry {

template<typename T>
constexpr RemoveReferenceT<T>&& move(T&& value) noexcept {
    return static_cast<RemoveReferenceT<T>&&>(value);
}

template<typename T>
constexpr T&& forward(RemoveReferenceT<T>& value) noexcept {
    return static_cast<T&&>(value);
}

template<typename T>
constexpr T&& forward(RemoveReferenceT<T>&& value) noexcept {
    static_assert(!is_lvalue_reference_v<T>, "forward() must not turn an rvalue into an lvalue");
    return static_cast<T&&>(value);
}

template<typename T>
constexpr void swap(T& a, T& b) noexcept {
    T tmp = move(a);
    a = move(b);
    b = move(tmp);
}

template<typename T, typename U = T>
constexpr T exchange(T& slot, U&& new_value) noexcept {
    T old = move(slot);
    slot = forward<U>(new_value);
    return old;
}

template<typename T>
constexpr T const& min(T const& a, T const& b) {
    return b < a ? b : a;
}

template<typename T>
constexpr T const& max(T const& a, T const& b) {
    return a < b ? b : a;
}

template<typename T>
constexpr T const& clamp(T const& value, T const& lo, T const& hi) {
    return value < lo ? lo : (hi < value ? hi : value);
}

template<UnsignedIntegral T>
constexpr T align_down(T value, T alignment) {
    return value & ~(alignment - 1);
}

template<UnsignedIntegral T>
constexpr T align_up(T value, T alignment) {
    return align_down<T>(value + alignment - 1, alignment);
}

template<UnsignedIntegral T>
constexpr bool is_aligned(T value, T alignment) {
    return (value & (alignment - 1)) == 0;
}

template<UnsignedIntegral T>
constexpr bool is_power_of_two(T value) {
    return value != 0 && (value & (value - 1)) == 0;
}

template<UnsignedIntegral T>
constexpr T div_ceil(T a, T b) {
    return a / b + (a % b != 0 ? 1 : 0);
}

template<UnsignedIntegral T>
constexpr T round_up_to_power_of_two(T value) {
    if (value <= 1)
        return 1;
    return static_cast<T>(T { 1 } << (sizeof(T) * 8 - static_cast<unsigned>(__builtin_clzll(value - 1))));
}

template<Enum E>
constexpr UnderlyingTypeT<E> to_underlying(E value) {
    return static_cast<UnderlyingTypeT<E>>(value);
}

template<typename T, usize N>
constexpr usize array_size(T const (&)[N]) {
    return N;
}

}
