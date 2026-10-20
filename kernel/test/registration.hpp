#pragma once

#include "kernel/base/string_view.hpp"

namespace barry::test {

using TestBody = void (*)();

struct TestCase {
    StringView name;
    TestBody body;
};

#ifdef BARRY_HOST_TEST

void register_host_test(TestCase const& test);

class HostTestRegistrar {
public:
    explicit HostTestRegistrar(TestCase const& test) { register_host_test(test); }
};

#    define KERNEL_TEST(test_name)                                                     \
        static void test_name();                                                       \
        static ::barry::test::TestCase const test_name##_test_case {                   \
            ::barry::StringView(#test_name), &test_name                                \
        };                                                                             \
        static ::barry::test::HostTestRegistrar const test_name##_registrar {           \
            test_name##_test_case                                                      \
        };                                                                             \
        static void test_name()

#else

#    define KERNEL_TEST(test_name)                                                     \
        static void test_name();                                                       \
        [[gnu::used, gnu::section(".ktests")]] static ::barry::test::TestCase const     \
            test_name##_test_case { ::barry::StringView(#test_name), &test_name };      \
        static void test_name()

#endif

}  // namespace barry::test
