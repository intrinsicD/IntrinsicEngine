#pragma once

#include <cstdint>
#include <cstdlib>

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

    // BUG-255: turn on Khronos synchronization validation for a validation smoke, so its
    // validation-error count also covers synchronization hazards. The layer reads the setting
    // when the Vulkan instance is created, so call this before bootstrapping the engine; ctest
    // runs every case in its own process, so it does not reach other cases.
    inline void EnableSynchronizationValidation()
    {
        constexpr const char* kName = "VK_LAYER_ENABLES";
        constexpr const char* kValue = "VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT";
#if defined(_WIN32)
        (void)::_putenv_s(kName, kValue);
#else
        (void)::setenv(kName, kValue, 1);
#endif
    }
}
