// GRAPHICS-036C — contract coverage that the runtime-owned `RenderWorldPool`
// (GRAPHICS-036A) is wired into the engine frame loop: `Engine` owns a pool
// sized from `RenderConfig::SynchronousExtraction` (one logical buffer in the
// default synchronous mode, triple-buffered when pipelined is requested), and
// `RunFrame` drives the producer/consumer slot lifecycle while mirroring the
// pool's three diagnostics counters (GRAPHICS-036B) onto the last extraction
// stats.
//
// The static-wiring assertions (pool ownership + buffer-count policy + a clean
// zero baseline before any frame runs) execute in every environment, including
// the displayless CPU gate where `Engine::Run()` executes zero frames because
// the GLFW window reports `ShouldClose()` immediately. The per-frame baseline
// assertion is therefore gated on a live window (backend-agnostic
// `GetWindow().ShouldClose()` probe) exactly like the RUNTIME-090 Slice B ImGui
// engine-wiring test; the slot-lifecycle sequencing itself is pinned in every
// environment by the GRAPHICS-036A `Test.RenderWorldPool.cpp` unit contract.

#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.RenderWorldPool;

using Extrinsic::Runtime::Engine;

namespace
{
    // Drives a bounded run: requests exit once `TargetFrames` variable ticks
    // have run so `Engine::Run()` executes exactly `TargetFrames` full frames on
    // a live-window lane.
    class BoundedRunApplication final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        explicit BoundedRunApplication(const std::uint32_t targetFrames)
            : m_TargetFrames(targetFrames)
        {
        }

        void Resolve() override {}
        void Frame(double /*alpha*/, double /*dt*/) override
        {
            auto& engine = Kernel();
            ++m_VariableTicks;
            if (m_VariableTicks >= m_TargetFrames)
                engine.RequestExit();
        }
        void Shutdown() override {}

    private:
        std::uint32_t m_TargetFrames{0u};
        std::uint32_t m_VariableTicks{0u};
    };

    // Reference scene + camera disabled so the bounded run exercises the minimal
    // frame path; `synchronous` selects the pool sizing under test.
    [[nodiscard]] Extrinsic::Core::Config::EngineConfig PoolConfig(const bool synchronous)
    {
        Extrinsic::Core::Config::EngineConfig config{};
        config.Simulation.WorkerThreadCount = 1u;
        config.ReferenceScene.Enabled       = false;
        config.Camera.Enabled               = false;
        config.Render.SynchronousExtraction = synchronous;
        return config;
    }
}

// Static wiring (runs everywhere, including displayless CI): the default
// synchronous config sizes the pool to one logical buffer, the pool is
// reachable through the engine accessor, and no frame has perturbed the
// diagnostics baseline yet.
TEST(RenderWorldPoolEngineWiring, EngineSizesSingleBufferPoolFromSynchronousConfig)
{
    Intrinsic::Tests::RuntimeTestKernel engine(PoolConfig(/*synchronous=*/true),
                                               std::make_unique<BoundedRunApplication>(1u));
    engine.Initialize();

    const auto& pool = engine.GetRenderWorldPool();
    EXPECT_EQ(pool.BufferCount(), 1u);
    EXPECT_TRUE(pool.IsSynchronous());

    // Nothing consumed yet: the three GRAPHICS-036 decision-7 counters and the
    // mirrored extraction stats are at their zero baseline.
    const auto& diag = pool.GetDiagnostics();
    EXPECT_EQ(diag.PipelineStallCount, 0u);
    EXPECT_EQ(diag.ExtractionSkipCount, 0u);
    EXPECT_EQ(diag.LastConsumedFrameAge, 0u);

    const auto& stats = engine.GetLastRenderExtractionStats();
    EXPECT_EQ(stats.RenderWorldPipelineStallCount, 0u);
    EXPECT_EQ(stats.RenderWorldExtractionSkipCount, 0u);
    EXPECT_EQ(stats.RenderWorldFrameAgeFrames, 0u);

    engine.Shutdown();
}

// Static wiring: requesting pipelined extraction sizes the pool for
// triple-buffering with reclamation (GRAPHICS-036 decision 1). The flip of the
// default itself is GRAPHICS-036D; here the flag is set explicitly.
TEST(RenderWorldPoolEngineWiring, EngineSizesTripleBufferedPoolWhenAsynchronous)
{
    Intrinsic::Tests::RuntimeTestKernel engine(PoolConfig(/*synchronous=*/false),
                                               std::make_unique<BoundedRunApplication>(1u));
    engine.Initialize();

    const auto& pool = engine.GetRenderWorldPool();
    EXPECT_EQ(pool.BufferCount(), Extrinsic::Runtime::RenderWorldPool::kDefaultBuffers);
    EXPECT_FALSE(pool.IsSynchronous());

    engine.Shutdown();
}

// Per-frame: a bounded `Engine::Run()` in synchronous mode drives the pool
// every frame and preserves the existing single-snapshot behavior — the
// consumer never stalls or skips and always reads a freshly published front
// (frame age 0). The pool's index/refcount sequencing is unit-tested in every
// environment by Test.RenderWorldPool.cpp; this asserts the engine drives that
// contract.
TEST(RenderWorldPoolEngineWiring, BoundedRunDrivesPoolSynchronousBaseline)
{
    constexpr std::uint32_t kFrames = 3u;
    Intrinsic::Tests::RuntimeTestKernel engine(PoolConfig(/*synchronous=*/true),
                                               std::make_unique<BoundedRunApplication>(kFrames));
    engine.Initialize();

    if (engine.GetWindow().ShouldClose())
    {
        // No live window backend (headless CI with no display): Engine::Run()
        // would execute zero frames. Static wiring is still asserted above; the
        // per-frame baseline needs a real window.
        EXPECT_EQ(engine.GetRenderWorldPool().BufferCount(), 1u);
        engine.Shutdown();
        GTEST_SKIP() << "window backend unavailable; per-frame pool coverage "
                        "requires a display";
    }

    engine.Run();

    const auto& diag = engine.GetRenderWorldPool().GetDiagnostics();
    EXPECT_EQ(diag.ExtractionSkipCount, 0u);
    EXPECT_EQ(diag.PipelineStallCount, 0u);
    EXPECT_EQ(diag.LastConsumedFrameAge, 0u);

    const auto& stats = engine.GetLastRenderExtractionStats();
    EXPECT_EQ(stats.RenderWorldExtractionSkipCount, 0u);
    EXPECT_EQ(stats.RenderWorldPipelineStallCount, 0u);
    EXPECT_EQ(stats.RenderWorldFrameAgeFrames, 0u);

    // After a frame retires the single synchronous slot is released, so no front
    // reference leaks across frames.
    EXPECT_EQ(engine.GetRenderWorldPool().RefCount(engine.GetRenderWorldPool().FrontSlot()), 0u);

    engine.Shutdown();
}
