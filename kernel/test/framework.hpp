#pragma once

#include "kernel/base/string_view.hpp"
#include "kernel/base/types.hpp"
#include "kernel/base/source_location.hpp"
#include "kernel/test/registration.hpp"

namespace barry::test {

struct TestSummary {
    usize total { 0 };
    usize passed { 0 };
    usize failed { 0 };

    bool all_passed() const { return failed == 0; }
};

void record_failure(StringView expression, SourceLocation location);

[[noreturn]] void abort_current_test(StringView expression, SourceLocation location);

TestSummary run_all_tests();

}  // namespace barry::test

#define EXPECT(expression)                                                                  \
    do {                                                                                    \
        if (!static_cast<bool>(expression)) [[unlikely]]                                    \
            ::barry::test::record_failure(::barry::StringView(#expression),                  \
                                          BARRY_SOURCE_LOCATION);                            \
    } while (false)

#define EXPECT_EQ(left, right)                                                              \
    do {                                                                                    \
        if (!((left) == (right))) [[unlikely]]                                              \
            ::barry::test::record_failure(::barry::StringView(#left " == " #right),          \
                                          BARRY_SOURCE_LOCATION);                            \
    } while (false)

#define EXPECT_OK(expression)                                                               \
    do {                                                                                    \
        auto&& _barry_expect_result = (expression);                                         \
        if (_barry_expect_result.is_error()) [[unlikely]]                                   \
            ::barry::test::record_failure(::barry::StringView(#expression " succeeded"),     \
                                          BARRY_SOURCE_LOCATION);                            \
    } while (false)

#define TRY_OR_FAIL(expression)                                                             \
    ({                                                                                      \
        auto&& _barry_required = (expression);                                              \
        if (_barry_required.is_error()) [[unlikely]]                                        \
            ::barry::test::abort_current_test(::barry::StringView(#expression),              \
                                              BARRY_SOURCE_LOCATION);                        \
        _barry_required.release_value();                                                    \
    })

#define EXPECT_ERROR(expression)                                                            \
    do {                                                                                    \
        auto&& _barry_expect_result = (expression);                                         \
        if (!_barry_expect_result.is_error()) [[unlikely]]                                   \
            ::barry::test::record_failure(::barry::StringView(#expression " failed"),        \
                                          BARRY_SOURCE_LOCATION);                            \
    } while (false)
