module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

module Extrinsic.Graphics.Reconstruction;

import Extrinsic.RHI.Descriptors;

namespace Extrinsic::Graphics
{
    namespace
    {
        [[nodiscard]] constexpr std::size_t PixelCount(const Core::Extent2D extent) noexcept
        {
            return static_cast<std::size_t>(Core::PositiveWidthOrZero(extent)) *
                   Core::PositiveHeightOrZero(extent);
        }

        [[nodiscard]] constexpr std::uint32_t Width(const Core::Extent2D extent) noexcept
        {
            return Core::PositiveWidthOrZero(extent);
        }

        [[nodiscard]] constexpr std::uint32_t Height(const Core::Extent2D extent) noexcept
        {
            return Core::PositiveHeightOrZero(extent);
        }

        [[nodiscard]] constexpr bool SameExtent(const Core::Extent2D a,
                                                const Core::Extent2D b) noexcept
        {
            return a.Width == b.Width && a.Height == b.Height;
        }

        [[nodiscard]] bool HintsMatchExtent(const ReconstructionHints& hints,
                                            const Core::Extent2D extent) noexcept
        {
            const bool inputOk = hints.InputExtent.Width == 0u && hints.InputExtent.Height == 0u
                ? true
                : SameExtent(hints.InputExtent, extent);
            const bool outputOk = hints.OutputExtent.Width == 0u && hints.OutputExtent.Height == 0u
                ? true
                : SameExtent(hints.OutputExtent, extent);
            return inputOk && outputOk;
        }

        [[nodiscard]] bool ValidateInputs(ReconstructionColorView jitteredColor,
                                          ReconstructionDepthView depth,
                                          ReconstructionMotionVectorView motionVectors,
                                          ReconstructionColorView historyColor,
                                          ReconstructionOutputView output,
                                          const ReconstructionHints& hints,
                                          ReconstructionFailReason& reason) noexcept
        {
            if (!jitteredColor.IsValid() || !depth.IsValid() ||
                !motionVectors.IsValid() || !historyColor.IsValid() ||
                !output.IsValid())
            {
                reason = ReconstructionFailReason::InvalidInput;
                return false;
            }

            const Core::Extent2D extent = jitteredColor.Extent;
            if (!SameExtent(depth.Extent, extent) ||
                !SameExtent(motionVectors.Extent, extent) ||
                !SameExtent(historyColor.Extent, extent) ||
                !SameExtent(output.Extent, extent) ||
                !HintsMatchExtent(hints, extent))
            {
                reason = ReconstructionFailReason::ExtentMismatch;
                return false;
            }

            reason = ReconstructionFailReason::None;
            return true;
        }

        void CopyCurrentToOutput(ReconstructionColorView current,
                                 ReconstructionOutputView output) noexcept
        {
            const std::size_t count = std::min(PixelCount(current.Extent), output.Pixels.size());
            for (std::size_t i = 0; i < count; ++i)
            {
                output.Pixels[i] = current.Pixels[i];
            }
        }

        [[nodiscard]] glm::vec3 RgbToYCoCg(const glm::vec3 rgb) noexcept
        {
            const float y = rgb.x * 0.25f + rgb.y * 0.5f + rgb.z * 0.25f;
            const float co = rgb.x - rgb.z;
            const float cg = rgb.y - (rgb.x + rgb.z) * 0.5f;
            return {y, co, cg};
        }

        [[nodiscard]] glm::vec3 YCoCgToRgb(const glm::vec3 ycocg) noexcept
        {
            const float t = ycocg.x - ycocg.z * 0.5f;
            return {
                t + ycocg.y * 0.5f,
                ycocg.x + ycocg.z * 0.5f,
                t - ycocg.y * 0.5f,
            };
        }

        [[nodiscard]] glm::vec4 ClampColorFinite(glm::vec4 color) noexcept
        {
            for (int i = 0; i < 4; ++i)
            {
                if (!std::isfinite(color[i]))
                {
                    color[i] = 0.f;
                }
            }
            color.r = std::max(0.f, color.r);
            color.g = std::max(0.f, color.g);
            color.b = std::max(0.f, color.b);
            color.a = std::clamp(color.a, 0.f, 1.f);
            return color;
        }

        [[nodiscard]] glm::vec3 ClampHistoryToNeighborhood(ReconstructionColorView current,
                                                           const std::uint32_t x,
                                                           const std::uint32_t y,
                                                           glm::vec3 historyRgb) noexcept
        {
            glm::vec3 sum{0.f};
            glm::vec3 sumSq{0.f};
            std::uint32_t count = 0u;
            const std::uint32_t width = Width(current.Extent);
            const std::uint32_t height = Height(current.Extent);

            for (int dy = -2; dy <= 2; ++dy)
            {
                const int sy = std::clamp<int>(static_cast<int>(y) + dy, 0, static_cast<int>(height) - 1);
                for (int dx = -2; dx <= 2; ++dx)
                {
                    const int sx = std::clamp<int>(static_cast<int>(x) + dx, 0, static_cast<int>(width) - 1);
                    const glm::vec4 sample = current.Pixels[static_cast<std::size_t>(sy) * width + static_cast<std::size_t>(sx)];
                    const glm::vec3 ycocg = RgbToYCoCg({sample.r, sample.g, sample.b});
                    sum += ycocg;
                    sumSq += ycocg * ycocg;
                    ++count;
                }
            }

            const glm::vec3 mean = sum / static_cast<float>(count);
            const glm::vec3 variance = glm::max(sumSq / static_cast<float>(count) - mean * mean, glm::vec3{0.f});
            const glm::vec3 sigma = glm::sqrt(variance);
            const glm::vec3 historyYCoCg = RgbToYCoCg(historyRgb);
            const glm::vec3 clipped = glm::clamp(historyYCoCg, mean - sigma, mean + sigma);
            return glm::max(YCoCgToRgb(clipped), glm::vec3{0.f});
        }

        [[nodiscard]] glm::vec4 SampleHistoryNearest(ReconstructionColorView history,
                                                     const glm::vec2 previousNdc,
                                                     bool& inside) noexcept
        {
            const float u = (previousNdc.x + 1.f) * 0.5f;
            const float v = (1.f - previousNdc.y) * 0.5f;
            inside = u >= 0.f && u <= 1.f && v >= 0.f && v <= 1.f;
            if (!inside)
            {
                return {};
            }

            const std::uint32_t width = Width(history.Extent);
            const std::uint32_t height = Height(history.Extent);
            const float px = u * static_cast<float>(width) - 0.5f;
            const float py = v * static_cast<float>(height) - 0.5f;
            const std::uint32_t x = static_cast<std::uint32_t>(
                std::clamp(std::lround(px), 0l, static_cast<long>(width - 1u)));
            const std::uint32_t y = static_cast<std::uint32_t>(
                std::clamp(std::lround(py), 0l, static_cast<long>(height - 1u)));
            return history.Pixels[static_cast<std::size_t>(y) * width + x];
        }

        [[nodiscard]] glm::vec2 PixelCenterToNdc(const std::uint32_t x,
                                                 const std::uint32_t y,
                                                 const Core::Extent2D extent) noexcept
        {
            return {
                ((static_cast<float>(x) + 0.5f) / static_cast<float>(Width(extent))) * 2.f - 1.f,
                1.f - ((static_cast<float>(y) + 0.5f) / static_cast<float>(Height(extent))) * 2.f,
            };
        }

        [[nodiscard]] float Luma(const glm::vec3 rgb) noexcept
        {
            return rgb.r * 0.2126f + rgb.g * 0.7152f + rgb.b * 0.0722f;
        }
    }

    ReconstructionResult ReferenceTAAReconstructor::Apply(
        ReconstructionColorView jitteredColor,
        ReconstructionDepthView depth,
        ReconstructionMotionVectorView motionVectors,
        ReconstructionColorView historyColor,
        ReconstructionOutputView output,
        const ReconstructionHints& hints) noexcept
    {
        ReconstructionFailReason reason = ReconstructionFailReason::None;
        if (!ValidateInputs(jitteredColor, depth, motionVectors, historyColor, output, hints, reason))
        {
            return ReconstructionResult{.FailReason = reason};
        }

        if (hints.Reset)
        {
            CopyCurrentToOutput(jitteredColor, output);
            return ReconstructionResult{
                .Applied = false,
                .DisocclusionPercent = 1.f,
                .DisoccludedPixelCount = static_cast<std::uint32_t>(PixelCount(jitteredColor.Extent)),
                .FailReason = ReconstructionFailReason::ResetHistory,
            };
        }

        const Core::Extent2D extent = jitteredColor.Extent;
        const std::uint32_t width = Width(extent);
        const std::uint32_t height = Height(extent);
        const float exposure = std::clamp(hints.Exposure, 0.001f, 64.f);
        const float sharpness = std::clamp(hints.Sharpness, 0.f, 1.f);
        std::uint32_t disoccluded = 0u;

        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(y) * width + x;
                const glm::vec4 current = ClampColorFinite(jitteredColor.Pixels[index]);
                const float currentDepth = depth.Pixels[index];
                const glm::vec2 currentNdc = PixelCenterToNdc(x, y, extent);
                const glm::vec2 previousNdc = currentNdc + motionVectors.Pixels[index];

                bool historyInside = false;
                glm::vec4 history = SampleHistoryNearest(historyColor, previousNdc, historyInside);
                const bool validDepth = std::isfinite(currentDepth) && currentDepth > 0.f;
                if (!historyInside || !validDepth)
                {
                    output.Pixels[index] = current;
                    ++disoccluded;
                    continue;
                }

                history = ClampColorFinite(history);
                const glm::vec3 clippedHistory =
                    ClampHistoryToNeighborhood(jitteredColor, x, y, {history.r, history.g, history.b});
                const float currentLum = Luma({current.r, current.g, current.b});
                const float historyLum = Luma(clippedHistory);
                const float luminanceDelta = std::abs(currentLum - historyLum) * exposure;
                const float historyWeight = std::clamp(0.92f - luminanceDelta * 0.35f - sharpness * 0.15f,
                                                       0.05f,
                                                       0.95f);
                const glm::vec3 resolvedRgb =
                    glm::mix(glm::vec3{current.r, current.g, current.b}, clippedHistory, historyWeight);
                const float resolvedAlpha = glm::mix(current.a, history.a, historyWeight);
                output.Pixels[index] = ClampColorFinite(glm::vec4{resolvedRgb, resolvedAlpha});
            }
        }

        const std::uint32_t total = static_cast<std::uint32_t>(PixelCount(extent));
        return ReconstructionResult{
            .Applied = true,
            .DisocclusionPercent = total == 0u ? 0.f : static_cast<float>(disoccluded) / static_cast<float>(total),
            .DisoccludedPixelCount = disoccluded,
            .FailReason = ReconstructionFailReason::None,
        };
    }

    ReconstructionHistoryDesc ComputeReconstructionHistoryDesc(
        const std::uint32_t renderWidth,
        const std::uint32_t renderHeight) noexcept
    {
        if (renderWidth == 0u || renderHeight == 0u)
        {
            return ReconstructionHistoryDesc{};
        }
        return ReconstructionHistoryDesc{
            .Width = renderWidth,
            .Height = renderHeight,
            .Fmt = RHI::Format::RGBA16_FLOAT,
        };
    }

    struct ReconstructionHistorySystem::Impl
    {
        struct RetiredLease
        {
            RHI::TextureManager::TextureLease Lease{};
            std::uint64_t Deadline{0u};
        };

        bool Initialized{false};
        RHI::TextureManager* TextureMgr{nullptr};

        bool Allocated{false};
        ReconstructionHistoryDesc Desc{};
        RHI::TextureManager::TextureLease Textures[2]{};
        std::uint32_t CurrentIndex{0u};

        std::vector<RetiredLease> Retire{};
        ReconstructionHistoryDiagnostics Diagnostics{};
    };

    ReconstructionHistorySystem::ReconstructionHistorySystem()
        : m_Impl(std::make_unique<Impl>())
    {}

    ReconstructionHistorySystem::~ReconstructionHistorySystem() = default;

    void ReconstructionHistorySystem::Initialize(RHI::IDevice& /*device*/, RHI::TextureManager& textureMgr)
    {
        m_Impl->Initialized = true;
        m_Impl->TextureMgr = &textureMgr;
    }

    void ReconstructionHistorySystem::Shutdown()
    {
        m_Impl->Textures[0] = {};
        m_Impl->Textures[1] = {};
        m_Impl->Retire.clear();
        m_Impl->Allocated = false;
        m_Impl->Desc = {};
        m_Impl->CurrentIndex = 0u;
        m_Impl->TextureMgr = nullptr;
        m_Impl->Initialized = false;
    }

    bool ReconstructionHistorySystem::IsInitialized() const noexcept
    {
        return m_Impl->Initialized;
    }

    bool ReconstructionHistorySystem::EnsureAllocated(const std::uint32_t renderWidth,
                                                      const std::uint32_t renderHeight,
                                                      const std::uint64_t currentFrame)
    {
        if (!m_Impl->Initialized || m_Impl->TextureMgr == nullptr)
        {
            return false;
        }

        const ReconstructionHistoryDesc desc = ComputeReconstructionHistoryDesc(renderWidth, renderHeight);
        if (!desc.IsValid())
        {
            return false;
        }

        if (m_Impl->Allocated && m_Impl->Desc == desc)
        {
            return true;
        }

        if (m_Impl->Allocated)
        {
            for (RHI::TextureManager::TextureLease& lease : m_Impl->Textures)
            {
                if (lease.IsValid())
                {
                    m_Impl->Retire.push_back(Impl::RetiredLease{std::move(lease), currentFrame});
                }
            }
            m_Impl->Textures[0] = {};
            m_Impl->Textures[1] = {};
            m_Impl->Allocated = false;
            ++m_Impl->Diagnostics.ReallocationCount;
        }

        const RHI::TextureDesc textureDesc{
            .Width = desc.Width,
            .Height = desc.Height,
            .MipLevels = 1u,
            .Fmt = desc.Fmt,
            .Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::ColorTarget,
            .DebugName = "Reconstruction.History",
        };

        auto leaseA = m_Impl->TextureMgr->Create(textureDesc);
        if (!leaseA.has_value())
        {
            return false;
        }
        auto leaseB = m_Impl->TextureMgr->Create(textureDesc);
        if (!leaseB.has_value())
        {
            return false;
        }

        m_Impl->Textures[0] = std::move(*leaseA);
        m_Impl->Textures[1] = std::move(*leaseB);
        m_Impl->CurrentIndex = 0u;
        m_Impl->Desc = desc;
        m_Impl->Allocated = true;
        ++m_Impl->Diagnostics.AllocationCount;
        return true;
    }

    void ReconstructionHistorySystem::Tick(const std::uint64_t currentFrame,
                                           const std::uint32_t framesInFlight)
    {
        if (m_Impl->Retire.empty())
        {
            m_Impl->Diagnostics.PendingRetireCount = 0u;
            return;
        }

        std::vector<Impl::RetiredLease> survivors{};
        survivors.reserve(m_Impl->Retire.size());
        for (Impl::RetiredLease& retired : m_Impl->Retire)
        {
            const bool elapsed = currentFrame >= retired.Deadline + framesInFlight;
            if (elapsed)
            {
                retired.Lease = {};
                ++m_Impl->Diagnostics.RetiredTextureCount;
            }
            else
            {
                survivors.push_back(std::move(retired));
            }
        }
        m_Impl->Retire = std::move(survivors);
        m_Impl->Diagnostics.PendingRetireCount = static_cast<std::uint32_t>(m_Impl->Retire.size());
    }

    void ReconstructionHistorySystem::AdvanceFrame() noexcept
    {
        m_Impl->CurrentIndex ^= 1u;
    }

    RHI::TextureHandle ReconstructionHistorySystem::CurrentHistory() const noexcept
    {
        if (!m_Impl->Allocated)
        {
            return {};
        }
        return m_Impl->Textures[m_Impl->CurrentIndex].GetHandle();
    }

    RHI::TextureHandle ReconstructionHistorySystem::PreviousHistory() const noexcept
    {
        if (!m_Impl->Allocated)
        {
            return {};
        }
        return m_Impl->Textures[m_Impl->CurrentIndex ^ 1u].GetHandle();
    }

    ReconstructionHistoryDesc ReconstructionHistorySystem::GetAllocatedDesc() const noexcept
    {
        return m_Impl->Allocated ? m_Impl->Desc : ReconstructionHistoryDesc{};
    }

    bool ReconstructionHistorySystem::IsAllocated() const noexcept
    {
        return m_Impl->Allocated;
    }

    ReconstructionHistoryDiagnostics ReconstructionHistorySystem::GetDiagnostics() const noexcept
    {
        return m_Impl->Diagnostics;
    }
}
