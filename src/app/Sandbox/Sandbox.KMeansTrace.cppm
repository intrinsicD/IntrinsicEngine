// ARCH-022 Q83: an opt-in per-frame trace of K-Means work in the running Sandbox (`--kmeans-trace`), so the path
// from a run request to its first visible preview can be split by frames. Each frame's clustering counters and its
// render extraction's GPU-front observations are sampled at that frame's Maintenance hook; the frame's pacing is
// attached one frame later, matched by frame index.
module;

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

export module Extrinsic.Sandbox.KMeansTrace;

import Extrinsic.Runtime.ClusteringTypes;
import Extrinsic.Runtime.FramePacingDiagnostics;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.RenderExtraction;

export namespace Extrinsic::Sandbox
{
    struct KMeansTraceSample
    {
        std::uint64_t FrameIndex{};
        std::uint64_t SteadyMicros{}; // steady_clock at this frame's Maintenance hook
        Runtime::ClusteringModuleStats Clustering{};
        std::uint32_t ScalarGpuFrontsObserved{}; // this frame's render extraction
        std::optional<Runtime::RuntimeFramePacingDiagnostics> Pacing{}; // this frame's, once it completed
    };

    class KMeansTraceModule final : public Runtime::IRuntimeModule
    {
    public:
        // Requests exit at `targetFrames` samples (0: never) or `kTailFrames` frames after `exitAfterRuns` K-Means
        // results were committed (0: never).
        static constexpr std::uint32_t kTailFrames = 120u;
        KMeansTraceModule(std::vector<KMeansTraceSample>& samples, std::uint32_t targetFrames,
            std::function<Runtime::RuntimeFramePacingDiagnostics()> readLastPacing,
            std::function<void()> requestExit, std::uint32_t exitAfterRuns = 0u);

        [[nodiscard]] std::string_view Name() const noexcept override { return "Sandbox.KMeansTrace"; }
        [[nodiscard]] Runtime::RuntimeModuleResult OnRegister(Runtime::EngineSetup& setup) override;
        [[nodiscard]] Runtime::RuntimeModuleResult OnResolve(Runtime::EngineSetup& setup) override;
        void OnShutdown(Runtime::RuntimeModuleShutdownContext&) override;

        // Attaches the last completed frame's pacing to its sample (after the loop ended, and each frame).
        void AttachCompletedPacing();

    private:
        void Sample(std::uint64_t frameIndex);

        std::vector<KMeansTraceSample>& m_Samples;
        std::uint32_t m_TargetFrames{};
        std::uint32_t m_ExitAfterRuns{};
        std::uint32_t m_TailFrames{};
        std::function<Runtime::RuntimeFramePacingDiagnostics()> m_ReadLastPacing{};
        std::function<void()> m_RequestExit{};
        const Runtime::ClusteringService* m_Clustering{};
        const Runtime::RenderExtractionCache* m_Extraction{};
    };

    [[nodiscard]] bool WriteKMeansTrace(const std::filesystem::path& path, std::span<const KMeansTraceSample> samples);
}
