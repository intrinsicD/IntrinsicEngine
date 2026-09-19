// Consolidation requests and result records and borrowed service access, independent of lifecycle setup.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
export module Extrinsic.Runtime.PointCloudConsolidationTypes;
import Extrinsic.Core.Error;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.CommandBus;
export import Extrinsic.Runtime.GeometryProperty.Types;
export import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.WorldHandle;
export import Geometry.PointCloud.Consolidation.Types;
export namespace Extrinsic::Runtime
{
    enum class PointCloudConsolidationRunStatus : std::uint8_t
    {
        Queued = 0u,
        Applied,
        MissingScene,
        InvalidProcessingParameters,
        StaleEntity,
        UnsupportedPropertySource,
        UnsafeSupportRadius,
        GeometryProcessingFailed,
        Cancelled,
        StaleSource,
        StaleWorld,
        ModuleUnavailable,
    };

    [[nodiscard]] std::string_view ToString(
        PointCloudConsolidationRunStatus status) noexcept;

    struct PointCloudConsolidationPropertyRefs
    {
        GeometryPropertyRef InputPositions{
            .Domain = GeometryElementDomain::PointCloudPoint,
            .Name = "v:position",
            .ValueKind = Geometry::PropertyValueKind::Vec3,
        };
        std::optional<GeometryPropertyRef> InputNormals{
            GeometryPropertyRef{
                .Domain = GeometryElementDomain::PointCloudPoint,
                .Name = "v:normal",
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            }};
        GeometryPropertyRef OutputPositions{
            .Domain = GeometryElementDomain::PointCloudPoint,
            .Name = "v:position",
            .ValueKind = Geometry::PropertyValueKind::Vec3,
        };
        std::optional<GeometryPropertyRef> OutputNormals{
            GeometryPropertyRef{
                .Domain = GeometryElementDomain::PointCloudPoint,
                .Name = "v:normal",
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            }};
    };

    [[nodiscard]] bool IsValidPointCloudConsolidationPropertyRefs(
        const PointCloudConsolidationPropertyRefs& properties) noexcept;

    [[nodiscard]] PointCloudConsolidationPropertyRefs
    MakePointCloudConsolidationPropertyRefs(
        GeometryElementDomain domain,
        std::string positionPropertyName,
        std::optional<std::string> normalPropertyName = std::nullopt);

    struct PointCloudConsolidationAvailability
    {
        bool Available{false};
        bool Pending{false};
        std::size_t InputPointCount{0u};
        bool CardinalityChanging{false};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};
    };

    struct PointCloudConsolidationRequest
    {
        std::uint32_t StableEntityId{0u};
        PointCloudConsolidationPropertyRefs Properties{};
        PointCloudConsolidationConfig Config{};
    };

    struct PointCloudConsolidationResult
    {
        CommandCorrelationId Correlation{};
        WorldHandle World{};
        PointCloudConsolidationRunStatus Status{
            PointCloudConsolidationRunStatus::Queued};
        std::uint32_t StableEntityId{0u};
        PointCloudConsolidationPropertyRefs Properties{};
        PointCloudConsolidationConfig Config{};
        PointCloudConsolidationBackend RequestedBackend{
            PointCloudConsolidationBackend::CpuReference};
        PointCloudConsolidationBackend ActualBackend{
            PointCloudConsolidationBackend::None};
        bool FellBackToCpu{false};
        bool ReusedSpatialIndex{false};
        std::uint32_t GpuQueryBatches{}, SpatialWorkspaceBuilds{};
        std::string BackendDiagnostic{};
        std::string ImplementationId{"cpu_reference"};
        std::string StrategyToken{"wlop"};
        std::string SupportRadiusAnalysisStatus{"not_run"};
        std::string SupportRadiusSource{"not_run"};
        std::string SupportRadiusQuantile{"not_run"};
        std::uint32_t SupportRadiusEstimatorVersion{0u};
        std::uint32_t SupportRadiusProfileSampleCount{0u};
        std::uint32_t SupportRadiusRequestedNeighborRank{0u};
        std::uint32_t SupportRadiusNeighborRank{0u};
        bool SupportRadiusWorkloadAdjusted{false};
        double SupportRadiusNeighborDistance{0.0};
        double ResolvedSupportRadius{0.0};
        double SupportRadiusBoundingBoxDiagonal{0.0};
        double SupportNeighborsP50{0.0};
        double SupportNeighborsP95{0.0};
        std::uint32_t SupportNeighborsMax{0u};
        std::uint64_t PredictedSupportQueryCount{0u};
        std::uint64_t PredictedContributionCount{0u};
        Geometry::PointCloud::Consolidation::Status GeometryStatus{
            Geometry::PointCloud::Consolidation::Status::EmptyInput};
        std::uint32_t InputPointCount{0u};
        std::uint32_t OutputPointCount{0u};
        std::uint32_t Iterations{0u};
        bool Converged{false};
        double AverageDisplacement{0.0};
        double MaxDisplacement{0.0};
        bool UsedAuthoredNormals{false};
        bool EstimatedNormals{false};
        std::uint32_t NormalRefinementIterations{0u};
        std::uint32_t InsertedPointCount{0u};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == PointCloudConsolidationRunStatus::Applied;
        }
    };

    struct PointCloudConsolidationModuleStats
    {
        std::uint64_t ReadinessChecksQueued{0u};
        std::uint64_t ReadinessPropertyScans{0u};
        std::uint64_t CommandsHandled{0u};
        std::uint64_t JobsSubmitted{0u};
        std::uint64_t JobSubmissionFailures{0u};
        std::uint64_t GpuRequestsAccepted{0u};
        std::uint64_t GpuFallbacks{0u};
        std::uint64_t GpuCompletions{0u};
        std::uint64_t CompletionEvents{0u};
        std::uint64_t ResultsCommitted{0u};
        std::uint64_t CommitsDropped{0u};
    };

    // Only the lifecycle owner may bind the service. C++ linkage lets its
    // definition stay in the lifecycle module without importing that module here.
    extern "C++" { class PointCloudConsolidationModule; }

    class PointCloudConsolidationService
    {
    public:
        PointCloudConsolidationService() = default;
        PointCloudConsolidationService(
            const PointCloudConsolidationService&) = delete;
        PointCloudConsolidationService& operator=(
            const PointCloudConsolidationService&) = delete;

        [[nodiscard]] bool Available() const noexcept;
        // Main-thread preparation queues missing finite checks; no geometry is
        // read beyond metadata until the command drain. Pending disables Run.
        // The bounded cache serves one active preview: changing the requested
        // source replaces its pending checks. Retained writes must MarkModified().
        [[nodiscard]] PointCloudConsolidationAvailability PrepareAvailability(
            WorldHandle world, const PointCloudConsolidationRequest& request);
        [[nodiscard]] CommandCorrelationId Run(
            PointCloudConsolidationRequest request);
        [[nodiscard]] KernelEventSubscription SubscribeCompleted(
            std::function<void(const PointCloudConsolidationResult&)> listener);
        void Unsubscribe(KernelEventSubscription subscription);
        [[nodiscard]] PointCloudConsolidationModuleStats Stats() const noexcept;

    private:
        friend class PointCloudConsolidationModule;

        void Bind(
            CommandBus* commands,
            KernelEventBus* events,
            const PointCloudConsolidationModuleStats* stats,
            std::function<PointCloudConsolidationAvailability(
                WorldHandle, const PointCloudConsolidationRequest&)> prepare = {}) noexcept;

        std::function<PointCloudConsolidationAvailability(
            WorldHandle, const PointCloudConsolidationRequest&)> m_PrepareAvailability{};
        CommandBus* m_Commands{};
        KernelEventBus* m_Events{};
        const PointCloudConsolidationModuleStats* m_Stats{};
    };

}
