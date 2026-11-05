module;

#include <string>

module Intrinsic.Plugin.PointSpacing;

import Extrinsic.Runtime.PluginContract;

namespace Intrinsic::Plugins::PointSpacing
{
    bool Register(R::PluginRegistrations& staged, std::string&) noexcept
    {
        staged.Name = "point_spacing";
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
