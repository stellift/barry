#pragma once

#include "kernel/boot/boot_info.hpp"

namespace barry {

[[noreturn]] void kernel_main(boot::BootInfo const& boot_info);

[[noreturn]] void boot_failure(StringView reason);

}  // namespace barry
