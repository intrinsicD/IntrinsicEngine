export module Extrinsic.RHI.Factory;

import <memory>;
import Extrinsic.Platform.Window;
import Extrinsic.RHI.Device;

namespace Extrinsic::RHI
{
    export struct BackendCreateInfo
    {
        Extrinsic::Core::RenderConfig RenderConfig;
        Extrinsic::Platform::IWindow& Window;
    };

    export std::unique_ptr<RHI::IDevice> CreateDevice(BackendCreateInfo const& createInfo);
}