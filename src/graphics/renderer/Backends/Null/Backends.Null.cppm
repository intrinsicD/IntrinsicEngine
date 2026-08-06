module;

#include <memory>

export module Extrinsic.Backends.Null;

import Extrinsic.RHI.Device;

namespace Extrinsic::Backends::Null
{
    // Stub IDevice that allocates pool slots without touching any real GPU.
    // IDevice::IsOperational() returns false so upstream managers know to
    // short-circuit. It remains the deterministic headless/test backend while
    // the separately owned Extrinsic.Backends.Vulkan module provides promoted
    // native GPU execution.
    export std::unique_ptr<RHI::IDevice> CreateNullDevice();
}
