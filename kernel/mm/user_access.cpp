#include "kernel/mm/user_access.hpp"

#include "kernel/base/bytes.hpp"
#include "kernel/base/utility.hpp"
#include "kernel/proc/user_layout.hpp"

namespace barry::mm {
namespace {

PageFlags required_flags_for(UserAccess access) {
    PageFlags flags = PageFlags::Present | PageFlags::UserAccessible;
    if (access == UserAccess::Write)
        flags |= PageFlags::Writable;
    return flags;
}

u64 bytes_until_next_page(VirtAddr address) {
    return PAGE_SIZE - address.page_offset();
}

}  // namespace

KResult validate_user_range(AddressSpace const& space, VirtRange range, UserAccess access) {
    if (!proc::is_user_range(range))
        return Error(Errno::EFAULT, "the range is outside user address space");

    PageFlags const required = required_flags_for(access);
    VirtAddr page = range.base.page_align_down();
    VirtAddr const limit = range.end();

    while (page < limit) {
        auto const entry = space.lookup(page);
        if (!entry.has_value())
            return Error(Errno::EFAULT, "a page in the range is not mapped");
        if (!has_all_flags(entry->flags(), required))
            return Error(Errno::EFAULT, "a page in the range lacks the required permission");

        page = page.offset(PAGE_SIZE);
    }

    return {};
}

KResult copy_from_user(AddressSpace const& space, Bytes destination, UserPtr<u8 const> source) {
    if (destination.is_empty())
        return {};

    VirtRange const range { source.address(), destination.size() };
    TRY(validate_user_range(space, range, UserAccess::Read));

    auto const* bytes = source.address().as_ptr<u8 const>();
    copy_bytes(destination, ReadonlyBytes(bytes, destination.size()));
    return {};
}

KResult copy_to_user(AddressSpace const& space, UserPtr<u8> destination, ReadonlyBytes source) {
    if (source.is_empty())
        return {};

    VirtRange const range { destination.address(), source.size() };
    TRY(validate_user_range(space, range, UserAccess::Write));

    auto* bytes = destination.address().as_ptr<u8>();
    copy_bytes(Bytes(bytes, source.size()), source);
    return {};
}

KResultOr<usize> copy_cstring_from_user(AddressSpace const& space, Span<char> destination,
                                        UserPtr<char const> source) {
    if (destination.size() < 2)
        return Error(Errno::EINVAL, "the destination buffer is too small for a string");

    usize written = 0;
    VirtAddr cursor = source.address();

    while (written + 1 < destination.size()) {
        u64 const chunk = min<u64>(bytes_until_next_page(cursor),
                                   destination.size() - 1 - written);

        TRY(validate_user_range(space, VirtRange { cursor, chunk }, UserAccess::Read));

        auto const* bytes = cursor.as_ptr<char const>();
        for (u64 index = 0; index < chunk; ++index) {
            destination[written] = bytes[index];
            if (bytes[index] == '\0')
                return written;
            ++written;
        }

        cursor = cursor.offset(chunk);
    }

    destination[written] = '\0';
    return Error(Errno::ENAMETOOLONG, "the user string did not fit in the buffer");
}

}  // namespace barry::mm
