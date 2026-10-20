#pragma once

#include "kernel/base/array.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/fs/open_file_description.hpp"

namespace barry::fs {

class ConsoleDescription final : public OpenFileDescription {
public:
    static constexpr usize CAPTURE_CAPACITY = 1024;

    KResultOr<usize> read(Bytes destination) override;
    KResultOr<usize> write(ReadonlyBytes source) override;

    bool is_terminal() const override { return true; }

    u64 bytes_written() const { return m_bytes_written; }
    u64 bytes_read() const { return m_bytes_read; }

    StringView captured() const { return StringView(m_capture.data(), m_captured_length); }
    void reset_capture();

private:
    Array<char, CAPTURE_CAPACITY> m_capture { {} };
    usize m_captured_length { 0 };
    u64 m_bytes_written { 0 };
    u64 m_bytes_read { 0 };
};

ConsoleDescription& console();

RefPtr<OpenFileDescription> console_description();

void initialize_console();

}  // namespace barry::fs
