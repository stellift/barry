#pragma once

#include "kernel/base/types.hpp"

namespace barry {

template<typename T, T V>
struct IntegralConstant {
    static constexpr T value = V;
    using value_type = T;
    constexpr operator T() const { return V; }
    constexpr T operator()() const { return V; }
};

using TrueType = IntegralConstant<bool, true>;
using FalseType = IntegralConstant<bool, false>;

template<bool B>
using BoolConstant = IntegralConstant<bool, B>;

template<typename...>
using VoidT = void;

template<bool B, typename T = void>
struct EnableIf {};
template<typename T>
struct EnableIf<true, T> {
    using Type = T;
};
template<bool B, typename T = void>
using EnableIfT = typename EnableIf<B, T>::Type;

template<bool B, typename T, typename F>
struct Conditional {
    using Type = T;
};
template<typename T, typename F>
struct Conditional<false, T, F> {
    using Type = F;
};
template<bool B, typename T, typename F>
using ConditionalT = typename Conditional<B, T, F>::Type;

template<typename T, typename U>
inline constexpr bool is_same_v = __is_same(T, U);

template<typename T>
using RemoveReferenceT = __remove_reference_t(T);
template<typename T>
using RemoveCvT = __remove_cv(T);
template<typename T>
using RemoveCvRefT = __remove_cvref(T);
template<typename T>
using RemovePointerT = __remove_pointer(T);
template<typename T>
using AddLvalueReferenceT = __add_lvalue_reference(T);
template<typename T>
using AddRvalueReferenceT = __add_rvalue_reference(T);
template<typename T>
using DecayT = __decay(T);
template<typename T>
using MakeUnsignedT = __make_unsigned(T);
template<typename T>
using MakeSignedT = __make_signed(T);
template<typename T>
using UnderlyingTypeT = __underlying_type(T);

template<typename T>
inline constexpr bool is_void_v = __is_void(T);
template<typename T>
inline constexpr bool is_const_v = __is_const(T);
template<typename T>
inline constexpr bool is_integral_v = __is_integral(T);
template<typename T>
inline constexpr bool is_floating_point_v = __is_floating_point(T);
template<typename T>
inline constexpr bool is_signed_v = __is_signed(T);
template<typename T>
inline constexpr bool is_unsigned_v = __is_unsigned(T);
template<typename T>
inline constexpr bool is_enum_v = __is_enum(T);
template<typename T>
inline constexpr bool is_class_v = __is_class(T);
template<typename T>
inline constexpr bool is_union_v = __is_union(T);
template<typename T>
inline constexpr bool is_pointer_v = __is_pointer(T);
template<typename T>
inline constexpr bool is_reference_v = __is_reference(T);
template<typename T>
inline constexpr bool is_lvalue_reference_v = __is_lvalue_reference(T);
template<typename T>
inline constexpr bool is_rvalue_reference_v = __is_rvalue_reference(T);
template<typename T>
inline constexpr bool is_array_v = __is_array(T);
template<typename T>
inline constexpr bool is_function_v = __is_function(T);
template<typename T>
inline constexpr bool is_empty_v = __is_empty(T);
template<typename T>
inline constexpr bool is_trivially_copyable_v = __is_trivially_copyable(T);
template<typename T>
inline constexpr bool is_trivially_destructible_v = __is_trivially_destructible(T);
template<typename T>
inline constexpr bool is_standard_layout_v = __is_standard_layout(T);

template<typename Base, typename Derived>
inline constexpr bool is_base_of_v = __is_base_of(Base, Derived);
template<typename From, typename To>
inline constexpr bool is_convertible_v = __is_convertible(From, To);

template<typename T, typename... Args>
inline constexpr bool is_constructible_v = __is_constructible(T, Args...);
template<typename T, typename... Args>
inline constexpr bool is_nothrow_constructible_v = __is_nothrow_constructible(T, Args...);
template<typename T, typename U>
inline constexpr bool is_assignable_v = __is_assignable(T, U);
template<typename T, typename U>
inline constexpr bool is_nothrow_assignable_v = __is_nothrow_assignable(T, U);

template<typename T>
inline constexpr bool is_default_constructible_v = __is_constructible(T);
template<typename T>
inline constexpr bool is_copy_constructible_v = __is_constructible(T, AddLvalueReferenceT<T const>);
template<typename T>
inline constexpr bool is_move_constructible_v = __is_constructible(T, AddRvalueReferenceT<T>);
template<typename T>
inline constexpr bool is_nothrow_move_constructible_v
    = __is_nothrow_constructible(T, AddRvalueReferenceT<T>);

template<typename T>
inline constexpr bool is_scalar_v
    = is_integral_v<T> || is_floating_point_v<T> || is_enum_v<T> || is_pointer_v<T>;

template<typename T>
AddRvalueReferenceT<T> declval() noexcept;

template<typename T>
concept Integral = is_integral_v<T>;
template<typename T>
concept UnsignedIntegral = is_integral_v<T> && is_unsigned_v<T>;
template<typename T>
concept SignedIntegral = is_integral_v<T> && is_signed_v<T>;
template<typename T>
concept Enum = is_enum_v<T>;
template<typename T>
concept TriviallyCopyable = is_trivially_copyable_v<T>;
template<typename T, typename U>
concept SameAs = is_same_v<T, U> && is_same_v<U, T>;
template<typename Derived, typename Base>
concept DerivedFrom = is_base_of_v<Base, Derived>;
template<typename From, typename To>
concept ConvertibleTo = is_convertible_v<From, To>;

template<typename F, typename... Args>
concept Invocable = requires(F&& f, Args&&... args) {
    static_cast<F&&>(f)(static_cast<Args&&>(args)...);
};

}
