module;

#include <string>

module Intrinsic.Plugin.DensityWeights;

import Extrinsic.Runtime.PluginContract;

namespace Intrinsic::Plugins::DensityWeights
{
    bool Register(R::PluginRegistrations& staged, std::string&) noexcept
    {
        staged.Name = "density_weights";
        staged.Generation = 1;
        staged.ConfigSections.push_back(R::MakeDerivedSectionRegistration<kSection>());
        AddTools(staged);
        AddWindow(staged);
        return true;
    }

    void Shutdown() noexcept
    {
        ShutdownWindow();
        ShutdownRuns();
    }
}
