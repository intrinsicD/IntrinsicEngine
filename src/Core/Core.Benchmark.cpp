module;

#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

module Core.Benchmark;

import Core.Telemetry;
import Core.Logging;

namespace Core::Benchmark
{
    bool BenchmarkRunner::RecordFrame(const Telemetry::TelemetrySystem& telemetry)
    {
        if (IsComplete())
            return false;

        const uint32_t frameIndex = m_FrameIndex;
        m_FrameIndex = frameIndex + 1;

        if (frameIndex < m_Config.WarmupFrames)
            return true;

        const auto& stats = telemetry.GetFrameStats(0);

        FrameSnapshot snap{};
        snap.FrameNumber = stats.FrameNumber;
        snap.CpuTimeNs = stats.CpuTimeNs;
        snap.GpuTimeNs = stats.GpuTimeNs;
        snap.FrameTimeNs = stats.FrameTimeNs;
        snap.DrawCalls = stats.DrawCalls;
        snap.TriangleCount = stats.TriangleCount;
        m_Snapshots.push_back(snap);

        // Accumulate per-pass timings.
        const auto& passTimings = telemetry.GetPassTimings();
        for (const auto& pt : passTimings)
        {
            const double gpuMs = static_cast<double>(pt.GpuTimeNs) / 1'000'000.0;
            const double cpuMs = static_cast<double>(pt.CpuTimeNs) / 1'000'000.0;

            bool found = false;
            for (auto& acc : m_PassAccum)
            {
                if (acc.Name == pt.Name)
                {
                    acc.TotalGpuMs += gpuMs;
                    acc.TotalCpuMs += cpuMs;
                    acc.SampleCount++;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                m_PassAccum.push_back(PassAccum{pt.Name, gpuMs, cpuMs, 1});
            }
        }

        return true;
    }

    BenchmarkStats BenchmarkRunner::ComputeStats() const
    {
        BenchmarkStats result{};
        const size_t n = m_Snapshots.size();
        if (n == 0) return result;

        result.TotalFrames = static_cast<uint32_t>(n);

        // Collect frame times for percentile computation.
        std::vector<double> frameTimes;
        frameTimes.reserve(n);

        double sumFrame = 0.0;
        double sumCpu = 0.0;
        double sumGpu = 0.0;
        double minFrame = 1e18;
        double maxFrame = 0.0;

        for (const auto& snap : m_Snapshots)
        {
            const double ms = static_cast<double>(snap.FrameTimeNs) / 1'000'000.0;
            frameTimes.push_back(ms);
            sumFrame += ms;
            sumCpu += static_cast<double>(snap.CpuTimeNs) / 1'000'000.0;
            sumGpu += static_cast<double>(snap.GpuTimeNs) / 1'000'000.0;
            if (ms < minFrame) minFrame = ms;
            if (ms > maxFrame) maxFrame = ms;
        }

        result.AvgFrameTimeMs = sumFrame / static_cast<double>(n);
        result.MinFrameTimeMs = minFrame;
        result.MaxFrameTimeMs = maxFrame;
        result.AvgCpuTimeMs = sumCpu / static_cast<double>(n);
        result.AvgGpuTimeMs = sumGpu / static_cast<double>(n);
        result.AvgFPS = result.AvgFrameTimeMs > 0.0 ? 1000.0 / result.AvgFrameTimeMs : 0.0;

        // Compute percentiles.
        std::sort(frameTimes.begin(), frameTimes.end());
        auto percentile = [&](double p) -> double
        {
            const size_t idx = static_cast<size_t>(p * static_cast<double>(n - 1));
            return frameTimes[std::min(idx, n - 1)];
        };
        result.P95FrameTimeMs = percentile(0.95);
        result.P99FrameTimeMs = percentile(0.99);

        // Per-pass averages.
        for (const auto& acc : m_PassAccum)
        {
            if (acc.SampleCount == 0) continue;
            result.PassAverages.push_back(BenchmarkStats::PassAvg{
                acc.Name,
                acc.TotalGpuMs / static_cast<double>(acc.SampleCount),
                acc.TotalCpuMs / static_cast<double>(acc.SampleCount)
            });
        }

        return result;
    }

    bool BenchmarkRunner::WriteJSON(const std::string& path) const
    {
        auto stats = ComputeStats();

        FILE* f = std::fopen(path.c_str(), "w");
        if (!f)
        {
            Core::Log::Error("BenchmarkRunner: Failed to open output file: {}", path);
            return false;
        }

        std::fprintf(f, "{\n");
        std::fprintf(f, "  \"totalFrames\": %u,\n", stats.TotalFrames);
        std::fprintf(f, "  \"avgFrameTimeMs\": %.4f,\n", stats.AvgFrameTimeMs);
        std::fprintf(f, "  \"minFrameTimeMs\": %.4f,\n", stats.MinFrameTimeMs);
        std::fprintf(f, "  \"maxFrameTimeMs\": %.4f,\n", stats.MaxFrameTimeMs);
        std::fprintf(f, "  \"p95FrameTimeMs\": %.4f,\n", stats.P95FrameTimeMs);
        std::fprintf(f, "  \"p99FrameTimeMs\": %.4f,\n", stats.P99FrameTimeMs);
        std::fprintf(f, "  \"avgCpuTimeMs\": %.4f,\n", stats.AvgCpuTimeMs);
        std::fprintf(f, "  \"avgGpuTimeMs\": %.4f,\n", stats.AvgGpuTimeMs);
        std::fprintf(f, "  \"avgFPS\": %.2f,\n", stats.AvgFPS);

        // Per-pass averages.
        std::fprintf(f, "  \"passTimings\": [\n");
        for (size_t i = 0; i < stats.PassAverages.size(); ++i)
        {
            const auto& pa = stats.PassAverages[i];
            std::fprintf(f, "    {\"name\": \"%s\", \"avgGpuMs\": %.4f, \"avgCpuMs\": %.4f}%s\n",
                          pa.Name.c_str(), pa.AvgGpuMs, pa.AvgCpuMs,
                          (i + 1 < stats.PassAverages.size()) ? "," : "");
        }
        std::fprintf(f, "  ],\n");

        // Per-frame data.
        std::fprintf(f, "  \"frames\": [\n");
        for (size_t i = 0; i < m_Snapshots.size(); ++i)
        {
            const auto& snap = m_Snapshots[i];
            const double frameMs = static_cast<double>(snap.FrameTimeNs) / 1'000'000.0;
            const double cpuMs = static_cast<double>(snap.CpuTimeNs) / 1'000'000.0;
            const double gpuMs = static_cast<double>(snap.GpuTimeNs) / 1'000'000.0;
            std::fprintf(f, "    {\"frame\": %llu, \"frameMs\": %.4f, \"cpuMs\": %.4f, \"gpuMs\": %.4f, \"draws\": %u, \"tris\": %u}%s\n",
                          static_cast<unsigned long long>(snap.FrameNumber),
                          frameMs, cpuMs, gpuMs, snap.DrawCalls, snap.TriangleCount,
                          (i + 1 < m_Snapshots.size()) ? "," : "");
        }
        std::fprintf(f, "  ]\n");
        std::fprintf(f, "}\n");
        std::fclose(f);

        Core::Log::Info("BenchmarkRunner: Results written to {}", path);
        Core::Log::Info("  Avg: {:.3f} ms ({:.1f} FPS), p95: {:.3f} ms, p99: {:.3f} ms",
                        stats.AvgFrameTimeMs, stats.AvgFPS, stats.P95FrameTimeMs, stats.P99FrameTimeMs);

        return true;
    }

    bool BenchmarkRunner::Finalize()
    {
        return WriteJSON(m_Config.OutputPath);
    }
}
