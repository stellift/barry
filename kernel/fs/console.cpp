#include "kernel/fs/console.hpp"

#include "kernel/base/bytes.hpp"
#include "kernel/base/utility.hpp"
#include "kernel/dev/serial.hpp"
#include "kernel/log.hpp"

namespace barry::fs {
namespace {

ConsoleDescription g_console;

}  // namespace

KResultOr<usize> ConsoleDescription::read(Bytes destination) {
    if (destination.is_empty())
        return usize { 0 };

    usize filled = 0;
    while (filled < destination.size()) {
        auto const byte = dev::debug_port().read_byte();
        if (!byte.has_value())
            break;

        destination[filled++] = byte.value();
        ++m_bytes_read;

        if (byte.value() == '\n')
            break;
    }

    if (filled == 0)
        return Error(Errno::EAGAIN, "no console input is available");

    return filled;
}

KResultOr<usize> ConsoleDescription::write(ReadonlyBytes source) {
    if (source.is_empty())
        return usize { 0 };

    log_write(StringView(reinterpret_cast<char const*>(source.data()), source.size()));

    usize const room = CAPTURE_CAPACITY - m_captured_length;
    usize const captured = min(room, source.size());
    if (captured != 0) {
        copy_bytes(as_writable_bytes(m_capture.span().slice(m_captured_length, captured)),
                   source.first(captured));
        m_captured_length += captured;
    }

    m_bytes_written += source.size();
    return source.size();
}

void ConsoleDescription::reset_capture() {
    m_captured_length = 0;
}

ConsoleDescription& console() {
    return g_console;
}

RefPtr<OpenFileDescription> console_description() {
    return RefPtr<OpenFileDescription>(g_console);
}

void initialize_console() {
    g_console.acquire_reference();
}

}  // namespace barry::fs
