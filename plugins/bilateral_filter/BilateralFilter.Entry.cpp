// The v1 entry points of the shared-library build (only the MODULE target compiles this file).
#include <string>

#include "PluginBuildId.h" // INTRINSIC_PLUGIN_BUILD_ID of the engine this build compiles against

import Extrinsic.Runtime.PluginContract;

extern "C" bool intrinsic_bilateral_filter_register(Extrinsic::Runtime::PluginRegistrations& staged,
                                                    std::string& error) noexcept;
extern "C" void intrinsic_bilateral_filter_shutdown() noexcept;

extern "C" const char* intrinsic_plugin_build_id_v1() noexcept { return INTRINSIC_PLUGIN_BUILD_ID; }

extern "C" bool intrinsic_plugin_register_v1(Extrinsic::Runtime::PluginRegistrations& staged,
                                             std::string& error) noexcept
{
    return intrinsic_bilateral_filter_register(staged, error);
}

extern "C" void intrinsic_plugin_shutdown_v1() noexcept { intrinsic_bilateral_filter_shutdown(); }
