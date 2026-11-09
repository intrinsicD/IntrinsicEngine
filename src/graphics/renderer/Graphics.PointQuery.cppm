// Shared point-query strategy and bounded exhaustive-query admission, without device dependencies.
module;
#include <cstdint>
#include <string_view>
export module Extrinsic.Graphics.PointQuery;

export namespace Extrinsic::Graphics
{
    // Explicit strategy; callers choose and report it. Lbvh builds and traverses the hierarchy,
    // BruteForce scans resident samples without one. No automatic selection: the default stays Lbvh
    // until a measured comparison supports a threshold.
    enum class PointQueryStrategy : std::uint8_t
    {
        Lbvh,
        BruteForce
    };
    // Brute-force admission, shared by the recorder, the cache and every consumer. Work counts one
    // distance plus `capacity` insert comparisons (retained slots: nearest 1, kNN k, radius capacity)
    // per (query, target) pair. Engineering limits, not measured timings: one dispatch stays within
    // K-Means' 2^24 submission pairs, and one invocation's serial scan within 2^20 (the LBVH point
    // limit), so a 1024-slot radius query cannot scan more than 1023 targets.
    // shortcut: bounded exhaustive queries; add query paging or target tiling when larger work is needed.
    inline constexpr std::uint64_t PointBruteForceDispatchWork = std::uint64_t{1} << 24u;
    inline constexpr std::uint64_t PointBruteForceInvocationWork = std::uint64_t{1} << 20u;
    // Empty when admitted, else the refusal diagnostic. Overflow-free for 32-bit counts.
    [[nodiscard]] constexpr std::string_view PointBruteForceRefusal(
        std::uint32_t queries, std::uint32_t targets, std::uint32_t capacity) noexcept
    {
        const auto serial = std::uint64_t(targets) * (std::uint64_t(capacity) + 1u);
        if (serial > PointBruteForceInvocationWork)
            return "Brute-force query exceeds its engineering limit: targets x (retained slots + 1) must be at "
                   "most 2^20 per query; use LBVH or fewer targets/slots.";
        if (std::uint64_t(queries) * serial > PointBruteForceDispatchWork)
            return "Brute-force query exceeds its engineering limit: queries x targets x (retained slots + 1) "
                   "must be at most 2^24 per dispatch; use LBVH or fewer queries per batch.";
        return {};
    }
}
