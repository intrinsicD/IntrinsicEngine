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

module Extrinsic.Runtime.ClusteringModule;

import Extrinsic.Core.Filesystem.PathResolver;
import Extrinsic.Graphics.GpuTransfer;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.RHI.Types;
import :GpuBackend;
import Geometry.KMeans;

#include "Modules/Clustering/Runtime.ClusteringGpuState.Internal.hpp"

namespace Extrinsic::Runtime
{
    namespace GK = Geometry::KMeans;

    bool ClusteringGpuResult::Succeeded() const noexcept
    {
        return Status == ClusteringGpuResultStatus::Completed &&
               Clustered.has_value();
    }

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

        [[nodiscard]] bool IsValidSnapshot(
            const KMeansSnapshot& snapshot) noexcept
        {
            return !snapshot.Points.empty() &&
                   snapshot.Points.size() <=
                       static_cast<std::size_t>(
                           std::numeric_limits<std::uint32_t>::max()) &&
                   snapshot.Params.ClusterCount > 0u &&
                   snapshot.Params.MaxIterations > 0u;
        }

        [[nodiscard]] std::uint32_t EffectiveClusterCount(
            const KMeansSnapshot& snapshot) noexcept
        {
            return std::min<std::uint32_t>(
                snapshot.Params.ClusterCount,
                static_cast<std::uint32_t>(snapshot.Points.size()));
        }

        [[nodiscard]] KMeansGpuPlanDesc BuildPlanDesc(
            const KMeansSnapshot& snapshot) noexcept
        {
            return KMeansGpuPlanDesc{
                .PointCount =
                    static_cast<std::uint32_t>(snapshot.Points.size()),
                .ClusterCount = EffectiveClusterCount(snapshot),
                .MaxIterations = snapshot.Params.MaxIterations,
                .GroupSize = kKMeansGpuGroupSize,
                .ConvergenceTolerance =
                    snapshot.Params.ConvergenceTolerance,
            };
        }

        [[nodiscard]] GK::KMeansResult BuildGeometryResult(
            const KMeansSnapshot& snapshot,
            const KMeansGpuDispatchPlan& plan,
            KMeansGpuReadbackResult readback)
        {
            GK::KMeansResult result{};
            result.Labels = std::move(readback.Labels);
            result.SquaredDistances =
                std::move(readback.SquaredDistances);
            result.Centroids = std::move(readback.Centroids);
            result.Iterations = plan.MaxIterations;
            result.Converged = false;
            result.Inertia = readback.Inertia;
            result.MaxDistanceIndex = readback.MaxDistanceIndex;
            result.RequestedBackend = snapshot.Command.Backend ==
                    ClusteringBackend::VulkanCompute
                ? GK::Backend::GPU
                : GK::Backend::CPU;
            result.ActualBackend = GK::Backend::GPU;
            result.FellBackToCPU = false;
            return result;
        }
    }

    struct ClusteringGpuState::Impl
    {
        struct ActiveOperation
        {
            KMeansSnapshot Snapshot{};
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

        ~Impl() { DestroyPipelines(); }

        [[nodiscard]] bool HasBusyState() const noexcept
        {
            return Active.has_value() || Completed.has_value();
        }

        [[nodiscard]] ClusteringGpuSubmission Start(
            KMeansSnapshot& snapshot)
        {
            if (HasBusyState())
            {
                return ClusteringGpuSubmission{
                    .Diagnostic =
                        "A clustering Vulkan operation is already pending.",
                };
            }
            if (Device == nullptr || TransferQueue == nullptr ||
                !IsValidSnapshot(snapshot))
            {
                return ClusteringGpuSubmission{
                    .Diagnostic =
                        "K-Means Vulkan execution requires non-empty points and positive cluster/iteration counts.",
                };
            }

            const std::uint32_t clusterCount =
                EffectiveClusterCount(snapshot);
            std::vector<glm::vec3> seeds = GK::BuildInitialCentroids(
                std::span<const glm::vec3>{
                    snapshot.Points.data(), snapshot.Points.size()},
                std::span<const glm::vec3>{},
                snapshot.Params,
                clusterCount);
            if (seeds.size() != clusterCount)
            {
                return ClusteringGpuSubmission{
                    .Diagnostic =
                        "K-Means Vulkan execution could not build a valid centroid seed set.",
                };
            }

            const KMeansGpuPlanDesc planDesc = BuildPlanDesc(snapshot);
            const KMeansGpuResolveResult resolved = ResolveKMeansGpuRequest(
                KMeansGpuResolveDesc{
                    .Device = Device,
                    .Plan = planDesc,
                });
            if (!resolved.GpuExecutionAvailable)
            {
                return ClusteringGpuSubmission{
                    .Diagnostic = resolved.Diagnostic.empty()
                        ? "K-Means Vulkan compute execution is unavailable."
                        : resolved.Diagnostic,
                };
            }

            Readbacks.Reset();
            Active = ActiveOperation{
                .Snapshot = std::move(snapshot),
                .Seeds = std::move(seeds),
                .PlanDesc = planDesc,
                .Plan = resolved.Plan,
            };
            return ClusteringGpuSubmission{.Accepted = true};
        }

        void RecordFrameCommands(RHI::ICommandContext& commandContext)
        {
            if (!Active.has_value() || Device == nullptr ||
                Completed.has_value())
            {
                return;
            }

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

            KMeansGpuReadbackResult readback =
                Readbacks.Collect(Active->Plan);
            if (!readback.Succeeded())
            {
                CompleteFailure(
                    readback.Diagnostic.empty()
                        ? "K-Means Vulkan result readback failed."
                        : std::move(readback.Diagnostic));
                return;
            }

            KMeansSnapshot snapshot = std::move(Active->Snapshot);
            GK::KMeansResult clustered = BuildGeometryResult(
                snapshot, Active->Plan, std::move(readback));
            Completed = ClusteringGpuResult{
                .Status = ClusteringGpuResultStatus::Completed,
                .Snapshot = std::move(snapshot),
                .Clustered = std::move(clustered),
            };
            Readbacks.Reset();
            Active.reset();
        }

        [[nodiscard]] std::optional<ClusteringGpuResult> ConsumeCompleted()
        {
            if (!Completed.has_value())
                return std::nullopt;
            std::optional<ClusteringGpuResult> result =
                std::move(Completed);
            Completed.reset();
            return result;
        }

        [[nodiscard]] bool EnsurePipelines()
        {
            if (Pipelines.IsValid())
                return true;
            if (Device == nullptr || !Device->IsOperational())
                return false;

            const std::string resetShader = Core::Filesystem::GetShaderPath(
                "shaders/kmeans_reset.comp.spv");
            const std::string assignShader = Core::Filesystem::GetShaderPath(
                "shaders/kmeans_assign.comp.spv");
            const std::string updateShader = Core::Filesystem::GetShaderPath(
                "shaders/kmeans_update.comp.spv");

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
                CompleteFailure(
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
                        Active->Snapshot.Points.data(),
                        Active->Snapshot.Points.size()},
                    .SeedCentroids = std::span<const glm::vec3>{
                        Active->Seeds.data(), Active->Seeds.size()},
                    .Plan = Active->PlanDesc,
                });
            if (!execution.Succeeded() || !execution.Recorded ||
                execution.Resources == nullptr)
            {
                CompleteFailure(
                    "K-Means Vulkan command recording failed.");
                return;
            }

            Active->Plan = execution.Plan;
            Active->Resources = execution.Resources;
            Active->ProducerCompletedFrame =
                Device->GetGlobalFrameNumber() + 1u;
            Active->ProducerSubmitted = true;
        }

        void RecordReadbackCommands(RHI::ICommandContext& commandContext)
        {
            if (Active->Resources == nullptr)
            {
                CompleteFailure(
                    "K-Means Vulkan result resources are missing.");
                return;
            }
            if (!Readbacks.Enqueue(commandContext, *Active->Resources))
            {
                CompleteFailure(
                    "K-Means Vulkan asynchronous result readback could not be enqueued.");
                return;
            }
            Active->ReadbackSubmitted = true;
        }

        void CompleteFailure(std::string diagnostic)
        {
            if (!Active.has_value())
                return;
            Completed = ClusteringGpuResult{
                .Status = ClusteringGpuResultStatus::Failed,
                .Snapshot = std::move(Active->Snapshot),
                .Diagnostic = std::move(diagnostic),
            };
            Readbacks.Reset();
            Active.reset();
        }

        RHI::IDevice* Device{nullptr};
        RHI::ITransferQueue* TransferQueue{nullptr};
        Graphics::GpuTransfer Transfer;
        KMeansGpuResourceCache Cache;
        KMeansGpuResultReadback Readbacks;
        KMeansGpuPipelineSet Pipelines{};
        std::optional<ActiveOperation> Active{};
        std::optional<ClusteringGpuResult> Completed{};
    };

    ClusteringGpuState::ClusteringGpuState(
        RHI::IDevice& device,
        RHI::BufferManager& buffers,
        RHI::ITransferQueue& transferQueue)
        : m_Impl(std::make_unique<Impl>(device, buffers, transferQueue))
    {
    }

    ClusteringGpuState::~ClusteringGpuState() = default;

    ClusteringGpuSubmission ClusteringGpuState::Start(
        KMeansSnapshot& snapshot)
    {
        return m_Impl != nullptr
            ? m_Impl->Start(snapshot)
            : ClusteringGpuSubmission{
                  .Diagnostic = "Clustering Vulkan state is unavailable.",
              };
    }

    void ClusteringGpuState::RecordFrameCommands(
        RHI::ICommandContext& commandContext)
    {
        if (m_Impl != nullptr)
            m_Impl->RecordFrameCommands(commandContext);
    }

    void ClusteringGpuState::DrainCompletedTransfers()
    {
        if (m_Impl != nullptr)
            m_Impl->DrainCompletedTransfers();
    }

    std::optional<ClusteringGpuResult>
    ClusteringGpuState::ConsumeCompleted()
    {
        return m_Impl != nullptr ? m_Impl->ConsumeCompleted() : std::nullopt;
    }

    bool ClusteringGpuState::HasInFlightWork() const noexcept
    {
        return m_Impl != nullptr && m_Impl->HasBusyState();
    }
}
