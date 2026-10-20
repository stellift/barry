#pragma once

#include "kernel/base/types.hpp"

namespace barry {

[[noreturn]] void assertion_failed(char const* expression, char const* file, u32 line,
                                   char const* function);

}

#define VERIFY(expr)                                                                     \
    do {                                                                                 \
        if (!static_cast<bool>(expr)) [[unlikely]]                                       \
            ::barry::assertion_failed(#expr, __FILE__, __LINE__, __PRETTY_FUNCTION__);   \
    } while (false)

#define VERIFY_NOT_REACHED() VERIFY(false && "reached unreachable code")

#define TODO() VERIFY(false && "not implemented yet")

#ifdef BARRY_DEBUG
#    define ASSERT(expr) VERIFY(expr)
#else
#    define ASSERT(expr) ((void)0)
#endif
