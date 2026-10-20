#include "kernel/base/types.hpp"
#include "kernel/panic.hpp"

extern "C" {

void* __dso_handle = nullptr;

int __cxa_atexit(void (*)(void*), void*, void*) {
    return 0;
}

void __cxa_pure_virtual() {
    PANIC("pure virtual function called");
}

void __stack_chk_fail() {
    PANIC("stack smashing detected");
}
}
