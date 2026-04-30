module;

#include <memory>

module Extrinsic.Platform.Window;

#if defined(INTRINSIC_PLATFORM_BACKEND_GLFW)
import Extrinsic.Platform.Backend.Glfw;
#else
import Extrinsic.Platform.Backend.Null;
#endif

namespace Extrinsic::Platform
{
    std::unique_ptr<IWindow> CreateWindow(const Core::Config::WindowConfig& config)
    {
#if defined(INTRINSIC_PLATFORM_BACKEND_GLFW)
        return Backends::Glfw::CreateWindow(config);
#else
        return Backends::Null::CreateWindow(config);
#endif
    }
}

