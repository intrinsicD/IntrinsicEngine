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
        // RUNTIME-289: why a Vulkan initial order ran on the CPU. A device order arrives through
        // InitialOrdering from the job InitialOrderJob, which the worker job depends on; RunWorker
        // moves it into Params.InitialOrder, from which results report the backend. ARCH-023: that job
        // orders on its worker but writes InitialOrdering only on the main thread, as it publishes.
        std::string InitialSamplingDiagnostic{};
        struct InitialOrderingState
        {
            std::optional<Geometry::PointSampling::Result> Order{};
            std::string Diagnostic{};
        };
        std::shared_ptr<InitialOrderingState> InitialOrdering{};
        JobToken InitialOrderJob{};
    };

    // A CPU or hybrid run (the module's NeighborhoodProjection owner), defined by the module implementation.
    struct PointCloudConsolidationProjectionRun;

    struct PointCloudConsolidationJobResult
    {
        PointCloudConsolidationSnapshot Snapshot{};
        PointCloudConsolidationResult Completion{};
        std::optional<ElementDomainPropertyState> AfterOutputs{};
        std::optional<ECS::Components::GeometrySources::Vertices>
            PointCloudReplacementAfter{};
        bool GpuPrepared{false};
        // A CPU preparation that initialized its projection (it owns the snapshot): the run starts from it.
        std::shared_ptr<PointCloudConsolidationProjectionRun> Projection{};
        // The published positions are a stopped run's last iteration (not the iteration cap's).
        bool Stopped{false};
    };

    struct PointCloudConsolidationJobCompleted
    {
        std::shared_ptr<PointCloudConsolidationJobResult> Result{};
    };

    struct PointCloudConsolidationGpuSubmission
    {
        bool Accepted{false};
        bool Refused{false};
        std::string Diagnostic{};
    };

    // A finished resident run: `Published` when it ended itself (accepted in place, discarded or failed), else the
    // read-back positions of a new output that the module publishes, answering the pending Commit through `Driver`.
    struct PointCloudConsolidationGpuResult
    {
        PointCloudConsolidationSnapshot Snapshot{};
        std::optional<Geometry::PointCloud::Consolidation::Result> Consolidated{};
        std::optional<PointCloudConsolidationResult> Published{};
        PointCloudConsolidationResult Metrics{};
        StepRunDriver Driver{};
    };

    class PointCloudConsolidationGpuState
    {
    public:
        // Buffers come from the run context's SpatialIndexCache::MethodScratch.
        explicit PointCloudConsolidationGpuState(RHI::IDevice& device);
        ~PointCloudConsolidationGpuState();

        PointCloudConsolidationGpuState(
            const PointCloudConsolidationGpuState&) = delete;
        PointCloudConsolidationGpuState& operator=(
            const PointCloudConsolidationGpuState&) = delete;

        // Moves the snapshot only when accepted so the caller can submit an
        // honest CPU-reference fallback after a planning/device rejection.
        // Every resident LOP or isotropic WLOP run is a step run of this service.
        void UseStepRuns(StepRunsService* steps) noexcept;
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
