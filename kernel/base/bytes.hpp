#pragma once

#include "kernel/base/span.hpp"
#include "kernel/base/traits.hpp"
#include "kernel/base/types.hpp"

namespace barry {

void copy_bytes(Bytes destination, ReadonlyBytes source);
void move_bytes(Bytes destination, ReadonlyBytes source);
void fill_bytes(Bytes destination, u8 value);
void zero_bytes(Bytes destination);
int compare_bytes(ReadonlyBytes left, ReadonlyBytes right);
bool bytes_equal(ReadonlyBytes left, ReadonlyBytes right);

constexpr usize length_of_cstring(char const* text) {
    if (text == nullptr)
        return 0;
    usize length = 0;
    while (text[length] != '\0')
        ++length;
    return length;
}

void wipe_bytes(Bytes destination);

template<TriviallyCopyable T>
void copy_objects(Span<T> destination, Span<T const> source) {
    copy_bytes(as_writable_bytes(destination), as_bytes(source));
}

template<TriviallyCopyable T>
void zero_object(T& object) {
    zero_bytes(Bytes(reinterpret_cast<u8*>(&object), sizeof(T)));
}

template<TriviallyCopyable T>
void wipe_object(T& object) {
    wipe_bytes(Bytes(reinterpret_cast<u8*>(&object), sizeof(T)));
}

}  // namespace barry
