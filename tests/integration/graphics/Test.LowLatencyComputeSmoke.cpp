// GRAPHICS-150/ARCH-023: SpatialIndexCache::QueueGpuCompute records every request on a command
// buffer of its own and submits it at once through the compute lane; there is no framed variant
// to compare with any more (the Framed/Immediate axis was removed), so this checks the one
// scheduling contract instead. The CPD E-step workspace (METHOD-056) runs the same request
// repeatedly: every readback is identical, and each round trip is delivered by the maintenance of
// the frame after the one that queued it. Frames and milliseconds per round trip are printed.
#include "RuntimeTestModule.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <gtest/gtest.h>
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Graphics.CoherentPointDriftEStep;
import Extrinsic.Core.Config.Engine;

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace Graphics = Extrinsic::Graphics;

    struct Shutdown
    {
        Runtime::Engine& Engine;
        ~Shutdown() { Engine.Shutdown(); }
    };

    // Float-float points (hi xyz w, lo xyz unused); the source's w is a log-weight of 0.
    std::vector<float> Points(std::size_t count, unsigned seed)
    {
        std::mt19937 random(seed);
        std::uniform_real_distribution<double> uniform(-1.0, 1.0);
        std::vector<float> out;
        for (std::size_t i = 0; i < count; ++i)
        {
            const double p[3]{uniform(random), uniform(random), 0.6 * uniform(random)};
            for (const double c : p) out.push_back(float(c));
            out.push_back(0.0f);
            for (const double c : p) out.push_back(float(c - double(float(c))));
            out.push_back(0.0f);
        }
        return out;
    }

    struct RoundTrip
    {
        std::shared_ptr<Runtime::SpatialGpuResult> Gpu{};
        std::shared_ptr<Graphics::CoherentPointDriftEStepWorkspace> Workspace{};
        int Frames{0};
        std::chrono::steady_clock::time_point Queued{};
        double Milliseconds{0.0};
    };

    class LatencyApp final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        static constexpr int kRuns = 6;

        void Resolve() override
        {
            Started = std::chrono::steady_clock::now();
            Cache = Kernel().Services().Find<Runtime::SpatialIndexCache>();
            Target = Points(4096, 11u);
            Source = Points(3000, 12u);
        }

        void Queue()
        {
            auto& trip = Trips.emplace_back();
            trip.Workspace = std::make_shared<Graphics::CoherentPointDriftEStepWorkspace>(
                Kernel().GetDevice(), *Cache->MethodScratch());
            trip.Queued = std::chrono::steady_clock::now();
            trip.Gpu = Cache->QueueGpuCompute(
                Graphics::CoherentPointDriftEStepWorkspace::ReadbackBytes(Target.size() / 8u, Source.size() / 8u),
                [this, workspace = trip.Workspace](
                    Extrinsic::RHI::ICommandContext& commands, const Runtime::SpatialGpuIndexView&)
                {
                    return workspace->Record(commands, {.Target = Target, .TargetGeneration = 1u, .Source = Source,
                                                        .Sigma2 = 0.05, .LogOutlier = -std::numeric_limits<double>::infinity()});
                });
        }

        void Frame(double, double) override
        {
            // Every frame lasts at least 8 ms, longer than this E-step's device round trip (3.4 ms on an RTX 3050,
            // about 2 ms on an RTX 4090), so immediate work has time to finish before the next poll and the frame count
            // does not depend on the host's frame rate (unpaced 64x64 frames take under 2 ms there; ARCH-022 Q90).
            // A scheduling check, not a measurement: the milliseconds include the pacing.
            struct Pace { ~Pace() { std::this_thread::sleep_for(std::chrono::milliseconds(8)); } } pace;
            if (std::chrono::steady_clock::now() - Started > std::chrono::seconds(60))
            {
                ADD_FAILURE() << "GRAPHICS-150 timeout after " << Trips.size() << " round trips";
                Kernel().RequestExit();
                return;
            }
            if (!Kernel().GetDevice().IsOperational() || Cache == nullptr) return;
            if (Trips.empty())
            {
                Queue();
                return;
            }
            auto& trip = Trips.back();
            if (trip.Gpu->State == Runtime::SpatialQueryState::Failed)
            {
                ADD_FAILURE() << "round trip " << Trips.size() << " failed: " << trip.Gpu->Diagnostic;
                Kernel().RequestExit();
                return;
            }
            // Frames counts the Frame() calls after the one that queued the work.
            if (trip.Gpu->State != Runtime::SpatialQueryState::Ready) { ++trip.Frames; return; }
            if (trip.Milliseconds == 0.0)
                trip.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - trip.Queued).count();
            if (int(Trips.size()) == kRuns) { Kernel().RequestExit(); return; }
            Queue();
        }

        // The workspaces own device buffers; the readback bytes stay for the assertions.
        void Shutdown() override
        {
            for (auto& trip : Trips) trip.Workspace.reset();
        }

        Runtime::SpatialIndexCache* Cache{};
        std::vector<float> Target{}, Source{};
        std::vector<RoundTrip> Trips{};
        std::chrono::steady_clock::time_point Started{};
    };
}

TEST(GRAPHICS150LowLatencyCompute, LaneSubmissionsReturnIdenticalBytesByTheNextFrame)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize()) GTEST_SKIP() << "GLFW unavailable";
    auto config = Runtime::CreateReferenceEngineConfig();
    config.Window.Width = 64;
    config.Window.Height = 64;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = false;
    config.ReferenceScene.Enabled = false;
    auto app = std::make_unique<LatencyApp>();
    auto* run = app.get();
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::move(app));
    engine.EmplaceModule<Runtime::SpatialIndexCache>();
    engine.Initialize();
    Shutdown shutdown{engine};
    if (!engine.GetDevice().SupportsShaderFloat64()) GTEST_SKIP() << "Shader float64 unavailable";
    engine.Run();
    ASSERT_EQ(run->Trips.size(), std::size_t(LatencyApp::kRuns));

    int frames = 0;
    double milliseconds = 0.0;
    const auto& reference = run->Trips.front().Gpu->Data;
    for (std::size_t i = 0; i < run->Trips.size(); ++i)
    {
        const auto& trip = run->Trips[i];
        ASSERT_EQ(trip.Gpu->State, Runtime::SpatialQueryState::Ready) << i;
        EXPECT_EQ(trip.Gpu->Data, reference) << "round trip " << i << " differs from the first one";
        frames += trip.Frames;
        milliseconds += trip.Milliseconds;
    }
    std::printf("GRAPHICS-150: %.1f frames / %.2f ms per round trip\n",
        frames / double(LatencyApp::kRuns),
        milliseconds / LatencyApp::kRuns);
    // Formerly only the immediate half met this bound and the framed half waited its frames in flight.
    EXPECT_LE(frames / double(LatencyApp::kRuns), 1.0)
        << "each submission is delivered by the next frame's maintenance";
}
