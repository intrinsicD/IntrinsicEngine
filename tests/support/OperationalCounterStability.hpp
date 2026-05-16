#pragma once

#include <cstdint>

// Reusable counter-stability helper for the GRAPHICS-033D MinimalDebug
// visible-triangle smoke (and any sibling gpu;vulkan fixture that needs the
// same "fallback counters do not increment across an operational frame"
// assertion). Header-only and engine-free; callers populate the snapshot
// from `Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot()` or any
// equivalent backend source.
//
// GRAPHICS-033D scaffold notice requires this assertion to be authored as a
// reusable helper so the canonical GRAPHICS-076/081 default-recipe smoke can
// call it byte-identical.

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
