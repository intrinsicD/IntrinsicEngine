module;

#include <string>

module Intrinsic.Plugin.JobFormSpike;

import Extrinsic.Runtime.PluginContract;

namespace Intrinsic::Plugins::JobFormSpike
{
    bool Register(R::PluginRegistrations& staged, std::string&) noexcept
    {
        staged.Name = "job_form_spike";
        staged.Generation = 1;
        AddTools(staged);
        AddWindow(staged);
        return true;
    }

    // Its runs end with the unload drain (the holder job is cancelled), before this.
    void Shutdown() noexcept { ShutdownWindow(); }
}
