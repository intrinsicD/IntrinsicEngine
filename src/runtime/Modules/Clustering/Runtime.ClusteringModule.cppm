module;

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.ClusteringModule;

import Extrinsic.Core.Error;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.CommandBus;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;

namespace Extrinsic::Runtime
{
    class ClusteringGpuState;

    export enum class ClusteringBackend : std::uint8_t
    {
        None,
        CpuReference,
        VulkanCompute,
    };

    export [[nodiscard]] std::string_view ToString(
        ClusteringBackend backend) noexcept;

    export enum class KMeansInitialization : std::uint8_t
    {
        Random,
        Hierarchical,
    };

    export struct KMeansParameters
    {
        std::uint32_t ClusterCount{8u};
        std::uint32_t MaxIterations{32u};
        std::uint32_t Seed{42u};
        KMeansInitialization Initialization{
            KMeansInitialization::Hierarchical};
    };

    export struct KMeansPropertyRefs
    {
        GeometryPropertyRef InputPositions{
            .Domain = GeometryElementDomain::PointCloudPoint,
            .Name = "v:position",
            .ValueKind = Geometry::PropertyValueKind::Vec3,
        };
        GeometryPropertyRef OutputLabels{
            .Domain = GeometryElementDomain::PointCloudPoint,
            .Name = "p:kmeans_label",
            .ValueKind = Geometry::PropertyValueKind::UInt32,
        };
        GeometryPropertyRef OutputColors{
            .Domain = GeometryElementDomain::PointCloudPoint,
            .Name = "p:kmeans_color",
            .ValueKind = Geometry::PropertyValueKind::Vec4,
        };
        std::optional<GeometryPropertyRef> OutputScalarLabels{};
    };

    export [[nodiscard]] KMeansPropertyRefs MakeKMeansPropertyRefs(
        GeometryElementDomain domain);

    export enum class KMeansRunStatus : std::uint8_t
    {
        Queued,
        Applied,
        MissingScene,
        InvalidProcessingParameters,
        StaleEntity,
        UnsupportedGeometryDomain,
        GeometryProcessingFailed,
        Cancelled,
        StaleSource,
        StaleWorld,
        ModuleUnavailable,
    };

    export [[nodiscard]] std::string_view ToString(
        KMeansRunStatus status) noexcept;

    export struct RunKMeans
    {
        std::uint32_t StableEntityId{0u};
        KMeansPropertyRefs Properties{};
        KMeansParameters Parameters{};
        ClusteringBackend Backend{ClusteringBackend::CpuReference};
    };

    export struct KMeansRunCompleted
    {
        CommandCorrelationId Correlation{};
        WorldHandle World{};
        KMeansRunStatus Status{KMeansRunStatus::Queued};
        std::uint32_t StableEntityId{0u};
        KMeansPropertyRefs Properties{};
        KMeansParameters Parameters{};
        std::uint32_t LabelCount{0u};
        std::uint32_t ClusterCount{0u};
        std::uint32_t Iterations{0u};
        bool Converged{false};
        float Inertia{0.0f};
        std::uint32_t MaxDistanceIndex{0u};
        ClusteringBackend RequestedBackend{ClusteringBackend::CpuReference};
        ClusteringBackend ActualBackend{ClusteringBackend::None};
        bool FellBackToCpu{false};
        std::string BackendDiagnostic{};
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
        GeometryPropertyRef Labels{};
        GeometryPropertyRef Colors{};
        std::uint32_t StableEntityId{0u};
        std::uint32_t LabelCount{0u};
    };

    export struct ClusteringModuleStats
    {
        std::uint64_t CommandsHandled{0};
        std::uint64_t JobsSubmitted{0};
        std::uint64_t JobSubmissionFailures{0};
        std::uint64_t GpuRequestsAccepted{0};
        std::uint64_t GpuFallbacks{0};
        std::uint64_t GpuCompletions{0};
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
        ClusteringModule();
        ~ClusteringModule() override;

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
        RHI::IDevice* m_Device{};
        std::unique_ptr<ClusteringGpuState> m_GpuState{};
        GpuQueueParticipantHandle m_GpuParticipant{};
        ClusteringModuleStats m_Stats{};
    };
}
