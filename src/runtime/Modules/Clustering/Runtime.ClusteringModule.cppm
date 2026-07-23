module;

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.ClusteringModule;

import Extrinsic.Core.Error;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    export enum class ClusteringDomain : std::uint8_t
    {
        MeshVertices,
        GraphVertices,
        PointCloudPoints,
    };

    export enum class ClusteringBackend : std::uint8_t
    {
        CpuReference,
        VulkanCompute,
    };

    export enum class KMeansRunStatus : std::uint8_t
    {
        Queued,
        Applied,
        MissingScene,
        InvalidProcessingParameters,
        StaleEntity,
        UnsupportedGeometryDomain,
        GeometryProcessingFailed,
        StaleSource,
        StaleWorld,
        ModuleUnavailable,
    };

    export struct RunKMeans
    {
        std::uint32_t StableEntityId{0u};
        ClusteringDomain Domain{ClusteringDomain::PointCloudPoints};
        std::uint32_t ClusterCount{8u};
        std::uint32_t MaxIterations{32u};
        std::uint32_t Seed{42u};
        bool UseHierarchicalInitialization{true};
        ClusteringBackend Backend{ClusteringBackend::CpuReference};
    };

    export struct KMeansRunCompleted
    {
        CommandCorrelationId Correlation{};
        WorldHandle World{};
        KMeansRunStatus Status{KMeansRunStatus::Queued};
        ClusteringDomain Domain{ClusteringDomain::PointCloudPoints};
        std::uint32_t StableEntityId{0u};
        std::uint32_t LabelCount{0u};
        std::uint32_t ClusterCount{0u};
        std::uint32_t Iterations{0u};
        bool Converged{false};
        float Inertia{0.0f};
        std::uint32_t MaxDistanceIndex{0u};
        ClusteringBackend RequestedBackend{ClusteringBackend::CpuReference};
        ClusteringBackend ActualBackend{ClusteringBackend::CpuReference};
        bool FellBackToCpu{false};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == KMeansRunStatus::Applied;
        }
    };

    export struct ClusterLabelsChanged
    {
        CommandCorrelationId Correlation{};
        WorldHandle World{};
        ClusteringDomain Domain{ClusteringDomain::PointCloudPoints};
        std::uint32_t StableEntityId{0u};
        std::uint32_t LabelCount{0u};
    };

    export struct ClusteringModuleStats
    {
        std::uint64_t CommandsHandled{0};
        std::uint64_t JobsSubmitted{0};
        std::uint64_t JobSubmissionFailures{0};
        std::uint64_t CompletionEvents{0};
        std::uint64_t LabelsCommitted{0};
        std::uint64_t CommitsDropped{0};
        std::uint64_t ClusterLabelsChangedEvents{0};
        std::uint64_t VisualizationRefreshReactions{0};
    };

    export class ClusteringService
    {
    public:
        ClusteringService() = default;
        ClusteringService(const ClusteringService&) = delete;
        ClusteringService& operator=(const ClusteringService&) = delete;

        [[nodiscard]] bool Available() const noexcept;
        [[nodiscard]] CommandCorrelationId RunKMeans(RunKMeans command);

        [[nodiscard]] KernelEventSubscription SubscribeRunCompleted(
            std::function<void(const KMeansRunCompleted&)> listener);
        [[nodiscard]] KernelEventSubscription SubscribeClusterLabelsChanged(
            std::function<void(const ClusterLabelsChanged&)> listener);
        void Unsubscribe(KernelEventSubscription subscription);

        [[nodiscard]] ClusteringModuleStats Stats() const noexcept;

    private:
        friend class ClusteringModule;

        void Bind(CommandBus* commands,
                  KernelEventBus* events,
                  const ClusteringModuleStats* stats) noexcept;

        CommandBus* m_Commands{};
        KernelEventBus* m_Events{};
        const ClusteringModuleStats* m_Stats{};
    };

    export class ClusteringModule final : public IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        [[nodiscard]] const ClusteringModuleStats& Stats() const noexcept
        {
            return m_Stats;
        }

    private:
        ClusteringService m_Service{};
        KernelEventSubscription m_JobCompletedSubscription{};
        KernelEventSubscription m_ClusterLabelsChangedSubscription{};
        KernelEventBus* m_Events{};
        JobService* m_Jobs{};
        WorldRegistry* m_Worlds{};
        ClusteringModuleStats m_Stats{};
    };
}
