#pragma once

#include "kernel/base/expected.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/base/types.hpp"

namespace barry::syscall {

inline constexpr usize SYSCALL_TABLE_SIZE = 512;

enum class Number : u64 {
    Read = 0,
    Write = 1,
    Open = 2,
    Close = 3,
    Stat = 4,
    Fstat = 5,
    Lseek = 8,
    Mmap = 9,
    Mprotect = 10,
    Munmap = 11,
    Brk = 12,
    Ioctl = 16,
    Writev = 20,
    GetPid = 39,
    Fork = 57,
    Execve = 59,
    Wait4 = 61,
    Exit = 60,
    GetUid = 102,
    GetGid = 104,
    GetEuid = 107,
    GetEgid = 108,
    ArchPrctl = 158,
    Openat = 257,
    SetThreadIdAddress = 218,
    ExitGroup = 231,
};

struct Arguments {
    u64 number { 0 };
    u64 first { 0 };
    u64 second { 0 };
    u64 third { 0 };
    u64 fourth { 0 };
    u64 fifth { 0 };
    u64 sixth { 0 };
};

using Handler = KResultOr<u64> (*)(Arguments const&);

KResult register_handler(Number number, Handler handler);

bool is_registered(Number number);

i64 dispatch(Arguments const& arguments);

u64 invocation_count();

u64 rejected_count();

}  // namespace barry::syscall
