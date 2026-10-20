#pragma once

#include "kernel/base/error.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/traits.hpp"

namespace barry {

template<Integral T>
constexpr Optional<T> checked_add(T a, T b) {
    T result {};
    if (__builtin_add_overflow(a, b, &result))
        return nullopt;
    return result;
}

template<Integral T>
constexpr Optional<T> checked_sub(T a, T b) {
    T result {};
    if (__builtin_sub_overflow(a, b, &result))
        return nullopt;
    return result;
}

template<Integral T>
constexpr Optional<T> checked_mul(T a, T b) {
    T result {};
    if (__builtin_mul_overflow(a, b, &result))
        return nullopt;
    return result;
}

template<Integral T>
constexpr KResultOr<T> try_add(T a, T b) {
    if (auto sum = checked_add(a, b); sum.has_value())
        return sum.release_value();
    return Error(Errno::EOVERFLOW, "integer addition overflowed");
}

template<Integral T>
constexpr KResultOr<T> try_mul(T a, T b) {
    if (auto product = checked_mul(a, b); product.has_value())
        return product.release_value();
    return Error(Errno::EOVERFLOW, "integer multiplication overflowed");
}

template<UnsignedIntegral T>
constexpr Optional<T> checked_align_up(T value, T alignment) {
    auto bumped = checked_add<T>(value, alignment - 1);
    if (!bumped.has_value())
        return nullopt;
    return static_cast<T>(bumped.value() & ~(alignment - 1));
}

template<UnsignedIntegral To, UnsignedIntegral From>
constexpr Optional<To> narrow(From value) {
    auto narrowed = static_cast<To>(value);
    if (static_cast<From>(narrowed) != value)
        return nullopt;
    return narrowed;
}

}  // namespace barry
