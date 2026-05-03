module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

module Extrinsic.Graphics.PostProcessSystem;

namespace Extrinsic::Graphics
{
    namespace
    {
        constexpr std::uint32_t kMinHistogramBins = 16u;
        constexpr std::uint32_t kMaxHistogramBins = 4096u;

        [[nodiscard]] bool IsSupportedAA(const PostProcessAntiAliasing aa) noexcept
        {
            switch (aa)
            {
            case PostProcessAntiAliasing::None:
            case PostProcessAntiAliasing::FXAA:
            case PostProcessAntiAliasing::SMAA:
                return true;
            }
            return false;
        }

        [[nodiscard]] const char* StageName(const PostProcessStageKind stage) noexcept
        {
            switch (stage)
            {
            case PostProcessStageKind::Histogram:
                return "Histogram";
            case PostProcessStageKind::Bloom:
                return "Bloom";
            case PostProcessStageKind::ToneMap:
                return "ToneMap";
            case PostProcessStageKind::FXAA:
                return "FXAA";
            case PostProcessStageKind::SMAA:
                return "SMAA";
            }
            return "Unknown";
        }
    }

    struct PostProcessSystem::Impl
    {
        bool Initialized{false};
        PostProcessSettings Settings{};
        PostProcessDiagnostics Diagnostics{};
    };

    PostProcessSystem::PostProcessSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    PostProcessSystem::~PostProcessSystem() = default;

    void PostProcessSystem::Initialize()
    {
        m_Impl->Initialized = true;
    }

    void PostProcessSystem::Shutdown()
    {
        m_Impl->Initialized = false;
    }

    void PostProcessSystem::SetSettings(const PostProcessSettings& settings)
    {
        m_Impl->Settings = settings;
        m_Impl->Diagnostics = {};

        if (!std::isfinite(m_Impl->Settings.Exposure) || m_Impl->Settings.Exposure <= 0.0f)
        {
            m_Impl->Settings.Exposure = 1.0f;
            ++m_Impl->Diagnostics.InvalidSettingCount;
        }
        if (!std::isfinite(m_Impl->Settings.Gamma) || m_Impl->Settings.Gamma <= 0.0f)
        {
            m_Impl->Settings.Gamma = 2.2f;
            ++m_Impl->Diagnostics.InvalidSettingCount;
        }
        if (!std::isfinite(m_Impl->Settings.BloomIntensity) || m_Impl->Settings.BloomIntensity < 0.0f)
        {
            m_Impl->Settings.BloomIntensity = 0.05f;
            ++m_Impl->Diagnostics.InvalidSettingCount;
        }
        if (m_Impl->Settings.HistogramBinCount == 0u)
        {
            m_Impl->Settings.HistogramBinCount = 256u;
            ++m_Impl->Diagnostics.InvalidSettingCount;
        }
        else
        {
            m_Impl->Settings.HistogramBinCount = std::clamp(m_Impl->Settings.HistogramBinCount,
                                                            kMinHistogramBins,
                                                            kMaxHistogramBins);
        }

        if (!IsSupportedAA(m_Impl->Settings.AntiAliasing))
        {
            m_Impl->Settings.AntiAliasing = PostProcessAntiAliasing::None;
            ++m_Impl->Diagnostics.UnsupportedCombinationCount;
        }

        m_Impl->Diagnostics.ChainEnabled = m_Impl->Settings.Enabled;
        m_Impl->Diagnostics.WritesLDR = m_Impl->Settings.Enabled;
    }

    bool PostProcessSystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }

    const PostProcessSettings& PostProcessSystem::GetSettings() const noexcept
    {
        return m_Impl->Settings;
    }

    PostProcessDiagnostics PostProcessSystem::GetDiagnostics() const noexcept
    {
        PostProcessDiagnostics diagnostics = m_Impl->Diagnostics;
        diagnostics.ChainEnabled = m_Impl->Settings.Enabled;
        diagnostics.WritesLDR = m_Impl->Settings.Enabled;
        return diagnostics;
    }

    PostProcessChainDesc PostProcessSystem::DescribeChain() const
    {
        PostProcessChainDesc desc{};
        desc.Enabled = m_Impl->Settings.Enabled;
        desc.WritesLDR = m_Impl->Settings.Enabled;
        desc.Diagnostics = GetDiagnostics();

        if (!m_Impl->Settings.Enabled)
        {
            desc.Stages.clear();
            return desc;
        }

        auto addStage = [&desc](const PostProcessStageKind kind,
                                const bool readsHDR,
                                const bool writesLDR,
                                const bool usesIntermediate) {
            desc.Stages.push_back(PostProcessStageDesc{
                .Kind = kind,
                .Name = StageName(kind),
                .ReadsHDR = readsHDR,
                .WritesLDR = writesLDR,
                .UsesIntermediate = usesIntermediate,
            });
        };

        if (m_Impl->Settings.EnableHistogram)
        {
            addStage(PostProcessStageKind::Histogram, true, false, false);
        }
        if (m_Impl->Settings.EnableBloom)
        {
            addStage(PostProcessStageKind::Bloom, true, false, true);
        }

        addStage(PostProcessStageKind::ToneMap, true, true, m_Impl->Settings.EnableBloom);

        switch (m_Impl->Settings.AntiAliasing)
        {
        case PostProcessAntiAliasing::FXAA:
            addStage(PostProcessStageKind::FXAA, false, true, true);
            break;
        case PostProcessAntiAliasing::SMAA:
            addStage(PostProcessStageKind::SMAA, false, true, true);
            break;
        case PostProcessAntiAliasing::None:
            break;
        }

        return desc;
    }

    bool PostProcessSystem::IsStageEnabled(const PostProcessStageKind stage) const
    {
        const PostProcessChainDesc desc = DescribeChain();
        return std::ranges::any_of(desc.Stages, [stage](const PostProcessStageDesc& candidate) {
            return candidate.Kind == stage;
        });
    }

    PostProcessPushConstants PostProcessSystem::BuildPushConstants(const PostProcessStageKind stage) const noexcept
    {
        return PostProcessPushConstants{
            .Exposure = m_Impl->Settings.Exposure,
            .Gamma = m_Impl->Settings.Gamma,
            .BloomIntensity = m_Impl->Settings.BloomIntensity,
            .HistogramBinCount = m_Impl->Settings.HistogramBinCount,
            .StageKind = static_cast<std::uint32_t>(stage),
        };
    }
}
