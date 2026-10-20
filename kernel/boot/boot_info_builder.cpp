#include "kernel/boot/boot_info_builder.hpp"

namespace barry::boot {

bool BootInfoBuilder::add_memory_region(MemoryRegion region) {
    if (region.length == 0)
        return false;
    if (m_memory_region_count >= MAX_MEMORY_REGIONS) {
        m_dropped_entries = true;
        return false;
    }
    m_memory_regions[m_memory_region_count++] = region;
    return true;
}

bool BootInfoBuilder::add_module(BootModule module) {
    if (m_module_count >= MAX_BOOT_MODULES) {
        m_dropped_entries = true;
        return false;
    }
    m_modules[m_module_count++] = module;
    return true;
}

bool BootInfoBuilder::add_processor(ProcessorDescriptor processor) {
    if (m_processor_count >= MAX_PROCESSORS) {
        m_dropped_entries = true;
        return false;
    }
    m_processors[m_processor_count++] = processor;
    return true;
}

BootInfo const& BootInfoBuilder::finish() {
    m_info.memory_map = Span<MemoryRegion const>(m_memory_regions.data(), m_memory_region_count);
    m_info.modules = Span<BootModule const>(m_modules.data(), m_module_count);
    m_info.processors = Span<ProcessorDescriptor const>(m_processors.data(), m_processor_count);
    return m_info;
}

}  // namespace barry::boot
