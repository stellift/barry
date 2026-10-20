#include "kernel/dev/serial.hpp"

#include "kernel/arch/x86_64/io.hpp"

namespace barry::dev {
namespace {

constexpr u32 UART_INPUT_CLOCK = 115200;

constexpr u8 LINE_CONTROL_8N1 = 0x03;
constexpr u8 LINE_CONTROL_ENABLE_DIVISOR_LATCH = 0x80;
constexpr u8 FIFO_ENABLE_AND_CLEAR = 0xc7;
constexpr u8 MODEM_DTR_RTS_OUT2 = 0x0b;
constexpr u8 MODEM_LOOPBACK = 0x1e;
constexpr u8 LINE_STATUS_TRANSMIT_EMPTY = 0x20;
constexpr u8 LINE_STATUS_DATA_READY = 0x01;
constexpr u8 LOOPBACK_PROBE_BYTE = 0xae;

}  // namespace

u8 SerialPort::read_register(Register reg) const {
    return arch::in8(static_cast<u16>(m_io_base + static_cast<u16>(reg)));
}

void SerialPort::write_register(Register reg, u8 value) const {
    arch::out8(static_cast<u16>(m_io_base + static_cast<u16>(reg)), value);
}

bool SerialPort::is_transmit_buffer_empty() const {
    return (read_register(Register::LineStatus) & LINE_STATUS_TRANSMIT_EMPTY) != 0;
}

bool SerialPort::has_received_byte() const {
    return (read_register(Register::LineStatus) & LINE_STATUS_DATA_READY) != 0;
}

bool SerialPort::passes_loopback_test() const {
    write_register(Register::ModemControl, MODEM_LOOPBACK);
    write_register(Register::Data, LOOPBACK_PROBE_BYTE);
    return read_register(Register::Data) == LOOPBACK_PROBE_BYTE;
}

bool SerialPort::initialize(u32 baud_rate) {
    if (baud_rate == 0 || baud_rate > UART_INPUT_CLOCK)
        return false;

    auto const divisor = static_cast<u16>(UART_INPUT_CLOCK / baud_rate);

    write_register(Register::InterruptEnable, 0);
    write_register(Register::LineControl, LINE_CONTROL_ENABLE_DIVISOR_LATCH);
    write_register(Register::DivisorLow, static_cast<u8>(divisor & 0xff));
    write_register(Register::DivisorHigh, static_cast<u8>(divisor >> 8));
    write_register(Register::LineControl, LINE_CONTROL_8N1);
    write_register(Register::FifoControl, FIFO_ENABLE_AND_CLEAR);
    write_register(Register::ModemControl, MODEM_DTR_RTS_OUT2);

    if (!passes_loopback_test()) {
        m_present = false;
        return false;
    }

    write_register(Register::ModemControl, MODEM_DTR_RTS_OUT2);
    m_present = true;
    return true;
}

void SerialPort::write_byte(u8 byte) {
    if (!m_present)
        return;
    while (!is_transmit_buffer_empty())
        arch::io_delay();
    write_register(Register::Data, byte);
}

Optional<u8> SerialPort::read_byte() {
    if (!m_present || !has_received_byte())
        return nullopt;
    return read_register(Register::Data);
}

void SerialPort::write(StringView text) {
    for (char const character : text) {
        if (character == '\n')
            write_byte('\r');
        write_byte(static_cast<u8>(character));
    }
}

namespace {
SerialPort g_debug_port { SerialPort::COM1 };
}

SerialPort& debug_port() {
    return g_debug_port;
}

}  // namespace barry::dev
