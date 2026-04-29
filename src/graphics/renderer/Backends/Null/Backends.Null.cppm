module;

#include <memory>

export module Extrinsic.Backends.Null;

import Extrinsic.RHI.Device;

namespace Extrinsic::Backends::Null
{
    // Stub IDevice that allocates pool slots without touching any real GPU.
    // IDevice::IsOperational() returns false so upstream managers know to
    // short-circuit. Intended for compile-time integration, unit tests, and
    // as the scaffold that a future Extrinsic.Backends.Vulkan module will
    // replace once real Vulkan is wired. TODO markers inside the .cpp
    // indicate the exact seams where vkCreateInstance / VMA / vkQueueSubmit
    // calls belong.
    export std::unique_ptr<RHI::IDevice> CreateNullDevice();
}

