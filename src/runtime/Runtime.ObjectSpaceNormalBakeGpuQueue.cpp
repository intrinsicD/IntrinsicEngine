module;

#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

module Extrinsic.Runtime.ObjectSpaceNormalBakeGpuQueue;

import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.RHI.CommandContext;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ObjectSpaceNormalBakeBinding;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission;
import Extrinsic.Runtime.RenderExtraction;

namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] bool TryStableEntityId(
            const RuntimeObjectSpaceNormalBakeSubmission& submission,
            std::uint32_t& out) noexcept
        {
            const std::uint64_t entity = submission.StaleKey.Bake.Source.EntityKey;
            if (entity == 0u ||
                entity > static_cast<std::uint64_t>(
                             std::numeric_limits<std::uint32_t>::max()))
            {
                out = 0u;
                return false;
            }
            out = static_cast<std::uint32_t>(entity);
            return out != 0u;
        }

        [[nodiscard]] bool ShouldRetrySubmission(
            const RuntimeObjectSpaceNormalBakeGpuSubmitStatus status) noexcept
        {
            switch (status)
            {
            case RuntimeObjectSpaceNormalBakeGpuSubmitStatus::Submitted:
                return false;
            case RuntimeObjectSpaceNormalBakeGpuSubmitStatus::CacheRejected:
            case RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidPlan:
                return true;
            case RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidStableEntity:
            case RuntimeObjectSpaceNormalBakeGpuSubmitStatus::InvalidQueueSubmission:
            case RuntimeObjectSpaceNormalBakeGpuSubmitStatus::StalePlan:
                return false;
            }
            return false;
        }
    }

    void RuntimeObjectSpaceNormalBakeGpuQueue::SetDependencies(
        RuntimeObjectSpaceNormalBakeGpuQueueDependencies deps)
    {
        if (deps.MaxSubmissionsPerFrame == 0u)
            deps.MaxSubmissionsPerFrame = 1u;
        if (deps.MaxBindingsPerDrain == 0u)
            deps.MaxBindingsPerDrain = 8u;
        m_Dependencies = std::move(deps);
    }

    RuntimeObjectSpaceNormalBakeQueue&
    RuntimeObjectSpaceNormalBakeGpuQueue::Queue() noexcept
    {
        return m_Queue;
    }

    const RuntimeObjectSpaceNormalBakeQueue&
    RuntimeObjectSpaceNormalBakeGpuQueue::Queue() const noexcept
    {
        return m_Queue;
    }

    const RuntimeObjectSpaceNormalBakeGpuQueueDiagnostics&
    RuntimeObjectSpaceNormalBakeGpuQueue::Diagnostics() const noexcept
    {
        return m_Diagnostics;
    }

    GpuQueueParticipantDesc
    RuntimeObjectSpaceNormalBakeGpuQueue::MakeGpuQueueParticipantDesc()
    {
        return GpuQueueParticipantDesc{
            .DebugName = "Runtime.ObjectSpaceNormalBakeGpuQueue",
            .RecordFrameCommands =
                [this](RHI::ICommandContext& commandContext)
                {
                    RecordFrameCommands(commandContext);
                },
            .DrainCompletedTransfers =
                [this]
                {
                    (void)DrainCompletedTransfers();
                },
            .HasInFlightWork =
                [this]
                {
                    return HasInFlightWork();
                },
            .ShutdownAfterDeviceIdle =
                [this]
                {
                    ShutdownAfterDeviceIdle();
                },
        };
    }

    void RuntimeObjectSpaceNormalBakeGpuQueue::RecordFrameCommands(
        RHI::ICommandContext& commandContext)
    {
        m_Diagnostics.LastRecordAttempted = 0u;
        m_Diagnostics.LastRecordSubmitted = 0u;

        if (m_Dependencies.GpuAssets == nullptr)
            return;

        std::vector<RuntimeObjectSpaceNormalBakeSubmission> pending =
            m_Queue.TakePendingSubmissions(
                m_Dependencies.MaxSubmissionsPerFrame);

        for (RuntimeObjectSpaceNormalBakeSubmission& submission : pending)
        {
            ++m_Diagnostics.LastRecordAttempted;

            if (!m_Queue.IsLatest(submission.StaleKey))
            {
                ++m_Diagnostics.PendingStaleDiscards;
                continue;
            }

            std::uint32_t stableEntityId = 0u;
            if (!TryStableEntityId(submission, stableEntityId))
            {
                ++m_Diagnostics.PlanRejected;
                continue;
            }

            if (m_Dependencies.RenderExtraction != nullptr)
            {
                RuntimeObjectSpaceNormalBakeBindingResult cached =
                    TryBindReadyObjectSpaceNormalBake(
                        m_Queue,
                        *m_Dependencies.RenderExtraction,
                        *m_Dependencies.GpuAssets,
                        stableEntityId,
                        submission.StaleKey);
                switch (cached.Status)
                {
                case RuntimeObjectSpaceNormalBakeBindingStatus::Bound:
                    ++m_Diagnostics.BoundCompletions;
                    continue;
                case RuntimeObjectSpaceNormalBakeBindingStatus::StaleCompletion:
                    ++m_Diagnostics.StaleCompletions;
                    continue;
                case RuntimeObjectSpaceNormalBakeBindingStatus::InvalidStableEntity:
                case RuntimeObjectSpaceNormalBakeBindingStatus::InvalidCompletion:
                    ++m_Diagnostics.InvalidCompletions;
                    continue;
                case RuntimeObjectSpaceNormalBakeBindingStatus::WaitingForGpuTexture:
                    break;
                }
            }

            if (!m_Dependencies.BuildPlan)
            {
                ++m_Diagnostics.PlanNotReady;
                m_Queue.RequeuePendingSubmission(std::move(submission));
                continue;
            }

            RuntimeObjectSpaceNormalBakeGpuQueuePlanResult plan =
                m_Dependencies.BuildPlan(submission);
            if (!plan.Succeeded())
            {
                if (plan.Status ==
                    RuntimeObjectSpaceNormalBakeGpuQueuePlanStatus::Invalid)
                {
                    ++m_Diagnostics.PlanRejected;
                }
                else
                {
                    ++m_Diagnostics.PlanNotReady;
                    m_Queue.RequeuePendingSubmission(std::move(submission));
                }
                continue;
            }

            RuntimeObjectSpaceNormalBakeGpuSubmitResult submitted =
                BeginObjectSpaceNormalBakeGpuSubmission(
                    *m_Dependencies.GpuAssets,
                    stableEntityId,
                    submission,
                    plan.Plan);
            if (!submitted.Succeeded())
            {
                if (submitted.Status ==
                    RuntimeObjectSpaceNormalBakeGpuSubmitStatus::CacheRejected)
                {
                    ++m_Diagnostics.CacheRejected;
                }
                else
                {
                    ++m_Diagnostics.PlanRejected;
                }

                if (ShouldRetrySubmission(submitted.Status))
                    m_Queue.RequeuePendingSubmission(std::move(submission));
                continue;
            }

            Core::Result recorded = Graphics::RecordObjectSpaceNormalTextureBake(
                commandContext,
                submitted.Ticket.RecordDesc);
            if (!recorded.has_value())
            {
                ++m_Diagnostics.RecordFailures;
                // BUG-074: BeginObjectSpaceNormalBakeGpuSubmission opened a
                // pending cache slot for this asset. Dropping the ticket without
                // failing the slot leaves it pending forever, so every future
                // re-schedule of the same entity hits ResourceBusy ->
                // CacheRejected -> requeue, livelocking. NotifyFailed retires the
                // pending lease and lets a later schedule start clean.
                m_Dependencies.GpuAssets->NotifyFailed(
                    submitted.Ticket.GeneratedTextureAsset);
                continue;
            }

            const std::uint64_t readyFrame = m_Dependencies.ReadyFrame
                ? m_Dependencies.ReadyFrame()
                : 0u;
            Core::Result markedReady = MarkObjectSpaceNormalBakeGpuSubmissionReady(
                *m_Dependencies.GpuAssets,
                submitted.Ticket,
                readyFrame);
            if (!markedReady.has_value())
            {
                ++m_Diagnostics.ReadyFrameFailures;
                // BUG-074: retire the pending cache slot on the ready-frame
                // failure path too (see the record-failure path above).
                m_Dependencies.GpuAssets->NotifyFailed(
                    submitted.Ticket.GeneratedTextureAsset);
                continue;
            }

            m_InFlight.push_back(std::move(submitted.Ticket));
            ++m_Diagnostics.RecordedSubmissions;
            ++m_Diagnostics.LastRecordSubmitted;
        }
    }

    std::uint64_t RuntimeObjectSpaceNormalBakeGpuQueue::DrainCompletedTransfers()
    {
        m_Diagnostics.LastDrainProcessed = 0u;
        m_Diagnostics.LastDrainBound = 0u;

        if (m_Dependencies.GpuAssets == nullptr ||
            m_Dependencies.RenderExtraction == nullptr ||
            m_InFlight.empty())
        {
            return 0u;
        }

        std::vector<RuntimeObjectSpaceNormalBakeGpuSubmissionTicket> remaining{};
        remaining.reserve(m_InFlight.size());

        for (RuntimeObjectSpaceNormalBakeGpuSubmissionTicket& ticket : m_InFlight)
        {
            if (m_Diagnostics.LastDrainProcessed >=
                m_Dependencies.MaxBindingsPerDrain)
            {
                remaining.push_back(std::move(ticket));
                continue;
            }

            ++m_Diagnostics.LastDrainProcessed;
            RuntimeObjectSpaceNormalBakeBindingResult bound =
                TryBindReadyObjectSpaceNormalBake(
                    m_Queue,
                    *m_Dependencies.RenderExtraction,
                    *m_Dependencies.GpuAssets,
                    ticket.StableEntityId,
                    ticket.Completion);

            switch (bound.Status)
            {
            case RuntimeObjectSpaceNormalBakeBindingStatus::Bound:
                ++m_Diagnostics.BoundCompletions;
                ++m_Diagnostics.LastDrainBound;
                break;
            case RuntimeObjectSpaceNormalBakeBindingStatus::WaitingForGpuTexture:
                ++m_Diagnostics.WaitingBindings;
                remaining.push_back(std::move(ticket));
                break;
            case RuntimeObjectSpaceNormalBakeBindingStatus::StaleCompletion:
                ++m_Diagnostics.StaleCompletions;
                break;
            case RuntimeObjectSpaceNormalBakeBindingStatus::InvalidStableEntity:
            case RuntimeObjectSpaceNormalBakeBindingStatus::InvalidCompletion:
                ++m_Diagnostics.InvalidCompletions;
                break;
            }
        }

        m_InFlight = std::move(remaining);
        return m_Diagnostics.LastDrainProcessed;
    }

    bool RuntimeObjectSpaceNormalBakeGpuQueue::HasInFlightWork() const noexcept
    {
        return m_Queue.PendingSubmissionCount() != 0u || !m_InFlight.empty();
    }

    void RuntimeObjectSpaceNormalBakeGpuQueue::ShutdownAfterDeviceIdle()
    {
        m_Diagnostics.ShutdownDiscards +=
            m_Queue.PendingSubmissionCount() + m_InFlight.size();
        m_Queue.ClearPending();
        m_InFlight.clear();
    }
}
