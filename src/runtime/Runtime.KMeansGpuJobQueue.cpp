module;

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Runtime.KMeansGpuJobQueue;

import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import Extrinsic.Runtime.KMeansGpuBackend;
import Geometry.KMeans;

namespace Extrinsic::Runtime
{
    namespace GK = Geometry::KMeans;

    namespace
    {
        class NoopCommandContext final : public RHI::ICommandContext
        {
        public:
            void Begin() override {}
            void End() override {}
            void BeginRenderPass(const RHI::RenderPassDesc&) override {}
            void EndRenderPass() override {}
            void SetViewport(float, float, float, float, float, float) override {}
            void SetScissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}
            void BindPipeline(RHI::PipelineHandle) override {}
            void BindIndexBuffer(RHI::BufferHandle, std::uint64_t, RHI::IndexType) override {}
            void PushConstants(const void*, std::uint32_t, std::uint32_t) override {}
            void Draw(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t) override {}
            void DrawIndexed(std::uint32_t, std::uint32_t, std::uint32_t, std::int32_t, std::uint32_t) override {}
            void DrawIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
            void DrawIndexedIndirect(RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
            void DrawIndexedIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
            void DrawIndirectCount(RHI::BufferHandle, std::uint64_t, RHI::BufferHandle, std::uint64_t, std::uint32_t) override {}
            void Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override {}
            void DispatchIndirect(RHI::BufferHandle, std::uint64_t) override {}
            void TextureBarrier(RHI::TextureHandle, RHI::TextureLayout, RHI::TextureLayout) override {}
            void BufferBarrier(RHI::BufferHandle, RHI::MemoryAccess, RHI::MemoryAccess) override {}
            void SubmitBarriers(const RHI::BarrierBatchDesc&) override {}
            void FillBuffer(RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint32_t) override {}
            void CopyBuffer(RHI::BufferHandle, RHI::BufferHandle, std::uint64_t, std::uint64_t, std::uint64_t) override {}
            void CopyBufferToTexture(RHI::BufferHandle, std::uint64_t, RHI::TextureHandle, std::uint32_t, std::uint32_t) override {}
        };

        [[nodiscard]] RuntimeKMeansGpuJobSubmission MakeSubmission(
            const RuntimeKMeansGpuJobStatus status,
            const std::uint64_t sequence,
            const KMeansGpuStatus gpuStatus,
            std::string diagnostic = {})
        {
            return RuntimeKMeansGpuJobSubmission{
                .Status = status,
                .Sequence = sequence,
                .GpuStatus = gpuStatus,
                .Diagnostic = std::move(diagnostic),
            };
        }

        [[nodiscard]] bool IsValidRequest(
            const RuntimeKMeansGpuJobRequest& request) noexcept
        {
            return !request.Points.empty() &&
                   request.Points.size() <=
                       static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) &&
                   request.Params.ClusterCount > 0u &&
                   request.Params.MaxIterations > 0u;
        }

        [[nodiscard]] std::uint32_t EffectiveClusterCount(
            const RuntimeKMeansGpuJobRequest& request) noexcept
        {
            return std::min<std::uint32_t>(
                request.Params.ClusterCount,
                static_cast<std::uint32_t>(request.Points.size()));
        }

        [[nodiscard]] KMeansGpuPlanDesc BuildPlanDesc(
            const RuntimeKMeansGpuJobRequest& request) noexcept
        {
            return KMeansGpuPlanDesc{
                .PointCount = static_cast<std::uint32_t>(request.Points.size()),
                .ClusterCount = EffectiveClusterCount(request),
                .MaxIterations = request.Params.MaxIterations,
                .GroupSize = kKMeansGpuGroupSize,
                .ConvergenceTolerance = request.Params.ConvergenceTolerance,
            };
        }

        [[nodiscard]] GK::KMeansResult BuildGeometryResult(
            const RuntimeKMeansGpuJobRequest& request,
            const KMeansGpuDispatchPlan& plan,
            KMeansGpuReadbackResult readback)
        {
            GK::KMeansResult result{};
            result.Labels = std::move(readback.Labels);
            result.SquaredDistances = std::move(readback.SquaredDistances);
            result.Centroids = std::move(readback.Centroids);
            result.Iterations = plan.MaxIterations;
            result.Converged = false;
            result.Inertia = readback.Inertia;
            result.MaxDistanceIndex = readback.MaxDistanceIndex;
            result.RequestedBackend = request.Params.Compute;
            result.ActualBackend = GK::Backend::GPU;
            result.FellBackToCPU = false;
            return result;
        }
    }

    const char* DebugNameForRuntimeKMeansGpuJobStatus(
        const RuntimeKMeansGpuJobStatus status) noexcept
    {
        switch (status)
        {
        case RuntimeKMeansGpuJobStatus::Idle: return "Idle";
        case RuntimeKMeansGpuJobStatus::Accepted: return "Accepted";
        case RuntimeKMeansGpuJobStatus::Busy: return "Busy";
        case RuntimeKMeansGpuJobStatus::InvalidInput: return "InvalidInput";
        case RuntimeKMeansGpuJobStatus::GpuUnavailable: return "GpuUnavailable";
        case RuntimeKMeansGpuJobStatus::PipelineUnavailable: return "PipelineUnavailable";
        case RuntimeKMeansGpuJobStatus::RecordFailed: return "RecordFailed";
        case RuntimeKMeansGpuJobStatus::ReadbackPending: return "ReadbackPending";
        case RuntimeKMeansGpuJobStatus::ReadbackFailed: return "ReadbackFailed";
        case RuntimeKMeansGpuJobStatus::Completed: return "Completed";
        }
        return "Unknown";
    }

    struct RuntimeKMeansGpuJobQueue::Impl
    {
        struct ActiveJob
        {
            RuntimeKMeansGpuJobRequest Request{};
            std::vector<glm::vec3> Seeds{};
            KMeansGpuPlanDesc PlanDesc{};
            KMeansGpuDispatchPlan Plan{};
            const KMeansGpuExecutionResources* Resources{nullptr};
            std::uint64_t ProducerCompletedFrame{0u};
            bool ProducerSubmitted{false};
            bool ReadbackSubmitted{false};
        };

        Impl(RHI::IDevice& device,
             RHI::BufferManager& buffers,
             RHI::ITransferQueue& transferQueue)
            : Device(&device)
            , TransferQueue(&transferQueue)
            , Transfer(transferQueue)
            , Cache(buffers)
            , Readbacks(Transfer)
        {
        }

        ~Impl()
        {
            DestroyPipelines();
        }

        [[nodiscard]] bool HasBusyState() const noexcept
        {
            return Active.has_value() || Completed.has_value();
        }

        [[nodiscard]] RuntimeKMeansGpuJobSubmission Submit(
            RuntimeKMeansGpuJobRequest request)
        {
            if (HasBusyState())
            {
                return MakeSubmission(
                    RuntimeKMeansGpuJobStatus::Busy,
                    Active.has_value() ? Active->Request.Sequence : Completed->Sequence,
                    KMeansGpuStatus::Success,
                    "A K-Means GPU job is already pending or completed but not consumed.");
            }

            if (Device == nullptr || TransferQueue == nullptr || !IsValidRequest(request))
            {
                return MakeSubmission(
                    RuntimeKMeansGpuJobStatus::InvalidInput,
                    request.Sequence,
                    KMeansGpuStatus::InvalidInput,
                    "K-Means GPU job requires non-empty points and positive cluster/iteration counts.");
            }

            request.Params.Compute = GK::Backend::GPU;
            if (request.Sequence == 0u)
                request.Sequence = ++NextSequence;
            else
                NextSequence = std::max(NextSequence, request.Sequence);

            const std::uint32_t clusterCount = EffectiveClusterCount(request);
            std::vector<glm::vec3> seeds =
                GK::BuildInitialCentroids(
                    std::span<const glm::vec3>{request.Points.data(), request.Points.size()},
                    std::span<const glm::vec3>{request.InitialCentroids.data(),
                                               request.InitialCentroids.size()},
                    request.Params,
                    clusterCount);
            if (seeds.size() != clusterCount)
            {
                return MakeSubmission(
                    RuntimeKMeansGpuJobStatus::InvalidInput,
                    request.Sequence,
                    KMeansGpuStatus::InvalidInput,
                    "K-Means GPU job could not build a valid centroid seed set.");
            }

            const KMeansGpuPlanDesc planDesc = BuildPlanDesc(request);
            const KMeansGpuResolveResult resolved =
                ResolveKMeansGpuRequest(KMeansGpuResolveDesc{
                    .Device = Device,
                    .Plan = planDesc,
                });
            if (!resolved.GpuExecutionAvailable)
            {
                return MakeSubmission(
                    RuntimeKMeansGpuJobStatus::GpuUnavailable,
                    request.Sequence,
                    resolved.Status,
                    resolved.Diagnostic.empty()
                        ? "K-Means Vulkan compute execution is unavailable."
                        : resolved.Diagnostic);
            }

            Readbacks.Reset();
            Active = ActiveJob{
                .Request = std::move(request),
                .Seeds = std::move(seeds),
                .PlanDesc = planDesc,
                .Plan = resolved.Plan,
            };
            return MakeSubmission(
                RuntimeKMeansGpuJobStatus::Accepted,
                Active->Request.Sequence,
                KMeansGpuStatus::Success);
        }

        void AdvanceGpuWork(RHI::ICommandContext& commandContext)
        {
            if (!Active.has_value() || Device == nullptr || Completed.has_value())
                return;

            if (!Active->ProducerSubmitted)
            {
                RecordProducerCommands(commandContext);
                return;
            }

            if (!Active->ReadbackSubmitted)
            {
                const std::uint32_t framesInFlight =
                    std::max(Device->GetFramesInFlight(), 1u);
                const std::uint64_t requiredFrame =
                    Active->ProducerCompletedFrame +
                    static_cast<std::uint64_t>(framesInFlight - 1u);
                if (Device->GetGlobalFrameNumber() < requiredFrame)
                    return;

                RecordReadbackCommands(commandContext);
            }
        }

        void DrainCompletedTransfers()
        {
            if (!Active.has_value() || !Active->ReadbackSubmitted)
                return;

            NoopCommandContext noopContext;
            Transfer.DrainCompleted(noopContext);
            if (!Readbacks.Poll())
                return;

            KMeansGpuReadbackResult readback = Readbacks.Collect(Active->Plan);
            if (!readback.Succeeded())
            {
                CompleteFailure(RuntimeKMeansGpuJobStatus::ReadbackFailed,
                                readback.Status,
                                readback.Diagnostic.empty()
                                    ? "K-Means GPU readback failed."
                                    : std::move(readback.Diagnostic));
                return;
            }

            RuntimeKMeansGpuJobResult completed{};
            completed.Status = RuntimeKMeansGpuJobStatus::Completed;
            completed.Sequence = Active->Request.Sequence;
            completed.StableEntityId = Active->Request.StableEntityId;
            completed.DomainTag = Active->Request.DomainTag;
            completed.GpuStatus = readback.Status;
            completed.Result = BuildGeometryResult(
                Active->Request,
                Active->Plan,
                std::move(readback));
            Completed = std::move(completed);
            Readbacks.Reset();
            Active.reset();
        }

        [[nodiscard]] std::optional<RuntimeKMeansGpuJobResult> ConsumeCompleted()
        {
            if (!Completed.has_value())
                return std::nullopt;
            std::optional<RuntimeKMeansGpuJobResult> out =
                std::move(Completed);
            Completed.reset();
            return out;
        }

        [[nodiscard]] bool EnsurePipelines()
        {
            if (Pipelines.IsValid())
                return true;
            if (Device == nullptr || !Device->IsOperational())
                return false;

            const std::string resetShader =
                Core::Filesystem::GetShaderPath("shaders/kmeans_reset.comp.spv");
            const std::string assignShader =
                Core::Filesystem::GetShaderPath("shaders/kmeans_assign.comp.spv");
            const std::string updateShader =
                Core::Filesystem::GetShaderPath("shaders/kmeans_update.comp.spv");

            Pipelines.Reset = Device->CreatePipeline(
                BuildKMeansResetPipelineDesc(resetShader.c_str()));
            Pipelines.Assign = Device->CreatePipeline(
                BuildKMeansAssignPipelineDesc(assignShader.c_str()));
            Pipelines.Update = Device->CreatePipeline(
                BuildKMeansUpdatePipelineDesc(updateShader.c_str()));

            if (!Pipelines.IsValid())
            {
                DestroyPipelines();
                return false;
            }
            return true;
        }

        void DestroyPipelines()
        {
            if (Device == nullptr)
            {
                Pipelines = {};
                return;
            }
            if (Pipelines.Reset.IsValid())
                Device->DestroyPipeline(Pipelines.Reset);
            if (Pipelines.Assign.IsValid())
                Device->DestroyPipeline(Pipelines.Assign);
            if (Pipelines.Update.IsValid())
                Device->DestroyPipeline(Pipelines.Update);
            Pipelines = {};
        }

        void RecordProducerCommands(RHI::ICommandContext& commandContext)
        {
            if (!EnsurePipelines())
            {
                CompleteFailure(RuntimeKMeansGpuJobStatus::PipelineUnavailable,
                                KMeansGpuStatus::InvalidGpuResource,
                                "K-Means Vulkan compute pipelines are unavailable.");
                return;
            }

            const KMeansGpuExecutionResult execution =
                RecordKMeansGpuExecution(KMeansGpuExecutionDesc{
                    .Device = Device,
                    .CommandContext = &commandContext,
                    .ResourceCache = &Cache,
                    .Pipelines = Pipelines,
                    .Points = std::span<const glm::vec3>{
                        Active->Request.Points.data(),
                        Active->Request.Points.size()},
                    .SeedCentroids = std::span<const glm::vec3>{
                        Active->Seeds.data(),
                        Active->Seeds.size()},
                    .Plan = Active->PlanDesc,
                });

            if (!execution.Succeeded() || !execution.Recorded ||
                execution.Resources == nullptr)
            {
                CompleteFailure(RuntimeKMeansGpuJobStatus::RecordFailed,
                                execution.Status,
                                "K-Means GPU command recording failed.");
                return;
            }

            Active->Plan = execution.Plan;
            Active->Resources = execution.Resources;
            Active->ProducerCompletedFrame = Device->GetGlobalFrameNumber() + 1u;
            Active->ProducerSubmitted = true;
        }

        void RecordReadbackCommands(RHI::ICommandContext& commandContext)
        {
            if (Active->Resources == nullptr)
            {
                CompleteFailure(RuntimeKMeansGpuJobStatus::ReadbackFailed,
                                KMeansGpuStatus::InvalidGpuResource,
                                "K-Means GPU readback resources are missing.");
                return;
            }

            const bool queued =
                Readbacks.Enqueue(commandContext, *Active->Resources);

            if (!queued)
            {
                CompleteFailure(RuntimeKMeansGpuJobStatus::ReadbackFailed,
                                KMeansGpuStatus::InvalidReadback,
                                "K-Means GPU async readbacks could not be enqueued.");
                return;
            }

            Active->ReadbackSubmitted = true;
        }

        void CompleteFailure(const RuntimeKMeansGpuJobStatus status,
                             const KMeansGpuStatus gpuStatus,
                             std::string diagnostic)
        {
            if (!Active.has_value())
                return;

            RuntimeKMeansGpuJobResult failed{};
            failed.Status = status;
            failed.Sequence = Active->Request.Sequence;
            failed.StableEntityId = Active->Request.StableEntityId;
            failed.DomainTag = Active->Request.DomainTag;
            failed.GpuStatus = gpuStatus;
            failed.Diagnostic = std::move(diagnostic);
            Completed = std::move(failed);
            Readbacks.Reset();
            Active.reset();
        }

        RHI::IDevice* Device{nullptr};
        RHI::ITransferQueue* TransferQueue{nullptr};
        Graphics::GpuTransfer Transfer;
        KMeansGpuResourceCache Cache;
        KMeansGpuAsyncReadbacks Readbacks;
        KMeansGpuPipelineSet Pipelines{};
        std::optional<ActiveJob> Active{};
        std::optional<RuntimeKMeansGpuJobResult> Completed{};
        std::uint64_t NextSequence{0u};
    };

    RuntimeKMeansGpuJobQueue::RuntimeKMeansGpuJobQueue(
        RHI::IDevice& device,
        RHI::BufferManager& buffers,
        RHI::ITransferQueue& transferQueue)
        : m_Impl(std::make_unique<Impl>(device, buffers, transferQueue))
    {
    }

    RuntimeKMeansGpuJobQueue::~RuntimeKMeansGpuJobQueue() = default;

    RuntimeKMeansGpuJobSubmission RuntimeKMeansGpuJobQueue::Submit(
        RuntimeKMeansGpuJobRequest request)
    {
        if (!m_Impl)
        {
            return MakeSubmission(RuntimeKMeansGpuJobStatus::InvalidInput,
                                  request.Sequence,
                                  KMeansGpuStatus::InvalidInput,
                                  "K-Means GPU queue is not initialized.");
        }
        return m_Impl->Submit(std::move(request));
    }

    void RuntimeKMeansGpuJobQueue::AdvanceGpuWork(
        RHI::ICommandContext& commandContext)
    {
        if (m_Impl)
            m_Impl->AdvanceGpuWork(commandContext);
    }

    void RuntimeKMeansGpuJobQueue::DrainCompletedTransfers()
    {
        if (m_Impl)
            m_Impl->DrainCompletedTransfers();
    }

    std::optional<RuntimeKMeansGpuJobResult>
    RuntimeKMeansGpuJobQueue::ConsumeCompleted()
    {
        if (!m_Impl)
            return std::nullopt;
        return m_Impl->ConsumeCompleted();
    }

    bool RuntimeKMeansGpuJobQueue::HasInFlightJob() const noexcept
    {
        return m_Impl && m_Impl->Active.has_value();
    }
}
