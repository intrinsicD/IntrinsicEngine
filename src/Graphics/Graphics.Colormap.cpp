module;

#include <array>
#include <cstdint>
#include <cmath>
#include <algorithm>

module Graphics.Colormap;

import Graphics.GpuColor;

using namespace Graphics;

// =============================================================================
// Colormap LUTs — 256 entries each, stored as RGB float triples.
// Generated from matplotlib / Moreland reference data.
// =============================================================================

namespace
{
    struct RGB { float r, g, b; };

    // Piecewise-linear interpolation into an N-entry control-point table.
    template <std::size_t N>
    [[nodiscard]] constexpr RGB Lerp(const std::array<RGB, N>& table, float t) noexcept
    {
        t = std::clamp(t, 0.0f, 1.0f);
        const float x = t * static_cast<float>(N - 1);
        const auto i0 = static_cast<std::size_t>(x);
        const auto i1 = std::min(i0 + 1, N - 1);
        const float frac = x - static_cast<float>(i0);
        return {
            table[i0].r + (table[i1].r - table[i0].r) * frac,
            table[i0].g + (table[i1].g - table[i0].g) * frac,
            table[i0].b + (table[i1].b - table[i0].b) * frac,
        };
    }

    // --- Viridis: 9 control points (matplotlib reference) ---
    constexpr std::array<RGB, 9> kViridis = {{
        {0.267004f, 0.004874f, 0.329415f},
        {0.282327f, 0.140926f, 0.457517f},
        {0.253935f, 0.265254f, 0.529983f},
        {0.206756f, 0.371758f, 0.553117f},
        {0.163625f, 0.471133f, 0.558148f},
        {0.127568f, 0.566949f, 0.550556f},
        {0.134692f, 0.658636f, 0.517649f},
        {0.477504f, 0.821444f, 0.318195f},
        {0.993248f, 0.906157f, 0.143936f},
    }};

    // --- Inferno: 9 control points (matplotlib reference) ---
    constexpr std::array<RGB, 9> kInferno = {{
        {0.001462f, 0.000466f, 0.013866f},
        {0.087411f, 0.044556f, 0.224813f},
        {0.258234f, 0.038571f, 0.406485f},
        {0.416331f, 0.090834f, 0.432943f},
        {0.578304f, 0.148039f, 0.404411f},
        {0.735683f, 0.215906f, 0.330245f},
        {0.865006f, 0.316822f, 0.226055f},
        {0.954506f, 0.468744f, 0.099874f},
        {0.988362f, 0.998364f, 0.644924f},
    }};

    // --- Plasma: 9 control points (matplotlib reference) ---
    constexpr std::array<RGB, 9> kPlasma = {{
        {0.050383f, 0.029803f, 0.527975f},
        {0.254627f, 0.013882f, 0.615419f},
        {0.417642f, 0.000564f, 0.658390f},
        {0.562738f, 0.051545f, 0.641509f},
        {0.692840f, 0.165141f, 0.564522f},
        {0.798216f, 0.280197f, 0.469538f},
        {0.881443f, 0.392529f, 0.383229f},
        {0.949217f, 0.517763f, 0.295662f},
        {0.940015f, 0.975158f, 0.131326f},
    }};

    // --- Jet: 9 control points (classic rainbow) ---
    constexpr std::array<RGB, 9> kJet = {{
        {0.000f, 0.000f, 0.500f},
        {0.000f, 0.000f, 1.000f},
        {0.000f, 0.500f, 1.000f},
        {0.000f, 1.000f, 1.000f},
        {0.500f, 1.000f, 0.500f},
        {1.000f, 1.000f, 0.000f},
        {1.000f, 0.500f, 0.000f},
        {1.000f, 0.000f, 0.000f},
        {0.500f, 0.000f, 0.000f},
    }};

    // --- Coolwarm: 9 control points (Moreland 2009 diverging) ---
    constexpr std::array<RGB, 9> kCoolwarm = {{
        {0.230f, 0.299f, 0.754f},
        {0.347f, 0.460f, 0.874f},
        {0.490f, 0.621f, 0.957f},
        {0.640f, 0.762f, 0.990f},
        {0.865f, 0.865f, 0.865f},
        {0.958f, 0.714f, 0.600f},
        {0.905f, 0.498f, 0.380f},
        {0.796f, 0.261f, 0.195f},
        {0.706f, 0.016f, 0.150f},
    }};

    [[nodiscard]] RGB SampleRGB(Colormap::Type map, float t) noexcept
    {
        switch (map)
        {
        case Colormap::Type::Viridis:  return Lerp(kViridis, t);
        case Colormap::Type::Inferno:  return Lerp(kInferno, t);
        case Colormap::Type::Plasma:   return Lerp(kPlasma, t);
        case Colormap::Type::Jet:      return Lerp(kJet, t);
        case Colormap::Type::Coolwarm: return Lerp(kCoolwarm, t);
        case Colormap::Type::Heat:
        {
            // Delegate to existing heat ramp logic (piecewise-linear).
            t = std::clamp(t, 0.0f, 1.0f);
            float r = 0.0f, g = 0.0f, b = 0.0f;
            if (t < 0.25f)      { b = 1.0f; g = t * 4.0f; }
            else if (t < 0.5f)  { g = 1.0f; b = 1.0f - (t - 0.25f) * 4.0f; }
            else if (t < 0.75f) { g = 1.0f; r = (t - 0.5f) * 4.0f; }
            else                { r = 1.0f; g = 1.0f - (t - 0.75f) * 4.0f; }
            return {r, g, b};
        }
        }
        return {1.0f, 1.0f, 1.0f};
    }
} // namespace

uint32_t Colormap::Sample(Type map, float t) noexcept
{
    const auto [r, g, b] = SampleRGB(map, t);
    return GpuColor::PackColorF(r, g, b, 1.0f);
}

uint32_t Colormap::SampleBinned(Type map, float t, uint32_t bins) noexcept
{
    if (bins > 0)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        t = std::floor(t * static_cast<float>(bins)) / static_cast<float>(bins);
        // Center each bin: shift by half-bin width.
        t += 0.5f / static_cast<float>(bins);
        t = std::clamp(t, 0.0f, 1.0f);
    }
    return Sample(map, t);
}

const char* Colormap::Name(Type map) noexcept
{
    switch (map)
    {
    case Type::Viridis:  return "Viridis";
    case Type::Inferno:  return "Inferno";
    case Type::Plasma:   return "Plasma";
    case Type::Jet:      return "Jet";
    case Type::Coolwarm: return "Coolwarm";
    case Type::Heat:     return "Heat";
    }
    return "Unknown";
}
