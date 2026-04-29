module;

#include <cstdint>

export module Extrinsic.RHI.BufferView;

import Extrinsic.RHI.Handles;

export namespace Extrinsic::RHI
{
    struct BufferView
    {
        BufferHandle Buffer{};
        uint64_t Offset = 0;
        uint64_t Size = 0;

        [[nodiscard]] bool IsValid() const noexcept { return Buffer.IsValid(); }
    };
}
