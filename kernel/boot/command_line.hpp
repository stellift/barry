#pragma once

#include "kernel/base/optional.hpp"
#include "kernel/base/string_view.hpp"

namespace barry::boot {

class CommandLine {
public:
    constexpr CommandLine() = default;
    constexpr explicit CommandLine(StringView text) : m_text(text) {}

    StringView text() const { return m_text; }

    bool has_flag(StringView name) const;
    Optional<StringView> value_of(StringView name) const;

private:
    class Iterator {
    public:
        constexpr Iterator(StringView text, usize offset) : m_text(text), m_offset(offset) {}

        Optional<StringView> next();

    private:
        StringView m_text;
        usize m_offset;
    };

    StringView m_text;
};

}  // namespace barry::boot
