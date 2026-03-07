module;

#include <array>
#include <cstdint>
#include <cmath>

export module Graphics:Colormap;

import :GpuColor;

// =============================================================================
// Scientific colormaps for scalar field visualization.
//
// Six colormaps covering perceptually uniform, rainbow, diverging, and legacy:
//   Viridis  — perceptually uniform, colorblind-safe (matplotlib default)
//   Inferno  — perceptually uniform, high-contrast dark-to-bright
//   Plasma   — perceptually uniform, purple-to-yellow
//   Jet      — classic rainbow (blue-cyan-green-yellow-red)
//   Coolwarm — diverging blue-to-red (Moreland 2009)
//   Heat     — legacy blue-cyan-green-yellow-red ramp
//
// All colormaps are 256-entry LUTs for O(1) lookup. Input t is clamped to [0,1].
// Output is packed ABGR (same convention as GpuColor::PackColorF).
// =============================================================================

export namespace Graphics::Colormap
{
    enum class Type : uint8_t
    {
        Viridis = 0,
        Inferno,
        Plasma,
        Jet,
        Coolwarm,
        Heat,
    };

    constexpr uint32_t Count = 6;

    /// Map t ∈ [0,1] → packed ABGR via the selected colormap.
    [[nodiscard]] uint32_t Sample(Type map, float t) noexcept;

    /// Map t ∈ [0,1] → packed ABGR with quantization (binning).
    /// bins=0 means continuous (no binning).
    [[nodiscard]] uint32_t SampleBinned(Type map, float t, uint32_t bins) noexcept;

    /// Human-readable name for UI display.
    [[nodiscard]] const char* Name(Type map) noexcept;
}
