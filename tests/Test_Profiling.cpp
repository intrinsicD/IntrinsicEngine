// =============================================================================
// Test_Profiling — Contract tests for Core.Profiling / Core.Telemetry.
//
// Covers: ScopedTimer RAII timing, TelemetrySystem sample recording,
//         category aggregation, and nested timer correctness.
//
// Target: IntrinsicCoreTests (pure algorithmic, no GPU, no ECS).
// =============================================================================

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

import Core.Telemetry;
import Core.Hash;

using namespace Core::Telemetry;
using namespace Core::Hash;

// Helper: sleep for at least N milliseconds (allows timing assertions).
static void BusyWaitMs(int ms)
{
    auto start = std::chrono::high_resolution_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::high_resolution_clock::now() - start)
               .count() < ms)
    {
        // Spin — avoids OS scheduler uncertainty of std::this_thread::sleep_for.
    }
}

// ---------------------------------------------------------------------------
// Basic ScopedTimer: records a sample to TelemetrySystem.
// ---------------------------------------------------------------------------
TEST(Profiling_ScopedTimer, RecordsSampleToTelemetry)
{
    auto& telemetry = TelemetrySystem::Get();
    telemetry.BeginFrame();

    {
        static constexpr uint32_t kHash = HashString("TestScope");
        ScopedTimer timer("TestScope", kHash);
        BusyWaitMs(1);
    }

    telemetry.EndFrame();

    // The sample should have been recorded in the current frame.
    const auto& stats = telemetry.GetFrameStats(0);
    EXPECT_GT(stats.SampleCount, 0u);
}

// ---------------------------------------------------------------------------
// Category aggregation: multiple samples with the same name hash accumulate.
// ---------------------------------------------------------------------------
TEST(Profiling_ScopedTimer, CategoryAccumulation)
{
    auto& telemetry = TelemetrySystem::Get();
    telemetry.BeginFrame();

    static constexpr uint32_t kHash = HashString("AccumScope");

    for (int i = 0; i < 3; ++i)
    {
        ScopedTimer timer("AccumScope", kHash);
        BusyWaitMs(1);
    }

    telemetry.EndFrame();

    // Find the category for our scope.
    const TimingCategory* categories = telemetry.GetCategories();
    const size_t count = telemetry.GetCategoryCount();

    bool found = false;
    for (size_t i = 0; i < count; ++i)
    {
        if (categories[i].NameHash == kHash)
        {
            EXPECT_GE(categories[i].CallCount, 3u);
            EXPECT_GT(categories[i].TotalTimeNs, 0u);
            EXPECT_GT(categories[i].MinTimeNs, 0u);
            EXPECT_GT(categories[i].MaxTimeNs, 0u);
            EXPECT_GE(categories[i].MaxTimeNs, categories[i].MinTimeNs);
            found = true;
            break;
        }
    }

    EXPECT_TRUE(found) << "Category 'AccumScope' not found in telemetry";
}

// ---------------------------------------------------------------------------
// Nested timers: both outer and inner scopes produce samples.
// ---------------------------------------------------------------------------
TEST(Profiling_ScopedTimer, NestedTimers)
{
    auto& telemetry = TelemetrySystem::Get();
    telemetry.BeginFrame();

    static constexpr uint32_t kOuterHash = HashString("OuterScope");
    static constexpr uint32_t kInnerHash = HashString("InnerScope");

    {
        ScopedTimer outer("OuterScope", kOuterHash);
        BusyWaitMs(1);
        {
            ScopedTimer inner("InnerScope", kInnerHash);
            BusyWaitMs(1);
        }
    }

    telemetry.EndFrame();

    // Both categories should exist.
    const TimingCategory* categories = telemetry.GetCategories();
    const size_t count = telemetry.GetCategoryCount();

    bool foundOuter = false, foundInner = false;
    for (size_t i = 0; i < count; ++i)
    {
        if (categories[i].NameHash == kOuterHash) foundOuter = true;
        if (categories[i].NameHash == kInnerHash) foundInner = true;
    }

    EXPECT_TRUE(foundOuter) << "Outer scope category not found";
    EXPECT_TRUE(foundInner) << "Inner scope category not found";
}

// ---------------------------------------------------------------------------
// TelemetrySystem: frame stats tracking across multiple frames.
// ---------------------------------------------------------------------------
TEST(Profiling_TelemetrySystem, FrameStatsHistory)
{
    auto& telemetry = TelemetrySystem::Get();

    // Run several frames.
    for (int f = 0; f < 5; ++f)
    {
        telemetry.BeginFrame();
        BusyWaitMs(1);
        telemetry.EndFrame();
    }

    // GetFrameStats(0) should return the most recent frame.
    const auto& latest = telemetry.GetFrameStats(0);
    EXPECT_GT(latest.FrameTimeNs, 0u);
}

// ---------------------------------------------------------------------------
// TelemetrySystem: draw call and triangle counting.
// ---------------------------------------------------------------------------
TEST(Profiling_TelemetrySystem, DrawCallTracking)
{
    auto& telemetry = TelemetrySystem::Get();
    telemetry.BeginFrame();

    telemetry.RecordDrawCall(100);
    telemetry.RecordDrawCall(200);

    telemetry.EndFrame();

    const auto& stats = telemetry.GetFrameStats(0);
    EXPECT_GE(stats.DrawCalls, 2u);
    EXPECT_GE(stats.TriangleCount, 300u);
}

// ---------------------------------------------------------------------------
// TelemetrySystem: fixed-step simulation telemetry is captured per frame.
// ---------------------------------------------------------------------------
TEST(Profiling_TelemetrySystem, SimulationStatsTracking)
{
    auto& telemetry = TelemetrySystem::Get();
    telemetry.BeginFrame();

    telemetry.SetSimulationStats(3, 1, 42'000);

    telemetry.EndFrame();

    const auto& stats = telemetry.GetFrameStats(0);
    EXPECT_EQ(stats.SimulationTickCount, 3u);
    EXPECT_EQ(stats.SimulationClampHitCount, 1u);
    EXPECT_EQ(stats.SimulationCpuTimeNs, 42'000u);
}

TEST(Profiling_TelemetrySystem, SimulationStatsResetAtBeginFrame)
{
    auto& telemetry = TelemetrySystem::Get();

    telemetry.BeginFrame();
    telemetry.SetSimulationStats(2, 1, 7'000);
    telemetry.EndFrame();

    telemetry.BeginFrame();
    telemetry.EndFrame();

    const auto& stats = telemetry.GetFrameStats(0);
    EXPECT_EQ(stats.SimulationTickCount, 0u);
    EXPECT_EQ(stats.SimulationClampHitCount, 0u);
    EXPECT_EQ(stats.SimulationCpuTimeNs, 0u);
}

// ---------------------------------------------------------------------------
// TelemetrySystem: average frame time is non-negative.
// ---------------------------------------------------------------------------
TEST(Profiling_TelemetrySystem, AverageFrameTime)
{
    auto& telemetry = TelemetrySystem::Get();

    telemetry.BeginFrame();
    telemetry.EndFrame();

    const double avgMs = telemetry.GetAverageFrameTimeMs(1);
    EXPECT_GE(avgMs, 0.0);

    const double fps = telemetry.GetAverageFPS(1);
    EXPECT_GE(fps, 0.0);
}

// ---------------------------------------------------------------------------
// TimingCategory: AddSample updates all statistics correctly.
// ---------------------------------------------------------------------------
TEST(Profiling_TimingCategory, AddSampleUpdatesStats)
{
    TimingCategory cat{};
    cat.NameHash = 42;
    cat.Name = "TestCat";

    cat.AddSample(1000);
    EXPECT_EQ(cat.CallCount, 1u);
    EXPECT_EQ(cat.TotalTimeNs, 1000u);
    EXPECT_EQ(cat.MinTimeNs, 1000u);
    EXPECT_EQ(cat.MaxTimeNs, 1000u);

    cat.AddSample(500);
    EXPECT_EQ(cat.CallCount, 2u);
    EXPECT_EQ(cat.TotalTimeNs, 1500u);
    EXPECT_EQ(cat.MinTimeNs, 500u);
    EXPECT_EQ(cat.MaxTimeNs, 1000u);

    cat.AddSample(2000);
    EXPECT_EQ(cat.CallCount, 3u);
    EXPECT_EQ(cat.TotalTimeNs, 3500u);
    EXPECT_EQ(cat.MinTimeNs, 500u);
    EXPECT_EQ(cat.MaxTimeNs, 2000u);

    EXPECT_NEAR(cat.AverageMs(), 3500.0 / 3.0 / 1e6, 1e-9);
    EXPECT_NEAR(cat.TotalMs(), 3500.0 / 1e6, 1e-9);
}

// ---------------------------------------------------------------------------
// TimingCategory: Reset clears all state.
// ---------------------------------------------------------------------------
TEST(Profiling_TimingCategory, ResetClearsState)
{
    TimingCategory cat{};
    cat.AddSample(1000);
    cat.AddSample(2000);

    cat.Reset();

    EXPECT_EQ(cat.CallCount, 0u);
    EXPECT_EQ(cat.TotalTimeNs, 0u);
    EXPECT_EQ(cat.MinTimeNs, UINT64_MAX);
    EXPECT_EQ(cat.MaxTimeNs, 0u);
}
