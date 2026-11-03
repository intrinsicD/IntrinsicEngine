// Include-only implementation detail for Runtime.ClusteringModule. Include
// after the module's RHI, runtime, step-run and Geometry.KMeans imports.
#pragma once

namespace Extrinsic::Runtime
{
    // Adds the scope's elapsed microseconds to a ClusteringModuleStats path time (null: not timed).
    struct ScopedMicros
    {
        std::uint64_t* Micros;
        std::chrono::steady_clock::time_point Begin{std::chrono::steady_clock::now()};
        inline ~ScopedMicros() // explicit: module-attached member functions are not implicitly inline
        {
            if (Micros != nullptr)
                *Micros += std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - Begin).count());
        }
    };

    struct KMeansOutputPropertyState
    {
        bool HadColors{false};
        GeometryScalarPropertySnapshot Labels{};
        std::vector<glm::vec4> Colors{};
        GeometryScalarPropertySnapshot ScalarLabels{};
    };

    struct KMeansSnapshot
    {
        RunKMeans Command{};
        WorldHandle World{};
        CommandCorrelationId Correlation{};
        std::vector<glm::vec3> Points{};
        std::vector<std::uint32_t> Slots{};
        std::size_t SlotCount{};
        KMeansOutputPropertyState BeforeOutputs{};
        Geometry::KMeans::KMeansParams Params{};
        std::string BackendDiagnostic{};
    };

    struct ClusteringGpuSubmission
    {
        bool Accepted{false};
        bool Refused{false};
        std::string Diagnostic{};
    };

    struct ClusteringGpuResult
    {
        KMeansRunCompleted Published{};
    };

    class ClusteringGpuState
    {
    public:
        // `stats` (the module's) receives the iteration and preview counts and the publication times.
        ClusteringGpuState(RHI::IDevice& device,
                           RHI::BufferManager& buffers,
                           RHI::ITransferQueue& transferQueue,
                           ClusteringModuleStats& stats);
        ~ClusteringGpuState();

        ClusteringGpuState(const ClusteringGpuState&) = delete;
        ClusteringGpuState& operator=(const ClusteringGpuState&) = delete;

        // Reads the snapshot during admission; the caller retains the publication
        // before-state on acceptance or can run the CPU fallback on rejection.
        // A run is a step run (started running) of the service UseStepRuns gave; without one a request falls back to
        // the CPU. Its holder and Accept are jobs of the context's surface.
        void UseStepRuns(StepRunsService* steps) noexcept;
        [[nodiscard]] ClusteringGpuSubmission Start(KMeansSnapshot& snapshot,
            const EditorProcessingContext&, std::function<bool()> current,
            std::function<EditorCommandStatus(const Geometry::KMeans::KMeansResult&)> publish);
        [[nodiscard]] KMeansGpuObservation GpuRun(CommandCorrelationId, KMeansGpuAction);
        void RecordFrameCommands(RHI::ICommandContext& commandContext);
        void DrainCompletedTransfers();
        [[nodiscard]] std::optional<ClusteringGpuResult> ConsumeCompleted();
        [[nodiscard]] bool HasInFlightWork() const noexcept;
        // A replacing request admitted in `world`: discards the active run on its output (that world, entity and
        // output) unless that run is being accepted. True while a run is in the way, so the request waits: that one,
        // or any finished run not yet settled; false when none is, or when another output's run is active (it is
        // refused as busy then).
        [[nodiscard]] bool Supersede(const RunKMeans& request, WorldHandle world);
        // Nothing active, unconsumed or with a page in flight: a waiting request may start (reusing the workspace).
        [[nodiscard]] bool Settled() const noexcept;
        [[nodiscard]] std::string ReadinessDiagnostic(const RunKMeans&, const EditorProcessingContext&) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
