#include "kernel/boot/initramfs.hpp"

#include "kernel/base/array.hpp"
#include "kernel/base/fixed_string_buffer.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/utility.hpp"
#include "kernel/fs/virtual_file_system.hpp"

extern "C" barry::u8 const barry_embedded_initramfs_start[];
extern "C" barry::u8 const barry_embedded_initramfs_end[];

namespace barry::boot {
namespace {

constexpr StringView NEW_ASCII_MAGIC = "070701"_sv;
constexpr StringView TRAILER_NAME = "TRAILER!!!"_sv;
constexpr usize HEADER_FIELD_WIDTH = 8;
constexpr u64 RECORD_ALIGNMENT = 4;
constexpr u32 FILE_TYPE_MASK = 0170000;
constexpr u32 FILE_TYPE_DIRECTORY = 0040000;
constexpr u32 FILE_TYPE_REGULAR = 0100000;
constexpr usize MAX_ENTRY_PATH = 192;

struct [[gnu::packed]] CpioHeader {
    char magic[6];
    char inode[HEADER_FIELD_WIDTH];
    char mode[HEADER_FIELD_WIDTH];
    char user_id[HEADER_FIELD_WIDTH];
    char group_id[HEADER_FIELD_WIDTH];
    char link_count[HEADER_FIELD_WIDTH];
    char modification_time[HEADER_FIELD_WIDTH];
    char file_size[HEADER_FIELD_WIDTH];
    char device_major[HEADER_FIELD_WIDTH];
    char device_minor[HEADER_FIELD_WIDTH];
    char raw_device_major[HEADER_FIELD_WIDTH];
    char raw_device_minor[HEADER_FIELD_WIDTH];
    char name_size[HEADER_FIELD_WIDTH];
    char checksum[HEADER_FIELD_WIDTH];
};

static_assert(sizeof(CpioHeader) == 110);

Optional<u32> parse_hex_field(char const (&field)[HEADER_FIELD_WIDTH]) {
    u32 value = 0;

    for (usize index = 0; index < HEADER_FIELD_WIDTH; ++index) {
        char const digit = field[index];
        u32 nibble = 0;

        if (digit >= '0' && digit <= '9')
            nibble = static_cast<u32>(digit - '0');
        else if (digit >= 'a' && digit <= 'f')
            nibble = static_cast<u32>(digit - 'a') + 10;
        else if (digit >= 'A' && digit <= 'F')
            nibble = static_cast<u32>(digit - 'A') + 10;
        else
            return nullopt;

        value = (value << 4) | nibble;
    }

    return value;
}

KResult install_entry(StringView name, u32 mode, ReadonlyBytes contents,
                      InitramfsSummary& summary) {
    FixedStringBuffer<MAX_ENTRY_PATH> path;
    path.write("/"_sv);
    path.write(name);
    if (path.is_truncated())
        return Error(Errno::ENAMETOOLONG, "an archive entry path is too long");

    u32 const permissions = mode & 07777;

    switch (mode & FILE_TYPE_MASK) {
    case FILE_TYPE_DIRECTORY:
        (void)TRY(fs::ensure_directory(path.view(), permissions));
        ++summary.directories;
        return {};
    case FILE_TYPE_REGULAR: {
        auto file = TRY(fs::create_file(path.view(), permissions));
        TRY(file->replace_contents(contents));
        ++summary.files;
        summary.bytes += contents.size();
        return {};
    }
    default:
        return {};
    }
}

}  // namespace

KResultOr<InitramfsSummary> populate_from_cpio_archive(ReadonlyBytes archive) {
    InitramfsSummary summary {};
    u64 offset = 0;

    while (offset + sizeof(CpioHeader) <= archive.size()) {
        auto const& header = *reinterpret_cast<CpioHeader const*>(archive.data() + offset);

        if (StringView(header.magic, 6) != NEW_ASCII_MAGIC)
            return Error(Errno::EINVAL, "the archive is not in new ASCII cpio format");

        auto const name_size = parse_hex_field(header.name_size);
        auto const file_size = parse_hex_field(header.file_size);
        auto const mode = parse_hex_field(header.mode);

        if (!name_size.has_value() || !file_size.has_value() || !mode.has_value())
            return Error(Errno::EINVAL, "an archive header field is malformed");
        if (name_size.value() == 0)
            return Error(Errno::EINVAL, "an archive entry has no name");

        u64 const name_offset = offset + sizeof(CpioHeader);
        if (name_offset + name_size.value() > archive.size())
            return Error(Errno::EINVAL, "an archive entry name is truncated");

        StringView const name(reinterpret_cast<char const*>(archive.data() + name_offset),
                              name_size.value() - 1);

        if (name == TRAILER_NAME)
            return summary;

        u64 const data_offset
            = align_up<u64>(name_offset + name_size.value(), RECORD_ALIGNMENT);
        if (data_offset + file_size.value() > archive.size())
            return Error(Errno::EINVAL, "an archive entry payload is truncated");

        auto const contents = archive.slice(data_offset, file_size.value());
        TRY(install_entry(name, mode.value(), contents, summary));

        offset = align_up<u64>(data_offset + file_size.value(), RECORD_ALIGNMENT);
    }

    return Error(Errno::EINVAL, "the archive ended without a trailer record");
}

ReadonlyBytes embedded_initramfs() {
    auto const size
        = static_cast<usize>(barry_embedded_initramfs_end - barry_embedded_initramfs_start);
    return ReadonlyBytes(barry_embedded_initramfs_start, size);
}

}  // namespace barry::boot
