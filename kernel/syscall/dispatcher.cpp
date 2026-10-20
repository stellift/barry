#include "kernel/syscall/dispatcher.hpp"

#include "kernel/base/array.hpp"
#include "kernel/base/atomic.hpp"

namespace barry::syscall {
namespace {

Array<Handler, SYSCALL_TABLE_SIZE> g_handlers { {} };
Atomic<u64> g_invocations { 0 };
Atomic<u64> g_rejections { 0 };

bool is_in_range(u64 number) {
    return number < SYSCALL_TABLE_SIZE;
}

}  // namespace

KResult register_handler(Number number, Handler handler) {
    u64 const index = to_underlying(number);
    if (!is_in_range(index))
        return Error(Errno::ERANGE, "system call number is outside the table");
    if (handler == nullptr)
        return Error(Errno::EINVAL, "a system call needs a handler");
    if (g_handlers[index] != nullptr)
        return Error(Errno::EBUSY, "that system call number is already registered");

    g_handlers[index] = handler;
    return {};
}

bool is_registered(Number number) {
    u64 const index = to_underlying(number);
    return is_in_range(index) && g_handlers[index] != nullptr;
}

i64 dispatch(Arguments const& arguments) {
    g_invocations.fetch_add(1, Order::Relaxed);

    if (!is_in_range(arguments.number)) {
        g_rejections.fetch_add(1, Order::Relaxed);
        return Error(Errno::ENOSYS).as_syscall_return();
    }

    Handler const handler = g_handlers[arguments.number];
    if (handler == nullptr) {
        g_rejections.fetch_add(1, Order::Relaxed);
        return Error(Errno::ENOSYS).as_syscall_return();
    }

    auto result = handler(arguments);
    if (result.is_error())
        return result.error().as_syscall_return();

    return static_cast<i64>(result.value());
}

u64 invocation_count() {
    return g_invocations.load(Order::Relaxed);
}

u64 rejected_count() {
    return g_rejections.load(Order::Relaxed);
}

}  // namespace barry::syscall
