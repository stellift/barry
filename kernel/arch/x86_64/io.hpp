#pragma once

#include "kernel/base/types.hpp"

namespace barry::arch {

inline void out8(u16 port, u8 value) {
    __asm__ volatile("outb %0, %1" ::"a"(value), "Nd"(port) : "memory");
}

inline void out16(u16 port, u16 value) {
    __asm__ volatile("outw %0, %1" ::"a"(value), "Nd"(port) : "memory");
}

inline void out32(u16 port, u32 value) {
    __asm__ volatile("outl %0, %1" ::"a"(value), "Nd"(port) : "memory");
}

inline u8 in8(u16 port) {
    u8 value = 0;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

inline u16 in16(u16 port) {
    u16 value = 0;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

inline u32 in32(u16 port) {
    u32 value = 0;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port) : "memory");
    return value;
}

inline void io_delay() {
    out8(0x80, 0);
}

}  // namespace barry::arch
