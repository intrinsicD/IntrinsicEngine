module;

#include <string>

module Intrinsic.Plugin.CoherentPointDrift;

import Extrinsic.Runtime.PluginContract;

namespace Intrinsic::Plugins::CoherentPointDrift
{
    bool Register(R::PluginRegistrations& staged, std::string&) noexcept
    {
        staged.Name = "coherent_point_drift";
        staged.Generation = 1;
        AddTools(staged);
        AddWindow(staged);
        return true;
    }

    // Its runs end with the unload drain (the holder job is cancelled), before this.
    void Shutdown() noexcept { ShutdownWindow(); }
}
