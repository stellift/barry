#pragma once

#include "kernel/base/string_view.hpp"
#include "kernel/base/types.hpp"

namespace barry::dev {

class SerialPort {
public:
    static constexpr u16 COM1 = 0x3f8;
    static constexpr u16 COM2 = 0x2f8;

    constexpr explicit SerialPort(u16 io_base) : m_io_base(io_base) {}

    bool initialize(u32 baud_rate = 115200);
    bool is_present() const { return m_present; }

    void write_byte(u8 byte);
    void write(StringView text);

private:
    enum class Register : u16 {
        Data = 0,
        InterruptEnable = 1,
        DivisorLow = 0,
        DivisorHigh = 1,
        FifoControl = 2,
        LineControl = 3,
        ModemControl = 4,
        LineStatus = 5,
    };

    u8 read_register(Register) const;
    void write_register(Register, u8) const;
    bool is_transmit_buffer_empty() const;
    bool passes_loopback_test() const;

    u16 m_io_base;
    bool m_present { false };
};

SerialPort& debug_port();

}  // namespace barry::dev
