#include <cstdio>
#include <cstdlib>

#include "kernel/base/assert.hpp"
#include "kernel/test/framework.hpp"

namespace barry {
namespace {

constexpr barry::usize MAX_HOST_TESTS = 256;

struct Registry {
    test::TestCase const* tests[MAX_HOST_TESTS] {};
    usize count { 0 };
};

Registry& registry() {
    static Registry instance;
    return instance;
}

usize g_failures_in_current_test = 0;

void print_view(StringView text) {
    (void)std::fwrite(text.data(), 1, text.length(), stdout);
}

}  // namespace

void assertion_failed(char const* expression, char const* file, u32 line,
                      char const* function) {
    std::fprintf(stderr, "\nVERIFY failed: %s\n  at %s:%u in %s\n", expression, file, line,
                 function);
    std::abort();
}

namespace test {

void register_host_test(TestCase const& test) {
    Registry& entries = registry();
    if (entries.count >= MAX_HOST_TESTS) {
        std::fprintf(stderr, "too many host tests registered\n");
        std::abort();
    }
    entries.tests[entries.count++] = &test;
}

void record_failure(StringView expression, SourceLocation location) {
    ++g_failures_in_current_test;
    std::fputs("      FAIL ", stdout);
    print_view(expression);
    std::fputs(" at ", stdout);
    print_view(location.file);
    std::fprintf(stdout, ":%u\n", location.line);
}

void abort_current_test(StringView expression, SourceLocation location) {
    record_failure(expression, location);
    std::fputs("a required test precondition failed\n", stderr);
    std::abort();
}

TestSummary run_all_tests() {
    Registry const& entries = registry();
    TestSummary summary {};

    std::printf("Running %zu host tests with address and undefined behaviour sanitizers:\n",
                entries.count);

    for (usize index = 0; index < entries.count; ++index) {
        TestCase const& test = *entries.tests[index];
        g_failures_in_current_test = 0;
        test.body();
        ++summary.total;

        if (g_failures_in_current_test == 0) {
            ++summary.passed;
            std::fputs("  [pass] ", stdout);
        } else {
            ++summary.failed;
            std::fputs("  [FAIL] ", stdout);
        }
        print_view(test.name);
        std::fputc('\n', stdout);
    }

    std::printf("\nTest summary: %zu passed, %zu failed, %zu total\n", summary.passed,
                summary.failed, summary.total);
    return summary;
}

}  // namespace test
}  // namespace barry

int main() {
    auto const summary = barry::test::run_all_tests();
    return summary.all_passed() ? 0 : 1;
}
