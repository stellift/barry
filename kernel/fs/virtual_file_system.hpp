#pragma once

#include "kernel/base/expected.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/fs/inode.hpp"

namespace barry::fs {

inline constexpr usize MAX_PATH_LENGTH = 255;
inline constexpr char PATH_SEPARATOR = '/';

KResult initialize_root_filesystem();

RefPtr<Inode> root_directory();

KResultOr<RefPtr<Inode>> resolve_path(StringView path);

KResultOr<RefPtr<Inode>> ensure_directory(StringView path, u32 permissions);

KResultOr<RefPtr<Inode>> create_file(StringView path, u32 permissions);

}  // namespace barry::fs
