module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

export module Extrinsic.Runtime.PointCloudConsolidationModule;

import Extrinsic.Core.Error;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.EditorCommandHistory;
export import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.PointCloud.Consolidation;

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

    [[nodiscard]] PointCloudConsolidationPropertyRefs
    MakePointCloudConsolidationPropertyRefs(
        GeometryElementDomain domain,
        std::string positionPropertyName,
        std::optional<std::string> normalPropertyName = std::nullopt);

    [[nodiscard]] bool IsValidPointCloudConsolidationPropertyRefs(
        const PointCloudConsolidationPropertyRefs& properties) noexcept;

    struct PointCloudConsolidationAvailability
    {
        bool Available{false};
        std::size_t InputPointCount{0u};
        bool CardinalityChanging{false};
        Core::ErrorCode Error{Core::ErrorCode::Success};
        std::string Message{};
    };

    [[nodiscard]] PointCloudConsolidationAvailability
    ResolvePointCloudConsolidationAvailability(
        const GeometryEntityAvailability& availability,
        const PointCloudConsolidationPropertyRefs& properties,
        const PointCloudConsolidationConfig& config);

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
        std::string ImplementationId{"cpu_reference"};
        std::string StrategyToken{"wlop"};
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
        std::uint64_t CommandsHandled{0u};
        std::uint64_t JobsSubmitted{0u};
        std::uint64_t JobSubmissionFailures{0u};
        std::uint64_t CompletionEvents{0u};
        std::uint64_t ResultsCommitted{0u};
        std::uint64_t CommitsDropped{0u};
    };

    class PointCloudConsolidationService
    {
    public:
        PointCloudConsolidationService() = default;
        PointCloudConsolidationService(
            const PointCloudConsolidationService&) = delete;
        PointCloudConsolidationService& operator=(
            const PointCloudConsolidationService&) = delete;

        [[nodiscard]] bool Available() const noexcept;
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
            const PointCloudConsolidationModuleStats* stats) noexcept;

        CommandBus* m_Commands{};
        KernelEventBus* m_Events{};
        const PointCloudConsolidationModuleStats* m_Stats{};
    };

    class PointCloudConsolidationModule final : public IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

        [[nodiscard]] const PointCloudConsolidationModuleStats& Stats()
            const noexcept
        {
            return m_Stats;
        }

    private:
        PointCloudConsolidationService m_Service{};
        KernelEventSubscription m_JobCompletedSubscription{};
        KernelEventBus* m_Events{};
        JobService* m_Jobs{};
        WorldRegistry* m_Worlds{};
        EditorCommandHistory* m_History{};
        PointCloudConsolidationModuleStats m_Stats{};
    };
}
