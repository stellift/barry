#include "kernel/base/array.hpp"
#include "kernel/base/bytes.hpp"
#include "kernel/fs/console.hpp"
#include "kernel/fs/virtual_file_system.hpp"
#include "kernel/mm/kernel_address_space.hpp"
#include "kernel/mm/user_access.hpp"
#include "kernel/proc/elf.hpp"
#include "kernel/proc/embedded_image.hpp"
#include "kernel/proc/process.hpp"
#include "kernel/sched/scheduler.hpp"
#include "kernel/test/framework.hpp"

namespace barry {
namespace {

constexpr u64 TICK_BUDGET = 2000;
constexpr i32 EXPECTED_EXIT_STATUS = 42;
constexpr StringView EXPECTED_GREETING = "hello from ring 3\n"_sv;
constexpr StringView EXPECTED_REPORT = "user space checks passed\n"_sv;
constexpr StringView EXPECTED_CHILD_MESSAGE = "child process reporting\n"_sv;
constexpr StringView EXPECTED_EXEC_MESSAGE = "replaced image reporting\n"_sv;

bool wait_for_exit(RefPtr<proc::Process> const& process) {
    u64 const deadline = sched::scheduler().statistics().timer_ticks + TICK_BUDGET;

    while (!process->has_exited()) {
        if (sched::scheduler().statistics().timer_ticks > deadline)
            return false;
        sched::scheduler().yield();
    }
    return true;
}

bool contains(StringView haystack, StringView needle) {
    if (needle.length() > haystack.length())
        return false;

    for (usize offset = 0; offset + needle.length() <= haystack.length(); ++offset) {
        if (haystack.substring(offset, needle.length()) == needle)
            return true;
    }
    return false;
}

KResultOr<proc::ProcessId> spawn_embedded_program(StringView name) {
    Array<StringView, 2> arguments { { name, "--check"_sv } };
    Array<StringView, 1> environment { { "PATH=/bin"_sv } };

    return proc::Process::spawn_from_image(name, proc::embedded_user_image(),
                                           arguments.span(), environment.span());
}

}  // namespace

KERNEL_TEST(the_kernel_carries_an_embedded_user_program) {
    EXPECT(proc::has_embedded_user_image());

    auto const image = proc::embedded_user_image();
    EXPECT(image.size() > sizeof(proc::ElfHeader));
    EXPECT_EQ(image[0], 0x7fu);
    EXPECT_EQ(image[1], static_cast<u8>('E'));
    EXPECT_EQ(image[2], static_cast<u8>('L'));
    EXPECT_EQ(image[3], static_cast<u8>('F'));
}

KERNEL_TEST(a_static_linux_binary_runs_in_ring_three) {
    fs::console().reset_capture();

    auto const id = spawn_embedded_program("hello"_sv);
    EXPECT(id.has_value());

    auto const process = proc::process_table().find(id.value());
    EXPECT(!process.is_null());

    EXPECT(wait_for_exit(process));
    EXPECT_EQ(process->exit_status(), EXPECTED_EXIT_STATUS);

    auto const output = fs::console().captured();
    EXPECT(contains(output, EXPECTED_GREETING));
    EXPECT(contains(output, EXPECTED_CHILD_MESSAGE));
    EXPECT(contains(output, EXPECTED_EXEC_MESSAGE));
    EXPECT(contains(output, EXPECTED_REPORT));
}

KERNEL_TEST(two_user_processes_run_independently) {
    auto const first = spawn_embedded_program("first"_sv);
    auto const second = spawn_embedded_program("second"_sv);

    EXPECT(first.has_value());
    EXPECT(second.has_value());
    EXPECT(first.value() != second.value());

    auto const first_process = proc::process_table().find(first.value());
    auto const second_process = proc::process_table().find(second.value());

    EXPECT(wait_for_exit(first_process));
    EXPECT(wait_for_exit(second_process));

    EXPECT_EQ(first_process->exit_status(), EXPECTED_EXIT_STATUS);
    EXPECT_EQ(second_process->exit_status(), EXPECTED_EXIT_STATUS);
    EXPECT(first_process->address_space().root() != second_process->address_space().root());
}

KERNEL_TEST(the_elf_loader_rejects_malformed_images) {
    auto space = proc::ProcessAddressSpace::create();
    EXPECT(space.has_value());

    Array<u8, 128> garbage { {} };
    EXPECT_ERROR(proc::load_static_executable(*space.value(), garbage.span()));

    auto const truncated = proc::embedded_user_image().first(16);
    EXPECT_ERROR(proc::load_static_executable(*space.value(), truncated));
}

KERNEL_TEST(user_access_refuses_kernel_addresses) {
    auto const& space = mm::kernel_address_space();
    Array<u8, 8> buffer { {} };

    EXPECT_ERROR(mm::copy_from_user(space, buffer.span(),
                                    UserPtr<u8 const>(VirtAddr(0xffffffff80000000))));
    EXPECT_ERROR(mm::copy_from_user(space, buffer.span(), UserPtr<u8 const>(VirtAddr(0))));
    EXPECT_ERROR(mm::validate_user_range(space, VirtRange { VirtAddr(0x1000), PAGE_SIZE },
                                         mm::UserAccess::Read));
}

KERNEL_TEST(a_program_loaded_from_the_file_system_runs) {
    auto const inode = TRY_OR_FAIL(fs::resolve_path("/bin/hello"_sv));
    auto const contents = TRY_OR_FAIL(inode->read_entire_file());

    Array<StringView, 1> arguments { { "/bin/hello"_sv } };
    Array<StringView, 1> environment { { "HOME=/"_sv } };

    auto const id = proc::Process::spawn_from_image("hello"_sv, contents.bytes(),
                                                    arguments.span(), environment.span());
    EXPECT(id.has_value());

    auto const process = proc::process_table().find(id.value());
    EXPECT(!process.is_null());

    EXPECT(wait_for_exit(process));
    EXPECT_EQ(process->exit_status(), EXPECTED_EXIT_STATUS);
}

}  // namespace barry
