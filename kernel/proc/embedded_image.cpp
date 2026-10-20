#include "kernel/proc/embedded_image.hpp"

extern "C" barry::u8 const barry_embedded_user_image_start[];
extern "C" barry::u8 const barry_embedded_user_image_end[];

namespace barry::proc {

ReadonlyBytes embedded_user_image() {
    auto const size
        = static_cast<usize>(barry_embedded_user_image_end - barry_embedded_user_image_start);
    return ReadonlyBytes(barry_embedded_user_image_start, size);
}

bool has_embedded_user_image() {
    return !embedded_user_image().is_empty();
}

}  // namespace barry::proc
