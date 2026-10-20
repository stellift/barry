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

void operator delete(void*) noexcept {
    PANIC("global delete is not available in the kernel; use OwnPtr or RefPtr");
}

void operator delete(void*, barry::usize) noexcept {
    PANIC("global sized delete is not available in the kernel; use OwnPtr or RefPtr");
}

void operator delete[](void*) noexcept {
    PANIC("global array delete is not available in the kernel");
}

void operator delete[](void*, barry::usize) noexcept {
    PANIC("global sized array delete is not available in the kernel");
}
