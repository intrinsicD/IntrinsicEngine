#include <gtest/gtest.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

import Core.Telemetry;
import Core.Benchmark;

using namespace Core::Benchmark;
using namespace Core::Telemetry;

// ---- BenchmarkRunner unit tests ----

TEST(Benchmark_Runner, DefaultNotComplete)
{
    BenchmarkRunner runner;
    BenchmarkConfig cfg{};
    cfg.FrameCount = 10;
    cfg.WarmupFrames = 2;
    runner.Configure(cfg);

    EXPECT_FALSE(runner.IsComplete());
    EXPECT_TRUE(runner.IsWarmingUp());
    EXPECT_EQ(runner.FramesRecorded(), 0u);
}

TEST(Benchmark_Runner, WarmupFramesSkipped)
{
    BenchmarkRunner runner;
    BenchmarkConfig cfg{};
    cfg.FrameCount = 5;
    cfg.WarmupFrames = 3;
    runner.Configure(cfg);

    auto& telemetry = TelemetrySystem::Get();

    // Run warmup frames — should not record snapshots.
    for (uint32_t i = 0; i < cfg.WarmupFrames; ++i)
    {
        telemetry.BeginFrame();
        telemetry.EndFrame();
        runner.RecordFrame(telemetry);
    }

    EXPECT_FALSE(runner.IsWarmingUp());
    EXPECT_FALSE(runner.IsComplete());
    EXPECT_EQ(runner.FramesRecorded(), 0u);
}

TEST(Benchmark_Runner, CompletesAfterConfiguredFrames)
{
    BenchmarkRunner runner;
    BenchmarkConfig cfg{};
    cfg.FrameCount = 5;
    cfg.WarmupFrames = 2;
    runner.Configure(cfg);

    auto& telemetry = TelemetrySystem::Get();

    const uint32_t totalFrames = cfg.WarmupFrames + cfg.FrameCount;
    for (uint32_t i = 0; i < totalFrames; ++i)
    {
        telemetry.BeginFrame();
        telemetry.EndFrame();
        runner.RecordFrame(telemetry);
    }

    EXPECT_TRUE(runner.IsComplete());
    EXPECT_EQ(runner.FramesRecorded(), cfg.FrameCount);
}

TEST(Benchmark_Runner, ComputeStatsBasic)
{
    BenchmarkRunner runner;
    BenchmarkConfig cfg{};
    cfg.FrameCount = 10;
    cfg.WarmupFrames = 0;
    runner.Configure(cfg);

    auto& telemetry = TelemetrySystem::Get();

    for (uint32_t i = 0; i < cfg.FrameCount; ++i)
    {
        telemetry.BeginFrame();
        telemetry.EndFrame();
        runner.RecordFrame(telemetry);
    }

    auto stats = runner.ComputeStats();
    EXPECT_EQ(stats.TotalFrames, 10u);
    EXPECT_GE(stats.AvgFrameTimeMs, 0.0);
    EXPECT_GE(stats.MinFrameTimeMs, 0.0);
    EXPECT_LE(stats.MinFrameTimeMs, stats.MaxFrameTimeMs);
    EXPECT_LE(stats.P95FrameTimeMs, stats.P99FrameTimeMs + 0.001);
}

TEST(Benchmark_Runner, WriteJSONCreatesFile)
{
    BenchmarkRunner runner;
    BenchmarkConfig cfg{};
    cfg.FrameCount = 5;
    cfg.WarmupFrames = 0;
    cfg.OutputPath = "test_benchmark_output.json";
    runner.Configure(cfg);

    auto& telemetry = TelemetrySystem::Get();

    for (uint32_t i = 0; i < cfg.FrameCount; ++i)
    {
        telemetry.BeginFrame();
        telemetry.EndFrame();
        runner.RecordFrame(telemetry);
    }

    EXPECT_TRUE(runner.WriteJSON(cfg.OutputPath));
    EXPECT_TRUE(std::filesystem::exists(cfg.OutputPath));

    // Verify JSON contains expected fields.
    std::ifstream file(cfg.OutputPath);
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("\"totalFrames\""), std::string::npos);
    EXPECT_NE(content.find("\"avgFrameTimeMs\""), std::string::npos);
    EXPECT_NE(content.find("\"avgFPS\""), std::string::npos);
    EXPECT_NE(content.find("\"frames\""), std::string::npos);
    EXPECT_NE(content.find("\"passTimings\""), std::string::npos);

    // Clean up.
    std::filesystem::remove(cfg.OutputPath);
}

TEST(Benchmark_Runner, FinalizeUsesConfiguredPath)
{
    BenchmarkRunner runner;
    BenchmarkConfig cfg{};
    cfg.FrameCount = 3;
    cfg.WarmupFrames = 0;
    cfg.OutputPath = "test_finalize_output.json";
    runner.Configure(cfg);

    auto& telemetry = TelemetrySystem::Get();

    for (uint32_t i = 0; i < cfg.FrameCount; ++i)
    {
        telemetry.BeginFrame();
        telemetry.EndFrame();
        runner.RecordFrame(telemetry);
    }

    EXPECT_TRUE(runner.Finalize());
    EXPECT_TRUE(std::filesystem::exists(cfg.OutputPath));

    std::filesystem::remove(cfg.OutputPath);
}

// ---- Telemetry PassTimings tests ----

TEST(Telemetry_PassTimings, SetAndGetPassGpuTimings)
{
    auto& telemetry = TelemetrySystem::Get();
    telemetry.BeginFrame();

    std::vector<PassTimingEntry> entries;
    entries.push_back({"SurfacePass", 500'000, 0});
    entries.push_back({"LinePass", 200'000, 0});
    telemetry.SetPassGpuTimings(std::move(entries));

    const auto& timings = telemetry.GetPassTimings();
    ASSERT_EQ(timings.size(), 2u);
    EXPECT_EQ(timings[0].Name, "SurfacePass");
    EXPECT_EQ(timings[0].GpuTimeNs, 500'000u);
    EXPECT_EQ(timings[1].Name, "LinePass");
    EXPECT_EQ(timings[1].GpuTimeNs, 200'000u);

    telemetry.EndFrame();
}

TEST(Telemetry_PassTimings, MergePassCpuTimings)
{
    auto& telemetry = TelemetrySystem::Get();
    telemetry.BeginFrame();

    // Set GPU timings first.
    std::vector<PassTimingEntry> gpuEntries;
    gpuEntries.push_back({"SurfacePass", 500'000, 0});
    gpuEntries.push_back({"LinePass", 200'000, 0});
    telemetry.SetPassGpuTimings(std::move(gpuEntries));

    // Merge CPU timings.
    std::vector<std::pair<std::string, uint64_t>> cpuTimings;
    cpuTimings.emplace_back("SurfacePass", 300'000);
    cpuTimings.emplace_back("LinePass", 150'000);
    cpuTimings.emplace_back("NewPass", 100'000); // CPU-only pass
    telemetry.MergePassCpuTimings(cpuTimings);

    const auto& timings = telemetry.GetPassTimings();
    ASSERT_GE(timings.size(), 3u);

    // Find entries by name.
    auto find = [&](const std::string& name) -> const PassTimingEntry*
    {
        for (const auto& t : timings)
            if (t.Name == name) return &t;
        return nullptr;
    };

    auto* surface = find("SurfacePass");
    ASSERT_NE(surface, nullptr);
    EXPECT_EQ(surface->GpuTimeNs, 500'000u);
    EXPECT_EQ(surface->CpuTimeNs, 300'000u);

    auto* line = find("LinePass");
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->GpuTimeNs, 200'000u);
    EXPECT_EQ(line->CpuTimeNs, 150'000u);

    auto* newPass = find("NewPass");
    ASSERT_NE(newPass, nullptr);
    EXPECT_EQ(newPass->GpuTimeNs, 0u);
    EXPECT_EQ(newPass->CpuTimeNs, 100'000u);

    telemetry.EndFrame();
}
