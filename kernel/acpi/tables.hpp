#pragma once

#include "kernel/base/addr.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/optional.hpp"
#include "kernel/base/span.hpp"
#include "kernel/base/string_view.hpp"
#include "kernel/base/types.hpp"
#include "kernel/boot/boot_info.hpp"

namespace barry::acpi {

struct [[gnu::packed]] TableHeader {
    char signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;

    StringView signature_view() const { return StringView(signature, 4); }
};

static_assert(sizeof(TableHeader) == 36);

struct [[gnu::packed]] RootPointer {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_address;
    u32 length;
    u64 xsdt_address;
    u8 extended_checksum;
    u8 reserved[3];
};

static_assert(sizeof(RootPointer) == 36);

struct [[gnu::packed]] MultipleApicDescriptionTable {
    TableHeader header;
    u32 local_apic_address;
    u32 flags;
};

struct [[gnu::packed]] ApicEntryHeader {
    u8 type;
    u8 length;
};

enum class ApicEntryType : u8 {
    LocalApic = 0,
    IoApic = 1,
    InterruptSourceOverride = 2,
    LocalApicNonMaskable = 4,
    LocalApicAddressOverride = 5,
    LocalX2Apic = 9,
};

struct [[gnu::packed]] LocalApicEntry {
    ApicEntryHeader header;
    u8 processor_id;
    u8 apic_id;
    u32 flags;
};

struct [[gnu::packed]] LocalX2ApicEntry {
    ApicEntryHeader header;
    u16 reserved;
    u32 apic_id;
    u32 flags;
    u32 processor_id;
};

inline constexpr u32 PROCESSOR_ENABLED = 1u << 0;
inline constexpr u32 PROCESSOR_ONLINE_CAPABLE = 1u << 1;

KResult initialize(PhysAddr root_pointer);

bool is_available();

Optional<PhysAddr> local_apic_address();

usize enumerate_processors(Span<boot::ProcessorDescriptor> processors);

}  // namespace barry::acpi
