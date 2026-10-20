#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/types.hpp"

namespace barry::arch {

struct UserContext {
    u64 rip;
    u64 rsp;
    u64 rflags;
    u64 rdi;
    u64 rsi;
    u64 rdx;
    u64 rcx;
    u64 r8;
    u64 r9;
    u64 rax;
    u64 rbx;
    u64 rbp;
    u64 r10;
    u64 r11;
    u64 r12;
    u64 r13;
    u64 r14;
    u64 r15;

    static UserContext for_fresh_process(VirtAddr entry_point, VirtAddr stack_pointer);
};

static_assert(__builtin_offsetof(UserContext, rip) == 0);
static_assert(__builtin_offsetof(UserContext, rsp) == 8);
static_assert(__builtin_offsetof(UserContext, rflags) == 16);
static_assert(__builtin_offsetof(UserContext, rdi) == 24);
static_assert(__builtin_offsetof(UserContext, rsi) == 32);
static_assert(__builtin_offsetof(UserContext, rdx) == 40);
static_assert(__builtin_offsetof(UserContext, rcx) == 48);
static_assert(__builtin_offsetof(UserContext, r8) == 56);
static_assert(__builtin_offsetof(UserContext, r9) == 64);
static_assert(__builtin_offsetof(UserContext, rax) == 72);
static_assert(__builtin_offsetof(UserContext, rbx) == 80);
static_assert(__builtin_offsetof(UserContext, rbp) == 88);
static_assert(__builtin_offsetof(UserContext, r10) == 96);
static_assert(__builtin_offsetof(UserContext, r11) == 104);
static_assert(__builtin_offsetof(UserContext, r12) == 112);
static_assert(__builtin_offsetof(UserContext, r13) == 120);
static_assert(__builtin_offsetof(UserContext, r14) == 128);
static_assert(__builtin_offsetof(UserContext, r15) == 136);
static_assert(sizeof(UserContext) == 144);

void enable_user_floating_point();

inline constexpr usize FPU_STATE_SIZE = 512;
inline constexpr usize FPU_STATE_ALIGNMENT = 16;
inline constexpr usize MXCSR_OFFSET = 24;
inline constexpr u32 DEFAULT_MXCSR = 0x1f80;

void initialize_fpu_state(Span<u8> state);
void save_fpu_state(Span<u8> state);
void restore_fpu_state(Span<u8 const> state);

}  // namespace barry::arch

extern "C" [[noreturn]] void barry_enter_user_mode(barry::arch::UserContext const* context);
