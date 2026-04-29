module;

#include <array>
#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <memory>

module Extrinsic.Graphics.ColormapSystem;

import Extrinsic.Graphics.Colormap;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.Device;

// ============================================================
// ColormapSystem — implementation
// ============================================================
// Each colourmap is stored as a table of (t, R, G, B) control
// points.  At Initialize() the system linearly interpolates to
// produce a 256-sample LUT stored as RGBA8_UNORM.
//
// Colourmap data sourced from matplotlib tabulated values.
// ============================================================

namespace Extrinsic::Graphics
{
    // ----------------------------------------------------------------
    // Control-point tables
    // ----------------------------------------------------------------
    struct CtrlPt { float T; std::uint8_t R, G, B; };

    static constexpr std::array<CtrlPt, 9> kViridis {{
        { 0.000f,  68,   1,  84 },
        { 0.125f,  71,  44, 122 },
        { 0.250f,  59,  81, 139 },
        { 0.375f,  44, 114, 142 },
        { 0.500f,  33, 145, 140 },
        { 0.625f,  54, 175, 126 },
        { 0.750f, 107, 202,  92 },
        { 0.875f, 178, 224,  44 },
        { 1.000f, 253, 231,  37 },
    }};

    static constexpr std::array<CtrlPt, 9> kInferno {{
        { 0.000f,   0,   0,   4 },
        { 0.125f,  31,  12,  72 },
        { 0.250f,  85,  15, 109 },
        { 0.375f, 139,  34,  95 },
        { 0.500f, 188,  55,  84 },
        { 0.625f, 229,  83,  37 },
        { 0.750f, 245, 142,  22 },
        { 0.875f, 249, 201,  50 },
        { 1.000f, 252, 255, 164 },
    }};

    static constexpr std::array<CtrlPt, 9> kPlasma {{
        { 0.000f,  13,   8, 135 },
        { 0.125f,  75,   3, 161 },
        { 0.250f, 125,   3, 168 },
        { 0.375f, 168,  34, 150 },
        { 0.500f, 204,  70, 120 },
        { 0.625f, 229, 107,  93 },
        { 0.750f, 242, 148,  64 },
        { 0.875f, 247, 195,  43 },
        { 1.000f, 240, 249,  33 },
    }};

    // Jet: classic rainbow (blue→cyan→green→yellow→red)
    static constexpr std::array<CtrlPt, 9> kJet {{
        { 0.000f,   0,   0, 143 },
        { 0.125f,   0,   0, 255 },
        { 0.250f,   0, 127, 255 },
        { 0.375f,   0, 255, 255 },
        { 0.500f, 127, 255, 127 },
        { 0.625f, 255, 255,   0 },
        { 0.750f, 255, 127,   0 },
        { 0.875f, 255,   0,   0 },
        { 1.000f, 127,   0,   0 },
    }};

    // Coolwarm: blue→white→red (diverging)
    static constexpr std::array<CtrlPt, 9> kCoolwarm {{
        { 0.000f,  59,  76, 192 },
        { 0.125f,  98, 130, 234 },
        { 0.250f, 141, 176, 254 },
        { 0.375f, 183, 212, 234 },
        { 0.500f, 221, 221, 221 },
        { 0.625f, 230, 185, 162 },
        { 0.750f, 219, 132, 104 },
        { 0.875f, 200,  80,  64 },
        { 1.000f, 180,   4,  38 },
    }};

    // Heat: black→dark-red→orange→yellow→white
    static constexpr std::array<CtrlPt, 9> kHeat {{
        { 0.000f,   0,   0,   0 },
        { 0.125f,  32,   0,   0 },
        { 0.250f,  85,   0,   0 },
        { 0.375f, 160,  10,   0 },
        { 0.500f, 200,  60,   0 },
        { 0.625f, 230, 120,   0 },
        { 0.750f, 250, 200,  20 },
        { 0.875f, 255, 240, 100 },
        { 1.000f, 255, 255, 255 },
    }};

    // ----------------------------------------------------------------
    // LUT generation helpers
    // ----------------------------------------------------------------
    using Lut256 = std::array<std::uint8_t, 256 * 4>; // RGBA8

    template <std::size_t N>
    static Lut256 BuildLut(const std::array<CtrlPt, N>& pts)
    {
        Lut256 lut{};
        for (int i = 0; i < 256; ++i)
        {
            const float t = static_cast<float>(i) / 255.f;

            // Find bracketing control points
            std::size_t lo = 0;
            for (std::size_t j = 1; j < N; ++j)
            {
                if (pts[j].T <= t) lo = j;
                else break;
            }
            const std::size_t hi = std::min(lo + 1, N - 1);

            float alpha = 0.f;
            if (const float span = pts[hi].T - pts[lo].T; span > 1e-6f)
                alpha = (t - pts[lo].T) / span;
            alpha = std::clamp(alpha, 0.f, 1.f);

            const auto lerp8 = [&](std::uint8_t a, std::uint8_t b) -> std::uint8_t {
                return static_cast<std::uint8_t>(
                    static_cast<float>(a) + alpha * (static_cast<float>(b) - static_cast<float>(a)));
            };

            lut[4 * i + 0] = lerp8(pts[lo].R, pts[hi].R);
            lut[4 * i + 1] = lerp8(pts[lo].G, pts[hi].G);
            lut[4 * i + 2] = lerp8(pts[lo].B, pts[hi].B);
            lut[4 * i + 3] = 255u; // alpha always opaque
        }
        return lut;
    }

    // ----------------------------------------------------------------
    // Impl
    // ----------------------------------------------------------------
    struct ColormapSystem::Impl
    {
        bool Initialized = false;

        // One texture lease + bindless index per colormap type
        struct Entry
        {
            RHI::TextureManager::TextureLease Lease;
            RHI::BindlessIndex                BindlessIdx = RHI::kInvalidBindlessIndex;
            Lut256                            CpuLut{};
        };

        std::array<Entry, static_cast<std::size_t>(Colormap::kColormapCount)> Entries;

        // The shared linear sampler used by all LUT textures.
        // Must outlive all TextureLease entries.
        RHI::SamplerManager::SamplerLease SharedSampler;
    };

    // ----------------------------------------------------------------
    // ColormapSystem
    // ----------------------------------------------------------------
    ColormapSystem::ColormapSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    ColormapSystem::~ColormapSystem() = default;

    // ----------------------------------------------------------------
    void ColormapSystem::Initialize(RHI::IDevice&        device,
                                    RHI::TextureManager& textureMgr,
                                    RHI::SamplerManager& samplerMgr)
    {
        if (m_Impl->Initialized) return;
        if (!device.IsOperational()) return;

        // Build all CPU LUTs
        m_Impl->Entries[0].CpuLut = BuildLut(kViridis);
        m_Impl->Entries[1].CpuLut = BuildLut(kInferno);
        m_Impl->Entries[2].CpuLut = BuildLut(kPlasma);
        m_Impl->Entries[3].CpuLut = BuildLut(kJet);
        m_Impl->Entries[4].CpuLut = BuildLut(kCoolwarm);
        m_Impl->Entries[5].CpuLut = BuildLut(kHeat);

        // Create a linear (not sRGB) sampler for the LUT — we want exact
        // values, not gamma-corrected ones.
        const RHI::SamplerDesc samplerDesc {
            .MagFilter = RHI::FilterMode::Linear,
            .MinFilter = RHI::FilterMode::Linear,
            .AddressU  = RHI::AddressMode::ClampToEdge,
            .AddressV  = RHI::AddressMode::ClampToEdge,
            .AddressW  = RHI::AddressMode::ClampToEdge,
            .DebugName = "ColormapLutSampler",
        };
        auto samplerLeaseOr = samplerMgr.GetOrCreate(samplerDesc);
        if (!samplerLeaseOr.has_value()) return;
        m_Impl->SharedSampler = std::move(*samplerLeaseOr);
        const RHI::SamplerHandle sampler = m_Impl->SharedSampler.GetHandle();

        // Upload one 256×1 texture per colormap
        for (std::size_t i = 0; i < static_cast<std::size_t>(Colormap::kColormapCount); ++i)
        {
            auto& entry = m_Impl->Entries[i];

            const RHI::TextureDesc texDesc {
                .Width     = 256,
                .Height    = 1,
                .MipLevels = 1,
                .Fmt       = RHI::Format::RGBA8_UNORM,
                .Dimension = RHI::TextureDimension::Tex1D,
                .Usage     = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst,
                .DebugName = "ColormapLut",
            };

            auto leaseOr = textureMgr.Create(texDesc, sampler);
            if (!leaseOr.has_value()) continue;

            entry.Lease     = std::move(*leaseOr);
            entry.BindlessIdx = textureMgr.GetBindlessIndex(entry.Lease.GetHandle());

            // Upload the CPU LUT
            device.WriteTexture(entry.Lease.GetHandle(),
                                entry.CpuLut.data(),
                                entry.CpuLut.size(),
                                /*mipLevel=*/0,
                                /*arrayLayer=*/0);
        }

        m_Impl->Initialized = true;
    }

    // ----------------------------------------------------------------
    void ColormapSystem::Shutdown()
    {
        for (auto& e : m_Impl->Entries)
            e.Lease = {};
        m_Impl->SharedSampler = {};
        m_Impl->Initialized = false;
    }

    // ----------------------------------------------------------------
    bool ColormapSystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }

    // ----------------------------------------------------------------
    RHI::BindlessIndex ColormapSystem::GetBindlessIndex(Colormap::Type t) const noexcept
    {
        const auto idx = static_cast<std::size_t>(t);
        if (idx >= static_cast<std::size_t>(Colormap::kColormapCount))
            return RHI::kInvalidBindlessIndex;
        return m_Impl->Entries[idx].BindlessIdx;
    }

    // ----------------------------------------------------------------
    ColormapSystem::RGBA8 ColormapSystem::SampleCpu(Colormap::Type t,
                                                    float           scalar) const noexcept
    {
        const auto idx = static_cast<std::size_t>(t);
        if (idx >= static_cast<std::size_t>(Colormap::kColormapCount))
            return {128, 128, 128, 255};

        const float clamped = std::clamp(scalar, 0.f, 1.f);
        const int   sample  = static_cast<int>(clamped * 255.f + 0.5f);
        const auto& lut     = m_Impl->Entries[idx].CpuLut;

        return {lut[4 * sample + 0],
                lut[4 * sample + 1],
                lut[4 * sample + 2],
                lut[4 * sample + 3]};
    }

    // ----------------------------------------------------------------
    std::uint32_t ColormapSystem::PackRGBA8(RGBA8 c) noexcept
    {
        return (static_cast<std::uint32_t>(c.R) << 24)
             | (static_cast<std::uint32_t>(c.G) << 16)
             | (static_cast<std::uint32_t>(c.B) <<  8)
             | (static_cast<std::uint32_t>(c.A));
    }

    // ----------------------------------------------------------------
    std::uint32_t ColormapSystem::PackVec4(glm::vec4 c) noexcept
    {
        const auto clamp8 = [](float v) -> std::uint8_t {
            return static_cast<std::uint8_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f);
        };
        return PackRGBA8({clamp8(c.r), clamp8(c.g), clamp8(c.b), clamp8(c.a)});
    }

} // namespace Extrinsic::Graphics

