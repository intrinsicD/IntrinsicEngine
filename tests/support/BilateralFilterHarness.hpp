// ARCH-022: the plugin harness (PluginHarness.hpp) with the bilateral filter plugin.
#pragma once

#include <memory>
#include <string>
#include <utility>

#include "PluginHarness.hpp"

import Intrinsic.Plugin.BilateralFilter;

namespace Intrinsic::Tests::BilateralFilter
{
    namespace Config = Extrinsic::Core::Config;
    namespace R = Extrinsic::Runtime;
    namespace BF = Intrinsic::Plugins::BilateralFilter;
    using Json = nlohmann::json;
    using PointCloudEditing::HeadlessConfig;

    struct Harness : Plugins::Harness
    {
        explicit Harness(Config::EngineConfig config = HeadlessConfig(),
                         std::unique_ptr<RuntimeTestModule> application = std::make_unique<OneFrameApplication>())
            : Plugins::Harness(&BF::Register, &BF::Shutdown, std::move(config), std::move(application))
        {
        }

        std::string Settled(const BF::Config& config) { return Plugins::Harness::Settled([&] { return BF::Preview(config); }); }
        [[nodiscard]] testing::AssertionResult Apply(const BF::Config& config) { return Plugins::Harness::Apply<BF::kSection>(config); }
    };
}
