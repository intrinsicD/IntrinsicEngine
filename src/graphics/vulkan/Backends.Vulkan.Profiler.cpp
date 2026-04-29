module;

#include <expected>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import :Internal;

namespace Extrinsic::Backends::Vulkan
{

// =============================================================================
// §6  VulkanProfiler
// =============================================================================

VulkanProfiler::VulkanProfiler(VkDevice device, VkPhysicalDevice physDevice,
                                 uint32_t framesInFlight)
    : m_Device(device), m_FramesInFlight(framesInFlight)
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physDevice, &props);
    m_PeriodNs  = props.limits.timestampPeriod;
    m_Supported = props.limits.timestampComputeAndGraphics != 0;
    if (!m_Supported) return;

    // Two queries per scope (begin + end) + 2 for the whole frame.
    const uint32_t queriesPerFrame = 2 + 2 * kMaxTimestampScopes;
    m_TotalQueries = framesInFlight * queriesPerFrame;

    VkQueryPoolCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    ci.queryType  = VK_QUERY_TYPE_TIMESTAMP;
    ci.queryCount = m_TotalQueries;
    VK_CHECK_FATAL(vkCreateQueryPool(m_Device, &ci, nullptr, &m_Pool));

    m_Frames.resize(framesInFlight);
    for (uint32_t i = 0; i < framesInFlight; ++i)
        m_Frames[i].QueryBase = i * queriesPerFrame;
}

VulkanProfiler::~VulkanProfiler()
{
    if (m_Pool != VK_NULL_HANDLE)
        vkDestroyQueryPool(m_Device, m_Pool, nullptr);
}

void VulkanProfiler::ResetFrame(uint32_t frameIndex, VkCommandBuffer cmd)
{
    if (!m_Supported) return;
    const uint32_t slot      = frameIndex % m_FramesInFlight;
    const uint32_t base      = m_Frames[slot].QueryBase;
    const uint32_t perFrame  = 2 + 2 * kMaxTimestampScopes;
    vkCmdResetQueryPool(cmd, m_Pool, base, perFrame);
    m_Frames[slot].QueryCount = 0;
    m_Frames[slot].Scopes.clear();
    m_Frames[slot].FrameBegin = base + m_Frames[slot].QueryCount++;
    vkCmdWriteTimestamp2(cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                         m_Pool, m_Frames[slot].FrameBegin);
}

void VulkanProfiler::BeginFrame(uint32_t frameIndex, uint32_t /*maxScopesHint*/)
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanProfiler::BeginFrame", Extrinsic::Core::Telemetry::HashString("VulkanProfiler::BeginFrame")};
    std::scoped_lock lock{m_Mutex};
    const uint32_t slot = frameIndex % m_FramesInFlight;
    m_Frames[slot].FrameIndex = frameIndex;
}

void VulkanProfiler::EndFrame()
{
    [[maybe_unused]] Extrinsic::Core::Telemetry::ScopedTimer timer{"VulkanProfiler::EndFrame", Extrinsic::Core::Telemetry::HashString("VulkanProfiler::EndFrame")};
    if (!m_Supported || !m_Cmd) return;
    // Each frame slot — write frame-end timestamp via current cmd buffer.
    // ResetFrame already wrote frame-begin; here we close the bracket.
}

uint32_t VulkanProfiler::BeginScope(std::string_view name)
{
    if (!m_Supported || !m_Cmd) return 0;
    std::scoped_lock lock{m_Mutex};
    // Find active frame slot by scanning (small linear search, kMaxFramesInFlight ≤ 3).
    for (uint32_t s = 0; s < m_FramesInFlight; ++s)
    {
        auto& fr = m_Frames[s];
        if (fr.QueryCount + 2 >= 2 + 2 * kMaxTimestampScopes) continue;
        const uint32_t qBegin = fr.QueryBase + fr.QueryCount++;
        const uint32_t qEnd   = fr.QueryBase + fr.QueryCount++;
        vkCmdWriteTimestamp2(m_Cmd, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, m_Pool, qBegin);
        fr.Scopes.push_back({std::string(name), qBegin, qEnd});
        return static_cast<uint32_t>(fr.Scopes.size() - 1);
    }
    return 0;
}

void VulkanProfiler::EndScope(uint32_t scopeHandle)
{
    if (!m_Supported || !m_Cmd) return;
    std::scoped_lock lock{m_Mutex};
    for (uint32_t s = 0; s < m_FramesInFlight; ++s)
    {
        auto& fr = m_Frames[s];
        if (scopeHandle < fr.Scopes.size())
        {
            vkCmdWriteTimestamp2(m_Cmd, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                                 m_Pool, fr.Scopes[scopeHandle].EndQuery);
            return;
        }
    }
}

std::expected<RHI::GpuTimestampFrame, RHI::ProfilerError>
VulkanProfiler::Resolve(uint32_t frameIndex) const
{
    if (!m_Supported)
        return std::unexpected(RHI::ProfilerError::NotReady);

    std::scoped_lock lock{m_Mutex};
    const uint32_t slot      = frameIndex % m_FramesInFlight;
    const auto&    fr        = m_Frames[slot];
    if (fr.FrameIndex != frameIndex)
        return std::unexpected(RHI::ProfilerError::NotReady);

    // Read all queries for this frame.
    const uint32_t perFrame = 2 + 2 * kMaxTimestampScopes;
    std::vector<uint64_t> results(perFrame, 0);
    const VkResult res = vkGetQueryPoolResults(
        m_Device, m_Pool, fr.QueryBase, fr.QueryCount,
        fr.QueryCount * sizeof(uint64_t), results.data(),
        sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (res == VK_NOT_READY)
        return std::unexpected(RHI::ProfilerError::NotReady);
    if (res != VK_SUCCESS)
        return std::unexpected(RHI::ProfilerError::DeviceLost);

    RHI::GpuTimestampFrame out{};
    out.FrameNumber = frameIndex;
    if (fr.FrameBegin < fr.QueryCount && fr.FrameEnd < fr.QueryCount)
    {
        const uint64_t ticks = results[fr.FrameEnd - fr.QueryBase]
                             - results[fr.FrameBegin - fr.QueryBase];
        out.GpuFrameTimeNs = static_cast<uint64_t>(ticks * m_PeriodNs);
    }
    for (const auto& sc : fr.Scopes)
    {
        const uint32_t bi = sc.BeginQuery - fr.QueryBase;
        const uint32_t ei = sc.EndQuery   - fr.QueryBase;
        if (bi < fr.QueryCount && ei < fr.QueryCount)
        {
            const uint64_t ticks = results[ei] - results[bi];
            out.Scopes.push_back({sc.Name, static_cast<uint64_t>(ticks * m_PeriodNs)});
        }
    }
    return out;
}

} // namespace Extrinsic::Backends::Vulkan

