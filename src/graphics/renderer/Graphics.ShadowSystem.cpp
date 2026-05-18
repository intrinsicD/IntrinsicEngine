module;

#include <memory>
#include <algorithm>

#include <glm/glm.hpp>

module Extrinsic.Graphics.ShadowSystem;

import Extrinsic.RHI.Descriptors;

namespace Extrinsic::Graphics
{
    struct ShadowSystem::Impl
    {
        ShadowParams Params{};
        ShadowCascadeData Cascades{};
        ShadowDiagnostics Diagnostics{};
        bool Initialized{false};

        // GRAPHICS-073 Slice B — managers/leases for the depth-only shadow
        // atlas + `sampler2DShadow`-bindable sampler. The managers are kept as
        // raw pointers because their lifetime is owned by the renderer; the
        // leases own their slots and are reset on `Shutdown()`.
        RHI::TextureManager* TextureMgr{nullptr};
        RHI::SamplerManager* SamplerMgr{nullptr};
        RHI::TextureManager::TextureLease AtlasLease{};
        RHI::SamplerManager::SamplerLease SamplerLease{};
        ShadowAtlasDesc AllocatedAtlas{};
    };

    namespace
    {
        [[nodiscard]] std::uint32_t ClampCascadeCount(std::uint32_t requested) noexcept
        {
            return std::min(requested, RHI::kMaxShadowCascades);
        }

        [[nodiscard]] RHI::SamplerDesc BuildShadowAtlasSamplerDesc() noexcept
        {
            // GRAPHICS-073 Slice B / GRAPHICS-009Q — `sampler2DShadow` requires
            // `CompareEnable=true`. `ClampToBorder` + `OpaqueWhiteFloat` keeps
            // out-of-atlas samples in "fully lit" so missing or off-screen
            // cascades do not produce self-shadow artifacts. PCF radius / bias
            // policy is published through `CameraUBO::ShadowBiasAndFilter`, not
            // baked into the sampler.
            return RHI::SamplerDesc{
                .MagFilter = RHI::FilterMode::Linear,
                .MinFilter = RHI::FilterMode::Linear,
                .MipFilter = RHI::MipmapMode::Nearest,
                .AddressU  = RHI::AddressMode::ClampToBorder,
                .AddressV  = RHI::AddressMode::ClampToBorder,
                .AddressW  = RHI::AddressMode::ClampToBorder,
                .BorderColor = RHI::SamplerBorderColor::OpaqueWhiteFloat,
                .CompareEnable = true,
                .Compare = RHI::CompareOp::Less,
                .DebugName = "ShadowAtlasSampler",
            };
        }

    }

    // GRAPHICS-073 Slice B — atlas + sampler allocator. Declared as a private
    // static member of `ShadowSystem` (see header) so it can name the private
    // `Impl` without `friend`. Idempotent: returns `true` when the atlas is
    // already allocated; allocates lazily otherwise.
    bool ShadowSystem::TryAllocateAtlas(ShadowSystem::Impl& impl) noexcept
    {
        if (!impl.Initialized || impl.TextureMgr == nullptr || impl.SamplerMgr == nullptr)
        {
            return false;
        }
        if (impl.AtlasLease.GetHandle().IsValid())
        {
            return true; // GRAPHICS-073 Slice B keeps the atlas byte-identical across params updates.
        }

        const std::uint32_t cascadeCount = ClampCascadeCount(impl.Params.CascadeCount);
        if (!impl.Params.Enabled || cascadeCount == 0u || impl.Params.AtlasResolution == 0u)
        {
            return false;
        }

        if (!impl.SamplerLease.GetHandle().IsValid())
        {
            auto samplerOr = impl.SamplerMgr->GetOrCreate(BuildShadowAtlasSamplerDesc());
            if (!samplerOr.has_value())
            {
                return false;
            }
            impl.SamplerLease = std::move(*samplerOr);
        }

        const ShadowAtlasDesc desc{
            .Enabled = true,
            .Width = impl.Params.AtlasResolution * cascadeCount,
            .Height = impl.Params.AtlasResolution,
            .CascadeCount = cascadeCount,
            .Resolution = impl.Params.AtlasResolution,
        };

        const RHI::TextureDesc atlasDesc{
            .Width = desc.Width,
            .Height = desc.Height,
            .Fmt = RHI::Format::D32_FLOAT,
            .Usage = RHI::TextureUsage::DepthTarget | RHI::TextureUsage::Sampled,
            .DebugName = "ShadowAtlas",
        };

        auto leaseOr = impl.TextureMgr->Create(atlasDesc, impl.SamplerLease.GetHandle());
        if (!leaseOr.has_value())
        {
            return false;
        }
        impl.AtlasLease = std::move(*leaseOr);
        impl.AllocatedAtlas = desc;
        return true;
    }

    ShadowSystem::ShadowSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    ShadowSystem::~ShadowSystem() = default;

    void ShadowSystem::Initialize(RHI::IDevice& device,
                                  RHI::TextureManager& textureMgr,
                                  RHI::SamplerManager& samplerMgr)
    {
        m_Impl->Initialized = true;
        m_Impl->TextureMgr = &textureMgr;
        m_Impl->SamplerMgr = &samplerMgr;
        // GRAPHICS-073 Slice B — only allocate the atlas eagerly when the
        // device is operational and the caller pre-populated params with
        // shadows enabled. Operational `Initialize()` typically runs with
        // shadows disabled; the lazy path inside `SetParams` covers the more
        // common "enable shadows later" flow.
        if (device.IsOperational())
        {
            (void)TryAllocateAtlas(*m_Impl);
        }
    }

    void ShadowSystem::Shutdown()
    {
        m_Impl->AtlasLease = {};
        m_Impl->SamplerLease = {};
        m_Impl->AllocatedAtlas = {};
        m_Impl->TextureMgr = nullptr;
        m_Impl->SamplerMgr = nullptr;
        m_Impl->Params = {};
        m_Impl->Cascades = {};
        m_Impl->Diagnostics = {};
        m_Impl->Initialized = false;
    }

    void ShadowSystem::SetParams(const ShadowParams& params) noexcept
    {
        m_Impl->Params = params;
        if (params.CascadeCount > RHI::kMaxShadowCascades)
        {
            ++m_Impl->Diagnostics.UnsupportedCascadeCount;
            m_Impl->Params.CascadeCount = RHI::kMaxShadowCascades;
        }
        if (!m_Impl->Params.Enabled || m_Impl->Params.CascadeCount == 0u || m_Impl->Params.AtlasResolution == 0u)
        {
            ++m_Impl->Diagnostics.DisabledShadowStateCount;
        }
        // GRAPHICS-073 Slice B — lazy atlas allocation. The byte-identical
        // contract requires that subsequent SetParams calls do not replace an
        // already-allocated atlas, so `TryAllocateAtlas` is a no-op
        // when the lease is already live. Atlas resizes therefore have to
        // route through `Shutdown()` + `Initialize(...)` today; an explicit
        // `Resize(...)` seam stays a GRAPHICS-072 follow-up.
        (void)TryAllocateAtlas(*m_Impl);
    }

    ShadowParams ShadowSystem::GetParams() const noexcept
    {
        return m_Impl->Params;
    }

    void ShadowSystem::SetCascadeData(const ShadowCascadeData& cascades) noexcept
    {
        m_Impl->Cascades = cascades;
        if (cascades.CascadeCount > RHI::kMaxShadowCascades)
        {
            ++m_Impl->Diagnostics.UnsupportedCascadeCount;
            m_Impl->Cascades.CascadeCount = RHI::kMaxShadowCascades;
        }
    }

    ShadowCascadeData ShadowSystem::GetCascadeData() const noexcept
    {
        return m_Impl->Cascades;
    }

    ShadowAtlasDesc ShadowSystem::BuildAtlasDesc() const noexcept
    {
        const std::uint32_t cascadeCount = ClampCascadeCount(m_Impl->Params.CascadeCount);
        if (!m_Impl->Params.Enabled || cascadeCount == 0u || m_Impl->Params.AtlasResolution == 0u)
        {
            return {};
        }

        return ShadowAtlasDesc{
            .Enabled = true,
            .Width = m_Impl->Params.AtlasResolution * cascadeCount,
            .Height = m_Impl->Params.AtlasResolution,
            .CascadeCount = cascadeCount,
            .Resolution = m_Impl->Params.AtlasResolution,
        };
    }

    void ShadowSystem::ApplyTo(RHI::CameraUBO& camera) const noexcept
    {
        const ShadowAtlasDesc atlas = BuildAtlasDesc();
        const std::uint32_t cascadeCount = std::min(ClampCascadeCount(m_Impl->Cascades.CascadeCount), atlas.CascadeCount);
        for (std::uint32_t i = 0; i < RHI::kMaxShadowCascades; ++i)
        {
            camera.ShadowCascadeMatrices[i] = m_Impl->Cascades.ViewProj[i];
        }
        camera.ShadowCascadeSplitsAndCount = glm::vec4(
            m_Impl->Cascades.Splits[0],
            m_Impl->Cascades.Splits[1],
            m_Impl->Cascades.Splits[2],
            static_cast<float>(cascadeCount));
        camera.ShadowBiasAndFilter = glm::vec4(
            m_Impl->Params.DepthBias,
            m_Impl->Params.NormalBias,
            m_Impl->Params.PcfRadius,
            m_Impl->Params.SplitLambda);
        camera.ShadowAtlasSizeAndFlags = glm::vec4(
            static_cast<float>(atlas.Width),
            static_cast<float>(atlas.Height),
            atlas.Enabled ? 1.f : 0.f,
            0.f);
    }

    ShadowDiagnostics ShadowSystem::GetDiagnostics() const noexcept
    {
        return m_Impl->Diagnostics;
    }

    bool ShadowSystem::IsEnabled() const noexcept
    {
        return m_Impl->Initialized && BuildAtlasDesc().Enabled;
    }

    bool ShadowSystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }

    RHI::TextureHandle ShadowSystem::GetAtlasTexture() const noexcept
    {
        return m_Impl->AtlasLease.GetHandle();
    }

    RHI::SamplerHandle ShadowSystem::GetAtlasSampler() const noexcept
    {
        return m_Impl->SamplerLease.GetHandle();
    }

    RHI::BindlessIndex ShadowSystem::GetAtlasBindlessIndex() const noexcept
    {
        if (!m_Impl->TextureMgr || !m_Impl->AtlasLease.GetHandle().IsValid())
        {
            return RHI::kInvalidBindlessIndex;
        }
        return m_Impl->TextureMgr->GetBindlessIndex(m_Impl->AtlasLease.GetHandle());
    }

    ShadowAtlasDesc ShadowSystem::GetAllocatedAtlasDesc() const noexcept
    {
        return m_Impl->AllocatedAtlas;
    }

    void ShadowSystem::RecordMissingCaster() noexcept
    {
        ++m_Impl->Diagnostics.MissingCasterCount;
    }
}
