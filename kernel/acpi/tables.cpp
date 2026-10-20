#include "kernel/acpi/tables.hpp"

#include "kernel/base/assert.hpp"
#include "kernel/log.hpp"
#include "kernel/mm/direct_map.hpp"
#include "kernel/mm/kernel_address_space.hpp"

namespace barry::acpi {
namespace {

constexpr StringView ROOT_POINTER_SIGNATURE = "RSD PTR "_sv;
constexpr StringView MADT_SIGNATURE = "APIC"_sv;
constexpr u8 EXTENDED_REVISION = 2;

MultipleApicDescriptionTable const* g_madt = nullptr;

bool checksum_is_valid(ReadonlyBytes bytes) {
    u8 total = 0;
    for (u8 const value : bytes)
        total = static_cast<u8>(total + value);
    return total == 0;
}

KResultOr<TableHeader const*> map_table(PhysAddr address) {
    TRY(mm::map_device_memory(PhysRange { address, sizeof(TableHeader) }));
    auto const* header = mm::direct_map_pointer<TableHeader>(address);

    if (header->length < sizeof(TableHeader))
        return Error(Errno::EINVAL, "an ACPI table reports an impossible length");

    TRY(mm::map_device_memory(PhysRange { address, header->length }));

    ReadonlyBytes const bytes(reinterpret_cast<u8 const*>(header), header->length);
    if (!checksum_is_valid(bytes))
        return Error(Errno::EINVAL, "an ACPI table failed its checksum");

    return header;
}

KResultOr<RootPointer const*> map_root_pointer(PhysAddr address) {
    TRY(mm::map_device_memory(PhysRange { address, sizeof(RootPointer) }));
    auto const* pointer = mm::direct_map_pointer<RootPointer>(address);

    if (StringView(pointer->signature, 8) != ROOT_POINTER_SIGNATURE)
        return Error(Errno::EINVAL, "the ACPI root pointer signature does not match");

    ReadonlyBytes const legacy(reinterpret_cast<u8 const*>(pointer), 20);
    if (!checksum_is_valid(legacy))
        return Error(Errno::EINVAL, "the ACPI root pointer failed its checksum");

    return pointer;
}

PhysAddr entry_address(TableHeader const& root, usize index, bool extended) {
    auto const* payload = reinterpret_cast<u8 const*>(&root) + sizeof(TableHeader);

    if (extended) {
        u64 value = 0;
        __builtin_memcpy(&value, payload + index * sizeof(u64), sizeof(u64));
        return PhysAddr(value);
    }

    u32 value = 0;
    __builtin_memcpy(&value, payload + index * sizeof(u32), sizeof(u32));
    return PhysAddr(value);
}

usize entry_count(TableHeader const& root, bool extended) {
    usize const payload = root.length - sizeof(TableHeader);
    return payload / (extended ? sizeof(u64) : sizeof(u32));
}

KResult scan_root_table(PhysAddr root_address, bool extended) {
    auto const* root = TRY(map_table(root_address));
    usize const count = entry_count(*root, extended);

    for (usize index = 0; index < count; ++index) {
        PhysAddr const address = entry_address(*root, index, extended);
        if (address.is_null())
            continue;

        auto table = map_table(address);
        if (table.is_error())
            continue;

        if (table.value()->signature_view() == MADT_SIGNATURE) {
            g_madt = reinterpret_cast<MultipleApicDescriptionTable const*>(table.value());
            return {};
        }
    }

    return Error(Errno::ENOENT, "no multiple APIC description table was found");
}

bool processor_is_usable(u32 flags) {
    return (flags & PROCESSOR_ENABLED) != 0 || (flags & PROCESSOR_ONLINE_CAPABLE) != 0;
}

}  // namespace

KResult initialize(PhysAddr root_pointer) {
    if (g_madt != nullptr)
        return {};

    auto const* pointer = TRY(map_root_pointer(root_pointer));

    if (pointer->revision >= EXTENDED_REVISION && pointer->xsdt_address != 0) {
        auto extended = scan_root_table(PhysAddr(pointer->xsdt_address), true);
        if (extended.has_value())
            return {};
    }

    if (pointer->rsdt_address == 0)
        return Error(Errno::ENOENT, "the ACPI root pointer names no system table");

    return scan_root_table(PhysAddr(pointer->rsdt_address), false);
}

bool is_available() {
    return g_madt != nullptr;
}

Optional<PhysAddr> local_apic_address() {
    if (g_madt == nullptr)
        return nullopt;
    return PhysAddr(g_madt->local_apic_address);
}

usize enumerate_processors(Span<boot::ProcessorDescriptor> processors) {
    if (g_madt == nullptr || processors.is_empty())
        return 0;

    auto const* base = reinterpret_cast<u8 const*>(g_madt);
    u32 offset = sizeof(MultipleApicDescriptionTable);
    usize written = 0;

    while (offset + sizeof(ApicEntryHeader) <= g_madt->header.length) {
        auto const* entry = reinterpret_cast<ApicEntryHeader const*>(base + offset);
        if (entry->length < sizeof(ApicEntryHeader))
            break;

        if (written < processors.size()) {
            switch (static_cast<ApicEntryType>(entry->type)) {
            case ApicEntryType::LocalApic: {
                auto const* local = reinterpret_cast<LocalApicEntry const*>(entry);
                if (processor_is_usable(local->flags)) {
                    processors[written] = boot::ProcessorDescriptor {
                        static_cast<u32>(written), local->apic_id
                    };
                    ++written;
                }
                break;
            }
            case ApicEntryType::LocalX2Apic: {
                auto const* local = reinterpret_cast<LocalX2ApicEntry const*>(entry);
                if (processor_is_usable(local->flags)) {
                    processors[written] = boot::ProcessorDescriptor {
                        static_cast<u32>(written), local->apic_id
                    };
                    ++written;
                }
                break;
            }
            default:
                break;
            }
        }

        offset += entry->length;
    }

    return written;
}

}  // namespace barry::acpi
