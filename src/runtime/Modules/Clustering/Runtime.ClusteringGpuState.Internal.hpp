// Include-only implementation detail for Runtime.ClusteringModule. Include
// after the module's RHI, runtime, and Geometry.KMeans imports.
#pragma once

namespace Extrinsic::Runtime
{
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
        ClusteringGpuState(RHI::IDevice& device,
                           RHI::BufferManager& buffers,
                           RHI::ITransferQueue& transferQueue);
        ~ClusteringGpuState();

        ClusteringGpuState(const ClusteringGpuState&) = delete;
        ClusteringGpuState& operator=(const ClusteringGpuState&) = delete;

        // Reads the snapshot during admission; the caller retains the publication
        // before-state on acceptance or can run the CPU fallback on rejection.
        [[nodiscard]] ClusteringGpuSubmission Start(KMeansSnapshot& snapshot,
            const EditorProcessingContext&, std::function<bool()> current,
            std::function<EditorCommandStatus(const Geometry::KMeans::KMeansResult&)> publish);
        [[nodiscard]] KMeansGpuObservation GpuRun(CommandCorrelationId, KMeansGpuAction);
        void RecordFrameCommands(RHI::ICommandContext& commandContext);
        void DrainCompletedTransfers();
        [[nodiscard]] std::optional<ClusteringGpuResult> ConsumeCompleted();
        [[nodiscard]] bool HasInFlightWork() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
