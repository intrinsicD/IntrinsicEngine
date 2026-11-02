// ARCH-022: the plugin harness (PluginHarness.hpp) with the FPFH descriptor analysis plugin.
#pragma once

#include <memory>
#include <string>
#include <utility>

#include "PluginHarness.hpp"

import Intrinsic.Plugin.DescriptorAnalysis;

namespace Intrinsic::Tests::DescriptorAnalysis
{
    namespace Config = Extrinsic::Core::Config;
    namespace R = Extrinsic::Runtime;
    namespace DA = Intrinsic::Plugins::DescriptorAnalysis;
    using Json = nlohmann::json;
    using PointCloudEditing::HeadlessConfig;

    struct Harness : Plugins::Harness
    {
        explicit Harness(Config::EngineConfig config = HeadlessConfig(),
                         std::unique_ptr<RuntimeTestModule> application = std::make_unique<OneFrameApplication>())
            : Plugins::Harness(&DA::Register, &DA::Shutdown, std::move(config), std::move(application))
        {
        }

        std::string Settled(const DA::Config& config) { return Plugins::Harness::Settled([&] { return DA::Preview(config); }); }
        [[nodiscard]] testing::AssertionResult Apply(const DA::Config& config) { return Plugins::Harness::Apply<DA::kSection>(config); }
    };
}
