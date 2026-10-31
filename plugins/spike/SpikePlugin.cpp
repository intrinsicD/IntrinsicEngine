// ARCH-022 slice 2, S1 build-recipe probe: imports host modules, defines one entry point.
#include <cstddef>

import Extrinsic.Core.Logging;
import Extrinsic.Runtime.EditorUiHost;

extern "C" std::size_t intrinsic_plugin_probe_v0() noexcept
{
    return Extrinsic::Core::Log::GetEntryCount();
}
