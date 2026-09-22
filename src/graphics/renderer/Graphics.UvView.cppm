// UV view controls and draw packets for renderer-owned texture-coordinate inspection.
module;

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

export module Extrinsic.Graphics.UvView;

import Extrinsic.Graphics.Colormap;
import Extrinsic.Graphics.SceneHandles;
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
        // An explicit baked texture shown texel-exact in atlas layout.
        BakedTexture,
    };

    // How displayed baked texels are interpreted. Stored data is never
    // modified: display transforms run in the shader only.
    enum class UvViewTextureDisplayMode : std::uint8_t
    {
        Color,          // RGB as stored (colormapped/label/colour encodings)
        ScalarColormap, // raw scalar in R, mapped through Range and Colormap
        VectorRange,    // raw vector in RGB, each component mapped through Range
    };

    struct UvViewTextureDisplay
    {
        RHI::BindlessIndex Texture = RHI::kInvalidBindlessIndex;
        UvViewTextureDisplayMode Mode = UvViewTextureDisplayMode::Color;
        // Display range for raw values; values outside it saturate, NaN is
        // shown in a distinct colour, and negative/zero values keep their
        // position in the range.
        float RangeMin = 0.0f;
        float RangeMax = 1.0f;
        Colormap::Type Colormap = Colormap::Type::Viridis;
        // Resolved by the renderer from Colormap; callers leave it invalid.
        RHI::BindlessIndex ColormapLut = RHI::kInvalidBindlessIndex;
    };

    // Explicit view window shared with the CPU pane so pan/zoom agree across
    // both paths. A non-positive half extent selects the automatic fit.
    struct UvViewNavigation
    {
        float CenterU = 0.5f;
        float CenterV = 0.5f;
        // UV half extent along the target height; width follows the aspect.
        float HalfExtentV = 0.0f;
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
        // Consumed when Background is BakedTexture.
        UvViewTextureDisplay BakedTexture{};
        UvViewNavigation Navigation{};
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
        std::uint32_t TextureDisplayMode = 0u;
        float TextureRangeMin = 0.0f;
        float TextureRangeMax = 1.0f;
        std::uint32_t ColormapBindlessIndex = RHI::kInvalidBindlessIndex;
        std::uint32_t Reserved = 0u;
    };

    static_assert(sizeof(UvViewPushConstants) == 64u);

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
