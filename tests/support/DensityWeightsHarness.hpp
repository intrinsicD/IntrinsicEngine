// ARCH-022: the plugin harness (PluginHarness.hpp) with the density weights plugin.
#pragma once

#include <memory>
#include <string>
#include <utility>

#include "PluginHarness.hpp"

import Intrinsic.Plugin.DensityWeights;

namespace Intrinsic::Tests::DensityWeights
{
    namespace Config = Extrinsic::Core::Config;
    namespace R = Extrinsic::Runtime;
    namespace DW = Intrinsic::Plugins::DensityWeights;
    using Json = nlohmann::json;
    using PointCloudEditing::HeadlessConfig;

    struct Harness : Plugins::Harness
    {
        explicit Harness(Config::EngineConfig config = HeadlessConfig(),
                         std::unique_ptr<RuntimeTestModule> application = std::make_unique<OneFrameApplication>())
            : Plugins::Harness(&DW::Register, &DW::Shutdown, std::move(config), std::move(application))
        {
        }

        std::string Settled(const DW::Config& config) { return Plugins::Harness::Settled([&] { return DW::Preview(config); }); }
        [[nodiscard]] testing::AssertionResult Apply(const DW::Config& config) { return Plugins::Harness::Apply<DW::kSection>(config); }
        // Frames until the run ended (a Vulkan run reaches Final through Refresh, as its window and tool do).
        const DW::Result& Wait(DW::Run& run, const int frames = 5000)
        {
            for (int frame = 0; !run.Final && frame < frames; ++frame)
            {
                DW::Refresh(run);
                if (!run.Final) Frame();
            }
            return Plugins::Harness::Wait(run, 1);
        }
    };
}
