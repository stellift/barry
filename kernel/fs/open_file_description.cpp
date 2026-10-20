#include "kernel/fs/open_file_description.hpp"

#include "kernel/fs/inode.hpp"

namespace barry::fs {

KResultOr<u64> OpenFileDescription::seek(i64, SeekOrigin) {
    return Error(Errno::ESPIPE, "this description cannot seek");
}

RefPtr<Inode> OpenFileDescription::inode() const {
    return nullptr;
}

}  // namespace barry::fs
