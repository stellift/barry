#pragma once

#include "kernel/base/span.hpp"
#include "kernel/base/types.hpp"

namespace barry::proc {

ReadonlyBytes embedded_user_image();

bool has_embedded_user_image();

}  // namespace barry::proc
