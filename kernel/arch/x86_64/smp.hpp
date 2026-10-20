#pragma once

#include "kernel/arch/x86_64/descriptor_tables.hpp"
#include "kernel/base/addr.hpp"
#include "kernel/base/expected.hpp"
#include "kernel/base/types.hpp"
#include "kernel/boot/boot_info.hpp"

namespace barry::arch {

struct SmpStatistics {
    u32 reported_processors { 0 };
    u32 started_processors { 0 };
    u32 failed_processors { 0 };

    bool every_processor_started() const {
        return failed_processors == 0 && started_processors == reported_processors;
    }
};

KResult start_application_processors(Span<boot::ProcessorDescriptor const> processors,
                                    u32 boot_processor_hardware_id);

SmpStatistics smp_statistics();

u32 online_processor_count();

}  // namespace barry::arch

extern "C" [[noreturn]] void barry_ap_entry(barry::u64 processor_index);
