module;

#include <cstdint>

export module Extrinsic.Graphics.Colormap;

export namespace Extrinsic::Graphics::Colormap
{
    enum class Type : uint8_t
    {
        Viridis  = 0,
        Inferno,
        Plasma,
        Jet,
        Coolwarm,
        Heat,
        Count    ///< Sentinel — always last. Do not use as an actual type value.
    };

    /// Number of distinct colormap types (excludes Count sentinel).
    inline constexpr uint8_t kColormapCount = static_cast<uint8_t>(Type::Count);
}
