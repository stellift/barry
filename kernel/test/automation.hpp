#pragma once

#include "kernel/boot/command_line.hpp"
#include "kernel/test/framework.hpp"

namespace barry::test {

inline constexpr StringView AUTOTEST_FLAG = "barry.autotest"_sv;

bool automation_requested(boot::CommandLine const& command_line);

[[noreturn]] void finish_automated_run(TestSummary const& summary);

}  // namespace barry::test
