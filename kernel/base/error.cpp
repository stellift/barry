#include "kernel/base/error.hpp"

namespace barry {

char const* errno_name(Errno code) {
    switch (code) {
    case Errno::Success:
        return "Success";
    case Errno::EPERM:
        return "EPERM";
    case Errno::ENOENT:
        return "ENOENT";
    case Errno::ESRCH:
        return "ESRCH";
    case Errno::EINTR:
        return "EINTR";
    case Errno::EIO:
        return "EIO";
    case Errno::ENXIO:
        return "ENXIO";
    case Errno::E2BIG:
        return "E2BIG";
    case Errno::ENOEXEC:
        return "ENOEXEC";
    case Errno::EBADF:
        return "EBADF";
    case Errno::ECHILD:
        return "ECHILD";
    case Errno::EAGAIN:
        return "EAGAIN";
    case Errno::ENOMEM:
        return "ENOMEM";
    case Errno::EACCES:
        return "EACCES";
    case Errno::EFAULT:
        return "EFAULT";
    case Errno::EBUSY:
        return "EBUSY";
    case Errno::EEXIST:
        return "EEXIST";
    case Errno::EXDEV:
        return "EXDEV";
    case Errno::ENODEV:
        return "ENODEV";
    case Errno::ENOTDIR:
        return "ENOTDIR";
    case Errno::EISDIR:
        return "EISDIR";
    case Errno::EINVAL:
        return "EINVAL";
    case Errno::ENFILE:
        return "ENFILE";
    case Errno::EMFILE:
        return "EMFILE";
    case Errno::ENOTTY:
        return "ENOTTY";
    case Errno::EFBIG:
        return "EFBIG";
    case Errno::ENOSPC:
        return "ENOSPC";
    case Errno::ESPIPE:
        return "ESPIPE";
    case Errno::EROFS:
        return "EROFS";
    case Errno::EMLINK:
        return "EMLINK";
    case Errno::EPIPE:
        return "EPIPE";
    case Errno::ERANGE:
        return "ERANGE";
    case Errno::ENAMETOOLONG:
        return "ENAMETOOLONG";
    case Errno::ENOSYS:
        return "ENOSYS";
    case Errno::ENOTEMPTY:
        return "ENOTEMPTY";
    case Errno::ELOOP:
        return "ELOOP";
    case Errno::EOVERFLOW:
        return "EOVERFLOW";
    case Errno::ETIMEDOUT:
        return "ETIMEDOUT";
    case Errno::EADDRINUSE:
        return "EADDRINUSE";
    case Errno::ECONNREFUSED:
        return "ECONNREFUSED";
    }
    return "EUNKNOWN";
}

}  // namespace barry
