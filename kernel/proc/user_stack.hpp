#pragma once

#include "kernel/base/expected.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/proc/elf.hpp"
#include "kernel/proc/process_address_space.hpp"

namespace barry::proc {

enum class AuxiliaryVector : u64 {
    Null = 0,
    ProgramHeaders = 3,
    ProgramHeaderEntrySize = 4,
    ProgramHeaderCount = 5,
    PageSize = 6,
    EntryPoint = 9,
    UserId = 11,
    EffectiveUserId = 12,
    GroupId = 13,
    EffectiveGroupId = 14,
    ClockTicks = 17,
    Secure = 23,
    Random = 25,
};

struct InitialUserStack {
    VirtAddr stack_pointer {};
    u64 used_bytes { 0 };
};

KResultOr<InitialUserStack> build_initial_user_stack(ProcessAddressSpace& space,
                                                     LoadedImage const& image,
                                                     Span<StringView const> arguments,
                                                     Span<StringView const> environment);

}  // namespace barry::proc
