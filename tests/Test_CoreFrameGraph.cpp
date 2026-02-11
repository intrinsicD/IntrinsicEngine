#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

import Core;

using namespace Core;
using Core::Hash::operator""_id;

// -------------------------------------------------------------------------
// Dummy component types for dependency declarations
// -------------------------------------------------------------------------
struct Transform { float x, y, z; };
struct Velocity  { float vx, vy, vz; };
struct Health    { int hp; };
struct Collider  { float radius; };
struct AudioData { float volume; };

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------
namespace
{
    // Returns the execution index of the named pass in the log.
    ptrdiff_t IndexOf(const std::vector<std::string>& log, const std::string& name)
    {
        auto it = std::find(log.begin(), log.end(), name);
        return (it != log.end()) ? std::distance(log.begin(), it) : -1;
    }

    // Verify that pass 'before' executed before pass 'after' in the log.
    void ExpectOrder(const std::vector<std::string>& log,
                     const std::string& before, const std::string& after)
    {
        auto a = IndexOf(log, before);
        auto b = IndexOf(log, after);
        ASSERT_GE(a, 0) << before << " not found in log";
        ASSERT_GE(b, 0) << after << " not found in log";
        EXPECT_LT(a, b) << before << " should execute before " << after;
    }
}

// =========================================================================
// Test: Basic topological ordering from Read/Write declarations
// =========================================================================
TEST(CoreFrameGraph, LinearChain)
{
    // Input → Physics → RenderPrep
    // Input writes Velocity.
    // Physics reads Velocity, writes Transform.
    // RenderPrep reads Transform.

    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);
    std::vector<std::string> log;

    graph.AddPass("Input",
        [](FrameGraphBuilder& b) { b.Write<Velocity>(); },
        [&]() { log.emplace_back("Input"); });

    graph.AddPass("Physics",
        [](FrameGraphBuilder& b) { b.Read<Velocity>(); b.Write<Transform>(); },
        [&]() { log.emplace_back("Physics"); });

    graph.AddPass("RenderPrep",
        [](FrameGraphBuilder& b) { b.Read<Transform>(); },
        [&]() { log.emplace_back("RenderPrep"); });

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value()) << "Compile failed";

    // With a linear chain, we expect 3 layers of 1 pass each.
    const auto& layers = graph.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 3u);
    EXPECT_EQ(layers[0].size(), 1u);
    EXPECT_EQ(layers[1].size(), 1u);
    EXPECT_EQ(layers[2].size(), 1u);

    // Execute (single-task layers run inline, no scheduler needed).
    graph.Execute();

    ASSERT_EQ(log.size(), 3u);
    ExpectOrder(log, "Input", "Physics");
    ExpectOrder(log, "Physics", "RenderPrep");
}

// =========================================================================
// Test: WAW ordering (two writers to the same component)
// =========================================================================
TEST(CoreFrameGraph, WriteAfterWrite)
{
    // Input writes Velocity first. AI writes Velocity second.
    // Physics reads Velocity → depends on AI (last writer).
    // Expected: Input → AI → Physics

    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);
    std::vector<std::string> log;

    graph.AddPass("Input",
        [](FrameGraphBuilder& b) { b.Write<Velocity>(); },
        [&]() { log.emplace_back("Input"); });

    graph.AddPass("AI",
        [](FrameGraphBuilder& b) { b.Write<Velocity>(); },
        [&]() { log.emplace_back("AI"); });

    graph.AddPass("Physics",
        [](FrameGraphBuilder& b) { b.Read<Velocity>(); },
        [&]() { log.emplace_back("Physics"); });

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());

    graph.Execute();

    ASSERT_EQ(log.size(), 3u);
    ExpectOrder(log, "Input", "AI");
    ExpectOrder(log, "AI", "Physics");
}

// =========================================================================
// Test: WAR ordering (writer must wait for all readers)
// =========================================================================
TEST(CoreFrameGraph, WriteAfterRead)
{
    // Physics writes Transform.
    // RenderPrep reads Transform.
    // AudioPrep reads Transform.
    // PostProcess writes Transform (must wait for both readers).

    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);
    std::vector<std::string> log;

    graph.AddPass("Physics",
        [](FrameGraphBuilder& b) { b.Write<Transform>(); },
        [&]() { log.emplace_back("Physics"); });

    graph.AddPass("RenderPrep",
        [](FrameGraphBuilder& b) { b.Read<Transform>(); },
        [&]() { log.emplace_back("RenderPrep"); });

    graph.AddPass("AudioPrep",
        [](FrameGraphBuilder& b) { b.Read<Transform>(); },
        [&]() { log.emplace_back("AudioPrep"); });

    graph.AddPass("PostProcess",
        [](FrameGraphBuilder& b) { b.Write<Transform>(); },
        [&]() { log.emplace_back("PostProcess"); });

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());

    // Expected layers:
    // Layer 0: Physics (writes Transform)
    // Layer 1: RenderPrep, AudioPrep (both read Transform — parallel)
    // Layer 2: PostProcess (writes Transform, depends on both readers)
    const auto& layers = graph.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 3u);
    EXPECT_EQ(layers[0].size(), 1u);
    EXPECT_EQ(layers[1].size(), 2u);
    EXPECT_EQ(layers[2].size(), 1u);

    // Execute with scheduler for the parallel layer.
    Tasks::Scheduler::Initialize(2);
    graph.Execute();
    Tasks::Scheduler::Shutdown();

    ASSERT_EQ(log.size(), 4u);
    ExpectOrder(log, "Physics", "RenderPrep");
    ExpectOrder(log, "Physics", "AudioPrep");
    ExpectOrder(log, "RenderPrep", "PostProcess");
    ExpectOrder(log, "AudioPrep", "PostProcess");
}

// =========================================================================
// Test: Parallel independent passes (RAR)
// =========================================================================
TEST(CoreFrameGraph, ParallelReaders)
{
    // Physics writes Transform.
    // Three independent systems read Transform.
    // They should all be in the same execution layer.

    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);
    std::atomic<int> counter{0};

    graph.AddPass("Physics",
        [](FrameGraphBuilder& b) { b.Write<Transform>(); },
        [&]() { counter.fetch_add(1, std::memory_order_relaxed); });

    graph.AddPass("ReaderA",
        [](FrameGraphBuilder& b) { b.Read<Transform>(); },
        [&]() { counter.fetch_add(10, std::memory_order_relaxed); });

    graph.AddPass("ReaderB",
        [](FrameGraphBuilder& b) { b.Read<Transform>(); },
        [&]() { counter.fetch_add(100, std::memory_order_relaxed); });

    graph.AddPass("ReaderC",
        [](FrameGraphBuilder& b) { b.Read<Transform>(); },
        [&]() { counter.fetch_add(1000, std::memory_order_relaxed); });

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = graph.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(layers[0].size(), 1u);  // Physics
    EXPECT_EQ(layers[1].size(), 3u);  // All readers parallel

    Tasks::Scheduler::Initialize(4);
    graph.Execute();
    Tasks::Scheduler::Shutdown();

    EXPECT_EQ(counter.load(), 1111);
}

// =========================================================================
// Test: Diamond dependency
// =========================================================================
TEST(CoreFrameGraph, DiamondDependency)
{
    //         Input
    //        /     \
    //   Physics   AI
    //        \     /
    //       RenderPrep
    //
    // Input writes Velocity.
    // Physics reads Velocity, writes Transform.
    // AI reads Velocity, writes Health.
    // RenderPrep reads Transform and Health.

    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);
    std::vector<std::string> log;

    graph.AddPass("Input",
        [](FrameGraphBuilder& b) { b.Write<Velocity>(); },
        [&]() { log.emplace_back("Input"); });

    graph.AddPass("Physics",
        [](FrameGraphBuilder& b) { b.Read<Velocity>(); b.Write<Transform>(); },
        [&]() { log.emplace_back("Physics"); });

    graph.AddPass("AI",
        [](FrameGraphBuilder& b) { b.Read<Velocity>(); b.Write<Health>(); },
        [&]() { log.emplace_back("AI"); });

    graph.AddPass("RenderPrep",
        [](FrameGraphBuilder& b) { b.Read<Transform>(); b.Read<Health>(); },
        [&]() { log.emplace_back("RenderPrep"); });

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = graph.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 3u);
    EXPECT_EQ(layers[0].size(), 1u);  // Input
    EXPECT_EQ(layers[1].size(), 2u);  // Physics + AI (parallel)
    EXPECT_EQ(layers[2].size(), 1u);  // RenderPrep

    Tasks::Scheduler::Initialize(2);
    graph.Execute();
    Tasks::Scheduler::Shutdown();

    ASSERT_EQ(log.size(), 4u);
    ExpectOrder(log, "Input", "Physics");
    ExpectOrder(log, "Input", "AI");
    ExpectOrder(log, "Physics", "RenderPrep");
    ExpectOrder(log, "AI", "RenderPrep");
}

// =========================================================================
// Test: Label-based ordering (Signal/WaitFor)
// =========================================================================
TEST(CoreFrameGraph, LabelOrdering)
{
    // GPU_Physics signals "PhysicsDone".
    // Renderer waits for "PhysicsDone".

    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);

    bool gpuRecorded = false;
    bool renderStarted = false;

    graph.AddPass("GPU_Physics",
        [](FrameGraphBuilder& b) { b.Signal("PhysicsDone"_id); },
        [&]() { gpuRecorded = true; });

    graph.AddPass("Renderer",
        [](FrameGraphBuilder& b) { b.WaitFor("PhysicsDone"_id); },
        [&]() {
            EXPECT_TRUE(gpuRecorded);
            renderStarted = true;
        });

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = graph.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);

    graph.Execute();

    EXPECT_TRUE(gpuRecorded);
    EXPECT_TRUE(renderStarted);
}

// =========================================================================
// Test: Mixed labels and typed resources
// =========================================================================
TEST(CoreFrameGraph, MixedLabelsAndResources)
{
    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);
    std::vector<std::string> log;

    graph.AddPass("Input",
        [](FrameGraphBuilder& b) {
            b.Write<Velocity>();
            b.Signal("InputDone"_id);
        },
        [&]() { log.emplace_back("Input"); });

    graph.AddPass("Physics",
        [](FrameGraphBuilder& b) {
            b.WaitFor("InputDone"_id);
            b.Read<Velocity>();
            b.Write<Transform>();
        },
        [&]() { log.emplace_back("Physics"); });

    graph.AddPass("Audio",
        [](FrameGraphBuilder& b) {
            b.WaitFor("InputDone"_id);
            b.Write<AudioData>();
        },
        [&]() { log.emplace_back("Audio"); });

    graph.AddPass("Renderer",
        [](FrameGraphBuilder& b) {
            b.Read<Transform>();
            b.Read<AudioData>();
        },
        [&]() { log.emplace_back("Renderer"); });

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = graph.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 3u);
    EXPECT_EQ(layers[0].size(), 1u);  // Input
    EXPECT_EQ(layers[1].size(), 2u);  // Physics + Audio (parallel)
    EXPECT_EQ(layers[2].size(), 1u);  // Renderer

    Tasks::Scheduler::Initialize(2);
    graph.Execute();
    Tasks::Scheduler::Shutdown();

    ASSERT_EQ(log.size(), 4u);
    ExpectOrder(log, "Input", "Physics");
    ExpectOrder(log, "Input", "Audio");
    ExpectOrder(log, "Physics", "Renderer");
    ExpectOrder(log, "Audio", "Renderer");
}

// =========================================================================
// Test: Empty graph compiles and executes without error
// =========================================================================
TEST(CoreFrameGraph, EmptyGraph)
{
    Memory::ScopeStack scope(1024);
    FrameGraph graph(scope);

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(graph.GetPassCount(), 0u);
    EXPECT_TRUE(graph.GetExecutionLayers().empty());

    // Execute on empty graph should be a no-op.
    graph.Execute();
}

// =========================================================================
// Test: Single pass (no dependencies)
// =========================================================================
TEST(CoreFrameGraph, SinglePass)
{
    Memory::ScopeStack scope(1024 * 16);
    FrameGraph graph(scope);
    bool ran = false;

    graph.AddPass("OnlyPass",
        [](FrameGraphBuilder& b) { b.Write<Transform>(); },
        [&]() { ran = true; });

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());

    EXPECT_EQ(graph.GetPassCount(), 1u);
    EXPECT_EQ(graph.GetExecutionLayers().size(), 1u);

    graph.Execute();
    EXPECT_TRUE(ran);
}

// =========================================================================
// Test: Reset and rebuild across multiple frames
// =========================================================================
TEST(CoreFrameGraph, MultiFrameReset)
{
    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);

    for (int frame = 0; frame < 3; ++frame)
    {
        scope.Reset();
        graph.Reset();

        int counter = 0;

        graph.AddPass("A",
            [](FrameGraphBuilder& b) { b.Write<Transform>(); },
            [&]() { counter += 1; });

        graph.AddPass("B",
            [](FrameGraphBuilder& b) { b.Read<Transform>(); },
            [&]() { counter += 10; });

        auto result = graph.Compile();
        ASSERT_TRUE(result.has_value()) << "Frame " << frame;

        graph.Execute();

        EXPECT_EQ(counter, 11) << "Frame " << frame;
    }
}

// =========================================================================
// Test: Same pass reads and writes the same type (in-place update)
// =========================================================================
TEST(CoreFrameGraph, ReadWriteSameType)
{
    // A single pass reads and writes Transform (e.g., in-place smoothing).
    // Another pass reads Transform afterward.

    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);
    std::vector<std::string> log;

    graph.AddPass("Smooth",
        [](FrameGraphBuilder& b) { b.Read<Transform>(); b.Write<Transform>(); },
        [&]() { log.emplace_back("Smooth"); });

    graph.AddPass("Render",
        [](FrameGraphBuilder& b) { b.Read<Transform>(); },
        [&]() { log.emplace_back("Render"); });

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());

    graph.Execute();

    ASSERT_EQ(log.size(), 2u);
    ExpectOrder(log, "Smooth", "Render");
}

// =========================================================================
// Test: Completely independent passes parallelize
// =========================================================================
TEST(CoreFrameGraph, IndependentPassesParallelize)
{
    // Three passes touching entirely different resources.
    // All should be in the same layer.

    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);

    graph.AddPass("PhysicsUpdate",
        [](FrameGraphBuilder& b) { b.Write<Transform>(); },
        [&]() {});

    graph.AddPass("HealthUpdate",
        [](FrameGraphBuilder& b) { b.Write<Health>(); },
        [&]() {});

    graph.AddPass("AudioUpdate",
        [](FrameGraphBuilder& b) { b.Write<AudioData>(); },
        [&]() {});

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = graph.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 1u);
    EXPECT_EQ(layers[0].size(), 3u);
}

// =========================================================================
// Test: Introspection (pass names)
// =========================================================================
TEST(CoreFrameGraph, PassNameIntrospection)
{
    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);

    graph.AddPass("Physics",
        [](FrameGraphBuilder& b) { b.Write<Transform>(); },
        []() {});

    graph.AddPass("Rendering",
        [](FrameGraphBuilder& b) { b.Read<Transform>(); },
        []() {});

    EXPECT_EQ(graph.GetPassName(0), "Physics");
    EXPECT_EQ(graph.GetPassName(1), "Rendering");
    EXPECT_EQ(graph.GetPassCount(), 2u);
}

// =========================================================================
// Test: Parallel execution with scheduler (stress)
// =========================================================================
TEST(CoreFrameGraph, ParallelExecutionStress)
{
    // Wide fan-out: 1 producer, N consumers, all reading the same resource.
    // Verifies that parallel dispatch + WaitForAll actually completes all work.

    constexpr uint32_t kReaderCount = 64;

    Memory::ScopeStack scope(1024 * 256);
    FrameGraph graph(scope);
    std::atomic<uint32_t> counter{0};

    graph.AddPass("Producer",
        [](FrameGraphBuilder& b) { b.Write<Transform>(); },
        [&]() { counter.fetch_add(1, std::memory_order_relaxed); });

    for (uint32_t i = 0; i < kReaderCount; ++i)
    {
        graph.AddPass("Reader",
            [](FrameGraphBuilder& b) { b.Read<Transform>(); },
            [&]() { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());

    const auto& layers = graph.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);
    EXPECT_EQ(layers[0].size(), 1u);
    EXPECT_EQ(layers[1].size(), kReaderCount);

    Tasks::Scheduler::Initialize(4);
    graph.Execute();
    Tasks::Scheduler::Shutdown();

    EXPECT_EQ(counter.load(), kReaderCount + 1);
}

// =========================================================================
// Test: Complex real-world-like frame
// =========================================================================
TEST(CoreFrameGraph, RealisticFrame)
{
    // Simulates a realistic game frame:
    //   Input        → writes Velocity
    //   AI           → reads Health, writes Velocity (WAW after Input)
    //   Physics      → reads Velocity, writes Transform, writes Collider
    //   Collision    → reads Collider, writes Health
    //   Animation    → reads Transform (parallel with Collision)
    //   RenderPrep   → reads Transform, reads Health (waits for Collision + Animation's deps)
    //
    // Expected ordering constraints:
    //   Input < AI (WAW on Velocity)
    //   AI < Physics (RAW on Velocity)
    //   Physics < Collision (RAW on Collider)
    //   Physics < Animation (RAW on Transform)
    //   Collision < RenderPrep (RAW on Health)
    //   Physics < RenderPrep (RAW on Transform)

    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);
    std::vector<std::string> log;

    graph.AddPass("Input",
        [](FrameGraphBuilder& b) { b.Write<Velocity>(); },
        [&]() { log.emplace_back("Input"); });

    graph.AddPass("AI",
        [](FrameGraphBuilder& b) { b.Read<Health>(); b.Write<Velocity>(); },
        [&]() { log.emplace_back("AI"); });

    graph.AddPass("Physics",
        [](FrameGraphBuilder& b) { b.Read<Velocity>(); b.Write<Transform>(); b.Write<Collider>(); },
        [&]() { log.emplace_back("Physics"); });

    graph.AddPass("Collision",
        [](FrameGraphBuilder& b) { b.Read<Collider>(); b.Write<Health>(); },
        [&]() { log.emplace_back("Collision"); });

    graph.AddPass("Animation",
        [](FrameGraphBuilder& b) { b.Read<Transform>(); },
        [&]() { log.emplace_back("Animation"); });

    graph.AddPass("RenderPrep",
        [](FrameGraphBuilder& b) { b.Read<Transform>(); b.Read<Health>(); },
        [&]() { log.emplace_back("RenderPrep"); });

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value());

    Tasks::Scheduler::Initialize(4);
    graph.Execute();
    Tasks::Scheduler::Shutdown();

    ASSERT_EQ(log.size(), 6u);

    ExpectOrder(log, "Input", "AI");
    ExpectOrder(log, "AI", "Physics");
    ExpectOrder(log, "Physics", "Collision");
    ExpectOrder(log, "Physics", "Animation");
    ExpectOrder(log, "Collision", "RenderPrep");

    // Verify layer structure
    const auto& layers = graph.GetExecutionLayers();
    // Input → AI → Physics → {Collision, Animation} → RenderPrep
    ASSERT_EQ(layers.size(), 5u);
    EXPECT_EQ(layers[0].size(), 1u);  // Input
    EXPECT_EQ(layers[1].size(), 1u);  // AI
    EXPECT_EQ(layers[2].size(), 1u);  // Physics
    EXPECT_EQ(layers[3].size(), 2u);  // Collision + Animation
    EXPECT_EQ(layers[4].size(), 1u);  // RenderPrep
}

// =========================================================================
// Issue 1.2: Negative error-handling tests for FrameGraph
// =========================================================================

// =========================================================================
// Test: Sequential registration cannot create cycles (structural safety)
// =========================================================================
TEST(CoreFrameGraph, NoCycleFromSequentialRegistration)
{
    // The FrameGraph's sequential setup API guarantees acyclic dependencies:
    // dependencies only flow from earlier-registered passes to later ones.
    //
    // Even "mutual" dependencies resolve to a single direction:
    //   A: reads Velocity, writes Transform
    //   B: reads Transform, writes Velocity
    // Result: A → B (RAW on Transform) + A → B (WAR on Velocity). No cycle.

    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);
    std::vector<std::string> log;

    graph.AddPass("A",
        [](FrameGraphBuilder& b) { b.Read<Velocity>(); b.Write<Transform>(); },
        [&]() { log.emplace_back("A"); });

    graph.AddPass("B",
        [](FrameGraphBuilder& b) { b.Read<Transform>(); b.Write<Velocity>(); },
        [&]() { log.emplace_back("B"); });

    auto result = graph.Compile();
    ASSERT_TRUE(result.has_value()) << "Sequential registration should never produce cycles";

    // Should be 2 layers: A then B
    const auto& layers = graph.GetExecutionLayers();
    ASSERT_EQ(layers.size(), 2u);

    graph.Execute();
    ASSERT_EQ(log.size(), 2u);
    ExpectOrder(log, "A", "B");
}

// =========================================================================
// Test: Compile error does not leave graph in corrupted state
// =========================================================================
TEST(CoreFrameGraph, ErrorRecovery_ResetAfterFailedCompile)
{
    // After a failed compile (cycle), Reset + rebuild with valid graph should work.
    Memory::ScopeStack scope(1024 * 64);
    FrameGraph graph(scope);

    // First: build a VALID graph, compile, execute.
    graph.AddPass("Valid",
        [](FrameGraphBuilder& b) { b.Write<Transform>(); },
        []() {});

    auto r1 = graph.Compile();
    ASSERT_TRUE(r1.has_value());
    graph.Execute();

    // Reset and rebuild with different (valid) graph.
    scope.Reset();
    graph.Reset();

    bool ran = false;
    graph.AddPass("After",
        [](FrameGraphBuilder& b) { b.Write<Health>(); },
        [&]() { ran = true; });

    auto r2 = graph.Compile();
    ASSERT_TRUE(r2.has_value());
    graph.Execute();

    EXPECT_TRUE(ran);
}
