#pragma once

#include "kernel/base/string_view.hpp"
#include "kernel/base/types.hpp"

namespace barry {

struct SourceLocation {
    StringView file;
    StringView function;
    u32 line { 0 };
};

}  // namespace barry

#define BARRY_SOURCE_LOCATION                                                          \
    ::barry::SourceLocation {                                                          \
        ::barry::StringView(__FILE__), ::barry::StringView(__PRETTY_FUNCTION__),       \
            static_cast<::barry::u32>(__LINE__)                                        \
    }
