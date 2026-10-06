// Core.IntegerMath: small constexpr integer helpers shared across layers
// (dispatch/group-count arithmetic for graphics and runtime GPU paths).
module;

#include <cstdint>

export module Extrinsic.Core.IntegerMath;

export namespace Extrinsic::Core
{
    // ceil(value / divisor) in uint32_t arithmetic. A zero divisor yields 0.
    // value + divisor - 1 wraps for values near UINT32_MAX; callers size
    // dispatches well below that range.
    [[nodiscard]] constexpr std::uint32_t CeilDiv(const std::uint32_t value,
                                                  const std::uint32_t divisor) noexcept
    {
        return divisor == 0u ? 0u : (value + divisor - 1u) / divisor;
    }
}
