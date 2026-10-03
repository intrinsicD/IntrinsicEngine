// Private consolidation snapshots and GPU producer state shared by the module
// implementation units; include after their runtime, geometry, ECS and RHI imports.
#pragma once

namespace Extrinsic::Runtime
{
    struct Vec3PropertySnapshot
    {
        std::string Name{};
        bool Exists{false};
        std::vector<glm::vec3> Values{};
    };

    struct ElementDomainPropertyState
    {
        GeometryElementDomain Domain{GeometryElementDomain::Unknown};
        std::size_t ElementCount{0u};
        std::vector<Vec3PropertySnapshot> Properties{};
    };

    struct PointCloudConsolidationSnapshot
    {
        PointCloudConsolidationRequest Request{};
        WorldHandle World{};
        CommandCorrelationId Correlation{};
        ElementDomainPropertyState SourceState{};
        ElementDomainPropertyState BeforeOutputs{};
        std::optional<ECS::Components::GeometrySources::Vertices>
            PointCloudReplacementBefore{};
        bool AllowsPointCloudReplacement{false};
        std::vector<glm::vec3> Positions{};
        std::vector<glm::vec3> Normals{};
        Geometry::PointCloud::Consolidation::Params Params{};
        std::optional<Geometry::SupportRadius::Analysis> RadiusAnalysis{};
        std::vector<glm::vec3> GpuInitialPositions{};
        std::vector<std::uint32_t> GpuSeedRows{};
        bool ForceCpu{false};
        std::shared_ptr<const Geometry::PointLBVH::Index> SourceIndex{};
        bool ReusedSpatialIndex{false};
        std::uint64_t GpuSourceHandle{};
        std::string BackendDiagnostic{};
    };

    struct PointCloudConsolidationJobResult
    {
        PointCloudConsolidationSnapshot Snapshot{};
        PointCloudConsolidationResult Completion{};
        std::optional<ElementDomainPropertyState> AfterOutputs{};
        std::optional<ECS::Components::GeometrySources::Vertices>
            PointCloudReplacementAfter{};
        bool GpuPrepared{false};
    };

    struct PointCloudConsolidationJobCompleted
    {
        std::shared_ptr<PointCloudConsolidationJobResult> Result{};
    };

    enum class PointCloudConsolidationGpuResultStatus : std::uint8_t
    {
        Completed,
        FallbackRequired,
    };

    struct PointCloudConsolidationGpuSubmission
    {
        bool Accepted{false};
        bool Refused{false};
        std::string Diagnostic{};
    };

    struct PointCloudConsolidationGpuResult
    {
        PointCloudConsolidationGpuResultStatus Status{
            PointCloudConsolidationGpuResultStatus::FallbackRequired};
        PointCloudConsolidationSnapshot Snapshot{};
        std::optional<Geometry::PointCloud::Consolidation::Result>
            Consolidated{};
        std::string Diagnostic{};
        std::optional<PointCloudConsolidationResult> Published{};
        PointCloudConsolidationResult Metrics{};

        [[nodiscard]] inline bool HasGpuResult() const noexcept
        {
            return Status ==
                       PointCloudConsolidationGpuResultStatus::Completed &&
                   Consolidated.has_value();
        }
    };

    class PointCloudConsolidationGpuState
    {
    public:
        PointCloudConsolidationGpuState(
            RHI::IDevice& device,
            RHI::BufferManager& buffers,
            RHI::ITransferQueue& transferQueue);
        ~PointCloudConsolidationGpuState();

        PointCloudConsolidationGpuState(
            const PointCloudConsolidationGpuState&) = delete;
        PointCloudConsolidationGpuState& operator=(
            const PointCloudConsolidationGpuState&) = delete;

        // Moves the snapshot only when accepted so the caller can submit an
        // honest CPU-reference fallback after a planning/device rejection.
        [[nodiscard]] PointCloudConsolidationGpuSubmission Start(
            PointCloudConsolidationSnapshot& snapshot, const EditorProcessingContext& context,
            const PointCloudConsolidationResult& prepared);
        [[nodiscard]] PointCloudConsolidationGpuObservation GpuRun(
            CommandCorrelationId correlation, PointCloudConsolidationGpuAction action);
        void RecordFrameCommands(RHI::ICommandContext& commandContext);
        void DrainCompletedTransfers();
        [[nodiscard]] std::optional<PointCloudConsolidationGpuResult>
        ConsumeCompleted();
        [[nodiscard]] bool HasInFlightWork() const noexcept;
        [[nodiscard]] bool HasPendingRun() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
