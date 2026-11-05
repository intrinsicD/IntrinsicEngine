// ARCH-022 Q83: the Sandbox's K-Means trace joins each frame's clustering counters with that frame's pacing.
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

#include <nlohmann/json.hpp>
#include <unistd.h>

#include "RuntimeTestModule.hpp"

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Window;
import Extrinsic.Runtime.AsyncWorkModule;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.StepRuns;
import Extrinsic.Sandbox.KMeansTrace;

namespace
{
    namespace R = Extrinsic::Runtime;

    struct DelayCommand
    {
    };
    constexpr auto kDelay = std::chrono::milliseconds{20};

    // At UiBuild of its 6th frame: one K-Means request and one command that busy-waits 20 ms; both are drained by
    // the next frame's command drain.
    class Driver final : public Intrinsic::Tests::RuntimeTestModule
    {
    public:
        [[nodiscard]] Extrinsic::Core::Result OnRegister(R::EngineSetup& setup) override
        {
            if (auto base = RuntimeTestModule::OnRegister(setup); !base) return base;
            setup.RegisterCommandHandler<DelayCommand>([](R::CommandContext&, const DelayCommand&)
            {
                const auto until = std::chrono::steady_clock::now() + kDelay;
                while (std::chrono::steady_clock::now() < until) {}
                return R::CommandOutcome::Ok();
            });
            return Extrinsic::Core::Ok();
        }
        void Frame(double, double) override
        {
            auto& engine = Kernel();
            if (++Frames == 6u)
            {
                (void)engine.Services().Find<R::ClusteringService>()->RunKMeans(R::RunKMeans{});
                engine.Commands().Enqueue(DelayCommand{});
            }
            if (Frames >= 12u) engine.RequestExit();
        }
        std::uint32_t Frames{};
    };
}

TEST(SandboxKMeansTrace, EachFrameCarriesItsOwnCountersAndPacing)
{
    Extrinsic::Core::Config::EngineConfig config{};
    config.ReferenceScene.Enabled = false;
    config.Window.Backend = Extrinsic::Core::Config::WindowBackend::Null;
    std::vector<Extrinsic::Sandbox::KMeansTraceSample> samples;
    Intrinsic::Tests::RuntimeTestKernel engine(config, std::make_unique<Driver>());
    engine.EmplaceModule<R::AsyncWorkModule>();
    engine.EmplaceModule<R::SceneDocumentModule>();
    engine.EmplaceModule<R::ClusteringModule>();
    engine.EmplaceModule<R::StepRunsModule>();
    auto* trace = &engine.EmplaceModule<Extrinsic::Sandbox::KMeansTraceModule>(samples, 0u,
        [&engine] { return engine.GetLastFramePacingDiagnostics(); }, [] {});
    engine.Initialize();
    engine.Run();
    trace->AttachCompletedPacing();

    ASSERT_GE(samples.size(), 10u);
    std::size_t submitted = samples.size();
    for (std::size_t i = 0; i < samples.size(); ++i)
    {
        if (i + 1u < samples.size())
        {
            EXPECT_EQ(samples[i + 1u].FrameIndex, samples[i].FrameIndex + 1u) << i;
            // Every frame but the exit's (whose pacing the engine does not complete) carries its own pacing.
            ASSERT_TRUE(samples[i].Pacing.has_value()) << "frame " << samples[i].FrameIndex << " has no pacing";
            EXPECT_EQ(samples[i].Pacing->FrameIndex, samples[i].FrameIndex) << i;
        }
        if (submitted == samples.size() && samples[i].Clustering.RequestsSubmitted == 1u) submitted = i;
    }
    ASSERT_LT(submitted + 1u, samples.size()) << "the request's frame and the next one are traced";
    // The request is counted in its own frame and handled in the next frame's command drain, where the delay
    // lands: the frame that carries the handled command also carries the 20 ms.
    EXPECT_EQ(samples[submitted].Clustering.CommandsHandled, 0u);
    EXPECT_EQ(samples[submitted + 1u].Clustering.CommandsHandled, 1u);
    const auto delayMicros = std::uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(kDelay).count());
    EXPECT_LT(samples[submitted].Pacing->CommandDrainMicros, delayMicros);
    EXPECT_GE(samples[submitted + 1u].Pacing->CommandDrainMicros, delayMicros);
    if (submitted + 2u < samples.size()) EXPECT_LT(samples[submitted + 2u].Pacing->CommandDrainMicros, delayMicros);

    const auto path = std::filesystem::temp_directory_path() /
        ("intrinsic-kmeans-trace-" + std::to_string(::getpid()) + ".json");
    ASSERT_TRUE(Extrinsic::Sandbox::WriteKMeansTrace(path, samples));
    std::ifstream file(path);
    const auto json = nlohmann::json::parse(file);
    std::filesystem::remove(path);
    EXPECT_EQ(json.at("schema"), "arch022.kmeans_trace.v1");
    ASSERT_EQ(json.at("frames").size(), samples.size());
    const auto& handled = json.at("frames").at(submitted + 1u);
    EXPECT_EQ(handled.at("clustering").at("commands_handled"), 1u);
    EXPECT_GE(handled.at("pacing").at("command_drain_us").get<std::uint64_t>(), delayMicros);
    EXPECT_EQ(handled.at("pacing").at("frame_index"), samples[submitted + 1u].FrameIndex);
}
