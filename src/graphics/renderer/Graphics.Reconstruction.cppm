module;

#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.Reconstruction;

import Extrinsic.Core.Geometry2D;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureManager;

export namespace Extrinsic::Graphics
{
    struct ReconstructionColorView
    {
        std::span<const glm::vec4> Pixels{};
        Core::Extent2D Extent{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return !Core::IsEmpty(Extent) &&
                   Pixels.size() >= static_cast<std::size_t>(Core::PositiveWidthOrZero(Extent)) *
                                        Core::PositiveHeightOrZero(Extent);
        }
    };

    struct ReconstructionDepthView
    {
        std::span<const float> Pixels{};
        Core::Extent2D Extent{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return !Core::IsEmpty(Extent) &&
                   Pixels.size() >= static_cast<std::size_t>(Core::PositiveWidthOrZero(Extent)) *
                                        Core::PositiveHeightOrZero(Extent);
        }
    };

    struct ReconstructionMotionVectorView
    {
        std::span<const glm::vec2> Pixels{};
        Core::Extent2D Extent{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return !Core::IsEmpty(Extent) &&
                   Pixels.size() >= static_cast<std::size_t>(Core::PositiveWidthOrZero(Extent)) *
                                        Core::PositiveHeightOrZero(Extent);
        }
    };

    struct ReconstructionOutputView
    {
        std::span<glm::vec4> Pixels{};
        Core::Extent2D Extent{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return !Core::IsEmpty(Extent) &&
                   Pixels.size() >= static_cast<std::size_t>(Core::PositiveWidthOrZero(Extent)) *
                                        Core::PositiveHeightOrZero(Extent);
        }
    };

    struct ReconstructionHints
    {
        float Sharpness{0.5f};
        float Exposure{1.0f};
        glm::vec2 JitterOffset{0.f};
        std::uint64_t FrameIndex{0u};
        Core::Extent2D InputExtent{};
        Core::Extent2D OutputExtent{};
        bool Reset{false};
    };

    enum class ReconstructionFailReason : std::uint8_t
    {
        None = 0,
        InvalidInput,
        ExtentMismatch,
        ResetHistory,
    };

    struct ReconstructionResult
    {
        bool Applied{false};
        float DisocclusionPercent{0.f};
        std::uint32_t DisoccludedPixelCount{0u};
        ReconstructionFailReason FailReason{ReconstructionFailReason::None};
    };

    class IReconstructor
    {
    public:
        virtual ~IReconstructor() = default;

        [[nodiscard]] virtual ReconstructionResult Apply(
            ReconstructionColorView jitteredColor,
            ReconstructionDepthView depth,
            ReconstructionMotionVectorView motionVectors,
            ReconstructionColorView historyColor,
            ReconstructionOutputView output,
            const ReconstructionHints& hints) noexcept = 0;
    };

    class ReferenceTAAReconstructor final : public IReconstructor
    {
    public:
        [[nodiscard]] ReconstructionResult Apply(
            ReconstructionColorView jitteredColor,
            ReconstructionDepthView depth,
            ReconstructionMotionVectorView motionVectors,
            ReconstructionColorView historyColor,
            ReconstructionOutputView output,
            const ReconstructionHints& hints) noexcept override;
    };

    struct ReconstructionHistoryDesc
    {
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        RHI::Format Fmt{RHI::Format::RGBA16_FLOAT};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Width > 0u && Height > 0u;
        }
        [[nodiscard]] bool operator==(const ReconstructionHistoryDesc&) const noexcept = default;
    };

    [[nodiscard]] ReconstructionHistoryDesc ComputeReconstructionHistoryDesc(
        std::uint32_t renderWidth,
        std::uint32_t renderHeight) noexcept;

    struct ReconstructionHistoryDiagnostics
    {
        std::uint32_t AllocationCount{0u};
        std::uint32_t ReallocationCount{0u};
        std::uint32_t RetiredTextureCount{0u};
        std::uint32_t PendingRetireCount{0u};
    };

    class ReconstructionHistorySystem
    {
    public:
        ReconstructionHistorySystem();
        ~ReconstructionHistorySystem();

        ReconstructionHistorySystem(const ReconstructionHistorySystem&) = delete;
        ReconstructionHistorySystem& operator=(const ReconstructionHistorySystem&) = delete;

        void Initialize(RHI::IDevice& device, RHI::TextureManager& textureMgr);
        void Shutdown();
        [[nodiscard]] bool IsInitialized() const noexcept;

        bool EnsureAllocated(std::uint32_t renderWidth,
                             std::uint32_t renderHeight,
                             std::uint64_t currentFrame);
        void Tick(std::uint64_t currentFrame, std::uint32_t framesInFlight);
        void AdvanceFrame() noexcept;

        [[nodiscard]] RHI::TextureHandle CurrentHistory() const noexcept;
        [[nodiscard]] RHI::TextureHandle PreviousHistory() const noexcept;
        [[nodiscard]] ReconstructionHistoryDesc GetAllocatedDesc() const noexcept;
        [[nodiscard]] bool IsAllocated() const noexcept;
        [[nodiscard]] ReconstructionHistoryDiagnostics GetDiagnostics() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
