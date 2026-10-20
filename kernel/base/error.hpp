#pragma once

#include "kernel/base/types.hpp"

namespace barry {

enum class Errno : i32 {
    Success = 0,
    EPERM = 1,
    ENOENT = 2,
    ESRCH = 3,
    EINTR = 4,
    EIO = 5,
    ENXIO = 6,
    E2BIG = 7,
    ENOEXEC = 8,
    EBADF = 9,
    ECHILD = 10,
    EAGAIN = 11,
    ENOMEM = 12,
    EACCES = 13,
    EFAULT = 14,
    EBUSY = 16,
    EEXIST = 17,
    EXDEV = 18,
    ENODEV = 19,
    ENOTDIR = 20,
    EISDIR = 21,
    EINVAL = 22,
    ENFILE = 23,
    EMFILE = 24,
    ENOTTY = 25,
    EFBIG = 27,
    ENOSPC = 28,
    ESPIPE = 29,
    EROFS = 30,
    EMLINK = 31,
    EPIPE = 32,
    ERANGE = 34,
    ENAMETOOLONG = 36,
    ENOSYS = 38,
    ENOTEMPTY = 39,
    ELOOP = 40,
    EOVERFLOW = 75,
    ETIMEDOUT = 110,
    EADDRINUSE = 98,
    ECONNREFUSED = 111,
};

char const* errno_name(Errno);

class [[nodiscard]] Error {
public:
    constexpr explicit Error(Errno code) : m_code(code) {}

    constexpr Error(Errno code, char const* context) : m_code(code), m_context(context) {}

    constexpr Errno code() const { return m_code; }
    constexpr char const* context() const { return m_context; }

    char const* name() const { return errno_name(m_code); }

    constexpr i64 as_syscall_return() const { return -static_cast<i64>(m_code); }

    constexpr bool operator==(Error const& other) const { return m_code == other.m_code; }
    constexpr bool operator==(Errno other) const { return m_code == other; }

private:
    Errno m_code { Errno::Success };
    char const* m_context { nullptr };
};

}
