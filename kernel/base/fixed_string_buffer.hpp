#pragma once

#include "kernel/base/array.hpp"
#include "kernel/base/bytes.hpp"
#include "kernel/base/format.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/base/types.hpp"

namespace barry {

template<usize Capacity>
class FixedStringBuffer final : public FormatSink {
public:
    void write(StringView text) override {
        usize const writable = Capacity - m_length;
        usize const count = min(writable, text.length());

        Bytes const destination(reinterpret_cast<u8*>(m_storage.data()) + m_length, count);
        copy_bytes(destination, text.bytes().first(count));

        m_length += count;
        m_truncated = m_truncated || count < text.length();
    }

    StringView view() const { return StringView(m_storage.data(), m_length); }
    usize length() const { return m_length; }
    bool is_truncated() const { return m_truncated; }
    void clear() {
        m_length = 0;
        m_truncated = false;
    }

private:
    Array<char, Capacity> m_storage { {} };
    usize m_length { 0 };
    bool m_truncated { false };
};

}  // namespace barry
