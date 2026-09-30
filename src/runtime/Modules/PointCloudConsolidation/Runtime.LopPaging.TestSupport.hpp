// Internal LOP work budgets, overridable by tests before starting a run.
#pragma once
#include <cstdint>

namespace Extrinsic::Runtime
{
    struct LopPagingLimits
    {
        std::uint32_t PagePairs{1u << 18u};
        // At most 2^24 candidate visits per submission, split into smaller dispatches
        // to bound watchdog exposure without imposing a point-count admission limit.
        std::uint32_t SubmissionPairs{1u << 24u};
        std::uint32_t ReduceRows{4096u};
    };
    inline LopPagingLimits LopPagingForTesting{};
}
