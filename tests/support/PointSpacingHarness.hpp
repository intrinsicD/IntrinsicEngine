// ARCH-022: the plugin harness (PluginHarness.hpp) with the point spacing plugin.
#pragma once

#include <memory>
#include <string>
#include <utility>

#include "PluginHarness.hpp"

import Intrinsic.Plugin.PointSpacing;

namespace Intrinsic::Tests::PointSpacing
{
    namespace Config = Extrinsic::Core::Config;
    namespace R = Extrinsic::Runtime;
    namespace PS = Intrinsic::Plugins::PointSpacing;
    using Json = nlohmann::json;
    using PointCloudEditing::HeadlessConfig;

    struct Harness : Plugins::Harness
    {
        explicit Harness(Config::EngineConfig config = HeadlessConfig(),
                         std::unique_ptr<RuntimeTestModule> application = std::make_unique<OneFrameApplication>())
            : Plugins::Harness(&PS::Register, &PS::Shutdown, std::move(config), std::move(application))
        {
        }

        std::string Settled(const PS::Config& config) { return Plugins::Harness::Settled([&] { return PS::Preview(config); }); }
        [[nodiscard]] testing::AssertionResult Apply(const PS::Config& config) { return Plugins::Harness::Apply<PS::kSection>(config); }
        // Frames until the run ended (a Vulkan run reaches Final through Refresh, as its window and tool do).
        const PS::Result& Wait(PS::Run& run, const int frames = 5000)
        {
            for (int frame = 0; !run.Final && frame < frames; ++frame)
            {
                PS::Refresh(run);
                if (!run.Final) Frame();
            }
            return Plugins::Harness::Wait(run, 1);
        }
    };
}
