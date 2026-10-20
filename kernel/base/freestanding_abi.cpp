#include "kernel/base/bytes.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/types.hpp"

namespace {

using barry::Bytes;
using barry::ReadonlyBytes;
using barry::u8;
using barry::usize;

Bytes writable(void* pointer, usize count) {
    return Bytes(static_cast<u8*>(pointer), count);
}

ReadonlyBytes readable(void const* pointer, usize count) {
    return ReadonlyBytes(static_cast<u8 const*>(pointer), count);
}

}  // namespace

extern "C" void* memcpy(void* destination, void const* source, usize count) {
    barry::copy_bytes(writable(destination, count), readable(source, count));
    return destination;
}

extern "C" void* memmove(void* destination, void const* source, usize count) {
    barry::move_bytes(writable(destination, count), readable(source, count));
    return destination;
}

extern "C" void* memset(void* destination, int value, usize count) {
    barry::fill_bytes(writable(destination, count), static_cast<u8>(value));
    return destination;
}

extern "C" int memcmp(void const* left, void const* right, usize count) {
    return barry::compare_bytes(readable(left, count), readable(right, count));
}
