module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

module Extrinsic.Runtime.ObjectSpaceNormalBakeService;

import Extrinsic.Core.Error;
import Extrinsic.Graphics.GpuAssetCache;
import Extrinsic.Graphics.ObjectSpaceNormalTextureBake;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.ObjectSpaceNormalBakeBinding;
import Extrinsic.Runtime.ObjectSpaceNormalBakeQueue;
import Extrinsic.Runtime.ObjectSpaceNormalBakeSubmission;
import Extrinsic.Runtime.RenderExtraction;

namespace Extrinsic::Runtime
{
    namespace
    {
        enum class ObjectSpaceNormalBakePlanStatus : std::uint8_t
        {
            Ready,
            NotReady,
            Invalid,
        };

        struct ObjectSpaceNormalBakePlanResult
        {
            ObjectSpaceNormalBakePlanStatus Status{
                ObjectSpaceNormalBakePlanStatus::NotReady};
            Graphics::ObjectSpaceNormalTextureBakePlan Plan{};

            [[nodiscard]] bool Succeeded() const noexcept
            {
                return Status == ObjectSpaceNormalBakePlanStatus::Ready &&
                       Plan.Succeeded();
            }
        };

        using ObjectSpaceNormalBakePlanProvider =
            std::function<ObjectSpaceNormalBakePlanResult(
                const RuntimeObjectSpaceNormalBakeSubmission&)>;
        using ObjectSpaceNormalBakeReadyFrameProvider =
            std::function<std::uint64_t()>;
        using ObjectSpaceNormalBakeMarkReady =
            std::function<Core::Result(
                Graphics::GpuAssetCache&,
                const RuntimeObjectSpaceNormalBakeGpuSubmissionTicket&,
                std::uint64_t)>;

        struct ObjectSpaceNormalBakeQueueDependencies
        {
            Graphics::GpuAssetCache* GpuAssets{};
            RenderExtractionCache* RenderExtraction{};
            ObjectSpaceNormalBakePlanProvider BuildPlan{};
            ObjectSpaceNormalBakeReadyFrameProvider ReadyFrame{};
            ObjectSpaceNormalBakeMarkReady MarkReady{};
            std::size_t MaxSubmissionsPerFrame{1u};
            std::size_t MaxBindingsPerDrain{8u};
        };

        struct ObjectSpaceNormalBakeGpuDiagnostics
        {
            std::uint64_t PendingStaleDiscards{0u};
            std::uint64_t PlanNotReady{0u};
            std::uint64_t PlanRejected{0u};
            std::uint64_t CacheRejected{0u};
            std::uint64_t RecordedSubmissions{0u};
            std::uint64_t RecordFailures{0u};
            std::uint64_t ReadyFrameFailures{0u};
            std::uint64_t WaitingBindings{0u};
            std::uint64_t BoundCompletions{0u};
            std::uint64_t StaleCompletions{0u};
            std::uint64_t InvalidCompletions{0u};
            std::uint64_t ShutdownDiscards{0u};
            std::uint64_t LastRecordAttempted{0u};
            std::uint64_t LastRecordSubmitted{0u};
            std::uint64_t LastDrainProcessed{0u};
            std::uint64_t LastDrainBound{0u};
        };

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

        [[nodiscard]] Graphics::ObjectSpaceNormalTextureBakePlan
        BuildDeterministicTestPlan(
            const RuntimeObjectSpaceNormalBakeSubmission& submission)
        {
            const Graphics::ObjectSpaceNormalTextureBakeCompletionKey& key =
                submission.StaleKey.Bake;
            return Graphics::BuildObjectSpaceNormalTextureBakePlan(
                Graphics::ObjectSpaceNormalTextureBakePlanRequest{
                    .GeneratedTextureAsset = submission.GeneratedTextureAsset,
                    .Geometry =
                        Graphics::ObjectSpaceNormalTextureBakeGeometryBuffers{
                            .IndexBuffer = RHI::BufferHandle{40u, 1u},
                            .TexcoordBDA = 0x1000u,
                            .NormalBDA = 0x2000u,
                            .VertexCount = 3u,
                            .IndexCount = 3u,
                        },
                    .Options = Graphics::ObjectSpaceNormalTextureBakeOptions{
                        .Width = key.Width,
                        .Height = key.Height,
                        .PaddingTexels = key.PaddingTexels,
                        .Space = key.Space,
                    },
                    .SourceKey = key.Source,
                    .Pipeline = RHI::PipelineHandle{5u, 1u},
                    .DebugName =
                        "runtime-object-space-normal-bake-service-test",
                });
        }
    }

    struct ObjectSpaceNormalBakeService::Impl
    {
        RuntimeObjectSpaceNormalBakeQueue Queue{};
        ObjectSpaceNormalBakeQueueDependencies QueueDependencies{};
        std::vector<RuntimeObjectSpaceNormalBakeGpuSubmissionTicket> InFlight{};
        ObjectSpaceNormalBakeGpuDiagnostics Diagnostics{};
        ObjectSpaceNormalBakeServiceDependencies ServiceDependencies{};
        ObjectSpaceNormalBakeServiceTestHooks TestHooks{};
        std::uint64_t TestPlanBuildCount{0u};
        std::uint64_t TestReadyPublicationCount{0u};
        GpuQueueParticipantHandle ParticipantHandle{};

        void ConfigureDependencies()
        {
            RHI::IDevice* device = ServiceDependencies.Device;
            ObjectSpaceNormalBakePlanProvider buildPlan{};
            if (TestHooks.EnableDeterministicPlan)
            {
                buildPlan =
                    [this](
                        const RuntimeObjectSpaceNormalBakeSubmission& submission)
                    {
                        Graphics::ObjectSpaceNormalTextureBakePlan plan =
                            BuildDeterministicTestPlan(submission);
                        if (TestHooks.InvalidateFirstRecord &&
                            TestPlanBuildCount == 0u)
                        {
                            plan.RecordTemplate.Pipeline = {};
                        }
                        ++TestPlanBuildCount;
                        return ObjectSpaceNormalBakePlanResult{
                            .Status = plan.Succeeded()
                                ? ObjectSpaceNormalBakePlanStatus::Ready
                                : ObjectSpaceNormalBakePlanStatus::Invalid,
                            .Plan = std::move(plan),
                        };
                    };
            }

            ObjectSpaceNormalBakeMarkReady markReady{};
            if (TestHooks.RejectFirstReadyPublication)
            {
                markReady =
                    [this](
                        Graphics::GpuAssetCache& gpuAssets,
                        const RuntimeObjectSpaceNormalBakeGpuSubmissionTicket&
                            ticket,
                        const std::uint64_t readyFrame) -> Core::Result
                    {
                        if (TestReadyPublicationCount++ == 0u)
                            return Core::Err(Core::ErrorCode::InvalidState);
                        return MarkObjectSpaceNormalBakeGpuSubmissionReady(
                            gpuAssets,
                            ticket,
                            readyFrame);
                    };
            }

            SetQueueDependencies(
                ObjectSpaceNormalBakeQueueDependencies{
                    .GpuAssets = ServiceDependencies.GpuAssets,
                    .RenderExtraction = ServiceDependencies.RenderExtraction,
                    .BuildPlan = std::move(buildPlan),
                    .ReadyFrame =
                        [device]() -> std::uint64_t
                        {
                            // BUG-073: gate readiness FramesInFlight ahead of
                            // the issue frame, not +1. The CPU cannot run more
                            // than FramesInFlight ahead of the GPU, so this is
                            // the first frame where binding is safe.
                            return device != nullptr
                                ? ObjectSpaceNormalBakeReadyFrame(
                                      device->GetGlobalFrameNumber(),
                                      device->GetFramesInFlight())
                                : 0u;
                        },
                    .MarkReady = std::move(markReady),
                });
        }

        void SetQueueDependencies(ObjectSpaceNormalBakeQueueDependencies deps)
        {
            if (deps.MaxSubmissionsPerFrame == 0u)
                deps.MaxSubmissionsPerFrame = 1u;
            if (deps.MaxBindingsPerDrain == 0u)
                deps.MaxBindingsPerDrain = 8u;
            QueueDependencies = std::move(deps);
        }

        [[nodiscard]] GpuQueueParticipantDesc MakeGpuQueueParticipantDesc()
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

        void RecordFrameCommands(RHI::ICommandContext& commandContext)
        {
            Diagnostics.LastRecordAttempted = 0u;
            Diagnostics.LastRecordSubmitted = 0u;

            if (QueueDependencies.GpuAssets == nullptr)
                return;

            std::vector<RuntimeObjectSpaceNormalBakeSubmission> pending =
                Queue.TakePendingSubmissions(
                    QueueDependencies.MaxSubmissionsPerFrame);

            for (RuntimeObjectSpaceNormalBakeSubmission& submission : pending)
            {
                ++Diagnostics.LastRecordAttempted;

                if (!Queue.IsLatest(submission.StaleKey))
                {
                    ++Diagnostics.PendingStaleDiscards;
                    continue;
                }

                std::uint32_t stableEntityId = 0u;
                if (!TryStableEntityId(submission, stableEntityId))
                {
                    ++Diagnostics.PlanRejected;
                    continue;
                }

                if (QueueDependencies.RenderExtraction != nullptr)
                {
                    RuntimeObjectSpaceNormalBakeBindingResult cached =
                        TryBindReadyObjectSpaceNormalBake(
                            Queue,
                            *QueueDependencies.RenderExtraction,
                            *QueueDependencies.GpuAssets,
                            stableEntityId,
                            submission.StaleKey);
                    switch (cached.Status)
                    {
                    case RuntimeObjectSpaceNormalBakeBindingStatus::Bound:
                        ++Diagnostics.BoundCompletions;
                        continue;
                    case RuntimeObjectSpaceNormalBakeBindingStatus::StaleCompletion:
                        ++Diagnostics.StaleCompletions;
                        continue;
                    case RuntimeObjectSpaceNormalBakeBindingStatus::InvalidStableEntity:
                    case RuntimeObjectSpaceNormalBakeBindingStatus::InvalidCompletion:
                        ++Diagnostics.InvalidCompletions;
                        continue;
                    case RuntimeObjectSpaceNormalBakeBindingStatus::WaitingForGpuTexture:
                        break;
                    }
                }

                if (!QueueDependencies.BuildPlan)
                {
                    ++Diagnostics.PlanNotReady;
                    Queue.RequeuePendingSubmission(std::move(submission));
                    continue;
                }

                ObjectSpaceNormalBakePlanResult plan =
                    QueueDependencies.BuildPlan(submission);
                if (!plan.Succeeded())
                {
                    if (plan.Status == ObjectSpaceNormalBakePlanStatus::Invalid)
                    {
                        ++Diagnostics.PlanRejected;
                    }
                    else
                    {
                        ++Diagnostics.PlanNotReady;
                        Queue.RequeuePendingSubmission(std::move(submission));
                    }
                    continue;
                }

                RuntimeObjectSpaceNormalBakeGpuSubmitResult submitted =
                    BeginObjectSpaceNormalBakeGpuSubmission(
                        *QueueDependencies.GpuAssets,
                        stableEntityId,
                        submission,
                        plan.Plan);
                if (!submitted.Succeeded())
                {
                    if (submitted.Status ==
                        RuntimeObjectSpaceNormalBakeGpuSubmitStatus::CacheRejected)
                    {
                        ++Diagnostics.CacheRejected;
                    }
                    else
                    {
                        ++Diagnostics.PlanRejected;
                    }

                    if (ShouldRetrySubmission(submitted.Status))
                        Queue.RequeuePendingSubmission(std::move(submission));
                    continue;
                }

                Core::Result recorded =
                    Graphics::RecordObjectSpaceNormalTextureBake(
                        commandContext,
                        submitted.Ticket.RecordDesc);
                if (!recorded.has_value())
                {
                    ++Diagnostics.RecordFailures;
                    // BUG-074: retire only the pending cache generation opened
                    // by this ticket so a later explicit schedule cannot enter
                    // ResourceBusy -> requeue livelock.
                    (void)QueueDependencies.GpuAssets->FailGpuProducedTexture(
                        submitted.Ticket.GeneratedTextureAsset,
                        submitted.Ticket.CacheGeneration);
                    continue;
                }

                const std::uint64_t readyFrame = QueueDependencies.ReadyFrame
                    ? QueueDependencies.ReadyFrame()
                    : 0u;
                Core::Result markedReady = QueueDependencies.MarkReady
                    ? QueueDependencies.MarkReady(
                          *QueueDependencies.GpuAssets,
                          submitted.Ticket,
                          readyFrame)
                    : MarkObjectSpaceNormalBakeGpuSubmissionReady(
                          *QueueDependencies.GpuAssets,
                          submitted.Ticket,
                          readyFrame);
                if (!markedReady.has_value())
                {
                    ++Diagnostics.ReadyFrameFailures;
                    // BUG-074: the ready-frame failure owns the same exact
                    // pending cache generation as the record-failure path.
                    (void)QueueDependencies.GpuAssets->FailGpuProducedTexture(
                        submitted.Ticket.GeneratedTextureAsset,
                        submitted.Ticket.CacheGeneration);
                    continue;
                }

                InFlight.push_back(std::move(submitted.Ticket));
                ++Diagnostics.RecordedSubmissions;
                ++Diagnostics.LastRecordSubmitted;
            }
        }

        [[nodiscard]] std::uint64_t DrainCompletedTransfers()
        {
            Diagnostics.LastDrainProcessed = 0u;
            Diagnostics.LastDrainBound = 0u;

            if (QueueDependencies.GpuAssets == nullptr ||
                QueueDependencies.RenderExtraction == nullptr ||
                InFlight.empty())
            {
                return 0u;
            }

            std::vector<RuntimeObjectSpaceNormalBakeGpuSubmissionTicket> remaining{};
            remaining.reserve(InFlight.size());

            for (RuntimeObjectSpaceNormalBakeGpuSubmissionTicket& ticket : InFlight)
            {
                if (Diagnostics.LastDrainProcessed >=
                    QueueDependencies.MaxBindingsPerDrain)
                {
                    remaining.push_back(std::move(ticket));
                    continue;
                }

                ++Diagnostics.LastDrainProcessed;
                RuntimeObjectSpaceNormalBakeBindingResult bound =
                    TryBindReadyObjectSpaceNormalBake(
                        Queue,
                        *QueueDependencies.RenderExtraction,
                        *QueueDependencies.GpuAssets,
                        ticket.StableEntityId,
                        ticket.Completion);

                switch (bound.Status)
                {
                case RuntimeObjectSpaceNormalBakeBindingStatus::Bound:
                    ++Diagnostics.BoundCompletions;
                    ++Diagnostics.LastDrainBound;
                    break;
                case RuntimeObjectSpaceNormalBakeBindingStatus::WaitingForGpuTexture:
                    ++Diagnostics.WaitingBindings;
                    remaining.push_back(std::move(ticket));
                    break;
                case RuntimeObjectSpaceNormalBakeBindingStatus::StaleCompletion:
                    ++Diagnostics.StaleCompletions;
                    break;
                case RuntimeObjectSpaceNormalBakeBindingStatus::InvalidStableEntity:
                case RuntimeObjectSpaceNormalBakeBindingStatus::InvalidCompletion:
                    ++Diagnostics.InvalidCompletions;
                    break;
                }
            }

            InFlight = std::move(remaining);
            return Diagnostics.LastDrainProcessed;
        }

        [[nodiscard]] bool HasInFlightWork() const noexcept
        {
            return Queue.PendingSubmissionCount() != 0u || !InFlight.empty();
        }

        void ShutdownAfterDeviceIdle()
        {
            Diagnostics.ShutdownDiscards +=
                Queue.PendingSubmissionCount() + InFlight.size();
            Queue.ClearPending();
            InFlight.clear();
        }
    };

    std::uint64_t ObjectSpaceNormalBakeReadyFrame(
        const std::uint64_t issueFrame,
        const std::uint32_t framesInFlight) noexcept
    {
        return issueFrame + framesInFlight;
    }

    ObjectSpaceNormalBakeService::ObjectSpaceNormalBakeService()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    ObjectSpaceNormalBakeService::~ObjectSpaceNormalBakeService() = default;

    void ObjectSpaceNormalBakeService::SetDependencies(
        ObjectSpaceNormalBakeServiceDependencies deps)
    {
        m_Impl->ServiceDependencies = deps;
        m_Impl->ConfigureDependencies();
    }

    void ObjectSpaceNormalBakeService::ClearDependencies()
    {
        m_Impl->ServiceDependencies = {};
        m_Impl->ConfigureDependencies();
        m_Impl->ParticipantHandle = {};
    }

    GpuQueueParticipantHandle
    ObjectSpaceNormalBakeService::RegisterGpuQueueParticipant(JobService& jobs)
    {
        if (m_Impl->ParticipantHandle.IsValid())
            return m_Impl->ParticipantHandle;

        m_Impl->ParticipantHandle = jobs.RegisterGpuQueueParticipant(
            m_Impl->MakeGpuQueueParticipantDesc());
        return m_Impl->ParticipantHandle;
    }

    RuntimeObjectSpaceNormalBakeQueue&
    ObjectSpaceNormalBakeService::Queue() noexcept
    {
        return m_Impl->Queue;
    }

    const RuntimeObjectSpaceNormalBakeQueue&
    ObjectSpaceNormalBakeService::Queue() const noexcept
    {
        return m_Impl->Queue;
    }

    const RuntimeObjectSpaceNormalBakeQueueDiagnostics&
    ObjectSpaceNormalBakeService::QueueDiagnostics() const noexcept
    {
        return m_Impl->Queue.Diagnostics();
    }

    std::size_t ObjectSpaceNormalBakeService::PendingCount() const noexcept
    {
        return m_Impl->Queue.PendingCount();
    }

    void SetObjectSpaceNormalBakeServiceTestHooks(
        ObjectSpaceNormalBakeService& service,
        ObjectSpaceNormalBakeServiceTestHooks hooks)
    {
        service.m_Impl->TestHooks = std::move(hooks);
        service.m_Impl->TestPlanBuildCount = 0u;
        service.m_Impl->TestReadyPublicationCount = 0u;
        service.m_Impl->ConfigureDependencies();
    }

    ObjectSpaceNormalBakeServiceTestDiagnostics
    GetObjectSpaceNormalBakeServiceTestDiagnostics(
        const ObjectSpaceNormalBakeService& service) noexcept
    {
        const ObjectSpaceNormalBakeGpuDiagnostics& diagnostics =
            service.m_Impl->Diagnostics;
        return ObjectSpaceNormalBakeServiceTestDiagnostics{
            .PendingStaleDiscards = diagnostics.PendingStaleDiscards,
            .CacheRejected = diagnostics.CacheRejected,
            .RecordedSubmissions = diagnostics.RecordedSubmissions,
            .RecordFailures = diagnostics.RecordFailures,
            .ReadyFrameFailures = diagnostics.ReadyFrameFailures,
            .WaitingBindings = diagnostics.WaitingBindings,
            .BoundCompletions = diagnostics.BoundCompletions,
            .LastRecordAttempted = diagnostics.LastRecordAttempted,
            .LastRecordSubmitted = diagnostics.LastRecordSubmitted,
            .LastDrainProcessed = diagnostics.LastDrainProcessed,
            .LastDrainBound = diagnostics.LastDrainBound,
        };
    }
}
