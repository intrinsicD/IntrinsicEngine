#pragma once

#include <cstdint>

// Reusable counter-stability helper for gpu;vulkan fixtures that need the same
// "fallback counters do not increment across an operational frame" assertion.
// Header-only and engine-free; callers populate the snapshot
// from `Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot()` or any
// equivalent backend source.

namespace Extrinsic::Tests::Support::OperationalCounterStability
{
    struct Snapshot
    {
        std::uint64_t FallbackToNull         = 0;
        std::uint64_t InitFailure            = 0;
        std::uint64_t ValidationError        = 0;
        std::uint64_t OperationalGateFailure = 0;
    };

    [[nodiscard]] constexpr bool IsStable(const Snapshot& before, const Snapshot& after) noexcept
    {
        return before.FallbackToNull         == after.FallbackToNull
            && before.InitFailure            == after.InitFailure
            && before.ValidationError        == after.ValidationError
            && before.OperationalGateFailure == after.OperationalGateFailure;
    }
}
