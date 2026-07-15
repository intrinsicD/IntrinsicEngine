module;

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

export module Extrinsic.Graphics.UvView;

import Extrinsic.Graphics.GpuWorld;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.PipelineManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;

export namespace Extrinsic::Graphics
{
    enum class UvViewBackgroundMode : std::uint8_t
    {
        Grid,
        Checker,
        TexelDensity,
        Texture,
    };

    enum class UvViewActiveMode : std::uint8_t
    {
        CpuLayout,
        GpuShaded,
    };

    enum class UvViewStatus : std::uint8_t
    {
        Disabled,
        CpuFallbackNonOperational,
        WaitingForGeometry,
        InvalidRequest,
        ResourceCreationFailed,
        Ready,
    };

    struct UvViewBounds
    {
        float MinU = 0.0f;
        float MinV = 0.0f;
        float MaxU = 1.0f;
        float MaxV = 1.0f;
    };

    struct UvViewRequest
    {
        bool Enabled = false;
        // Non-zero identity of the complete request payload. Submit once before
        // every Prepare call to keep the pass enabled; reusing a token refreshes
        // that heartbeat without copying dense topology into retained storage.
        std::uint64_t RequestToken = 0u;
        GpuGeometryHandle Geometry{};
        std::uint32_t Width = 0u;
        std::uint32_t Height = 0u;
        UvViewBounds Bounds{};
        UvViewBackgroundMode Background = UvViewBackgroundMode::Grid;
        RHI::BindlessIndex BackgroundTexture = RHI::kInvalidBindlessIndex;
        bool ShowDistortionHeatmap = false;

        // Triangle edges expanded as (a,b,b,c,c,a). These are copied on
        // submission and uploaded into UV-view-owned storage.
        std::vector<std::uint32_t> LineIndices;

        // One canonical conformal-distortion value per rendered triangle in
        // gl_PrimitiveID order. Invalid entries use quiet NaN and remain
        // visibly distinct in the shader.
        std::vector<float> TriangleConformalDistortion;
    };

    struct UvViewOutput
    {
        UvViewStatus Status = UvViewStatus::Disabled;
        UvViewActiveMode ActiveMode = UvViewActiveMode::CpuLayout;
        UvViewBackgroundMode RequestedBackground = UvViewBackgroundMode::Grid;
        UvViewBackgroundMode ActiveBackground = UvViewBackgroundMode::Grid;
        bool HeatmapActive = false;
        bool HasCompletedContents = false;
        std::uint64_t RequestToken = 0u;
        RHI::TextureHandle Texture{};
        RHI::BindlessIndex BindlessIndex = RHI::kInvalidBindlessIndex;
        std::uint32_t Width = 0u;
        std::uint32_t Height = 0u;
        std::uint64_t TargetGeneration = 0u;
        std::uint64_t RecordedPassCount = 0u;
        std::string Diagnostic;

        [[nodiscard]] bool IsGpuReady() const noexcept
        {
            return Status == UvViewStatus::Ready &&
                   ActiveMode == UvViewActiveMode::GpuShaded &&
                   HasCompletedContents &&
                   Texture.IsValid() &&
                   BindlessIndex != RHI::kInvalidBindlessIndex;
        }
    };

    struct UvViewPushConstants
    {
        std::uint64_t TexcoordBDA = 0u;
        std::uint64_t DistortionBDA = 0u;
        float UvCenterX = 0.5f;
        float UvCenterY = 0.5f;
        float UvHalfExtentX = 0.55f;
        float UvHalfExtentY = 0.55f;
        std::uint32_t BackgroundMode = 0u;
        std::uint32_t BackgroundTextureBindlessIndex = RHI::kInvalidBindlessIndex;
        std::uint32_t ShowHeatmap = 0u;
        std::uint32_t Reserved = 0u;
    };

    static_assert(sizeof(UvViewPushConstants) == 48u);

    class UvView
    {
    public:
        UvView();
        ~UvView();

        UvView(const UvView&) = delete;
        UvView& operator=(const UvView&) = delete;

        void Initialize(RHI::IDevice& device,
                        RHI::BufferManager& buffers,
                        RHI::TextureManager& textures,
                        RHI::SamplerManager& samplers,
                        RHI::PipelineManager& pipelines);
        [[nodiscard]] bool RebuildOperationalResources(RHI::IDevice& device);
        void Shutdown();

        void Submit(UvViewRequest request);
        void Prepare(GpuWorld& gpuWorld);
        void Record(RHI::ICommandContext& commandContext);
        void CompleteFrame(bool graphExecutionSucceeded) noexcept;

        [[nodiscard]] const UvViewOutput& GetOutput() const noexcept;
        [[nodiscard]] RHI::TextureHandle GetTarget() const noexcept;
        [[nodiscard]] bool ShouldRecord() const noexcept;
        [[nodiscard]] bool TargetHasShaderReadContents() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
