#pragma once

#include "kernel/base/bytes.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/types.hpp"

namespace barry {

class StringView {
public:
    constexpr StringView() = default;
    constexpr StringView(char const* data, usize length) : m_data(data), m_length(length) {}

    constexpr StringView(char const* cstring)
        : m_data(cstring), m_length(length_of_cstring(cstring)) {}

    constexpr char const* data() const { return m_data; }
    constexpr usize length() const { return m_length; }
    constexpr bool is_empty() const { return m_length == 0; }

    constexpr char const* begin() const { return m_data; }
    constexpr char const* end() const { return m_data + m_length; }

    constexpr char operator[](usize index) const {
        VERIFY(index < m_length);
        return m_data[index];
    }

    constexpr ReadonlyBytes bytes() const {
        return ReadonlyBytes(reinterpret_cast<u8 const*>(m_data), m_length);
    }

    constexpr StringView substring(usize offset) const {
        VERIFY(offset <= m_length);
        return StringView(m_data + offset, m_length - offset);
    }

    constexpr StringView substring(usize offset, usize length) const {
        VERIFY(offset <= m_length);
        VERIFY(length <= m_length - offset);
        return StringView(m_data + offset, length);
    }

    bool operator==(StringView other) const { return bytes_equal(bytes(), other.bytes()); }

    bool starts_with(StringView prefix) const {
        if (prefix.m_length > m_length)
            return false;
        return bytes_equal(substring(0, prefix.m_length).bytes(), prefix.bytes());
    }

    Optional<usize> find(char needle) const {
        for (usize i = 0; i < m_length; ++i) {
            if (m_data[i] == needle)
                return i;
        }
        return nullopt;
    }

private:
    char const* m_data { nullptr };
    usize m_length { 0 };
};

constexpr StringView operator""_sv(char const* data, usize length) {
    return StringView(data, length);
}

}  // namespace barry
