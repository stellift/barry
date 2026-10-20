#include "kernel/fs/virtual_file_system.hpp"

#include "kernel/base/optional.hpp"

namespace barry::fs {
namespace {

Inode* g_root = nullptr;

class PathComponents {
public:
    explicit PathComponents(StringView path) : m_path(path) {}

    Optional<StringView> next() {
        while (m_offset < m_path.length() && m_path[m_offset] == PATH_SEPARATOR)
            ++m_offset;

        if (m_offset >= m_path.length())
            return nullopt;

        usize const start = m_offset;
        while (m_offset < m_path.length() && m_path[m_offset] != PATH_SEPARATOR)
            ++m_offset;

        return m_path.substring(start, m_offset - start);
    }

private:
    StringView m_path;
    usize m_offset { 0 };
};

KResult validate_path(StringView path) {
    if (path.is_empty() || path[0] != PATH_SEPARATOR)
        return Error(Errno::EINVAL, "only absolute paths are supported");
    if (path.length() > MAX_PATH_LENGTH)
        return Error(Errno::ENAMETOOLONG, "the path is too long");
    return {};
}

Optional<usize> last_separator(StringView path) {
    for (usize index = path.length(); index > 0; --index) {
        if (path[index - 1] == PATH_SEPARATOR)
            return index - 1;
    }
    return nullopt;
}

}  // namespace

KResult initialize_root_filesystem() {
    if (g_root != nullptr)
        return Error(Errno::EBUSY, "the root filesystem already exists");

    auto root = TRY(Inode::create_directory(0755));
    g_root = root.leak();
    return {};
}

RefPtr<Inode> root_directory() {
    if (g_root == nullptr)
        return nullptr;
    return RefPtr<Inode>(*g_root);
}

KResultOr<RefPtr<Inode>> resolve_path(StringView path) {
    TRY(validate_path(path));

    auto current = root_directory();
    if (current.is_null())
        return Error(Errno::ENODEV, "no root filesystem is mounted");

    PathComponents components(path);
    while (auto const component = components.next()) {
        if (!current->is_directory())
            return Error(Errno::ENOTDIR, "a path component is not a directory");
        current = TRY(current->lookup(component.value()));
    }

    return current;
}

KResultOr<RefPtr<Inode>> ensure_directory(StringView path, u32 permissions) {
    TRY(validate_path(path));

    auto current = root_directory();
    if (current.is_null())
        return Error(Errno::ENODEV, "no root filesystem is mounted");

    PathComponents components(path);
    while (auto const component = components.next()) {
        auto existing = current->lookup(component.value());

        if (existing.has_value()) {
            current = existing.release_value();
            if (!current->is_directory())
                return Error(Errno::ENOTDIR, "a path component is not a directory");
            continue;
        }

        auto created = TRY(Inode::create_directory(permissions));
        TRY(current->add_entry(component.value(), created));
        current = created;
    }

    return current;
}

KResultOr<RefPtr<Inode>> create_file(StringView path, u32 permissions) {
    TRY(validate_path(path));

    auto const separator = last_separator(path);
    if (!separator.has_value())
        return Error(Errno::EINVAL, "the path has no parent directory");

    StringView const parent_path
        = separator.value() == 0 ? StringView("/") : path.substring(0, separator.value());
    StringView const name = path.substring(separator.value() + 1);

    if (name.is_empty())
        return Error(Errno::EINVAL, "the path has no file name");

    auto parent = TRY(ensure_directory(parent_path, 0755));
    auto created = TRY(Inode::create_file(permissions));
    TRY(parent->add_entry(name, created));
    return created;
}

}  // namespace barry::fs
