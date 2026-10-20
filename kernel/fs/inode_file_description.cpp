#include "kernel/fs/inode_file_description.hpp"

namespace barry::fs {

KResultOr<RefPtr<OpenFileDescription>> InodeFileDescription::create(RefPtr<Inode> inode) {
    if (inode.is_null())
        return Error(Errno::EINVAL, "a description needs an inode");

    auto description = TRY(make_ref<InodeFileDescription>(move(inode)));
    return RefPtr<OpenFileDescription>(description);
}

KResultOr<usize> InodeFileDescription::read(Bytes destination) {
    auto guard = m_offset.lock();
    usize const count = TRY(m_inode->read_at(*guard, destination));
    *guard += count;
    return count;
}

KResultOr<usize> InodeFileDescription::write(ReadonlyBytes source) {
    auto guard = m_offset.lock();
    usize const count = TRY(m_inode->write_at(*guard, source));
    *guard += count;
    return count;
}

KResultOr<u64> InodeFileDescription::seek(i64 offset, SeekOrigin origin) {
    auto guard = m_offset.lock();

    i64 base = 0;
    switch (origin) {
    case SeekOrigin::Start:
        base = 0;
        break;
    case SeekOrigin::Current:
        base = static_cast<i64>(*guard);
        break;
    case SeekOrigin::End:
        base = static_cast<i64>(m_inode->size());
        break;
    }

    i64 const target = base + offset;
    if (target < 0)
        return Error(Errno::EINVAL, "cannot seek before the start of a file");

    *guard = static_cast<u64>(target);
    return *guard;
}

}  // namespace barry::fs
