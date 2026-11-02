module;

#include <string>

module Intrinsic.Plugin.BilateralFilter;

import Extrinsic.Runtime.PluginContract;

namespace Intrinsic::Plugins::BilateralFilter
{
    bool Register(R::PluginRegistrations& staged, std::string&) noexcept
    {
        staged.Generation = 1;
        staged.ConfigSections.push_back(R::MakeDerivedSectionRegistration<kSection>());
        AddTools(staged);
        AddWindow(staged);
        return true;
    }

    void Shutdown() noexcept { ShutdownWindow(); }
}

// The plugin linked into a static Sandbox (no dlopen): the Sandbox wraps these in an in-process image.
extern "C" bool intrinsic_bilateral_filter_register(Extrinsic::Runtime::PluginRegistrations& staged,
                                                    std::string& error) noexcept
{
    return Intrinsic::Plugins::BilateralFilter::Register(staged, error);
}

extern "C" void intrinsic_bilateral_filter_shutdown() noexcept { Intrinsic::Plugins::BilateralFilter::Shutdown(); }
