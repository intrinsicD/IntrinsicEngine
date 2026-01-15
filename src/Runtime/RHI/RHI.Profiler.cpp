module;

#include "RHI.Vulkan.hpp"

#include <array>
#include <limits>
#include <memory>
#include <expected>
#include <vector>

module RHI:Profiler.Impl;
import :Profiler;

namespace RHI
{
    static uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + (a - 1u)) & ~(a - 1u); }

    GpuProfiler::GpuProfiler(std::shared_ptr<VulkanDevice> device)
        : m_Device(std::move(device))
    {
        if (!m_Device || !m_Device->IsValid())
        {
            m_Supported = false;
            return;
        }

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_Device->GetPhysicalDevice(), &props);

        // timestampPeriod is in nanoseconds per tick.
        m_TimestampPeriodNs = static_cast<double>(props.limits.timestampPeriod);

        // If timestampPeriod == 0, timestamps are effectively unusable.
        m_Supported = (m_TimestampPeriodNs > 0.0);

        // Create a small initial pool. Capacity will grow as needed.
        EnsurePoolCapacity(1024);
    }

    GpuProfiler::~GpuProfiler()
    {
        if (m_QueryPool != VK_NULL_HANDLE && m_Device)
        {
            vkDestroyQueryPool(m_Device->GetLogicalDevice(), m_QueryPool, nullptr);
        }
        m_QueryPool = VK_NULL_HANDLE;
    }

    void GpuProfiler::EnsurePoolCapacity(uint32_t requiredQueryCount)
    {
        if (!m_Supported) return;
        if (requiredQueryCount <= m_MaxQueries && m_QueryPool != VK_NULL_HANDLE) return;

        uint32_t newMax = std::max(requiredQueryCount, m_MaxQueries);
        newMax = std::max(newMax, 1024u);
        newMax = AlignUp(newMax, 256u);

        if (m_QueryPool != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(m_Device->GetLogicalDevice(), m_QueryPool, nullptr);
            m_QueryPool = VK_NULL_HANDLE;
        }

        VkQueryPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        info.queryCount = newMax;
        VK_CHECK(vkCreateQueryPool(m_Device->GetLogicalDevice(), &info, nullptr, &m_QueryPool));

        m_MaxQueries = newMax;
    }

    void GpuProfiler::BeginFrame(VkCommandBuffer cmd, uint32_t frameIndex, uint32_t maxScopesEstimate)
    {
        if (!m_Supported || m_QueryPool == VK_NULL_HANDLE) return;

        FrameState& fs = m_Frames[frameIndex % kFramesInFlight];
        fs.FrameNumber = m_Device->GetGlobalFrameNumber();
        fs.Scopes.clear();

        // Layout:
        // [frameStart, frameEnd, scope0_begin, scope0_end, scope1_begin, scope1_end, ...]
        const uint32_t queryCount = 2u + (2u * maxScopesEstimate);
        EnsurePoolCapacity(queryCount);

        fs.QueryBase = 0;
        fs.QueryCount = queryCount;
        fs.FrameStartQuery = fs.QueryBase + 0;
        fs.FrameEndQuery = fs.QueryBase + 1;

        // Reset the range before writing any timestamps.
        vkCmdResetQueryPool(cmd, m_QueryPool, fs.QueryBase, fs.QueryCount);
    }

    void GpuProfiler::WriteFrameStart(VkCommandBuffer cmd)
    {
        if (!m_Supported || m_QueryPool == VK_NULL_HANDLE) return;
        const uint32_t frameIndex = m_Device->GetCurrentFrameIndex();
        FrameState& fs = m_Frames[frameIndex % kFramesInFlight];
        if (fs.FrameStartQuery == ~0u) return;

        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, m_QueryPool, fs.FrameStartQuery);
    }

    void GpuProfiler::WriteFrameEnd(VkCommandBuffer cmd)
    {
        if (!m_Supported || m_QueryPool == VK_NULL_HANDLE) return;
        const uint32_t frameIndex = m_Device->GetCurrentFrameIndex();
        FrameState& fs = m_Frames[frameIndex % kFramesInFlight];
        if (fs.FrameEndQuery == ~0u) return;

        vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, m_QueryPool, fs.FrameEndQuery);
    }

    uint32_t GpuProfiler::BeginScope(std::string_view name)
    {
        if (!m_Supported || m_QueryPool == VK_NULL_HANDLE) return 0;

        const uint32_t frameIndex = m_Device->GetCurrentFrameIndex();
        FrameState& fs = m_Frames[frameIndex % kFramesInFlight];

        const uint32_t scopeIndex = static_cast<uint32_t>(fs.Scopes.size());

        const uint32_t beginQuery = fs.QueryBase + 2u + scopeIndex * 2u + 0u;
        const uint32_t endQuery = fs.QueryBase + 2u + scopeIndex * 2u + 1u;

        fs.Scopes.push_back(GpuTimestampScope{
            .NameHash = Core::Telemetry::HashString(name.data()),
            .BeginQuery = beginQuery,
            .EndQuery = endQuery,
        });

        return scopeIndex;
    }

    void GpuProfiler::WriteScopeBegin(VkCommandBuffer cmd, uint32_t scopeIndex, VkPipelineStageFlags2 stage)
    {
        if (!m_Supported || m_QueryPool == VK_NULL_HANDLE) return;
        const uint32_t frameIndex = m_Device->GetCurrentFrameIndex();
        FrameState& fs = m_Frames[frameIndex % kFramesInFlight];
        if (scopeIndex >= fs.Scopes.size()) return;

        vkCmdWriteTimestamp2(cmd, stage, m_QueryPool, fs.Scopes[scopeIndex].BeginQuery);
    }

    void GpuProfiler::WriteScopeEnd(VkCommandBuffer cmd, uint32_t scopeIndex, VkPipelineStageFlags2 stage)
    {
        if (!m_Supported || m_QueryPool == VK_NULL_HANDLE) return;
        const uint32_t frameIndex = m_Device->GetCurrentFrameIndex();
        FrameState& fs = m_Frames[frameIndex % kFramesInFlight];
        if (scopeIndex >= fs.Scopes.size()) return;

        vkCmdWriteTimestamp2(cmd, stage, m_QueryPool, fs.Scopes[scopeIndex].EndQuery);
    }

    std::expected<GpuTimestampFrame, GpuTimestampError> GpuProfiler::Resolve(uint32_t frameIndex) const
    {
        if (!m_Supported || m_QueryPool == VK_NULL_HANDLE) return std::unexpected(GpuTimestampError::InvalidState);

        const FrameState& fs = m_Frames[frameIndex % kFramesInFlight];

        // Read back only what we used this frame: frame timestamps + per-scope begin/end.
        const uint32_t scopeCount = static_cast<uint32_t>(fs.Scopes.size());
        const uint32_t queriesToRead = 2u + scopeCount * 2u;

        struct TS
        {
            uint64_t Value = 0;
            uint64_t Available = 0;
        };

        std::vector<TS> results;
        results.resize(queriesToRead);

        const VkResult r = vkGetQueryPoolResults(
            m_Device->GetLogicalDevice(),
            m_QueryPool,
            fs.QueryBase,
            queriesToRead,
            results.size() * sizeof(TS),
            results.data(),
            sizeof(TS),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

        if (r == VK_NOT_READY)
        {
            return std::unexpected(GpuTimestampError::NotReady);
        }
        if (r == VK_ERROR_DEVICE_LOST)
        {
            return std::unexpected(GpuTimestampError::DeviceLost);
        }
        if (r != VK_SUCCESS)
        {
            return std::unexpected(GpuTimestampError::InvalidState);
        }

        for (uint32_t i = 0; i < queriesToRead; ++i)
        {
            if (results[i].Available == 0)
            {
                return std::unexpected(GpuTimestampError::NotReady);
            }
        }

        const uint64_t frameStart = results[0].Value;
        const uint64_t frameEnd = results[1].Value;

        GpuTimestampFrame out{};
        out.FrameNumber = fs.FrameNumber;
        out.ScopeCount = scopeCount;
        out.ScopeDurationsNs.resize(scopeCount);

        const double tickToNs = m_TimestampPeriodNs;
        if (frameEnd > frameStart)
        {
            out.GpuFrameTimeNs = static_cast<uint64_t>(static_cast<double>(frameEnd - frameStart) * tickToNs);
        }

        for (uint32_t s = 0; s < scopeCount; ++s)
        {
            const uint64_t begin = results[2u + s * 2u + 0u].Value;
            const uint64_t end = results[2u + s * 2u + 1u].Value;
            if (end > begin)
                out.ScopeDurationsNs[s] = static_cast<uint64_t>(static_cast<double>(end - begin) * tickToNs);
        }

        return out;
    }
}

