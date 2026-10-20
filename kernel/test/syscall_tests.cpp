#include "kernel/arch/x86_64/syscall.hpp"
#include "kernel/syscall/dispatcher.hpp"
#include "kernel/test/framework.hpp"

namespace barry {
namespace {

constexpr u64 FAKE_PROCESS_ID = 1234;
constexpr auto FIRST_TEST_NUMBER = static_cast<syscall::Number>(400);
constexpr auto SECOND_TEST_NUMBER = static_cast<syscall::Number>(401);
constexpr u64 UNREGISTERED_NUMBER = 400;
constexpr u64 OUT_OF_RANGE_NUMBER = syscall::SYSCALL_TABLE_SIZE + 1;

KResultOr<u64> fake_get_process_id(syscall::Arguments const&) {
    return FAKE_PROCESS_ID;
}

KResultOr<u64> always_fails(syscall::Arguments const& arguments) {
    if (arguments.first == 0)
        return Error(Errno::EBADF, "no such descriptor");
    return arguments.first;
}

i64 negative_errno(Errno code) {
    return -static_cast<i64>(code);
}

}  // namespace

KERNEL_TEST(syscall_dispatcher_rejects_unknown_numbers) {
    syscall::Arguments arguments {};
    arguments.number = UNREGISTERED_NUMBER;
    EXPECT_EQ(syscall::dispatch(arguments), negative_errno(Errno::ENOSYS));

    arguments.number = OUT_OF_RANGE_NUMBER;
    EXPECT_EQ(syscall::dispatch(arguments), negative_errno(Errno::ENOSYS));
}

KERNEL_TEST(syscall_dispatcher_routes_registered_handlers) {
    EXPECT(!syscall::is_registered(FIRST_TEST_NUMBER));
    EXPECT_OK(syscall::register_handler(FIRST_TEST_NUMBER, &fake_get_process_id));
    EXPECT(syscall::is_registered(FIRST_TEST_NUMBER));
    EXPECT_ERROR(syscall::register_handler(FIRST_TEST_NUMBER, &fake_get_process_id));

    syscall::Arguments arguments {};
    arguments.number = to_underlying(FIRST_TEST_NUMBER);
    EXPECT_EQ(syscall::dispatch(arguments), static_cast<i64>(FAKE_PROCESS_ID));
}

KERNEL_TEST(syscall_errors_become_negative_errno) {
    EXPECT_OK(syscall::register_handler(SECOND_TEST_NUMBER, &always_fails));

    syscall::Arguments arguments {};
    arguments.number = to_underlying(SECOND_TEST_NUMBER);
    arguments.first = 0;
    EXPECT_EQ(syscall::dispatch(arguments), negative_errno(Errno::EBADF));

    arguments.first = 7;
    EXPECT_EQ(syscall::dispatch(arguments), 7);
}

KERNEL_TEST(fast_syscalls_are_enabled) {
    EXPECT(arch::fast_syscalls_enabled());
}

}  // namespace barry
