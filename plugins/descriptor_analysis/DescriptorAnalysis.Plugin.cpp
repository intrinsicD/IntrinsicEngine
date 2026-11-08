module;

#include <string>

module Intrinsic.Plugin.DescriptorAnalysis;

import Extrinsic.Runtime.PluginContract;

namespace Intrinsic::Plugins::DescriptorAnalysis
{
    bool Register(R::PluginRegistrations& staged, std::string&) noexcept
    {
        staged.Name = "descriptor_analysis";
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
