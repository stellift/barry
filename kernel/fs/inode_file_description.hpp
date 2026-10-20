#pragma once

#include "kernel/fs/inode.hpp"
#include "kernel/fs/open_file_description.hpp"
#include "kernel/sync/spin_lock.hpp"

namespace barry::fs {

class InodeFileDescription final : public OpenFileDescription {
public:
    static KResultOr<RefPtr<OpenFileDescription>> create(RefPtr<Inode> inode);

    explicit InodeFileDescription(RefPtr<Inode> inode) : m_inode(move(inode)) {}

    KResultOr<usize> read(Bytes destination) override;
    KResultOr<usize> write(ReadonlyBytes source) override;
    KResultOr<u64> seek(i64 offset, SeekOrigin origin) override;

    RefPtr<Inode> inode() const override { return m_inode; }

private:
    RefPtr<Inode> m_inode;
    sync::Protected<u64> m_offset;
};

}  // namespace barry::fs
