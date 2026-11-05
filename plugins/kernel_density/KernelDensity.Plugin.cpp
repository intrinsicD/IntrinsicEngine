module;

#include <string>

module Intrinsic.Plugin.KernelDensity;

import Extrinsic.Runtime.PluginContract;

namespace Intrinsic::Plugins::KernelDensity
{
    bool Register(R::PluginRegistrations& staged, std::string&) noexcept
    {
        staged.Name = "kernel_density";
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
