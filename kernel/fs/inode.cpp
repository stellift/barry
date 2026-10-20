#include "kernel/fs/inode.hpp"

#include "kernel/base/atomic.hpp"
#include "kernel/base/bytes.hpp"

namespace barry::fs {
namespace {

Atomic<u64> g_next_identifier { 1 };

}  // namespace

Inode::Inode(InodeType type, u32 permissions, u64 identifier)
    : m_type(type), m_permissions(permissions & MODE_PERMISSION_MASK),
      m_identifier(identifier) {}

KResultOr<RefPtr<Inode>> Inode::create_directory(u32 permissions) {
    return TRY(make_ref<Inode>(InodeType::Directory, permissions,
                               g_next_identifier.fetch_add(1, Order::Relaxed)));
}

KResultOr<RefPtr<Inode>> Inode::create_file(u32 permissions) {
    return TRY(make_ref<Inode>(InodeType::File, permissions,
                               g_next_identifier.fetch_add(1, Order::Relaxed)));
}

u32 Inode::mode() const {
    u32 const kind = is_directory() ? MODE_DIRECTORY : MODE_REGULAR_FILE;
    return kind | m_permissions;
}

u64 Inode::size() const {
    auto guard = m_state.lock();
    return guard->contents.size();
}

KResultOr<usize> Inode::read_at(u64 offset, Bytes destination) const {
    if (!is_file())
        return Error(Errno::EISDIR, "cannot read a directory as a file");

    auto guard = m_state.lock();
    auto const contents = guard->contents.bytes();

    if (offset >= contents.size())
        return usize { 0 };

    usize const available = contents.size() - offset;
    usize const count = min(available, destination.size());
    copy_bytes(destination.first(count), contents.slice(offset, count));
    return count;
}

KResultOr<usize> Inode::write_at(u64 offset, ReadonlyBytes source) {
    if (!is_file())
        return Error(Errno::EISDIR, "cannot write to a directory as a file");
    if (source.is_empty())
        return usize { 0 };

    auto guard = m_state.lock();

    usize const required = offset + source.size();
    if (required > guard->contents.size())
        TRY(guard->contents.resize(required));

    copy_bytes(guard->contents.bytes().slice(offset, source.size()), source);
    return source.size();
}

KResult Inode::replace_contents(ReadonlyBytes source) {
    if (!is_file())
        return Error(Errno::EISDIR, "cannot replace the contents of a directory");

    auto guard = m_state.lock();
    TRY(guard->contents.resize(source.size()));
    if (!source.is_empty())
        copy_bytes(guard->contents.bytes(), source);
    return {};
}

KResultOr<ByteBuffer> Inode::read_entire_file() const {
    if (!is_file())
        return Error(Errno::EISDIR, "cannot read a directory as a file");

    auto guard = m_state.lock();
    auto copy = TRY(ByteBuffer::create_zeroed(guard->contents.size()));
    if (!copy.is_empty())
        copy_bytes(copy.bytes(), guard->contents.bytes());
    return copy;
}

KResultOr<RefPtr<Inode>> Inode::lookup(StringView name) const {
    if (!is_directory())
        return Error(Errno::ENOTDIR, "only directories can be searched");

    auto guard = m_state.lock();

    for (usize index = 0; index < guard->entry_count; ++index) {
        auto const& entry = guard->entries[index];
        if (entry.name_view() == name && entry.child != nullptr)
            return RefPtr<Inode>(*entry.child);
    }

    return Error(Errno::ENOENT, "no such directory entry");
}

KResult Inode::add_entry(StringView name, RefPtr<Inode> child) {
    if (!is_directory())
        return Error(Errno::ENOTDIR, "only directories hold entries");
    if (name.is_empty() || name.length() > MAX_NAME_LENGTH)
        return Error(Errno::ENAMETOOLONG, "the entry name does not fit");
    if (child.is_null())
        return Error(Errno::EINVAL, "cannot link a null inode");

    auto guard = m_state.lock();

    for (usize index = 0; index < guard->entry_count; ++index) {
        if (guard->entries[index].name_view() == name)
            return Error(Errno::EEXIST, "that name already exists");
    }

    if (guard->entry_count >= MAX_DIRECTORY_ENTRIES)
        return Error(Errno::ENOSPC, "the directory is full");

    auto& entry = guard->entries[guard->entry_count];
    copy_bytes(as_writable_bytes(entry.name.span().first(name.length())), name.bytes());
    entry.name_length = name.length();
    entry.child = child.leak();

    ++guard->entry_count;
    return {};
}

usize Inode::entry_count() const {
    auto guard = m_state.lock();
    return guard->entry_count;
}

}  // namespace barry::fs
