#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/mm/paging.hpp"

namespace barry::mm {

enum class UserAccess : u8 {
    Read,
    Write,
};

KResult validate_user_range(AddressSpace const& space, VirtRange range, UserAccess access);

KResult copy_from_user(AddressSpace const& space, Bytes destination, UserPtr<u8 const> source);

KResult copy_to_user(AddressSpace const& space, UserPtr<u8> destination, ReadonlyBytes source);

KResultOr<usize> copy_cstring_from_user(AddressSpace const& space, Span<char> destination,
                                        UserPtr<char const> source);

template<typename T>
KResultOr<T> read_user_object(AddressSpace const& space, UserPtr<T const> source) {
    static_assert(is_trivially_copyable_v<T>);

    T value {};
    Bytes const destination(reinterpret_cast<u8*>(&value), sizeof(T));
    TRY(copy_from_user(space, destination, source.template cast<u8 const>()));
    return value;
}

template<typename T>
KResult write_user_object(AddressSpace const& space, UserPtr<T> destination, T const& value) {
    static_assert(is_trivially_copyable_v<T>);

    ReadonlyBytes const source(reinterpret_cast<u8 const*>(&value), sizeof(T));
    return copy_to_user(space, destination.template cast<u8>(), source);
}

}  // namespace barry::mm
