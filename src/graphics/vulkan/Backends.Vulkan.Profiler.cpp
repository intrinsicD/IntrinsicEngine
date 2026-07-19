module;

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include "Vulkan.hpp"

module Extrinsic.Backends.Vulkan;

import :Diagnostics;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.QueueAffinity;

namespace Extrinsic::Backends::Vulkan
{
namespace
{
    constexpr std::uint32_t kProfiledQueueCount = 2u;
    constexpr std::uint32_t kEnvelopeQueryCount = 2u;
    constexpr std::uint32_t kScopeQueryCount =
        2u * RHI::kMaxTimestampScopesPerFrame;
    constexpr std::uint32_t kQueriesPerQueueRange =
        kEnvelopeQueryCount + kScopeQueryCount;

    [[nodiscard]] constexpr bool IsValidTimestampWidth(
        const std::uint32_t validBits) noexcept
    {
        return validBits > 0u && validBits <= 64u;
    }

    [[nodiscard]] constexpr std::optional<std::uint32_t> QueueIndex(
        const RHI::QueueAffinity queue) noexcept
    {
        switch (queue)
        {
        case RHI::QueueAffinity::Graphics:
            return 0u;
        case RHI::QueueAffinity::AsyncCompute:
            return 1u;
        case RHI::QueueAffinity::Transfer:
            return std::nullopt;
        }
        return std::nullopt;
    }

    [[nodiscard]] constexpr RHI::QueueAffinity QueueFromIndex(
        const std::uint32_t index) noexcept
    {
        return index == 0u
                   ? RHI::QueueAffinity::Graphics
                   : RHI::QueueAffinity::AsyncCompute;
    }

    [[nodiscard]] constexpr RHI::TimestampQueryRange QueueQueryRange(
        const std::uint32_t frameSlot,
        const std::uint32_t queueIndex) noexcept
    {
        return RHI::TimestampQueryRange{
            .Base =
                (frameSlot * kProfiledQueueCount + queueIndex) *
                kQueriesPerQueueRange,
            .Count = kQueriesPerQueueRange,
        };
    }

    static_assert(
        QueueQueryRange(0u, 0u).Base +
                QueueQueryRange(0u, 0u).Count ==
            QueueQueryRange(0u, 1u).Base);
    static_assert(
        QueueQueryRange(0u, 1u).Base +
                QueueQueryRange(0u, 1u).Count ==
            QueueQueryRange(1u, 0u).Base);

    [[nodiscard]] std::string ReadyDiagnostic(
        const bool graphicsSupported,
        const bool asyncComputeSupported)
    {
        if (graphicsSupported && asyncComputeSupported)
        {
            return "Native Vulkan timestamps are ready for graphics and "
                   "async-compute queues.";
        }
        if (graphicsSupported)
        {
            return "Native Vulkan timestamps are ready for graphics; the "
                   "async-compute queue is unavailable or unsupported.";
        }
        return "Native Vulkan timestamps are ready for async-compute; the "
               "graphics queue does not expose timestamp bits.";
    }
}

VulkanProfilerBootstrapDecision EvaluateVulkanProfilerBootstrap(
    const VulkanProfilerBootstrapInputs inputs) noexcept
{
    VulkanProfilerBootstrapDecision decision{};
    if (inputs.FramesInFlight == 0u)
    {
        decision.Status = RHI::ProfilerBackendStatus::InitializationFailed;
        return decision;
    }
    if (!std::isfinite(inputs.TimestampPeriodNs) ||
        inputs.TimestampPeriodNs <= 0.0)
    {
        decision.Status = RHI::ProfilerBackendStatus::Unsupported;
        return decision;
    }

    decision.GraphicsTimestampsSupported =
        IsValidTimestampWidth(inputs.GraphicsTimestampValidBits);
    decision.AsyncComputeTimestampsSupported =
        inputs.AsyncComputeQueueAvailable &&
        IsValidTimestampWidth(inputs.AsyncComputeTimestampValidBits);
    if (!decision.GraphicsTimestampsSupported &&
        !decision.AsyncComputeTimestampsSupported)
    {
        decision.Status = RHI::ProfilerBackendStatus::Unsupported;
        return decision;
    }

    const std::uint64_t queryCount =
        static_cast<std::uint64_t>(inputs.FramesInFlight) *
        kProfiledQueueCount *
        kQueriesPerQueueRange;
    if (queryCount > std::numeric_limits<std::uint32_t>::max())
    {
        decision.Status = RHI::ProfilerBackendStatus::InitializationFailed;
        return decision;
    }

    decision.QueryPoolRequired = true;
    decision.TotalQueryCount = static_cast<std::uint32_t>(queryCount);
    decision.Status = inputs.QueryPoolCreationSucceeded
                          ? RHI::ProfilerBackendStatus::Ready
                          : RHI::ProfilerBackendStatus::InitializationFailed;
    return decision;
}

struct VulkanProfiler::Impl
{
    enum class ScopeLifecycle : std::uint8_t
    {
        Planned = 0,
        Begun,
        Ended,
    };

    enum class QueueLifecycle : std::uint8_t
    {
        Unused = 0,
        Open,
        Closed,
    };

    struct QueryPair
    {
        std::uint32_t Begin = 0u;
        std::uint32_t End = 0u;
        bool Supported = false;
    };

    struct ActiveQueue
    {
        RHI::TimestampQueryRange Range{};
        QueueLifecycle Lifecycle = QueueLifecycle::Unused;
        bool Supported = false;
    };

    struct ActiveFrame
    {
        RHI::ProfilerFrameKey Frame{};
        std::uint64_t PlanGeneration = 0u;
        std::vector<RHI::ProfilerScopeDesc> Scopes{};
        std::vector<QueryPair> ScopeQueries{};
        std::unique_ptr<std::atomic<ScopeLifecycle>[]> ScopeLifecycles{};
        std::array<ActiveQueue, kProfiledQueueCount> Queues{};
    };

    struct PendingFrame
    {
        RHI::ProfilerFrameKey Frame{};
        std::vector<RHI::ProfilerScopeDesc> Scopes{};
        std::vector<QueryPair> ScopeQueries{};
        std::vector<bool> ScopeEnded{};
        std::array<ActiveQueue, kProfiledQueueCount> Queues{};
    };

    struct RawTimestampQuery
    {
        std::uint64_t Ticks = 0u;
        std::uint64_t Available = 0u;
    };

    struct TerminalFrameError
    {
        RHI::ProfilerFrameKey Frame{};
        RHI::ProfilerError Error = RHI::ProfilerError::InvalidState;
    };

    explicit Impl(const VulkanProfilerCreateInfo& createInfo)
        : Device(createInfo.Device)
        , FramesInFlight(createInfo.FramesInFlight)
        , Owner(createInfo.Owner)
        , ResolveContext(createInfo.ResolveContext)
        , NotifyDeviceLost(createInfo.NotifyDeviceLost)
        , PendingFrames(createInfo.FramesInFlight)
        , ResolvedFrames(createInfo.FramesInFlight)
        , TerminalErrors(createInfo.FramesInFlight)
    {
        static_assert(
            kMaxTimestampScopes == RHI::kMaxTimestampScopesPerFrame);

        if (Device == VK_NULL_HANDLE ||
            createInfo.PhysicalDevice == VK_NULL_HANDLE ||
            Owner == nullptr ||
            ResolveContext == nullptr ||
            NotifyDeviceLost == nullptr ||
            FramesInFlight == 0u)
        {
            Status = RHI::ProfilerStatusSnapshot{
                .Status = RHI::ProfilerBackendStatus::InitializationFailed,
                .Source = RHI::GpuTimestampSource::Unavailable,
                .Diagnostic =
                    "Vulkan profiler create info is incomplete.",
            };
            return;
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(
            createInfo.PhysicalDevice,
            &properties);
        TimestampPeriodNs =
            static_cast<double>(properties.limits.timestampPeriod);

        std::uint32_t queueFamilyCount = 0u;
        vkGetPhysicalDeviceQueueFamilyProperties(
            createInfo.PhysicalDevice,
            &queueFamilyCount,
            nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        if (queueFamilyCount > 0u)
        {
            vkGetPhysicalDeviceQueueFamilyProperties(
                createInfo.PhysicalDevice,
                &queueFamilyCount,
                queueFamilies.data());
            queueFamilies.resize(queueFamilyCount);
        }

        const auto validBitsForFamily =
            [&queueFamilies](const std::uint32_t family) noexcept
        {
            return family < queueFamilies.size()
                       ? queueFamilies[family].timestampValidBits
                       : 0u;
        };
        QueueValidBits[0] =
            validBitsForFamily(createInfo.GraphicsQueueFamily);
        QueueValidBits[1] =
            createInfo.AsyncComputeQueueAvailable
                ? validBitsForFamily(createInfo.AsyncComputeQueueFamily)
                : 0u;

        VulkanProfilerBootstrapInputs bootstrapInputs{
            .TimestampPeriodNs = TimestampPeriodNs,
            .GraphicsTimestampValidBits = QueueValidBits[0],
            .AsyncComputeQueueAvailable =
                createInfo.AsyncComputeQueueAvailable,
            .AsyncComputeTimestampValidBits = QueueValidBits[1],
            .FramesInFlight = FramesInFlight,
            .QueryPoolCreationSucceeded = true,
        };
        VulkanProfilerBootstrapDecision decision =
            EvaluateVulkanProfilerBootstrap(bootstrapInputs);
        QueueSupported[0] =
            decision.GraphicsTimestampsSupported;
        QueueSupported[1] =
            decision.AsyncComputeTimestampsSupported;
        if (decision.Status != RHI::ProfilerBackendStatus::Ready)
        {
            Status = RHI::ProfilerStatusSnapshot{
                .Status = decision.Status,
                .Source = RHI::GpuTimestampSource::Unavailable,
                .Diagnostic =
                    std::isfinite(TimestampPeriodNs) &&
                            TimestampPeriodNs > 0.0
                        ? "No promoted Vulkan graphics or async-compute "
                          "queue exposes native timestamp bits."
                        : "Vulkan timestampPeriod is not finite and positive.",
            };
            return;
        }

        VkQueryPoolCreateInfo queryPoolInfo{};
        queryPoolInfo.sType =
            VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryPoolInfo.queryCount = decision.TotalQueryCount;
        const VkResult result =
            vkCreateQueryPool(Device, &queryPoolInfo, nullptr, &QueryPool);

        bootstrapInputs.QueryPoolCreationSucceeded =
            result == VK_SUCCESS && QueryPool != VK_NULL_HANDLE;
        decision = EvaluateVulkanProfilerBootstrap(bootstrapInputs);
        if (decision.Status != RHI::ProfilerBackendStatus::Ready)
        {
            if (QueryPool != VK_NULL_HANDLE)
            {
                vkDestroyQueryPool(Device, QueryPool, nullptr);
                QueryPool = VK_NULL_HANDLE;
            }
            Status = RHI::ProfilerStatusSnapshot{
                .Status = RHI::ProfilerBackendStatus::InitializationFailed,
                .Source = RHI::GpuTimestampSource::Unavailable,
                .Diagnostic =
                    "Vulkan timestamp query-pool creation failed; rendering "
                    "remains available without native profiling.",
            };
            return;
        }

        Status = RHI::ProfilerStatusSnapshot{
            .Status = RHI::ProfilerBackendStatus::Ready,
            .Source = RHI::GpuTimestampSource::NativeGpu,
            .Diagnostic = ReadyDiagnostic(
                QueueSupported[0],
                QueueSupported[1]),
        };
    }

    ~Impl()
    {
        if (Device != VK_NULL_HANDLE && QueryPool != VK_NULL_HANDLE)
        {
            vkDestroyQueryPool(Device, QueryPool, nullptr);
            QueryPool = VK_NULL_HANDLE;
        }
    }

    [[nodiscard]] std::optional<RHI::ProfilerError>
    ValidateQueue(const RHI::QueueAffinity queue) const noexcept
    {
        switch (queue)
        {
        case RHI::QueueAffinity::Graphics:
        case RHI::QueueAffinity::AsyncCompute:
            return std::nullopt;
        case RHI::QueueAffinity::Transfer:
            return RHI::ProfilerError::Unsupported;
        }
        return RHI::ProfilerError::InvalidArgument;
    }

    [[nodiscard]] VulkanProfilerCommandContextView ContextView(
        RHI::ICommandContext& context) const noexcept
    {
        if (Owner == nullptr || ResolveContext == nullptr)
        {
            return {};
        }
        return ResolveContext(*Owner, context);
    }

    [[nodiscard]] std::expected<VulkanProfilerCommandContextView,
                                RHI::ProfilerError>
    ValidateContext(
        RHI::ICommandContext& context,
        const RHI::QueueAffinity queue,
        const bool primaryRequired) const noexcept
    {
        const VulkanProfilerCommandContextView view =
            ContextView(context);
        if (!view.IsValid() || view.Queue != queue)
        {
            return std::unexpected(
                RHI::ProfilerError::InvalidArgument);
        }
        if (!view.Recording)
        {
            return std::unexpected(
                RHI::ProfilerError::InvalidState);
        }
        if (primaryRequired && !view.Primary)
        {
            return std::unexpected(
                RHI::ProfilerError::InvalidArgument);
        }
        return view;
    }

    [[nodiscard]] bool ScopeBelongsToActiveFrame(
        const RHI::ProfilerScopeToken token) const noexcept
    {
        return Active.has_value() &&
               token.IsValid() &&
               token.PlanGeneration == Active->PlanGeneration &&
               token.ScopeIndex < Active->Scopes.size();
    }

    [[nodiscard]] std::expected<std::uint64_t, RHI::ProfilerError>
    ReadDuration(
        const QueryPair pair,
        const std::uint32_t validBits) const
    {
        if (DeviceLost)
        {
            return std::unexpected(RHI::ProfilerError::DeviceLost);
        }
        if (QueryPool == VK_NULL_HANDLE || !pair.Supported)
        {
            return std::unexpected(RHI::ProfilerError::Unsupported);
        }

        std::array<RawTimestampQuery, 2u> queries{};
        const VkResult result = vkGetQueryPoolResults(
            Device,
            QueryPool,
            pair.Begin,
            2u,
            sizeof(queries),
            queries.data(),
            sizeof(RawTimestampQuery),
            VK_QUERY_RESULT_64_BIT |
                VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (result == VK_NOT_READY)
        {
            return std::unexpected(RHI::ProfilerError::NotReady);
        }
        if (result == VK_ERROR_DEVICE_LOST)
        {
            DeviceLost = true;
            NotifyDeviceLost(*Owner);
            Status = RHI::ProfilerStatusSnapshot{
                .Status = RHI::ProfilerBackendStatus::DeviceLost,
                .Source = RHI::GpuTimestampSource::Unavailable,
                .Diagnostic =
                    "Vulkan device loss disabled native timestamp queries.",
            };
            return std::unexpected(RHI::ProfilerError::DeviceLost);
        }
        if (result != VK_SUCCESS)
        {
            return std::unexpected(RHI::ProfilerError::InvalidState);
        }

        return RHI::ResolveTimestampDurationNs(
            RHI::TimestampQueryValue{
                .Ticks = queries[0].Ticks,
                .Available = queries[0].Available != 0u,
            },
            RHI::TimestampQueryValue{
                .Ticks = queries[1].Ticks,
                .Available = queries[1].Available != 0u,
            },
            validBits,
            TimestampPeriodNs);
    }

    [[nodiscard]] std::expected<RHI::GpuTimestampFrame, RHI::ProfilerError>
    ResolvePending(const std::uint32_t frameSlot) const
    {
        if (frameSlot >= PendingFrames.size() ||
            !PendingFrames[frameSlot].has_value())
        {
            return std::unexpected(RHI::ProfilerError::NotReady);
        }

        const PendingFrame& pending = *PendingFrames[frameSlot];
        RHI::GpuTimestampFrame result{
            .Frame = pending.Frame,
            .Source = RHI::GpuTimestampSource::Unavailable,
        };
        bool hasNativeResult = false;
        for (std::uint32_t queueIndex = 0u;
             queueIndex < kProfiledQueueCount;
             ++queueIndex)
        {
            const ActiveQueue& queue = pending.Queues[queueIndex];
            if (queue.Lifecycle != QueueLifecycle::Closed)
            {
                continue;
            }

            std::optional<std::uint64_t> duration{};
            if (queue.Supported)
            {
                const auto resolved = ReadDuration(
                    QueryPair{
                        .Begin = queue.Range.Base,
                        .End = queue.Range.Base + 1u,
                        .Supported = true,
                    },
                    QueueValidBits[queueIndex]);
                if (!resolved)
                {
                    return std::unexpected(resolved.error());
                }
                duration = *resolved;
                hasNativeResult = true;
            }
            result.QueueEnvelopes.push_back(
                RHI::GpuTimestampQueueEnvelope{
                    .Queue = QueueFromIndex(queueIndex),
                    .Source = queue.Supported
                                  ? RHI::GpuTimestampSource::NativeGpu
                                  : RHI::GpuTimestampSource::Unavailable,
                    .DurationNs = duration,
                });
        }

        result.Scopes.reserve(pending.Scopes.size());
        for (std::size_t scopeIndex = 0u;
             scopeIndex < pending.Scopes.size();
             ++scopeIndex)
        {
            if (!pending.ScopeEnded[scopeIndex])
            {
                continue;
            }

            const RHI::ProfilerScopeDesc& descriptor =
                pending.Scopes[scopeIndex];
            const QueryPair pair =
                pending.ScopeQueries[scopeIndex];
            std::optional<std::uint64_t> duration{};
            if (pair.Supported)
            {
                const std::optional<std::uint32_t> queueIndex =
                    QueueIndex(descriptor.Queue);
                if (!queueIndex.has_value())
                {
                    return std::unexpected(
                        RHI::ProfilerError::InvalidState);
                }
                const auto resolved =
                    ReadDuration(pair, QueueValidBits[*queueIndex]);
                if (!resolved)
                {
                    return std::unexpected(resolved.error());
                }
                duration = *resolved;
                hasNativeResult = true;
            }
            result.Scopes.push_back(RHI::GpuTimestampScope{
                .Ordinal = descriptor.Ordinal,
                .Name = descriptor.Name,
                .Queue = descriptor.Queue,
                .Source = pair.Supported
                              ? RHI::GpuTimestampSource::NativeGpu
                              : RHI::GpuTimestampSource::Unavailable,
                .DurationNs = duration,
            });
        }
        result.Source = hasNativeResult
                            ? RHI::GpuTimestampSource::NativeGpu
                            : RHI::GpuTimestampSource::Unavailable;
        return result;
    }

    void RetireCompletedSlot(const std::uint32_t frameSlot) const noexcept
    {
        if (frameSlot >= PendingFrames.size() ||
            !PendingFrames[frameSlot].has_value() ||
            DeviceLost)
        {
            return;
        }

        const RHI::ProfilerFrameKey pendingFrame =
            PendingFrames[frameSlot]->Frame;
        const auto resolved = ResolvePending(frameSlot);
        if (resolved)
        {
            ResolvedFrames[frameSlot] = *resolved;
            TerminalErrors[frameSlot].reset();
            PendingFrames[frameSlot].reset();
            return;
        }
        if (resolved.error() == RHI::ProfilerError::NotReady)
        {
            // Never wait and never reset this slot while query availability
            // still says the exact submitted frame is incomplete.
            return;
        }

        TerminalErrors[frameSlot] = TerminalFrameError{
            .Frame = pendingFrame,
            .Error = resolved.error(),
        };
        PendingFrames[frameSlot].reset();
    }

    VkDevice Device = VK_NULL_HANDLE;
    VkQueryPool QueryPool = VK_NULL_HANDLE;
    std::uint32_t FramesInFlight = 0u;
    VulkanDevice* Owner = nullptr;
    VulkanProfilerContextResolver ResolveContext = nullptr;
    VulkanProfilerDeviceLostNotifier NotifyDeviceLost = nullptr;
    double TimestampPeriodNs = 0.0;
    std::array<std::uint32_t, kProfiledQueueCount> QueueValidBits{};
    std::array<bool, kProfiledQueueCount> QueueSupported{};
    mutable RHI::ProfilerStatusSnapshot Status{};
    mutable bool DeviceLost = false;
    std::optional<ActiveFrame> Active{};
    mutable std::vector<std::optional<PendingFrame>> PendingFrames{};
    mutable std::vector<std::optional<RHI::GpuTimestampFrame>>
        ResolvedFrames{};
    mutable std::vector<std::optional<TerminalFrameError>>
        TerminalErrors{};
    std::uint64_t NextPlanGeneration = 1u;
};

VulkanProfiler::VulkanProfiler(
    const VulkanProfilerCreateInfo& createInfo)
    : m_Impl(std::make_unique<Impl>(createInfo))
{
}

VulkanProfiler::~VulkanProfiler() = default;

std::expected<RHI::ProfilerFramePlan, RHI::ProfilerError>
VulkanProfiler::BeginFrame(
    const RHI::ProfilerFrameKey frame,
    const std::span<const RHI::ProfilerScopeDesc> scopes)
{
    if (m_Impl->DeviceLost)
    {
        return std::unexpected(RHI::ProfilerError::DeviceLost);
    }
    if (m_Impl->Status.Status != RHI::ProfilerBackendStatus::Ready)
    {
        return std::unexpected(RHI::ProfilerError::Unsupported);
    }
    if (m_Impl->Active.has_value())
    {
        return std::unexpected(RHI::ProfilerError::InvalidState);
    }
    if (frame.FrameSlot >= m_Impl->FramesInFlight)
    {
        return std::unexpected(RHI::ProfilerError::InvalidArgument);
    }
    if (scopes.size() > RHI::kMaxTimestampScopesPerFrame)
    {
        return std::unexpected(RHI::ProfilerError::Exhausted);
    }

    for (std::size_t scopeIndex = 0u;
         scopeIndex < scopes.size();
         ++scopeIndex)
    {
        if (scopes[scopeIndex].Name.empty())
        {
            return std::unexpected(RHI::ProfilerError::InvalidArgument);
        }
        if (const std::optional<RHI::ProfilerError> queueError =
                m_Impl->ValidateQueue(scopes[scopeIndex].Queue);
            queueError.has_value())
        {
            return std::unexpected(*queueError);
        }
        for (std::size_t prior = 0u; prior < scopeIndex; ++prior)
        {
            if (scopes[prior].Ordinal == scopes[scopeIndex].Ordinal)
            {
                return std::unexpected(
                    RHI::ProfilerError::InvalidArgument);
            }
        }
    }

    if (m_Impl->PendingFrames[frame.FrameSlot].has_value())
    {
        // Normal VulkanDevice use has already called this at the slot-fence
        // proof. Keep the same nonblocking guard for direct backend callers.
        m_Impl->RetireCompletedSlot(frame.FrameSlot);
        if (m_Impl->DeviceLost)
        {
            return std::unexpected(RHI::ProfilerError::DeviceLost);
        }
        if (m_Impl->PendingFrames[frame.FrameSlot].has_value())
        {
            return std::unexpected(RHI::ProfilerError::NotReady);
        }
    }

    const std::uint64_t planGeneration =
        m_Impl->NextPlanGeneration;
    ++m_Impl->NextPlanGeneration;
    if (m_Impl->NextPlanGeneration ==
        RHI::ProfilerScopeToken::InvalidGeneration)
    {
        m_Impl->NextPlanGeneration = 1u;
    }

    Impl::ActiveFrame active{
        .Frame = frame,
        .PlanGeneration = planGeneration,
    };
    for (std::uint32_t queueIndex = 0u;
         queueIndex < kProfiledQueueCount;
         ++queueIndex)
    {
        active.Queues[queueIndex] = Impl::ActiveQueue{
            .Range = QueueQueryRange(frame.FrameSlot, queueIndex),
            .Lifecycle = Impl::QueueLifecycle::Unused,
            .Supported = m_Impl->QueueSupported[queueIndex],
        };
    }

    active.Scopes.assign(scopes.begin(), scopes.end());
    active.ScopeQueries.reserve(scopes.size());
    active.ScopeLifecycles =
        std::make_unique<std::atomic<Impl::ScopeLifecycle>[]>(
            scopes.size());
    std::array<std::uint32_t, kProfiledQueueCount>
        scopeCountByQueue{};

    RHI::ProfilerFramePlan plan{
        .Frame = frame,
    };
    plan.ScopeTokens.reserve(scopes.size());
    for (std::uint32_t scopeIndex = 0u;
         scopeIndex < static_cast<std::uint32_t>(scopes.size());
         ++scopeIndex)
    {
        const std::uint32_t queueIndex =
            *QueueIndex(scopes[scopeIndex].Queue);
        const std::uint32_t localScope =
            scopeCountByQueue[queueIndex]++;
        const std::uint32_t beginQuery =
            active.Queues[queueIndex].Range.Base +
            kEnvelopeQueryCount +
            localScope * 2u;
        active.ScopeQueries.push_back(Impl::QueryPair{
            .Begin = beginQuery,
            .End = beginQuery + 1u,
            .Supported = active.Queues[queueIndex].Supported,
        });
        active.ScopeLifecycles[scopeIndex].store(
            Impl::ScopeLifecycle::Planned,
            std::memory_order_relaxed);
        plan.ScopeTokens.push_back(RHI::ProfilerScopeToken{
            .PlanGeneration = planGeneration,
            .ScopeIndex = scopeIndex,
        });
    }

    m_Impl->Active = std::move(active);
    return plan;
}

std::expected<void, RHI::ProfilerError>
VulkanProfiler::BeginQueue(
    RHI::ICommandContext& context,
    const RHI::QueueAffinity queue)
{
    if (m_Impl->DeviceLost)
    {
        return std::unexpected(RHI::ProfilerError::DeviceLost);
    }
    if (const std::optional<RHI::ProfilerError> queueError =
            m_Impl->ValidateQueue(queue);
        queueError.has_value())
    {
        return std::unexpected(*queueError);
    }
    if (!m_Impl->Active.has_value())
    {
        return std::unexpected(RHI::ProfilerError::InvalidState);
    }

    const auto contextView =
        m_Impl->ValidateContext(context, queue, true);
    if (!contextView)
    {
        return std::unexpected(contextView.error());
    }

    const std::uint32_t queueIndex = *QueueIndex(queue);
    Impl::ActiveQueue& queueState =
        m_Impl->Active->Queues[queueIndex];
    if (queueState.Lifecycle != Impl::QueueLifecycle::Unused)
    {
        return std::unexpected(RHI::ProfilerError::InvalidState);
    }

    if (queueState.Supported)
    {
        vkCmdResetQueryPool(
            contextView->CommandBuffer,
            m_Impl->QueryPool,
            queueState.Range.Base,
            queueState.Range.Count);
        vkCmdWriteTimestamp2(
            contextView->CommandBuffer,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            m_Impl->QueryPool,
            queueState.Range.Base);
    }
    queueState.Lifecycle = Impl::QueueLifecycle::Open;
    return {};
}

std::expected<void, RHI::ProfilerError>
VulkanProfiler::EndQueue(
    RHI::ICommandContext& context,
    const RHI::QueueAffinity queue)
{
    if (m_Impl->DeviceLost)
    {
        return std::unexpected(RHI::ProfilerError::DeviceLost);
    }
    if (const std::optional<RHI::ProfilerError> queueError =
            m_Impl->ValidateQueue(queue);
        queueError.has_value())
    {
        return std::unexpected(*queueError);
    }
    if (!m_Impl->Active.has_value())
    {
        return std::unexpected(RHI::ProfilerError::InvalidState);
    }

    const auto contextView =
        m_Impl->ValidateContext(context, queue, true);
    if (!contextView)
    {
        return std::unexpected(contextView.error());
    }
    const std::uint32_t queueIndex = *QueueIndex(queue);
    Impl::ActiveQueue& queueState =
        m_Impl->Active->Queues[queueIndex];
    if (queueState.Lifecycle != Impl::QueueLifecycle::Open)
    {
        return std::unexpected(RHI::ProfilerError::InvalidState);
    }

    for (std::size_t scopeIndex = 0u;
         scopeIndex < m_Impl->Active->Scopes.size();
         ++scopeIndex)
    {
        if (m_Impl->Active->Scopes[scopeIndex].Queue == queue &&
            m_Impl->Active->ScopeLifecycles[scopeIndex].load(
                std::memory_order_acquire) ==
                Impl::ScopeLifecycle::Begun)
        {
            return std::unexpected(RHI::ProfilerError::InvalidState);
        }
    }

    if (queueState.Supported)
    {
        vkCmdWriteTimestamp2(
            contextView->CommandBuffer,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            m_Impl->QueryPool,
            queueState.Range.Base + 1u);
    }
    queueState.Lifecycle = Impl::QueueLifecycle::Closed;
    return {};
}

std::expected<void, RHI::ProfilerError>
VulkanProfiler::BeginScope(
    RHI::ICommandContext& context,
    const RHI::ProfilerScopeToken scope)
{
    if (m_Impl->DeviceLost)
    {
        return std::unexpected(RHI::ProfilerError::DeviceLost);
    }
    if (!m_Impl->ScopeBelongsToActiveFrame(scope))
    {
        return std::unexpected(RHI::ProfilerError::InvalidState);
    }

    Impl::ActiveFrame& active = *m_Impl->Active;
    const RHI::QueueAffinity queue =
        active.Scopes[scope.ScopeIndex].Queue;
    const std::uint32_t queueIndex = *QueueIndex(queue);
    if (active.Queues[queueIndex].Lifecycle !=
        Impl::QueueLifecycle::Open)
    {
        return std::unexpected(RHI::ProfilerError::InvalidState);
    }
    const auto contextView =
        m_Impl->ValidateContext(context, queue, false);
    if (!contextView)
    {
        return std::unexpected(contextView.error());
    }

    Impl::ScopeLifecycle expected =
        Impl::ScopeLifecycle::Planned;
    if (!active.ScopeLifecycles[scope.ScopeIndex]
             .compare_exchange_strong(
                 expected,
                 Impl::ScopeLifecycle::Begun,
                 std::memory_order_acq_rel,
                 std::memory_order_acquire))
    {
        return std::unexpected(RHI::ProfilerError::InvalidState);
    }

    const Impl::QueryPair pair =
        active.ScopeQueries[scope.ScopeIndex];
    if (pair.Supported)
    {
        vkCmdWriteTimestamp2(
            contextView->CommandBuffer,
            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            m_Impl->QueryPool,
            pair.Begin);
    }
    return {};
}

std::expected<void, RHI::ProfilerError>
VulkanProfiler::EndScope(
    RHI::ICommandContext& context,
    const RHI::ProfilerScopeToken scope)
{
    if (m_Impl->DeviceLost)
    {
        return std::unexpected(RHI::ProfilerError::DeviceLost);
    }
    if (!m_Impl->ScopeBelongsToActiveFrame(scope))
    {
        return std::unexpected(RHI::ProfilerError::InvalidState);
    }

    Impl::ActiveFrame& active = *m_Impl->Active;
    const RHI::QueueAffinity queue =
        active.Scopes[scope.ScopeIndex].Queue;
    const std::uint32_t queueIndex = *QueueIndex(queue);
    if (active.Queues[queueIndex].Lifecycle !=
        Impl::QueueLifecycle::Open)
    {
        return std::unexpected(RHI::ProfilerError::InvalidState);
    }
    const auto contextView =
        m_Impl->ValidateContext(context, queue, false);
    if (!contextView)
    {
        return std::unexpected(contextView.error());
    }

    Impl::ScopeLifecycle expected =
        Impl::ScopeLifecycle::Begun;
    if (!active.ScopeLifecycles[scope.ScopeIndex]
             .compare_exchange_strong(
                 expected,
                 Impl::ScopeLifecycle::Ended,
                 std::memory_order_acq_rel,
                 std::memory_order_acquire))
    {
        return std::unexpected(RHI::ProfilerError::InvalidState);
    }

    const Impl::QueryPair pair =
        active.ScopeQueries[scope.ScopeIndex];
    if (pair.Supported)
    {
        vkCmdWriteTimestamp2(
            contextView->CommandBuffer,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            m_Impl->QueryPool,
            pair.End);
    }
    return {};
}

std::expected<void, RHI::ProfilerError>
VulkanProfiler::EndFrame(
    const RHI::ProfilerFrameKey frame,
    const RHI::ProfilerFrameDisposition disposition)
{
    if (!m_Impl->Active.has_value() ||
        m_Impl->Active->Frame != frame)
    {
        return std::unexpected(RHI::ProfilerError::InvalidState);
    }
    if (disposition == RHI::ProfilerFrameDisposition::Discarded)
    {
        m_Impl->Active.reset();
        return {};
    }
    if (m_Impl->DeviceLost)
    {
        m_Impl->Active.reset();
        return std::unexpected(RHI::ProfilerError::DeviceLost);
    }

    Impl::ActiveFrame& active = *m_Impl->Active;
    std::vector<bool> scopeEnded(active.Scopes.size(), false);
    for (std::size_t scopeIndex = 0u;
         scopeIndex < active.Scopes.size();
         ++scopeIndex)
    {
        const Impl::ScopeLifecycle lifecycle =
            active.ScopeLifecycles[scopeIndex].load(
                std::memory_order_acquire);
        if (lifecycle == Impl::ScopeLifecycle::Begun)
        {
            return std::unexpected(RHI::ProfilerError::InvalidState);
        }
        scopeEnded[scopeIndex] =
            lifecycle == Impl::ScopeLifecycle::Ended;
    }
    for (const Impl::ActiveQueue& queue : active.Queues)
    {
        if (queue.Lifecycle == Impl::QueueLifecycle::Open)
        {
            return std::unexpected(RHI::ProfilerError::InvalidState);
        }
    }
    if (m_Impl->PendingFrames[frame.FrameSlot].has_value())
    {
        return std::unexpected(RHI::ProfilerError::InvalidState);
    }

    m_Impl->PendingFrames[frame.FrameSlot] = Impl::PendingFrame{
        .Frame = frame,
        .Scopes = std::move(active.Scopes),
        .ScopeQueries = std::move(active.ScopeQueries),
        .ScopeEnded = std::move(scopeEnded),
        .Queues = active.Queues,
    };
    m_Impl->Active.reset();
    return {};
}

std::expected<RHI::GpuTimestampFrame, RHI::ProfilerError>
VulkanProfiler::Resolve(const RHI::ProfilerFrameKey frame) const
{
    if (frame.FrameSlot >= m_Impl->FramesInFlight)
    {
        return std::unexpected(RHI::ProfilerError::InvalidArgument);
    }
    if (m_Impl->DeviceLost)
    {
        return std::unexpected(RHI::ProfilerError::DeviceLost);
    }

    const std::optional<RHI::GpuTimestampFrame>& cached =
        m_Impl->ResolvedFrames[frame.FrameSlot];
    if (cached.has_value() && cached->Frame == frame)
    {
        return *cached;
    }

    const std::optional<Impl::TerminalFrameError>& terminal =
        m_Impl->TerminalErrors[frame.FrameSlot];
    if (terminal.has_value() && terminal->Frame == frame)
    {
        return std::unexpected(terminal->Error);
    }

    const std::optional<Impl::PendingFrame>& pending =
        m_Impl->PendingFrames[frame.FrameSlot];
    if (!pending.has_value() || pending->Frame != frame)
    {
        return std::unexpected(RHI::ProfilerError::NotReady);
    }

    const auto resolved =
        m_Impl->ResolvePending(frame.FrameSlot);
    if (!resolved)
    {
        return std::unexpected(resolved.error());
    }
    m_Impl->ResolvedFrames[frame.FrameSlot] = *resolved;
    m_Impl->PendingFrames[frame.FrameSlot].reset();
    return *resolved;
}

RHI::ProfilerStatusSnapshot VulkanProfiler::GetStatus() const
{
    return m_Impl->Status;
}

std::uint32_t VulkanProfiler::GetFramesInFlight() const
{
    return m_Impl->FramesInFlight;
}

void VulkanProfiler::NotifyFrameSlotComplete(
    const std::uint32_t frameSlot) noexcept
{
    m_Impl->RetireCompletedSlot(frameSlot);
}

void VulkanProfiler::NotifyDeviceLost() noexcept
{
    m_Impl->DeviceLost = true;
    m_Impl->Status = RHI::ProfilerStatusSnapshot{
        .Status = RHI::ProfilerBackendStatus::DeviceLost,
        .Source = RHI::GpuTimestampSource::Unavailable,
        .Diagnostic =
            "Vulkan device loss disabled native timestamp queries.",
    };
}
}
