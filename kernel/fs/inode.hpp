#pragma once

#include "kernel/base/array.hpp"
#include "kernel/base/byte_buffer.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/ref_ptr.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/sync/spin_lock.hpp"

namespace barry::fs {

enum class InodeType : u8 {
    Directory,
    File,
};

inline constexpr u32 MODE_DIRECTORY = 0040000;
inline constexpr u32 MODE_REGULAR_FILE = 0100000;
inline constexpr u32 MODE_PERMISSION_MASK = 07777;

class Inode : public RefCounted<Inode> {
public:
    static constexpr usize MAX_DIRECTORY_ENTRIES = 64;
    static constexpr usize MAX_NAME_LENGTH = 63;

    static KResultOr<RefPtr<Inode>> create_directory(u32 permissions);
    static KResultOr<RefPtr<Inode>> create_file(u32 permissions);

    Inode(InodeType type, u32 permissions, u64 identifier);

    InodeType type() const { return m_type; }
    bool is_directory() const { return m_type == InodeType::Directory; }
    bool is_file() const { return m_type == InodeType::File; }

    u64 identifier() const { return m_identifier; }
    u32 mode() const;
    u64 size() const;

    KResultOr<usize> read_at(u64 offset, Bytes destination) const;
    KResultOr<usize> write_at(u64 offset, ReadonlyBytes source);
    KResult replace_contents(ReadonlyBytes source);

    KResultOr<ByteBuffer> read_entire_file() const;

    KResultOr<RefPtr<Inode>> lookup(StringView name) const;
    KResult add_entry(StringView name, RefPtr<Inode> child);
    usize entry_count() const;

private:
    struct DirectoryEntry {
        Array<char, MAX_NAME_LENGTH + 1> name { {} };
        usize name_length { 0 };
        Inode* child { nullptr };

        StringView name_view() const { return StringView(name.data(), name_length); }
    };

    struct State {
        ByteBuffer contents;
        Array<DirectoryEntry, MAX_DIRECTORY_ENTRIES> entries { {} };
        usize entry_count { 0 };
    };

    InodeType m_type;
    u32 m_permissions;
    u64 m_identifier;
    mutable sync::Protected<State> m_state;
};

}  // namespace barry::fs
