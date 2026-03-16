module;

#include <cstdint>
#include <string>
#include <vector>

export module Core.Benchmark;

import Core.Telemetry;

export namespace Core::Benchmark
{
    // Configuration for a benchmark run.
    struct BenchmarkConfig
    {
        uint32_t FrameCount = 300;         // Number of frames to capture
        uint32_t WarmupFrames = 30;        // Frames to skip before recording
        std::string OutputPath = "benchmark.json";
    };

    // Per-frame snapshot captured during a benchmark run.
    struct FrameSnapshot
    {
        uint64_t FrameNumber = 0;
        uint64_t CpuTimeNs = 0;
        uint64_t GpuTimeNs = 0;
        uint64_t FrameTimeNs = 0;
        uint32_t DrawCalls = 0;
        uint32_t TriangleCount = 0;
    };

    // Aggregated statistics from a benchmark run.
    struct BenchmarkStats
    {
        double AvgFrameTimeMs = 0.0;
        double MinFrameTimeMs = 0.0;
        double MaxFrameTimeMs = 0.0;
        double P95FrameTimeMs = 0.0;
        double P99FrameTimeMs = 0.0;
        double AvgCpuTimeMs = 0.0;
        double AvgGpuTimeMs = 0.0;
        double AvgFPS = 0.0;
        uint32_t TotalFrames = 0;

        // Per-pass average timings (name, avgGpuMs, avgCpuMs).
        struct PassAvg
        {
            std::string Name{};
            double AvgGpuMs = 0.0;
            double AvgCpuMs = 0.0;
        };
        std::vector<PassAvg> PassAverages{};
    };

    // Benchmark runner: collects frame snapshots during a run and produces
    // aggregated statistics and JSON output.
    class BenchmarkRunner
    {
    public:
        void Configure(const BenchmarkConfig& config)
        {
            m_Config = config;
            m_Snapshots.clear();
            m_PassAccum.clear();
            m_FrameIndex = 0;
        }

        // Call once per frame during the benchmark run.
        // Returns true when the benchmark is still running, false when complete.
        bool RecordFrame(const Telemetry::TelemetrySystem& telemetry);

        // Compute aggregated statistics from recorded snapshots.
        [[nodiscard]] BenchmarkStats ComputeStats() const;

        // Write benchmark results to JSON file. Returns true on success.
        bool WriteJSON(const std::string& path) const;

        // Convenience: compute stats and write to configured output path.
        bool Finalize();

        [[nodiscard]] bool IsComplete() const { return m_FrameIndex >= m_Config.WarmupFrames + m_Config.FrameCount; }
        [[nodiscard]] bool IsWarmingUp() const { return m_FrameIndex < m_Config.WarmupFrames; }
        [[nodiscard]] uint32_t FramesRecorded() const { return static_cast<uint32_t>(m_Snapshots.size()); }

    private:
        BenchmarkConfig m_Config;
        std::vector<FrameSnapshot> m_Snapshots;
        uint32_t m_FrameIndex = 0;

        // Per-pass timing accumulation across frames.
        struct PassAccum
        {
            std::string Name{};
            double TotalGpuMs = 0.0;
            double TotalCpuMs = 0.0;
            uint32_t SampleCount = 0;
        };
        std::vector<PassAccum> m_PassAccum;
    };
}
