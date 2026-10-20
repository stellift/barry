#pragma once

#include "kernel/base/expected.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/types.hpp"
#include "kernel/proc/process_address_space.hpp"

namespace barry::proc {

struct [[gnu::packed]] ElfIdentity {
    u8 magic[4];
    u8 object_class;
    u8 data_encoding;
    u8 header_version;
    u8 os_abi;
    u8 abi_version;
    u8 padding[7];
};

struct [[gnu::packed]] ElfHeader {
    ElfIdentity identity;
    u16 type;
    u16 machine;
    u32 object_version;
    u64 entry;
    u64 program_header_offset;
    u64 section_header_offset;
    u32 flags;
    u16 header_size;
    u16 program_header_entry_size;
    u16 program_header_count;
    u16 section_header_entry_size;
    u16 section_header_count;
    u16 section_name_index;
};

static_assert(sizeof(ElfHeader) == 64);

struct [[gnu::packed]] ElfProgramHeader {
    u32 type;
    u32 flags;
    u64 offset;
    u64 virtual_address;
    u64 physical_address;
    u64 file_size;
    u64 memory_size;
    u64 alignment;
};

static_assert(sizeof(ElfProgramHeader) == 56);

enum class ElfType : u16 {
    Executable = 2,
    SharedObject = 3,
};

enum class ElfSegmentType : u32 {
    Null = 0,
    Load = 1,
    Dynamic = 2,
    Interpreter = 3,
    Note = 4,
    ProgramHeaders = 6,
    ThreadLocalStorage = 7,
};

inline constexpr u32 SEGMENT_EXECUTABLE = 1u << 0;
inline constexpr u32 SEGMENT_WRITABLE = 1u << 1;
inline constexpr u32 SEGMENT_READABLE = 1u << 2;

struct LoadedImage {
    VirtAddr entry_point {};
    VirtAddr program_headers {};
    u64 program_header_count { 0 };
    u64 program_header_entry_size { 0 };
    VirtAddr highest_address {};
};

KResultOr<LoadedImage> load_static_executable(ProcessAddressSpace& space, ReadonlyBytes image);

}  // namespace barry::proc
