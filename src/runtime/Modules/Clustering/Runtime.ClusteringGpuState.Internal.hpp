#pragma once

// Include-only implementation detail for Runtime.ClusteringModule. Include
// after the module's RHI, runtime, and Geometry.KMeans imports.

namespace Extrinsic::Runtime
{
    struct KMeansSnapshot
    {
        RunKMeans Command{};
        WorldHandle World{};
        CommandCorrelationId Correlation{};
        std::vector<glm::vec3> Points{};
        Geometry::KMeans::KMeansParams Params{};
        std::string BackendDiagnostic{};
    };

    enum class ClusteringGpuResultStatus : std::uint8_t
    {
        Completed,
        Failed,
    };

    struct ClusteringGpuSubmission
    {
        bool Accepted{false};
        std::string Diagnostic{};
    };

    struct ClusteringGpuResult
    {
        ClusteringGpuResultStatus Status{ClusteringGpuResultStatus::Failed};
        KMeansSnapshot Snapshot{};
        std::optional<Geometry::KMeans::KMeansResult> Clustered{};
        std::string Diagnostic{};

        [[nodiscard]] bool Succeeded() const noexcept;
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

        // Moves `snapshot` only when accepted so the caller can run the CPU
        // reference as an honest fallback on rejection.
        [[nodiscard]] ClusteringGpuSubmission Start(KMeansSnapshot& snapshot);
        void RecordFrameCommands(RHI::ICommandContext& commandContext);
        void DrainCompletedTransfers();
        [[nodiscard]] std::optional<ClusteringGpuResult> ConsumeCompleted();
        [[nodiscard]] bool HasInFlightWork() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
