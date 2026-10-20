#include "kernel/arch/x86_64/stack_walk.hpp"

namespace barry::arch {

bool StackFrameWalker::looks_like_kernel_frame(VirtAddr candidate) {
    return !candidate.is_null() && candidate.is_canonical() && candidate.is_kernel_space()
           && (candidate.raw() % sizeof(u64)) == 0;
}

Optional<VirtAddr> StackFrameWalker::next_return_address() {
    if (m_depth >= MAX_BACKTRACE_DEPTH)
        return nullopt;
    if (!looks_like_kernel_frame(m_frame_pointer))
        return nullopt;

    auto const* frame = m_frame_pointer.as_ptr<Frame const>();
    VirtAddr const return_address { frame->return_address };
    VirtAddr const caller_frame { frame->saved_frame_pointer };

    if (return_address.is_null() || !return_address.is_kernel_space())
        return nullopt;
    if (caller_frame <= m_frame_pointer)
        return nullopt;

    m_frame_pointer = caller_frame;
    ++m_depth;
    return return_address;
}

}  // namespace barry::arch
