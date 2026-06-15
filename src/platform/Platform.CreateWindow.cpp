module;

#include <memory>

module Extrinsic.Platform.Window;

import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Backend.Null;
#if defined(INTRINSIC_PLATFORM_BACKEND_GLFW)
import Extrinsic.Platform.Backend.Glfw;
#endif

namespace Extrinsic::Platform
{
    std::unique_ptr<IWindow> CreateWindow(const Core::Config::WindowConfig& config)
    {
        if (config.Backend == Core::Config::WindowBackend::Null)
            return Backends::Null::CreateWindow(config);

#if defined(INTRINSIC_PLATFORM_BACKEND_GLFW)
        return Backends::Glfw::CreateWindow(config);
#else
        return Backends::Null::CreateWindow(config);
#endif
    }
}
