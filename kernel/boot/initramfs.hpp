#pragma once

#include "kernel/base/expected.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/types.hpp"

namespace barry::boot {

struct InitramfsSummary {
    usize directories { 0 };
    usize files { 0 };
    u64 bytes { 0 };
};

KResultOr<InitramfsSummary> populate_from_cpio_archive(ReadonlyBytes archive);

ReadonlyBytes embedded_initramfs();

}  // namespace barry::boot
