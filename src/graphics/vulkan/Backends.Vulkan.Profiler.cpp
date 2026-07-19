module;

#include <expected>
#include <span>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import :Diagnostics;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.QueueAffinity;

namespace Extrinsic::Backends::Vulkan
{
    VulkanProfiler::VulkanProfiler(const VkDevice device,
                                   const VkPhysicalDevice physicalDevice,
                                   const uint32_t framesInFlight)
        : m_Device(device)
        , m_PhysicalDevice(physicalDevice)
        , m_FramesInFlight(framesInFlight)
    {
    }

    std::expected<RHI::ProfilerFramePlan, RHI::ProfilerError>
    VulkanProfiler::BeginFrame(
        const RHI::ProfilerFrameKey,
        const std::span<const RHI::ProfilerScopeDesc>)
    {
        return std::unexpected(RHI::ProfilerError::Unsupported);
    }

    std::expected<void, RHI::ProfilerError>
    VulkanProfiler::BeginQueue(RHI::ICommandContext&,
                               const RHI::QueueAffinity)
    {
        return std::unexpected(RHI::ProfilerError::Unsupported);
    }

    std::expected<void, RHI::ProfilerError>
    VulkanProfiler::EndQueue(RHI::ICommandContext&,
                             const RHI::QueueAffinity)
    {
        return std::unexpected(RHI::ProfilerError::Unsupported);
    }

    std::expected<void, RHI::ProfilerError>
    VulkanProfiler::BeginScope(RHI::ICommandContext&,
                               const RHI::ProfilerScopeToken)
    {
        return std::unexpected(RHI::ProfilerError::Unsupported);
    }

    std::expected<void, RHI::ProfilerError>
    VulkanProfiler::EndScope(RHI::ICommandContext&,
                             const RHI::ProfilerScopeToken)
    {
        return std::unexpected(RHI::ProfilerError::Unsupported);
    }

    std::expected<void, RHI::ProfilerError>
    VulkanProfiler::EndFrame(
        const RHI::ProfilerFrameKey,
        const RHI::ProfilerFrameDisposition)
    {
        return std::unexpected(RHI::ProfilerError::Unsupported);
    }

    std::expected<RHI::GpuTimestampFrame, RHI::ProfilerError>
    VulkanProfiler::Resolve(const RHI::ProfilerFrameKey) const
    {
        return std::unexpected(RHI::ProfilerError::Unsupported);
    }

    RHI::ProfilerStatusSnapshot VulkanProfiler::GetStatus() const
    {
        return RHI::ProfilerStatusSnapshot{
            .Status = RHI::ProfilerBackendStatus::Unsupported,
            .Source = RHI::GpuTimestampSource::Unavailable,
            .Diagnostic =
                "Native Vulkan timestamp query lifecycle is unavailable.",
        };
    }
}
