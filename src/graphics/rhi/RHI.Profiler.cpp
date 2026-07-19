module;

#include <cmath>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string_view>

module Extrinsic.RHI.Profiler;

namespace Extrinsic::RHI
{
    std::string_view ProfilerErrorName(const ProfilerError error) noexcept
    {
        switch (error)
        {
        case ProfilerError::NotReady: return "NotReady";
        case ProfilerError::DeviceLost: return "DeviceLost";
        case ProfilerError::InvalidState: return "InvalidState";
        case ProfilerError::InvalidArgument: return "InvalidArgument";
        case ProfilerError::Unsupported: return "Unsupported";
        case ProfilerError::Exhausted: return "Exhausted";
        case ProfilerError::Overflow: return "Overflow";
        }
        return "Unknown";
    }

    std::optional<std::uint64_t>
    GpuTimestampFrame::FindScopeDurationNs(const std::string_view name) const
    {
        for (const GpuTimestampScope& scope : Scopes)
        {
            if (scope.Name == name)
            {
                return scope.DurationNs;
            }
        }
        return std::nullopt;
    }

    bool TimestampQueryRange::Contains(
        const std::uint32_t absoluteIndex) const noexcept
    {
        return absoluteIndex >= Base &&
               absoluteIndex - Base < Count;
    }

    std::expected<std::uint32_t, ProfilerError>
    TimestampQueryRange::LocalIndex(
        const std::uint32_t absoluteIndex) const noexcept
    {
        if (!Contains(absoluteIndex))
        {
            return std::unexpected(ProfilerError::InvalidArgument);
        }
        return absoluteIndex - Base;
    }

    std::expected<std::uint64_t, ProfilerError>
    ComputeTimestampDeltaTicks(const std::uint64_t beginTicks,
                               const std::uint64_t endTicks,
                               const std::uint32_t validBits) noexcept
    {
        if (validBits == 0u)
        {
            return std::unexpected(ProfilerError::Unsupported);
        }
        if (validBits > 64u)
        {
            return std::unexpected(ProfilerError::InvalidArgument);
        }
        if (validBits == 64u)
        {
            return endTicks - beginTicks;
        }

        const std::uint64_t mask =
            (std::uint64_t{1} << validBits) - std::uint64_t{1};
        return ((endTicks & mask) - (beginTicks & mask)) & mask;
    }

    std::expected<std::uint64_t, ProfilerError>
    ResolveTimestampDurationNs(const TimestampQueryValue begin,
                               const TimestampQueryValue end,
                               const std::uint32_t validBits,
                               const double timestampPeriodNs,
                               const std::uint64_t intervalUpperBoundNs) noexcept
    {
        if (!begin.Available || !end.Available)
        {
            return std::unexpected(ProfilerError::NotReady);
        }
        if (!std::isfinite(timestampPeriodNs) || timestampPeriodNs <= 0.0)
        {
            return std::unexpected(ProfilerError::InvalidArgument);
        }

        const std::expected<std::uint64_t, ProfilerError> ticks =
            ComputeTimestampDeltaTicks(begin.Ticks, end.Ticks, validBits);
        if (!ticks)
        {
            return std::unexpected(ticks.error());
        }

        const long double counterPeriodNs =
            std::ldexp(1.0L, static_cast<int>(validBits)) *
            static_cast<long double>(timestampPeriodNs);
        if (!std::isfinite(counterPeriodNs) ||
            static_cast<long double>(intervalUpperBoundNs) >=
                counterPeriodNs)
        {
            // The modular tick delta cannot distinguish one wrap from two or
            // more. The host-clock envelope is only an ambiguity guard; it is
            // never substituted for the native GPU duration.
            return std::unexpected(ProfilerError::Overflow);
        }

        const long double durationNs =
            static_cast<long double>(*ticks) *
            static_cast<long double>(timestampPeriodNs);
        if (!std::isfinite(durationNs) ||
            durationNs >
                static_cast<long double>(
                    std::numeric_limits<std::uint64_t>::max()))
        {
            return std::unexpected(ProfilerError::Overflow);
        }
        return static_cast<std::uint64_t>(durationNs);
    }
}
