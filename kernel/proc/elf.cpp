#include "kernel/proc/elf.hpp"

#include "kernel/base/bytes.hpp"
#include "kernel/base/utility.hpp"

namespace barry::proc {
namespace {

constexpr u8 ELF_MAGIC[4] = { 0x7f, 'E', 'L', 'F' };
constexpr u8 ELF_CLASS_64BIT = 2;
constexpr u8 ELF_DATA_LITTLE_ENDIAN = 1;
constexpr u8 ELF_HEADER_VERSION = 1;
constexpr u16 ELF_MACHINE_X86_64 = 62;

bool magic_matches(ElfIdentity const& identity) {
    for (usize index = 0; index < 4; ++index) {
        if (identity.magic[index] != ELF_MAGIC[index])
            return false;
    }
    return true;
}

KResult validate_header(ElfHeader const& header, usize image_size) {
    if (!magic_matches(header.identity))
        return Error(Errno::ENOEXEC, "the image is not an ELF file");
    if (header.identity.object_class != ELF_CLASS_64BIT)
        return Error(Errno::ENOEXEC, "only 64-bit ELF images are supported");
    if (header.identity.data_encoding != ELF_DATA_LITTLE_ENDIAN)
        return Error(Errno::ENOEXEC, "only little-endian ELF images are supported");
    if (header.identity.header_version != ELF_HEADER_VERSION)
        return Error(Errno::ENOEXEC, "unsupported ELF header version");
    if (header.machine != ELF_MACHINE_X86_64)
        return Error(Errno::ENOEXEC, "the image targets another machine");
    if (static_cast<ElfType>(header.type) != ElfType::Executable)
        return Error(Errno::ENOEXEC, "only non-relocatable executables are supported");
    if (header.program_header_entry_size != sizeof(ElfProgramHeader))
        return Error(Errno::ENOEXEC, "unexpected program header size");
    if (header.program_header_count == 0)
        return Error(Errno::ENOEXEC, "the image has no program headers");

    u64 const table_bytes = u64 { header.program_header_count } * header.program_header_entry_size;
    if (header.program_header_offset + table_bytes > image_size)
        return Error(Errno::ENOEXEC, "the program header table is outside the image");

    return {};
}

mm::PageFlags page_flags_for(u32 segment_flags) {
    mm::PageFlags flags = mm::PageFlags::Present | mm::PageFlags::UserAccessible;
    if ((segment_flags & SEGMENT_WRITABLE) != 0)
        flags |= mm::PageFlags::Writable;
    if ((segment_flags & SEGMENT_EXECUTABLE) == 0)
        flags |= mm::PageFlags::NoExecute;
    return flags;
}

KResult load_segment(ProcessAddressSpace& space, ElfProgramHeader const& segment,
                     ReadonlyBytes image) {
    if (segment.memory_size == 0)
        return {};
    if (segment.file_size > segment.memory_size)
        return Error(Errno::ENOEXEC, "a segment claims more file bytes than memory bytes");
    if (segment.offset + segment.file_size > image.size())
        return Error(Errno::ENOEXEC, "a segment extends past the end of the image");

    VirtAddr const start = VirtAddr(segment.virtual_address);
    VirtAddr const first_page = start.page_align_down();
    VirtAddr const last_page = start.offset(segment.memory_size).page_align_up();
    VirtRange const range { first_page, first_page.distance_to(last_page) };

    if (!is_user_range(range))
        return Error(Errno::ENOEXEC, "a segment lies outside user address space");

    TRY(space.map_anonymous(range, page_flags_for(segment.flags)));

    if (segment.file_size != 0) {
        auto const contents = image.slice(segment.offset, segment.file_size);
        TRY(space.copy_in(start, contents));
    }

    return {};
}

}  // namespace

KResultOr<LoadedImage> load_static_executable(ProcessAddressSpace& space, ReadonlyBytes image) {
    if (image.size() < sizeof(ElfHeader))
        return Error(Errno::ENOEXEC, "the image is smaller than an ELF header");

    auto const& header = *reinterpret_cast<ElfHeader const*>(image.data());
    TRY(validate_header(header, image.size()));

    LoadedImage loaded {};
    loaded.entry_point = VirtAddr(header.entry);
    loaded.program_header_count = header.program_header_count;
    loaded.program_header_entry_size = header.program_header_entry_size;

    for (u16 index = 0; index < header.program_header_count; ++index) {
        u64 const offset = header.program_header_offset
                           + u64 { index } * header.program_header_entry_size;
        auto const& segment = *reinterpret_cast<ElfProgramHeader const*>(image.data() + offset);

        auto const type = static_cast<ElfSegmentType>(segment.type);
        if (type == ElfSegmentType::Interpreter)
            return Error(Errno::ENOEXEC, "dynamically linked images are not supported yet");
        if (type == ElfSegmentType::ProgramHeaders)
            loaded.program_headers = VirtAddr(segment.virtual_address);
        if (type != ElfSegmentType::Load)
            continue;

        TRY(load_segment(space, segment, image));

        VirtAddr const segment_end
            = VirtAddr(segment.virtual_address).offset(segment.memory_size).page_align_up();
        if (segment_end > loaded.highest_address)
            loaded.highest_address = segment_end;
    }

    if (loaded.highest_address.is_null())
        return Error(Errno::ENOEXEC, "the image has no loadable segments");
    if (!is_user_range(VirtRange { loaded.entry_point, 1 }))
        return Error(Errno::ENOEXEC, "the entry point is outside user address space");

    return loaded;
}

}  // namespace barry::proc
