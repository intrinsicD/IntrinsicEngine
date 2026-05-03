module;

#include <memory>
#include <algorithm>

#include <glm/glm.hpp>

module Extrinsic.Graphics.ShadowSystem;

namespace Extrinsic::Graphics
{
    struct ShadowSystem::Impl
    {
        ShadowParams Params{};
        ShadowCascadeData Cascades{};
        ShadowDiagnostics Diagnostics{};
        bool Initialized{false};
    };

    namespace
    {
        [[nodiscard]] std::uint32_t ClampCascadeCount(std::uint32_t requested) noexcept
        {
            return std::min(requested, RHI::kMaxShadowCascades);
        }
    }

    ShadowSystem::ShadowSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    ShadowSystem::~ShadowSystem() = default;

    void ShadowSystem::Initialize()
    {
        m_Impl->Initialized = true;
    }

    void ShadowSystem::Shutdown()
    {
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
}
