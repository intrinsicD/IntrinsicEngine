// Internal pair budgets copied at admission; tests can force multi-page submissions.
#pragma once
#include <cstdint>
namespace Extrinsic::Runtime
{
    // Count lane scans, six tree levels and the ordered page combine.
    inline constexpr std::uint32_t KMeansSubmissionSerialDepth = 1u << 14u;
    inline constexpr std::uint32_t KMeansPageScanRows = 1u << 12u;
    struct KMeansPagingLimits
    {
        std::uint32_t PagePairs{1u << 18u};
        std::uint32_t SubmissionPairs{1u << 24u};
    };
    inline KMeansPagingLimits KMeansPagingForTesting{};
}
