#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/types.hpp"

namespace barry::arch {

class StackFrameWalker {
public:
    static StackFrameWalker from_current_frame() {
        return StackFrameWalker(VirtAddr::from_ptr(__builtin_frame_address(0)));
    }

    explicit StackFrameWalker(VirtAddr frame_pointer) : m_frame_pointer(frame_pointer) {}

    Optional<VirtAddr> next_return_address();

private:
    struct Frame {
        u64 saved_frame_pointer;
        u64 return_address;
    };

    static bool looks_like_kernel_frame(VirtAddr candidate);

    VirtAddr m_frame_pointer;
    usize m_depth { 0 };
};

inline constexpr usize MAX_BACKTRACE_DEPTH = 32;

}  // namespace barry::arch
