// ARCH-022: the plugin harness (PluginHarness.hpp) with the kernel density plugin.
#pragma once

#include <memory>
#include <string>
#include <utility>

#include "PluginHarness.hpp"

import Intrinsic.Plugin.KernelDensity;

namespace Intrinsic::Tests::KernelDensity
{
    namespace Config = Extrinsic::Core::Config;
    namespace R = Extrinsic::Runtime;
    namespace KD = Intrinsic::Plugins::KernelDensity;
    using Json = nlohmann::json;
    using PointCloudEditing::HeadlessConfig;

    struct Harness : Plugins::Harness
    {
        explicit Harness(Config::EngineConfig config = HeadlessConfig(),
                         std::unique_ptr<RuntimeTestModule> application = std::make_unique<OneFrameApplication>())
            : Plugins::Harness(&KD::Register, &KD::Shutdown, std::move(config), std::move(application))
        {
        }

        std::string Settled(const KD::Config& config) { return Plugins::Harness::Settled([&] { return KD::Preview(config); }); }
        [[nodiscard]] testing::AssertionResult Apply(const KD::Config& config) { return Plugins::Harness::Apply<KD::kSection>(config); }
        // Frames until the run ended (a Vulkan run reaches Final through Refresh, as its window and tool do).
        const KD::Result& Wait(KD::Run& run, const int frames = 5000)
        {
            for (int frame = 0; !run.Final && frame < frames; ++frame)
            {
                KD::Refresh(run);
                if (!run.Final) Frame();
            }
            return Plugins::Harness::Wait(run, 1);
        }
    };
}
