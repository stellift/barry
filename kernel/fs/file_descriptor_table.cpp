#include "kernel/fs/file_descriptor_table.hpp"

namespace barry::fs {

KResultOr<FileDescriptor> FileDescriptorTable::insert(RefPtr<OpenFileDescription> description) {
    if (description.is_null())
        return Error(Errno::EINVAL, "cannot install a null description");

    for (usize index = 0; index < MAX_DESCRIPTORS; ++index) {
        if (!m_descriptions[index].is_null())
            continue;
        m_descriptions[index] = move(description);
        return static_cast<FileDescriptor>(index);
    }

    return Error(Errno::EMFILE, "the descriptor table is full");
}

KResult FileDescriptorTable::insert_at(FileDescriptor descriptor,
                                       RefPtr<OpenFileDescription> description) {
    if (!is_in_range(descriptor))
        return Error(Errno::EBADF, "descriptor number out of range");
    if (description.is_null())
        return Error(Errno::EINVAL, "cannot install a null description");

    m_descriptions[static_cast<usize>(descriptor)] = move(description);
    return {};
}

KResultOr<RefPtr<OpenFileDescription>> FileDescriptorTable::lookup(
    FileDescriptor descriptor) const {
    if (!is_in_range(descriptor))
        return Error(Errno::EBADF, "descriptor number out of range");

    auto const& slot = m_descriptions[static_cast<usize>(descriptor)];
    if (slot.is_null())
        return Error(Errno::EBADF, "no such descriptor");

    return slot;
}

KResult FileDescriptorTable::close(FileDescriptor descriptor) {
    if (!is_in_range(descriptor))
        return Error(Errno::EBADF, "descriptor number out of range");

    auto& slot = m_descriptions[static_cast<usize>(descriptor)];
    if (slot.is_null())
        return Error(Errno::EBADF, "no such descriptor");

    slot = nullptr;
    return {};
}

usize FileDescriptorTable::open_count() const {
    usize count = 0;
    for (usize index = 0; index < MAX_DESCRIPTORS; ++index) {
        if (!m_descriptions[index].is_null())
            ++count;
    }
    return count;
}

}  // namespace barry::fs
