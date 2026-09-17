// Clustering parameters, property bindings and completion records and borrowed service access, independent of lifecycle setup.
module;
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <entt/entity/fwd.hpp>
export module Extrinsic.Runtime.ClusteringTypes;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.CommandBus;
export import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.WorldHandle;
export namespace Extrinsic::Runtime
{
    enum class ClusteringBackend : std::uint8_t
    {
        None,
        CpuReference,
        VulkanCompute,
    };

    [[nodiscard]] std::string_view ToString(
        ClusteringBackend backend) noexcept;

    enum class KMeansInitialization : std::uint8_t
    {
        Random,
        Hierarchical,
    };

    struct KMeansParameters
    {
        std::uint32_t ClusterCount{8u};
        std::uint32_t MaxIterations{32u};
        std::uint32_t Seed{42u};
        KMeansInitialization Initialization{
            KMeansInitialization::Hierarchical};
    };

    struct KMeansPropertyRefs
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

    [[nodiscard]] KMeansPropertyRefs MakeKMeansPropertyRefs(
        GeometryElementDomain domain);

    [[nodiscard]] bool IsValidKMeansPropertyBindings(const KMeansPropertyRefs&) noexcept;

    enum class KMeansRunStatus : std::uint8_t
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

    [[nodiscard]] std::string_view ToString(
        KMeansRunStatus status) noexcept;

    struct RunKMeans
    {
        std::uint32_t StableEntityId{0u};
        KMeansPropertyRefs Properties{};
        KMeansParameters Parameters{};
        ClusteringBackend Backend{ClusteringBackend::CpuReference};
    };

    struct KMeansRunCompleted
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

    // Metadata-only admission: nullopt admits; a value describes rejection.
    // Execution still checks finite values and captures exact publication state.
    // Dispatch fills world/correlation; an admission rejection has neither.
    [[nodiscard]] std::optional<KMeansRunCompleted> ValidateKMeansRequest(
        const entt::registry*, const RunKMeans&);

    struct ClusterLabelsChanged
    {
        CommandCorrelationId Correlation{};
        WorldHandle World{};
        GeometryPropertyRef Labels{};
        GeometryPropertyRef Colors{};
        std::uint32_t StableEntityId{0u};
        std::uint32_t LabelCount{0u};
    };

    struct ClusteringModuleStats
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

    // Only the lifecycle owner may bind the service. C++ linkage lets its
    // definition stay in the lifecycle module without importing that module here.
    extern "C++" { class ClusteringModule; }

    class ClusteringService
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

}
