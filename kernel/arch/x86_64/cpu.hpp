#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/types.hpp"

namespace barry::arch {

inline constexpr u32 MSR_EFER = 0xc0000080;
inline constexpr u32 MSR_STAR = 0xc0000081;
inline constexpr u32 MSR_LSTAR = 0xc0000082;
inline constexpr u32 MSR_SFMASK = 0xc0000084;
inline constexpr u32 MSR_FS_BASE = 0xc0000100;
inline constexpr u32 MSR_GS_BASE = 0xc0000101;
inline constexpr u32 MSR_KERNEL_GS_BASE = 0xc0000102;
inline constexpr u32 MSR_APIC_BASE = 0x0000001b;

inline constexpr u64 EFER_SYSCALL_ENABLE = 1ULL << 0;
inline constexpr u64 EFER_NO_EXECUTE_ENABLE = 1ULL << 11;

inline constexpr u64 RFLAGS_INTERRUPT_ENABLE = 1ULL << 9;

inline constexpr u64 CR4_PAGE_GLOBAL_ENABLE = 1ULL << 7;

inline void disable_interrupts() {
    __asm__ volatile("cli" ::: "memory");
}

inline void enable_interrupts() {
    __asm__ volatile("sti" ::: "memory");
}

inline void halt() {
    __asm__ volatile("hlt" ::: "memory");
}

[[noreturn]] inline void halt_forever() {
    for (;;) {
        disable_interrupts();
        halt();
    }
}

inline u64 read_rflags() {
    u64 flags = 0;
    __asm__ volatile("pushfq; popq %0" : "=r"(flags)::"memory");
    return flags;
}

inline bool interrupts_enabled() {
    return (read_rflags() & RFLAGS_INTERRUPT_ENABLE) != 0;
}

inline u64 read_msr(u32 msr) {
    u32 low = 0;
    u32 high = 0;
    __asm__ volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<u64>(high) << 32) | low;
}

inline void write_msr(u32 msr, u64 value) {
    auto const low = static_cast<u32>(value);
    auto const high = static_cast<u32>(value >> 32);
    __asm__ volatile("wrmsr" ::"a"(low), "d"(high), "c"(msr) : "memory");
}

inline PhysAddr read_cr3() {
    u64 value = 0;
    __asm__ volatile("movq %%cr3, %0" : "=r"(value));
    return PhysAddr(value);
}

inline void write_cr3(PhysAddr page_table_root) {
    __asm__ volatile("movq %0, %%cr3" ::"r"(page_table_root.raw()) : "memory");
}

inline u64 read_cr0() {
    u64 value = 0;
    __asm__ volatile("movq %%cr0, %0" : "=r"(value));
    return value;
}

inline void write_cr0(u64 value) {
    __asm__ volatile("movq %0, %%cr0" ::"r"(value) : "memory");
}

inline u64 read_cr4() {
    u64 value = 0;
    __asm__ volatile("movq %%cr4, %0" : "=r"(value));
    return value;
}

inline void write_cr4(u64 value) {
    __asm__ volatile("movq %0, %%cr4" ::"r"(value) : "memory");
}

inline VirtAddr read_cr2() {
    u64 value = 0;
    __asm__ volatile("movq %%cr2, %0" : "=r"(value));
    return VirtAddr(value);
}

inline void invalidate_page(VirtAddr address) {
    __asm__ volatile("invlpg (%0)" ::"r"(address.raw()) : "memory");
}

inline void flush_entire_tlb() {
    write_cr3(read_cr3());
}

inline u64 read_timestamp_counter() {
    u32 low = 0;
    u32 high = 0;
    __asm__ volatile("rdtsc" : "=a"(low), "=d"(high));
    return (static_cast<u64>(high) << 32) | low;
}

struct CpuidResult {
    u32 eax { 0 };
    u32 ebx { 0 };
    u32 ecx { 0 };
    u32 edx { 0 };
};

inline CpuidResult cpuid(u32 leaf, u32 subleaf = 0) {
    CpuidResult result {};
    __asm__ volatile("cpuid"
                     : "=a"(result.eax), "=b"(result.ebx), "=c"(result.ecx), "=d"(result.edx)
                     : "a"(leaf), "c"(subleaf));
    return result;
}

class InterruptGuard {
public:
    InterruptGuard() : m_were_enabled(interrupts_enabled()) { disable_interrupts(); }

    ~InterruptGuard() {
        if (m_were_enabled)
            enable_interrupts();
    }

    InterruptGuard(InterruptGuard const&) = delete;
    InterruptGuard& operator=(InterruptGuard const&) = delete;

private:
    bool m_were_enabled;
};

}  // namespace barry::arch
