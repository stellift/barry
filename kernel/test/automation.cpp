#include "kernel/test/automation.hpp"

#include "kernel/arch/x86_64/cpu.hpp"
#include "kernel/arch/x86_64/io.hpp"
#include "kernel/log.hpp"

namespace barry::test {
namespace {

constexpr u16 QEMU_DEBUG_EXIT_PORT = 0xf4;
constexpr u8 EXIT_CODE_SUCCESS = 0;
constexpr u8 EXIT_CODE_FAILURE = 1;

}  // namespace

bool automation_requested(boot::CommandLine const& command_line) {
    return command_line.has_flag(AUTOTEST_FLAG);
}

void finish_automated_run(TestSummary const& summary) {
    dbgln("");
    dbgln("Automated run finished: {}", summary.all_passed() ? "SUCCESS"_sv : "FAILURE"_sv);

    arch::out32(QEMU_DEBUG_EXIT_PORT,
                summary.all_passed() ? EXIT_CODE_SUCCESS : EXIT_CODE_FAILURE);
    arch::halt_forever();
}

}  // namespace barry::test
