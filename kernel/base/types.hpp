#pragma once

namespace barry {

using u8 = unsigned char;
using u16 = unsigned short;
using u32 = unsigned int;
using u64 = unsigned long long;

using i8 = signed char;
using i16 = short;
using i32 = int;
using i64 = long long;

using usize = decltype(sizeof(0));
using isize = long long;
using uptr = unsigned long long;
using nullptr_t = decltype(nullptr);

static_assert(sizeof(u8) == 1 && sizeof(u16) == 2 && sizeof(u32) == 4 && sizeof(u64) == 8);
static_assert(sizeof(usize) == 8, "Barry targets 64-bit only");
static_assert(sizeof(void*) == sizeof(uptr));

constexpr u64 operator""_KiB(unsigned long long n) { return n * 1024ULL; }
constexpr u64 operator""_MiB(unsigned long long n) { return n * 1024ULL * 1024ULL; }
constexpr u64 operator""_GiB(unsigned long long n) { return n * 1024ULL * 1024ULL * 1024ULL; }

class NonCopyable {
public:
    NonCopyable() = default;
    NonCopyable(NonCopyable const&) = delete;
    NonCopyable& operator=(NonCopyable const&) = delete;

protected:
    ~NonCopyable() = default;
};

class NonMovable {
public:
    NonMovable() = default;
    NonMovable(NonMovable&&) = delete;
    NonMovable& operator=(NonMovable&&) = delete;

protected:
    ~NonMovable() = default;
};

class Pinned : public NonCopyable, public NonMovable {};

}

namespace std {
template<typename T>
class initializer_list {
public:
    using value_type = T;
    using size_type = barry::usize;

    constexpr initializer_list() = default;

    constexpr barry::usize size() const { return m_size; }
    constexpr T const* begin() const { return m_begin; }
    constexpr T const* end() const { return m_begin + m_size; }

private:

    constexpr initializer_list(T const* begin, barry::usize size) : m_begin(begin), m_size(size) {}

    T const* m_begin { nullptr };
    barry::usize m_size { 0 };
};
}

namespace barry {
template<typename T>
using InitializerList = std::initializer_list<T>;
}
