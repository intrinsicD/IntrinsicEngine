module;

#include <string>

module Intrinsic.Plugin.BilateralFilter;

import Extrinsic.Runtime.PluginContract;

namespace Intrinsic::Plugins::BilateralFilter
{
    bool Register(R::PluginRegistrations& staged, std::string&) noexcept
    {
        staged.Name = "bilateral_filter";
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
