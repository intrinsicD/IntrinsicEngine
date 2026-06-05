module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

module Extrinsic.Graphics.PostProcessSystem;

import Extrinsic.Core.Logging;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

namespace Extrinsic::Graphics
{
    namespace
    {
        constexpr std::uint32_t kMinHistogramBins = 16u;
        constexpr std::uint32_t kMaxHistogramBins = 4096u;

        // -----------------------------------------------------------------
        // GRAPHICS-075 Slice D.2b — SMAA lookup-texture byte generators.
        // Ported byte-for-byte from
        // `src/legacy/Graphics/Passes/Graphics.SMAALookupTextures.hpp` so
        // promoted graphics/renderer code never imports from legacy. The
        // analytical area / search texture math follows the SMAA reference
        // (Jimenez et al. 2012, https://github.com/iryoku/smaa).
        // -----------------------------------------------------------------
        constexpr int kAreaTexMaxDist     = 16;
        constexpr int kAreaTexSubtexels   = 7;
        constexpr int kNumOrthoPatterns   = 16;
        constexpr float kSmoothMaxDist    = 32.0f;

        struct Vec2 { float x, y; };

        inline float Saturate(float v) { return std::clamp(v, 0.0f, 1.0f); }
        inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

        Vec2 AreaUnderLine(Vec2 p1, Vec2 p2, float x)
        {
            const float dx = p2.x - p1.x;
            const float dy = p2.y - p1.y;
            float x0 = x;
            float x1 = x + 1.0f;
            if (x0 < p1.x) x0 = p1.x;
            if (x1 > p2.x) x1 = p2.x;
            if (x0 >= x1) return {0.0f, 0.0f};
            const float t0 = (std::abs(dx) > 1e-9f) ? (x0 - p1.x) / dx : 0.0f;
            const float t1 = (std::abs(dx) > 1e-9f) ? (x1 - p1.x) / dx : 0.0f;
            const float y0 = p1.y + dy * t0;
            const float y1 = p1.y + dy * t1;
            const float width = x1 - x0;
            if (y0 >= 0.0f && y1 >= 0.0f)
            {
                const float area = (y0 + y1) * 0.5f * width;
                return {0.0f, area};
            }
            if (y0 <= 0.0f && y1 <= 0.0f)
            {
                const float area = -(y0 + y1) * 0.5f * width;
                return {area, 0.0f};
            }
            const float xCross = x0 + (-y0 / (y1 - y0)) * width;
            const float wLeft  = xCross - x0;
            const float wRight = x1 - xCross;
            const float areaLeft  = std::abs(y0) * wLeft * 0.5f;
            const float areaRight = std::abs(y1) * wRight * 0.5f;
            if (y0 < 0.0f) return {areaLeft, areaRight};
            return {areaRight, areaLeft};
        }

        Vec2 SmoothArea(float d, Vec2 area)
        {
            Vec2 smoothed;
            smoothed.x = std::sqrt(area.x * 2.0f) * 0.5f;
            smoothed.y = std::sqrt(area.y * 2.0f) * 0.5f;
            const float t = Saturate(d / kSmoothMaxDist);
            return {Lerp(smoothed.x, area.x, t), Lerp(smoothed.y, area.y, t)};
        }

        Vec2 AreaOrthoPattern(int pattern, float left, float right, float offset)
        {
            const float d  = left + right + 1.0f;
            const float o1 = 0.5f + offset;
            const float o2 = 0.5f + offset - 1.0f;
            Vec2 result = {0.0f, 0.0f};
            switch (pattern)
            {
            case 0: break;
            case 1:
                if (left <= right) {
                    auto a = AreaUnderLine({0.0f, o2}, {d * 0.5f, 0.0f}, left);
                    result = {a.x, a.y};
                }
                break;
            case 2:
                if (left >= right) {
                    auto a = AreaUnderLine({d * 0.5f, 0.0f}, {d, o2}, left);
                    result = {a.x, a.y};
                }
                break;
            case 3: {
                auto a1 = AreaUnderLine({0.0f, o2}, {d * 0.5f, 0.0f}, left);
                auto a2 = AreaUnderLine({d * 0.5f, 0.0f}, {d, o2}, left);
                result = SmoothArea(d, {a1.x + a2.x, a1.y + a2.y});
                break;
            }
            case 4:
                if (left <= right) {
                    auto a = AreaUnderLine({0.0f, o1}, {d * 0.5f, 0.0f}, left);
                    result = {a.x, a.y};
                }
                break;
            case 5: break;
            case 6: {
                auto aFull = AreaUnderLine({0.0f, o1}, {d, o2}, left);
                if (std::abs(offset) > 1e-6f) {
                    auto aL1 = AreaUnderLine({0.0f, o1}, {d * 0.5f, 0.0f}, left);
                    auto aL2 = AreaUnderLine({d * 0.5f, 0.0f}, {d, o2}, left);
                    result = {(aFull.x + aL1.x + aL2.x) * 0.5f, (aFull.y + aL1.y + aL2.y) * 0.5f};
                } else {
                    result = aFull;
                }
                break;
            }
            case 7: {
                auto a = AreaUnderLine({0.0f, o1}, {d, o2}, left);
                result = {a.x, a.y};
                break;
            }
            case 8:
                if (left >= right) {
                    auto a = AreaUnderLine({d * 0.5f, 0.0f}, {d, o1}, left);
                    result = {a.x, a.y};
                }
                break;
            case 9: {
                auto aFull = AreaUnderLine({0.0f, o2}, {d, o1}, left);
                if (std::abs(offset) > 1e-6f) {
                    auto aL1 = AreaUnderLine({0.0f, o2}, {d * 0.5f, 0.0f}, left);
                    auto aL2 = AreaUnderLine({d * 0.5f, 0.0f}, {d, o1}, left);
                    result = {(aFull.x + aL1.x + aL2.x) * 0.5f, (aFull.y + aL1.y + aL2.y) * 0.5f};
                } else {
                    result = aFull;
                }
                break;
            }
            case 10: break;
            case 11: {
                auto a = AreaUnderLine({0.0f, o2}, {d, o1}, left);
                result = {a.x, a.y};
                break;
            }
            case 12: {
                auto a1 = AreaUnderLine({0.0f, o1}, {d * 0.5f, 0.0f}, left);
                auto a2 = AreaUnderLine({d * 0.5f, 0.0f}, {d, o1}, left);
                result = SmoothArea(d, {a1.x + a2.x, a1.y + a2.y});
                break;
            }
            case 13: {
                auto a = AreaUnderLine({0.0f, o2}, {d, o1}, left);
                result = {a.x, a.y};
                break;
            }
            case 14: {
                auto a = AreaUnderLine({0.0f, o1}, {d, o2}, left);
                result = {a.x, a.y};
                break;
            }
            case 15: break;
            }
            return result;
        }

        constexpr std::array<std::pair<int, int>, 16> kOrthoTilePos = {{
            {0, 0}, {3, 0}, {0, 3}, {3, 3},
            {1, 0}, {4, 0}, {1, 3}, {4, 3},
            {0, 1}, {3, 1}, {0, 4}, {3, 4},
            {1, 1}, {4, 1}, {1, 4}, {4, 4}
        }};

        constexpr std::array<float, 7> kOrthoSubpixelOffsets = {
            0.0f, -0.25f, 0.25f, -0.125f, 0.125f, -0.375f, 0.375f
        };

        float BilinearDecode(float e0, float e1, float e2, float e3)
        {
            const float a = Lerp(e0, e1, 0.75f);
            const float b = Lerp(e2, e3, 0.75f);
            return Lerp(a, b, 0.875f);
        }

        int DeltaLeft(const float left[4], const float top[4])
        {
            int delta = 0;
            if (top[3] == 1.0f) delta = 1;
            if (delta == 1 && top[2] == 1.0f && left[1] != 1.0f && left[3] != 1.0f)
                delta = 2;
            return delta;
        }

        int DeltaRight(const float left[4], const float top[4])
        {
            int delta = 0;
            if (top[3] == 1.0f && left[1] != 1.0f && left[3] != 1.0f)
                delta = 1;
            if (delta == 1 && top[2] == 1.0f && left[0] != 1.0f && left[2] != 1.0f)
                delta = 2;
            return delta;
        }

        std::vector<std::uint8_t> GenerateSMAAAreaTextureBytes()
        {
            const int width  = static_cast<int>(kPostProcessSMAAAreaTextureWidth);
            const int height = static_cast<int>(kPostProcessSMAAAreaTextureHeight);
            std::vector<std::uint8_t> data(static_cast<std::size_t>(width * height * 2), 0);
            const int subtexSize = kAreaTexMaxDist;
            for (int subtex = 0; subtex < kAreaTexSubtexels; ++subtex)
            {
                const float offset = kOrthoSubpixelOffsets[static_cast<std::size_t>(subtex)];
                for (int pattern = 0; pattern < kNumOrthoPatterns; ++pattern)
                {
                    const auto [tileCol, tileRow] = kOrthoTilePos[static_cast<std::size_t>(pattern)];
                    for (int y = 0; y < subtexSize; ++y)
                    {
                        for (int x = 0; x < subtexSize; ++x)
                        {
                            const float d1 = static_cast<float>(y * y);
                            const float d2 = static_cast<float>(x * x);
                            const Vec2 area = AreaOrthoPattern(pattern, d1, d2, offset);
                            const int px = x + tileCol * subtexSize;
                            const int py = y + (tileRow + subtex * 5) * subtexSize;
                            if (px >= 0 && px < width && py >= 0 && py < height)
                            {
                                const int idx = (py * width + px) * 2;
                                data[static_cast<std::size_t>(idx) + 0] = static_cast<std::uint8_t>(
                                    std::clamp(area.x * 255.0f, 0.0f, 255.0f));
                                data[static_cast<std::size_t>(idx) + 1] = static_cast<std::uint8_t>(
                                    std::clamp(area.y * 255.0f, 0.0f, 255.0f));
                            }
                        }
                    }
                }
            }
            return data;
        }

        std::vector<std::uint8_t> GenerateSMAASearchTextureBytes()
        {
            const int width  = static_cast<int>(kPostProcessSMAASearchTextureWidth);
            const int height = static_cast<int>(kPostProcessSMAASearchTextureHeight);
            std::vector<std::uint8_t> data(static_cast<std::size_t>(width * height), 0);

            struct EdgeConfig { float e[4]; float bilinear; };
            std::array<EdgeConfig, 16> configs;
            for (int i = 0; i < 16; ++i)
            {
                const float e0 = (i & 1) ? 1.0f : 0.0f;
                const float e1 = (i & 2) ? 1.0f : 0.0f;
                const float e2 = (i & 4) ? 1.0f : 0.0f;
                const float e3 = (i & 8) ? 1.0f : 0.0f;
                configs[static_cast<std::size_t>(i)] = {{e0, e1, e2, e3}, BilinearDecode(e0, e1, e2, e3)};
            }
            const float quantStep = 1.0f / 32.0f;
            for (int y = 0; y < height; ++y)
            {
                const float topBilinear = static_cast<float>(y) * quantStep;
                for (int x = 0; x < width; ++x)
                {
                    const bool isRight = (x >= 33);
                    const int localX = isRight ? (x - 33) : x;
                    const float leftBilinear = static_cast<float>(localX) * quantStep;
                    auto findClosest = [&](float target) -> int {
                        int best = 0;
                        float bestDist = std::abs(configs[0].bilinear - target);
                        for (int i = 1; i < 16; ++i)
                        {
                            const float dist = std::abs(configs[static_cast<std::size_t>(i)].bilinear - target);
                            if (dist < bestDist) { bestDist = dist; best = i; }
                        }
                        return best;
                    };
                    const int leftIdx = findClosest(leftBilinear);
                    const int topIdx  = findClosest(topBilinear);
                    const float* leftEdges = configs[static_cast<std::size_t>(leftIdx)].e;
                    const float* topEdges  = configs[static_cast<std::size_t>(topIdx)].e;
                    const int delta = isRight ? DeltaRight(leftEdges, topEdges) : DeltaLeft(leftEdges, topEdges);
                    data[static_cast<std::size_t>(y * width + x)] =
                        static_cast<std::uint8_t>(std::clamp(127 * delta, 0, 255));
                }
            }
            return data;
        }

        [[nodiscard]] bool IsSupportedAA(const PostProcessAntiAliasing aa) noexcept
        {
            switch (aa)
            {
            case PostProcessAntiAliasing::None:
            case PostProcessAntiAliasing::FXAA:
            case PostProcessAntiAliasing::SMAA:
            case PostProcessAntiAliasing::TAA:
            case PostProcessAntiAliasing::ExternalReconstructor:
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

        // GRAPHICS-075 Slice D.2b — retained-resource ownership. The
        // managers are non-owning pointers; the leases own their slots
        // and are released in Shutdown() before the managers themselves
        // are torn down by the renderer.
        RHI::TextureManager* TextureMgr{nullptr};
        RHI::BufferManager*  BufferMgr{nullptr};
        RHI::TextureManager::TextureLease AreaLutLease{};
        RHI::TextureManager::TextureLease SearchLutLease{};
        RHI::BufferManager::BufferLease   ExposureHistoryLease{};
        // GRAPHICS-075 Slice E.2 — CPU mirror of the device-side
        // `PostProcess.ExposureHistory` storage buffer. Updated by
        // `PublishHistogramReadback(...)` after each completed-frame drain
        // decodes the 256-bin payload; uploaded to the GPU buffer through
        // the transfer queue when an operational device is available. Tests
        // read the mirror via `GetExposureHistorySnapshot()` so the drain →
        // publish handshake is observable without round-tripping through
        // the GPU buffer.
        PostProcessExposureHistory ExposureHistory{};
        std::uint32_t              HistogramPublishCount{0u};
    };

    // GRAPHICS-075 Slice D.2b — allocate + upload the retained SMAA LUT
    // textures and the exposure-adaptation history buffer. Idempotent:
    // returns immediately when the leases are already valid, so calling
    // from both the renderer's Initialize() and a later
    // RebuildOperationalResources() (covering the "non-operational at
    // first Initialize, operational by rebuild" case) does not
    // re-allocate or re-upload.
    void PostProcessSystem::TryAllocateRetainedResources(PostProcessSystem::Impl& impl,
                                                        RHI::IDevice& device)
    {
        if (!impl.TextureMgr || !impl.BufferMgr || !device.IsOperational())
        {
            return;
        }
        if (impl.AreaLutLease.GetHandle().IsValid() &&
            impl.SearchLutLease.GetHandle().IsValid() &&
            impl.ExposureHistoryLease.GetHandle().IsValid())
        {
            return;
        }

        if (!impl.AreaLutLease.GetHandle().IsValid())
        {
            const RHI::TextureDesc areaDesc{
                .Width  = kPostProcessSMAAAreaTextureWidth,
                .Height = kPostProcessSMAAAreaTextureHeight,
                .Fmt    = RHI::Format::RG8_UNORM,
                .Usage  = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst,
                .DebugName = "PostProcess.SMAA.AreaTex",
            };
            auto areaOr = impl.TextureMgr->Create(areaDesc);
            if (areaOr.has_value())
            {
                RHI::TextureManager::TextureLease lease = std::move(*areaOr);
                const std::vector<std::uint8_t> bytes = GenerateSMAAAreaTextureBytes();
                // UploadTexture returns an invalid token (Value == 0) when
                // the backend rejects the upload (e.g. staging allocation
                // failure). Roll the lease back in that case so the
                // idempotence check sees an invalid handle and retries the
                // allocate+upload on the next Initialize(...) call rather
                // than leaving SMAA sampling uninitialized texture content.
                const RHI::TransferToken token = device.GetTransferQueue().UploadTexture(
                    lease.GetHandle(),
                    bytes.data(),
                    static_cast<std::uint64_t>(bytes.size()));
                if (token.IsValid())
                {
                    impl.AreaLutLease = std::move(lease);
                }
                else
                {
                    Core::Log::Warn("[Graphics] PostProcess.SMAA.AreaTex upload rejected; lease released, will retry on next Initialize.");
                }
            }
        }

        if (!impl.SearchLutLease.GetHandle().IsValid())
        {
            const RHI::TextureDesc searchDesc{
                .Width  = kPostProcessSMAASearchTextureWidth,
                .Height = kPostProcessSMAASearchTextureHeight,
                .Fmt    = RHI::Format::R8_UNORM,
                .Usage  = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst,
                .DebugName = "PostProcess.SMAA.SearchTex",
            };
            auto searchOr = impl.TextureMgr->Create(searchDesc);
            if (searchOr.has_value())
            {
                RHI::TextureManager::TextureLease lease = std::move(*searchOr);
                const std::vector<std::uint8_t> bytes = GenerateSMAASearchTextureBytes();
                const RHI::TransferToken token = device.GetTransferQueue().UploadTexture(
                    lease.GetHandle(),
                    bytes.data(),
                    static_cast<std::uint64_t>(bytes.size()));
                if (token.IsValid())
                {
                    impl.SearchLutLease = std::move(lease);
                }
                else
                {
                    Core::Log::Warn("[Graphics] PostProcess.SMAA.SearchTex upload rejected; lease released, will retry on next Initialize.");
                }
            }
        }

        if (!impl.ExposureHistoryLease.GetHandle().IsValid())
        {
            const RHI::BufferDesc historyDesc{
                .SizeBytes   = sizeof(PostProcessExposureHistory),
                .Usage       = RHI::BufferUsage::Storage | RHI::BufferUsage::TransferDst,
                .HostVisible = false,
                .DebugName   = "PostProcess.ExposureHistory",
            };
            auto historyOr = impl.BufferMgr->Create(historyDesc);
            if (historyOr.has_value())
            {
                impl.ExposureHistoryLease = std::move(*historyOr);
            }
        }
    }

    PostProcessSystem::PostProcessSystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    PostProcessSystem::~PostProcessSystem() = default;

    void PostProcessSystem::Initialize()
    {
        m_Impl->Initialized = true;
    }

    void PostProcessSystem::Initialize(RHI::IDevice& device,
                                       RHI::TextureManager& textureMgr,
                                       RHI::BufferManager& bufferMgr)
    {
        m_Impl->Initialized = true;
        m_Impl->TextureMgr  = &textureMgr;
        m_Impl->BufferMgr   = &bufferMgr;
        PostProcessSystem::TryAllocateRetainedResources(*m_Impl, device);
    }

    void PostProcessSystem::Shutdown()
    {
        // GRAPHICS-075 Slice D.2b — drop leases before clearing the
        // manager pointers so the lease destructors call back through
        // a still-live manager. The renderer's Shutdown() tears the
        // managers down only after PostProcessSystem::Shutdown() has
        // returned.
        m_Impl->AreaLutLease         = {};
        m_Impl->SearchLutLease       = {};
        m_Impl->ExposureHistoryLease = {};
        m_Impl->TextureMgr = nullptr;
        m_Impl->BufferMgr  = nullptr;
        m_Impl->Initialized = false;
        // GRAPHICS-075 Slice E.2 — reset the exposure-history mirror + publish
        // counter so a fresh Initialize() after Shutdown() does not inherit
        // the previous session's adaptation state.
        m_Impl->ExposureHistory      = {};
        m_Impl->HistogramPublishCount = 0u;
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
        case PostProcessAntiAliasing::TAA:
        case PostProcessAntiAliasing::ExternalReconstructor:
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

    RHI::TextureHandle PostProcessSystem::GetSMAAAreaTexture() const noexcept
    {
        return m_Impl->AreaLutLease.GetHandle();
    }

    RHI::TextureHandle PostProcessSystem::GetSMAASearchTexture() const noexcept
    {
        return m_Impl->SearchLutLease.GetHandle();
    }

    RHI::BufferHandle PostProcessSystem::GetExposureHistoryBuffer() const noexcept
    {
        return m_Impl->ExposureHistoryLease.GetHandle();
    }

    namespace
    {
        // GRAPHICS-075 Slice E.2 — canonical log-luminance bounds matching
        // `PostProcessHistogramPushConstants` (and `GRAPHICS-013AQ`).
        // The histogram covers `[-10, +10]` log2 stops over 256 bins. Keep
        // these in lock-step with `Pass.PostProcess.Histogram.cpp` so the
        // CPU-side average decode matches the GPU-side bin assignment.
        constexpr float kHistogramMinLogLum = -10.0f;
        constexpr float kHistogramMaxLogLum =  10.0f;
        constexpr std::uint32_t kHistogramBinCount = 256u;

        // GRAPHICS-075 Slice E.2 — eye-adaptation velocity used as the
        // exponential moving average factor when blending the freshly
        // observed average log luminance into the retained history.
        // 0 means "snap to the current frame" (no adaptation); 1 means
        // "retain the previous frame forever". The 0.05 default produces a
        // gentle one-pole IIR that visibly tracks luminance changes over a
        // handful of frames, matching the `PostProcessSettings::Exposure`
        // adaptation pacing the bloom + tonemap chain expects.
        constexpr float kHistogramAdaptationVelocity = 0.05f;

        // Decode the average log luminance from a 256-bin payload by
        // weighting each bin centre by its sample count. The histogram is
        // partitioned over `[kHistogramMinLogLum, kHistogramMaxLogLum]` log2
        // stops, so bin `i` represents the range
        // `[minLogLum + i*step, minLogLum + (i+1)*step)` with
        // `step = (maxLogLum - minLogLum) / 256`. Returns `minLogLum` when
        // the payload is empty (no contribution) so the adaptation history
        // does not jump to an undefined state on the first frame.
        [[nodiscard]] float DecodeAverageLogLuminance(std::span<const std::uint32_t> bins) noexcept
        {
            if (bins.size() != kHistogramBinCount)
            {
                return kHistogramMinLogLum;
            }
            const float step = (kHistogramMaxLogLum - kHistogramMinLogLum)
                               / static_cast<float>(kHistogramBinCount);
            std::uint64_t totalCount = 0u;
            double weightedSum = 0.0;
            for (std::uint32_t i = 0; i < kHistogramBinCount; ++i)
            {
                const std::uint32_t count = bins[i];
                if (count == 0u)
                {
                    continue;
                }
                const float binCentre = kHistogramMinLogLum +
                                        (static_cast<float>(i) + 0.5f) * step;
                weightedSum += static_cast<double>(count) * static_cast<double>(binCentre);
                totalCount  += count;
            }
            if (totalCount == 0u)
            {
                return kHistogramMinLogLum;
            }
            return static_cast<float>(weightedSum / static_cast<double>(totalCount));
        }
    }

    void PostProcessSystem::PublishHistogramReadback(std::span<const std::uint32_t> bins,
                                                     const std::uint64_t frameIndex,
                                                     RHI::IDevice* const device) noexcept
    {
        if (bins.size() != kHistogramBinCount)
        {
            ++m_Impl->Diagnostics.InvalidSettingCount;
            return;
        }

        const float observedLogLum = DecodeAverageLogLuminance(bins);
        // One-pole IIR: blend the freshly observed average into the retained
        // history. The very first publish call (HistogramPublishCount == 0)
        // snaps directly to the observed value so the history is not anchored
        // to the default-constructed `0.0` on startup.
        if (m_Impl->HistogramPublishCount == 0u)
        {
            m_Impl->ExposureHistory.PreviousAverageLogLum = observedLogLum;
        }
        else
        {
            m_Impl->ExposureHistory.PreviousAverageLogLum =
                kHistogramAdaptationVelocity * m_Impl->ExposureHistory.PreviousAverageLogLum +
                (1.0f - kHistogramAdaptationVelocity) * observedLogLum;
        }
        m_Impl->ExposureHistory.AdaptationVelocity = kHistogramAdaptationVelocity;
        m_Impl->ExposureHistory.FrameIndex = static_cast<std::uint32_t>(frameIndex & 0xFFFFFFFFull);
        ++m_Impl->HistogramPublishCount;

        // GRAPHICS-075 Slice E.2 — propagate the new history payload to the
        // device-side `PostProcess.ExposureHistory` storage buffer through the
        // transfer queue (mirroring the SMAA LUT upload path Slice D.2b
        // uses). When the device is non-operational or the lease is
        // unavailable, the CPU mirror still updates so the drain → publish
        // handshake remains observable through `GetExposureHistorySnapshot()`.
        if (device != nullptr && device->IsOperational() &&
            m_Impl->ExposureHistoryLease.GetHandle().IsValid())
        {
            const RHI::TransferToken token = device->GetTransferQueue().UploadBuffer(
                m_Impl->ExposureHistoryLease.GetHandle(),
                &m_Impl->ExposureHistory,
                sizeof(m_Impl->ExposureHistory),
                /*offset=*/0u);
            if (!token.IsValid())
            {
                Core::Log::Warn("[Graphics] PostProcess.ExposureHistory upload rejected; retained history will be stale this frame.");
            }
        }
    }

    PostProcessExposureHistory PostProcessSystem::GetExposureHistorySnapshot() const noexcept
    {
        return m_Impl->ExposureHistory;
    }

    std::uint32_t PostProcessSystem::GetHistogramPublishCount() const noexcept
    {
        return m_Impl->HistogramPublishCount;
    }
}
