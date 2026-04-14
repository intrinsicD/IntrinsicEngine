module;

#include <cstdint>

export module Extrinsic.RHI.FrameHandle;

namespace Extrinsic::RHI
{
    export struct FrameHandle
    {
        std::uint32_t FrameIndex{0};
        std::uint32_t SwapchainImageIndex{0};
    };
}