#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_FAULTMANAGER_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_FAULTMANAGER_H_

#include "common/abi.h"
#include "graphics/host_gpu/renderer/cache/streamBuffer.h"

#include <array>
#include <cstdint>

namespace Libs::Graphics {

class BufferCache;

class FaultManager {
        static constexpr size_t MaxPendingFaults = 8;

public:
        FaultManager(GraphicContext& graphics, CommandScheduler& scheduler, BufferCache& buffer_cache);
        ~FaultManager();
        KYTY_CLASS_NO_COPY(FaultManager);

        [[nodiscard]] Buffer* GetFaultBuffer() noexcept { return &m_fault_buffer; }
        void                  ProcessFaultBuffer();

        // Kyty-018: Diagnostic accessors for fault statistics.
        // Updated during ProcessFaultBuffer's deferred operation.
        struct FaultStats {
                uint32_t total_faults    = 0; // Total faults processed since init
                uint32_t total_mapped     = 0; // Faults that found a backing buffer
                uint32_t total_unmapped   = 0; // Faults with no backing buffer
                uint32_t last_batch_count = 0; // Faults in the most recent batch
        };
        [[nodiscard]] const FaultStats& GetFaultStats() const noexcept { return m_fault_stats; }

private:
        GraphicContext&                            m_graphics;
        CommandScheduler&                          m_scheduler;
        BufferCache&                               m_buffer_cache;
        Buffer                                     m_fault_buffer;
        Buffer                                     m_download_buffer;
        std::array<uint64_t, MaxPendingFaults>      m_fault_areas {};
        uint32_t                                   m_current_area = 0;
        vk::DescriptorSetLayout                    m_fault_process_desc_layout = nullptr;
        vk::Pipeline                               m_fault_process_pipeline = nullptr;
        vk::PipelineLayout                         m_fault_process_pipeline_layout = nullptr;
        FaultStats                                 m_fault_stats {};
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_CACHE_FAULTMANAGER_H_
