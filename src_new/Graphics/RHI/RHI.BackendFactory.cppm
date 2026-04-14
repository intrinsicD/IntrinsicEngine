export module Extrinsic.RHI.Factory;

import <memory>;
import Extrinsic.Core.Config;
import Extrinsic.RHI.Device;

namespace Extrinsic::RHI
{
    export std::unique_ptr<RHI::IDevice> CreateDevice(Core::GraphicsBackend backend);
}
