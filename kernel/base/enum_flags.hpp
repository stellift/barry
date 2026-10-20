#pragma once

#include "kernel/base/traits.hpp"
#include "kernel/base/utility.hpp"

namespace barry {

template<typename E>
struct EnableEnumFlags : FalseType {};

template<typename E>
concept FlagsEnum = is_enum_v<E> && EnableEnumFlags<E>::value;

template<FlagsEnum E>
constexpr E operator|(E left, E right) {
    return static_cast<E>(to_underlying(left) | to_underlying(right));
}

template<FlagsEnum E>
constexpr E operator&(E left, E right) {
    return static_cast<E>(to_underlying(left) & to_underlying(right));
}

template<FlagsEnum E>
constexpr E operator^(E left, E right) {
    return static_cast<E>(to_underlying(left) ^ to_underlying(right));
}

template<FlagsEnum E>
constexpr E operator~(E value) {
    return static_cast<E>(~to_underlying(value));
}

template<FlagsEnum E>
constexpr E& operator|=(E& left, E right) {
    left = left | right;
    return left;
}

template<FlagsEnum E>
constexpr E& operator&=(E& left, E right) {
    left = left & right;
    return left;
}

template<FlagsEnum E>
constexpr bool has_all_flags(E value, E required) {
    return (value & required) == required;
}

template<FlagsEnum E>
constexpr bool has_any_flag(E value, E candidates) {
    return to_underlying(value & candidates) != 0;
}

template<FlagsEnum E>
constexpr bool is_empty_flags(E value) {
    return to_underlying(value) == 0;
}

}  // namespace barry
