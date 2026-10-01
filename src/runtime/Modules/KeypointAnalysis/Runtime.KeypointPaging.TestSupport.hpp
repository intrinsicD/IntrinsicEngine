// Internal traversal budgets captured at admission; tests force real multi-page work.
#pragma once
#include <cstdint>
namespace Extrinsic::Runtime
{
    struct KeypointPagingLimits
    {
        std::uint32_t Rows{16384}, Visits{1024}, Pairs{1u << 24u};
    };
    inline KeypointPagingLimits KeypointPagingForTesting{};
}
