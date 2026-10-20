#include "kernel/test/framework.hpp"

#include "kernel/base/span.hpp"
#include "kernel/log.hpp"
#include "kernel/panic.hpp"

extern "C" barry::test::TestCase const __kernel_tests_start[];
extern "C" barry::test::TestCase const __kernel_tests_end[];

namespace barry::test {
namespace {

usize g_failures_in_current_test = 0;

Span<TestCase const> registered_tests() {
    auto const count = static_cast<usize>(__kernel_tests_end - __kernel_tests_start);
    return Span<TestCase const>(__kernel_tests_start, count);
}

}  // namespace

void record_failure(StringView expression, SourceLocation location) {
    ++g_failures_in_current_test;
    dbgln("      FAIL {} at {}:{}", expression, location.file, location.line);
}

void abort_current_test(StringView expression, SourceLocation location) {
    record_failure(expression, location);
    PANIC("a required test precondition failed: {}", expression);
}

TestSummary run_all_tests() {
    TestSummary summary {};
    auto const tests = registered_tests();

    dbgln("");
    dbgln("Running {} in-kernel tests:", tests.size());

    for (auto const& test : tests) {
        g_failures_in_current_test = 0;
        test.body();
        ++summary.total;

        if (g_failures_in_current_test == 0) {
            ++summary.passed;
            dbgln("  [pass] {}", test.name);
        } else {
            ++summary.failed;
            dbgln("  [FAIL] {} ({} checks failed)", test.name, g_failures_in_current_test);
        }
    }

    dbgln("");
    dbgln("Test summary: {} passed, {} failed, {} total", summary.passed, summary.failed,
          summary.total);
    return summary;
}

}  // namespace barry::test
