module;

#include "RHI.Vulkan.hpp"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>
#include <expected>

export module RHI:Profiler;

import :Device;

export namespace RHI
{
    // Lightweight GPU timestamp query system.
    // - Per-frame ring buffer (frames-in-flight)
    // - Non-blocking resolve (availability bit)
    // - Designed to be called from the render submission path and/or render graph.

    enum class GpuTimestampError
    {
        NotReady,
        DeviceLost,
        InvalidState
    };

    struct GpuTimestampScope
    {
        uint32_t NameHash = 0;
        uint32_t BeginQuery = 0;
        uint32_t EndQuery = 0;
    };

    struct GpuTimestampFrame
    {
        uint64_t FrameNumber = 0;
        uint32_t ScopeCount = 0;
        uint64_t GpuFrameTimeNs = 0;
        // Flat list of per-scope durations in ns (same order as scopes registered this frame)
        std::vector<uint64_t> ScopeDurationsNs{};
    };

    class GpuProfiler
    {
    public:
        explicit GpuProfiler(std::shared_ptr<VulkanDevice> device);
        ~GpuProfiler();

        GpuProfiler(const GpuProfiler&) = delete;
        GpuProfiler& operator=(const GpuProfiler&) = delete;

        // Call once per CPU frame, after you've determined the current frame index.
        // Typically in SimpleRenderer::BeginFrame() after waiting for the in-flight fence.
        void BeginFrame(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t maxScopesEstimate = 256);

        // Optional: write whole-frame timestamps.
        void WriteFrameStart(VkCommandBuffer cmd);
        void WriteFrameEnd(VkCommandBuffer cmd);

        // Register a named scope. Returns an index you can use to write begin/end.
        // NOTE: currently expects scopes to be recorded sequentially within the same command buffer.
        uint32_t BeginScope(std::string_view name);

        void WriteScopeBegin(VkCommandBuffer cmd, uint32_t scopeIndex, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        void WriteScopeEnd(VkCommandBuffer cmd, uint32_t scopeIndex, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);

        // Resolve results from an older frame (typically frameIndex from N frames ago.
        // Non-blocking by default. If results aren't ready, returns unexpected(NotReady).
        [[nodiscard]] std::expected<GpuTimestampFrame, GpuTimestampError> Resolve(uint32_t frameIndex) const;

        [[nodiscard]] bool IsSupported() const { return m_Supported; }

    private:
        struct FrameState
        {
            uint64_t FrameNumber = 0;
            uint32_t QueryBase = 0;
            uint32_t QueryCount = 0;

            uint32_t FrameStartQuery = ~0u;
            uint32_t FrameEndQuery = ~0u;

            std::vector<GpuTimestampScope> Scopes{};
        };

        void EnsurePoolCapacity(uint32_t requiredQueryCount);

        std::shared_ptr<VulkanDevice> m_Device;
        VkQueryPool m_QueryPool = VK_NULL_HANDLE;

        uint32_t m_MaxQueries = 0;
        double m_TimestampPeriodNs = 0.0;
        bool m_Supported = false;

        static constexpr uint32_t kFramesInFlight = VulkanDevice::GetFramesInFlight();
        FrameState m_Frames[kFramesInFlight]{};
    };
}

