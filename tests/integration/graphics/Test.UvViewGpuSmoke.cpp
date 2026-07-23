#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <gtest/gtest.h>

#include "OperationalCounterStability.hpp"

#include "RuntimeTestModule.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Graphics.GpuWorld;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;

namespace
{
namespace Counters =
    Extrinsic::Tests::Support::OperationalCounterStability;

using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
using Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs;
using Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::ToString;
using Extrinsic::Graphics::RenderCommandPassStatus;
using Extrinsic::Runtime::Engine;

inline constexpr std::uint32_t kTargetWidth = 128u;
inline constexpr std::uint32_t kTargetHeight = 128u;
inline constexpr std::uint32_t kPixelBytes = 4u;
inline constexpr std::uint32_t kMinimumFrames = 8u;
inline constexpr std::uint32_t kMaximumFrames = 12u;
inline constexpr std::uint64_t kReadbackBytes =
    static_cast<std::uint64_t>(kTargetWidth) * kTargetHeight * kPixelBytes;
inline constexpr std::uint32_t kTextureBackgroundWidth = 4u;
inline constexpr std::uint32_t kTextureBackgroundHeight = 4u;
inline constexpr std::size_t kHeatmapTriangleCount = 4u;

// Four disjoint, deliberately non-mirrored UV triangles. Primitive order is
// the heatmap payload order, so centroid probes below identify low, mid, high,
// and invalid distortion without touching the yellow wire overlay.
inline const std::array<std::array<glm::vec2, 3u>, kHeatmapTriangleCount>
    kHeatmapUvTriangles{{
        std::array<glm::vec2, 3u>{
            glm::vec2{0.10f, 0.10f},
            glm::vec2{0.34f, 0.13f},
            glm::vec2{0.16f, 0.33f}},
        std::array<glm::vec2, 3u>{
            glm::vec2{0.52f, 0.10f},
            glm::vec2{0.91f, 0.17f},
            glm::vec2{0.64f, 0.36f}},
        std::array<glm::vec2, 3u>{
            glm::vec2{0.08f, 0.50f},
            glm::vec2{0.30f, 0.57f},
            glm::vec2{0.17f, 0.91f}},
        std::array<glm::vec2, 3u>{
            glm::vec2{0.58f, 0.52f},
            glm::vec2{0.88f, 0.59f},
            glm::vec2{0.74f, 0.82f}},
    }};
inline const std::array<float, kHeatmapTriangleCount>
    kConformalDistortionValues{
    1.0f,
    2.0f * std::numbers::sqrt2_v<float>,
    8.0f,
    std::numeric_limits<float>::quiet_NaN(),
};

enum class UvViewSmokeMode : std::uint8_t
{
    CheckerHeatmap = 0u,
    TexelDensity,
    Texture,
    Count,
};

inline constexpr std::size_t kUvViewSmokeModeCount =
    static_cast<std::size_t>(UvViewSmokeMode::Count);
inline constexpr std::array<std::uint64_t, kUvViewSmokeModeCount>
    kRequestTokens{
        0x4752415048494353ull,
        0x4752415048494354ull,
        0x4752415048494355ull,
    };
inline constexpr std::array<Extrinsic::Graphics::UvViewBackgroundMode,
                            kUvViewSmokeModeCount>
    kBackgroundModes{
        Extrinsic::Graphics::UvViewBackgroundMode::Checker,
        Extrinsic::Graphics::UvViewBackgroundMode::TexelDensity,
        Extrinsic::Graphics::UvViewBackgroundMode::Texture,
    };
using TextureRgba = std::array<std::uint8_t, 4u>;
inline constexpr TextureRgba kTextureLowULowV{230u, 35u, 45u, 255u};
inline constexpr TextureRgba kTextureHighULowV{30u, 220u, 70u, 255u};
inline constexpr TextureRgba kTextureLowUHighV{35u, 70u, 230u, 255u};
inline constexpr TextureRgba kTextureHighUHighV{240u, 210u, 25u, 255u};

[[nodiscard]] constexpr std::size_t ModeIndex(
    const UvViewSmokeMode mode) noexcept
{
    return static_cast<std::size_t>(mode);
}

Counters::Snapshot ToCounterSnapshot(
    const Extrinsic::Backends::Vulkan::VulkanOperationalDiagnosticsSnapshot& vk) noexcept
{
    return Counters::Snapshot{
        vk.VulkanFallbackToNullCount,
        vk.VulkanInitFailureCount,
        vk.VulkanValidationErrorCount,
        vk.VulkanOperationalGateFailureCount,
    };
}

[[nodiscard]] const Extrinsic::Graphics::RenderGraphCommandPassStats*
FindCommandPass(const Extrinsic::Graphics::RenderGraphFrameStats& stats,
                const std::string_view passName) noexcept
{
    const auto found = std::ranges::find_if(
        stats.CommandRecords.Passes,
        [passName](const auto& pass) { return pass.Name == passName; });
    return found == stats.CommandRecords.Passes.end() ? nullptr : &*found;
}

class UvViewSmokeApp final : public Intrinsic::Tests::RuntimeTestModule
{
public:
    UvViewSmokeApp()
    {
        BuildAsymmetricHeatmapMesh();
    }

    void Resolve() override
    {
        auto& engine   = Kernel();
        m_Engine       = &engine;
        m_ReadbackHook = engine.GetRenderer().RegisterRuntimeFrameCommandHook(
            [this](Extrinsic::RHI::ICommandContext& commandContext)
            {
                RecordReadback(commandContext);
            });
        if (!m_ReadbackHook.IsValid())
            m_Error = "Renderer rejected the UV-view runtime readback hook.";
    }


    void Frame(double, double) override
    {
        auto& engine = Kernel();
        ++m_Frames;
        if (!m_Error.empty())
        {
            engine.RequestExit();
            return;
        }

        if (!engine.GetDevice().IsOperational())
        {
            if (m_Frames >= kMaximumFrames)
            {
                m_TimedOut = true;
                engine.RequestExit();
            }
            return;
        }

        if (!m_ResourcesReady)
        {
            InitializeResources(engine);
            if (!m_Error.empty())
            {
                engine.RequestExit();
                return;
            }
        }

        const std::size_t currentModeIndex = ModeIndex(m_CurrentMode);
        if (static_cast<std::size_t>(m_ReadbackCopyCount) >
                currentModeIndex &&
            currentModeIndex + 1u < kUvViewSmokeModeCount)
        {
            m_CurrentMode = static_cast<UvViewSmokeMode>(currentModeIndex + 1u);
        }

        // The retained view uses frame-scoped submission semantics: repeat the
        // same token while a mode remains active, then advance with a distinct
        // token only after that mode's completed target has been copied.
        SubmitCurrentRequest(engine);

        const Extrinsic::Graphics::UvViewOutput output =
            engine.GetRenderer().GetUvViewOutput();
        switch (output.Status)
        {
        case Extrinsic::Graphics::UvViewStatus::InvalidRequest:
        case Extrinsic::Graphics::UvViewStatus::ResourceCreationFailed:
        case Extrinsic::Graphics::UvViewStatus::CpuFallbackNonOperational:
        case Extrinsic::Graphics::UvViewStatus::WaitingForGeometry:
            m_Error = "GPU UV view failed after an operational request: " +
                      output.Diagnostic;
            engine.RequestExit();
            return;
        case Extrinsic::Graphics::UvViewStatus::Disabled:
        case Extrinsic::Graphics::UvViewStatus::Ready:
            break;
        }

        if (m_ReadbackCopyCount ==
                static_cast<std::uint32_t>(kUvViewSmokeModeCount) &&
            m_Frames >= kMinimumFrames)
        {
            engine.RequestExit();
            return;
        }

        if (m_Frames >= kMaximumFrames)
        {
            m_TimedOut = true;
            engine.RequestExit();
        }
    }

    void Shutdown() override
    {
        auto& engine = Kernel();
        if (m_ReadbackHook.IsValid())
        {
            engine.GetRenderer().UnregisterRuntimeFrameCommandHook(
                m_ReadbackHook);
            m_ReadbackHook = {};
        }
        for (Extrinsic::RHI::BufferHandle& readback : m_ReadbackBuffers)
        {
            if (readback.IsValid())
            {
                engine.GetDevice().DestroyBuffer(readback);
                readback = {};
            }
        }
        if (m_TextureBackgroundBindlessIndex !=
            Extrinsic::RHI::kInvalidBindlessIndex)
        {
            engine.GetDevice().GetBindlessHeap().FreeSlot(
                m_TextureBackgroundBindlessIndex);
            engine.GetDevice().GetBindlessHeap().FlushPending();
            m_TextureBackgroundBindlessIndex =
                Extrinsic::RHI::kInvalidBindlessIndex;
        }
        if (m_TextureBackgroundSampler.IsValid())
        {
            engine.GetDevice().DestroySampler(m_TextureBackgroundSampler);
            m_TextureBackgroundSampler = {};
        }
        if (m_TextureBackground.IsValid())
        {
            engine.GetDevice().DestroyTexture(m_TextureBackground);
            m_TextureBackground = {};
        }
        m_Engine = nullptr;
    }

    [[nodiscard]] const std::string& Error() const noexcept { return m_Error; }
    [[nodiscard]] bool TimedOut() const noexcept { return m_TimedOut; }
    [[nodiscard]] std::uint32_t Frames() const noexcept { return m_Frames; }
    [[nodiscard]] std::uint32_t ReadbackCopyCount() const noexcept
    {
        return m_ReadbackCopyCount;
    }
    [[nodiscard]] Extrinsic::RHI::BufferHandle ReadbackBuffer(
        const UvViewSmokeMode mode) const noexcept
    {
        return m_ReadbackBuffers[ModeIndex(mode)];
    }
    [[nodiscard]] Extrinsic::RHI::TextureHandle CopiedTexture(
        const UvViewSmokeMode mode) const noexcept
    {
        return m_CopiedTextures[ModeIndex(mode)];
    }
    [[nodiscard]] const Extrinsic::Graphics::UvViewOutput& CapturedOutput(
        const UvViewSmokeMode mode) const noexcept
    {
        return m_CapturedOutputs[ModeIndex(mode)];
    }
    [[nodiscard]] Extrinsic::RHI::BindlessIndex
    TextureBackgroundBindlessIndex() const noexcept
    {
        return m_TextureBackgroundBindlessIndex;
    }

private:
    void BuildAsymmetricHeatmapMesh()
    {
        constexpr std::size_t kVertexCount =
            kHeatmapTriangleCount * 3u;
        m_Positions.reserve(kVertexCount);
        m_Texcoords.reserve(kVertexCount);
        m_SurfaceIndices.reserve(kVertexCount);
        m_LineIndices.reserve(kHeatmapTriangleCount * 6u);
        m_ConformalDistortion.assign(
            kConformalDistortionValues.begin(),
            kConformalDistortionValues.end());

        for (const auto& triangle : kHeatmapUvTriangles)
        {
            const std::uint32_t a =
                static_cast<std::uint32_t>(m_Texcoords.size());
            for (const glm::vec2 uv : triangle)
            {
                m_Positions.emplace_back(
                    uv.x - 0.5f, uv.y - 0.5f, 0.0f);
                m_Texcoords.push_back(uv);
            }
            const std::uint32_t b = a + 1u;
            const std::uint32_t c = a + 2u;
            m_SurfaceIndices.insert(m_SurfaceIndices.end(), {a, b, c});
            m_LineIndices.insert(m_LineIndices.end(), {a, b, b, c, c, a});
        }
    }

    void InitializeResources(Engine& engine)
    {
        constexpr std::array<const char*, kUvViewSmokeModeCount>
            kReadbackDebugNames{
                "UvViewGpuSmoke.CheckerHeatmapReadback",
                "UvViewGpuSmoke.TexelDensityReadback",
                "UvViewGpuSmoke.TextureReadback",
            };
        for (std::size_t index = 0u; index < kUvViewSmokeModeCount; ++index)
        {
            m_ReadbackBuffers[index] = engine.GetDevice().CreateBuffer({
                .SizeBytes = kReadbackBytes,
                .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
                .HostVisible = true,
                .DebugName = kReadbackDebugNames[index],
            });
            if (!m_ReadbackBuffers[index].IsValid())
            {
                m_Error = "Operational Vulkan device failed to allocate the UV-view "
                          "readback buffers.";
                return;
            }
        }

        Extrinsic::Graphics::GpuWorld& gpuWorld =
            engine.GetRenderer().GetGpuWorld();
        m_Geometry = gpuWorld.UploadGeometry({
            .PackedVertexBytes = {},
            .PositionBytes = std::as_bytes(
                std::span<const glm::vec3>{m_Positions}),
            .TexcoordBytes = std::as_bytes(
                std::span<const glm::vec2>{m_Texcoords}),
            .NormalBytes = {},
            .SurfaceIndices =
                std::span<const std::uint32_t>{m_SurfaceIndices},
            .LineIndices = {},
            .VertexCount = static_cast<std::uint32_t>(m_Positions.size()),
            .LocalBounds = {},
            .DebugName = "UvViewGpuSmoke.AsymmetricHeatmapMesh",
            .PackedVertexColors = {},
        });
        if (!m_Geometry.IsValid())
        {
            m_Error = "Operational GpuWorld rejected the asymmetric UV heatmap mesh "
                      "upload.";
            return;
        }

        m_TextureBackground = engine.GetDevice().CreateTexture({
            .Width = kTextureBackgroundWidth,
            .Height = kTextureBackgroundHeight,
            .DepthOrArrayLayers = 1u,
            .MipLevels = 1u,
            .Fmt = Extrinsic::RHI::Format::RGBA8_UNORM,
            .Dimension = Extrinsic::RHI::TextureDimension::Tex2D,
            .Usage = Extrinsic::RHI::TextureUsage::Sampled |
                     Extrinsic::RHI::TextureUsage::TransferDst,
            .InitialLayout = Extrinsic::RHI::TextureLayout::Undefined,
            .DebugName = "UvViewGpuSmoke.RealTextureBackground",
        });
        if (!m_TextureBackground.IsValid())
        {
            m_Error = "Operational Vulkan device failed to create the real UV-view "
                      "texture background.";
            return;
        }

        std::array<std::uint8_t,
                   kTextureBackgroundWidth * kTextureBackgroundHeight *
                       kPixelBytes>
            texturePixels{};
        for (std::uint32_t y = 0u; y < kTextureBackgroundHeight; ++y)
        {
            for (std::uint32_t x = 0u; x < kTextureBackgroundWidth; ++x)
            {
                const bool highU = x >= kTextureBackgroundWidth / 2u;
                const bool highV = y >= kTextureBackgroundHeight / 2u;
                const TextureRgba& color = highV
                    ? (highU ? kTextureHighUHighV : kTextureLowUHighV)
                    : (highU ? kTextureHighULowV : kTextureLowULowV);
                const std::size_t offset =
                    (static_cast<std::size_t>(y) *
                         kTextureBackgroundWidth +
                     x) *
                    kPixelBytes;
                for (std::size_t channel = 0u; channel < color.size();
                     ++channel)
                {
                    texturePixels[offset + channel] = color[channel];
                }
            }
        }
        engine.GetDevice().WriteTexture(
            m_TextureBackground,
            texturePixels.data(),
            texturePixels.size(),
            0u,
            0u);

        m_TextureBackgroundSampler = engine.GetDevice().CreateSampler({
            .MagFilter = Extrinsic::RHI::FilterMode::Nearest,
            .MinFilter = Extrinsic::RHI::FilterMode::Nearest,
            .MipFilter = Extrinsic::RHI::MipmapMode::Nearest,
            .AddressU = Extrinsic::RHI::AddressMode::Repeat,
            .AddressV = Extrinsic::RHI::AddressMode::Repeat,
            .AddressW = Extrinsic::RHI::AddressMode::Repeat,
            .DebugName = "UvViewGpuSmoke.RealTextureSampler",
        });
        if (!m_TextureBackgroundSampler.IsValid())
        {
            m_Error = "Operational Vulkan device failed to create the UV-view "
                      "texture sampler.";
            return;
        }

        m_TextureBackgroundBindlessIndex =
            engine.GetDevice().GetBindlessHeap().AllocateTextureSlot(
                m_TextureBackground,
                m_TextureBackgroundSampler);
        if (m_TextureBackgroundBindlessIndex ==
            Extrinsic::RHI::kInvalidBindlessIndex)
        {
            m_Error = "Operational Vulkan bindless heap rejected the real UV-view "
                      "texture background.";
            return;
        }
        engine.GetDevice().GetBindlessHeap().FlushPending();
        m_ResourcesReady = true;
    }

    void SubmitCurrentRequest(Engine& engine)
    {
        const std::size_t modeIndex = ModeIndex(m_CurrentMode);
        const bool checkerHeatmap =
            m_CurrentMode == UvViewSmokeMode::CheckerHeatmap;
        const bool textureBackground =
            m_CurrentMode == UvViewSmokeMode::Texture;
        engine.GetRenderer().SubmitUvViewRequest(
            Extrinsic::Graphics::UvViewRequest{
                .Enabled = true,
                .RequestToken = kRequestTokens[modeIndex],
                .Geometry = m_Geometry,
                .Width = kTargetWidth,
                .Height = kTargetHeight,
                .Bounds = {
                    .MinU = 0.12f,
                    .MinV = 0.12f,
                    .MaxU = 0.88f,
                    .MaxV = 0.88f,
                },
                .Background = kBackgroundModes[modeIndex],
                .BackgroundTexture = textureBackground
                    ? m_TextureBackgroundBindlessIndex
                    : Extrinsic::RHI::kInvalidBindlessIndex,
                .ShowDistortionHeatmap = checkerHeatmap,
                .LineIndices = m_LineIndices,
                .TriangleConformalDistortion = m_ConformalDistortion,
            });
    }

    void RecordReadback(Extrinsic::RHI::ICommandContext& commandContext)
    {
        const std::size_t modeIndex = ModeIndex(m_CurrentMode);
        if (m_Engine == nullptr ||
            !m_ReadbackBuffers[modeIndex].IsValid() ||
            m_ModeCaptured[modeIndex])
        {
            return;
        }

        const Extrinsic::Graphics::UvViewOutput output =
            m_Engine->GetRenderer().GetUvViewOutput();
        if (!output.IsGpuReady() || output.RecordedPassCount < 2u ||
            output.RequestToken != kRequestTokens[modeIndex] ||
            output.ActiveBackground != kBackgroundModes[modeIndex])
        {
            return;
        }

        commandContext.TextureBarrier(
            output.Texture,
            Extrinsic::RHI::TextureLayout::ShaderReadOnly,
            Extrinsic::RHI::TextureLayout::TransferSrc);
        commandContext.CopyTextureToBuffer(
            output.Texture,
            Extrinsic::RHI::TextureLayout::TransferSrc,
            0u,
            0u,
            m_ReadbackBuffers[modeIndex],
            0u,
            0u,
            0u,
            kTargetWidth,
            kTargetHeight);
        commandContext.TextureBarrier(
            output.Texture,
            Extrinsic::RHI::TextureLayout::TransferSrc,
            Extrinsic::RHI::TextureLayout::ShaderReadOnly);
        m_CopiedTextures[modeIndex] = output.Texture;
        m_CapturedOutputs[modeIndex] = output;
        m_ModeCaptured[modeIndex] = true;
        ++m_ReadbackCopyCount;
    }

    Engine* m_Engine = nullptr;
    Extrinsic::Graphics::RuntimeFrameCommandHookHandle m_ReadbackHook{};
    Extrinsic::Graphics::GpuGeometryHandle m_Geometry{};
    std::array<Extrinsic::RHI::BufferHandle, kUvViewSmokeModeCount>
        m_ReadbackBuffers{};
    std::array<Extrinsic::RHI::TextureHandle, kUvViewSmokeModeCount>
        m_CopiedTextures{};
    std::array<Extrinsic::Graphics::UvViewOutput, kUvViewSmokeModeCount>
        m_CapturedOutputs{};
    std::array<bool, kUvViewSmokeModeCount> m_ModeCaptured{};
    Extrinsic::RHI::TextureHandle m_TextureBackground{};
    Extrinsic::RHI::SamplerHandle m_TextureBackgroundSampler{};
    Extrinsic::RHI::BindlessIndex m_TextureBackgroundBindlessIndex{
        Extrinsic::RHI::kInvalidBindlessIndex};
    std::vector<glm::vec3> m_Positions;
    std::vector<glm::vec2> m_Texcoords;
    std::vector<std::uint32_t> m_SurfaceIndices;
    std::vector<std::uint32_t> m_LineIndices;
    std::vector<float> m_ConformalDistortion;
    std::string m_Error;
    std::uint32_t m_Frames = 0u;
    std::uint32_t m_ReadbackCopyCount = 0u;
    UvViewSmokeMode m_CurrentMode{UvViewSmokeMode::CheckerHeatmap};
    bool m_ResourcesReady = false;
    bool m_TimedOut = false;
};

struct UvViewSmokeBootstrap
{
    std::unique_ptr<Engine> EnginePtr;
    UvViewSmokeApp* App = nullptr;
    bool Skipped = false;
    std::string SkipReason;
};

[[nodiscard]] UvViewSmokeBootstrap BootstrapUvViewSmoke()
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        return UvViewSmokeBootstrap{
            .EnginePtr  = nullptr,
            .App        = nullptr,
            .Skipped    = true,
            .SkipReason = "GLFW could not initialize in this environment; "
                          "gpu;vulkan UV-view smoke is opt-in.",
        };
    }

    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
    config.Window.Title = "Intrinsic UV-view gpu;vulkan smoke";
    config.Window.Width = kTargetWidth;
    config.Window.Height = kTargetHeight;
    config.Window.Resizable = false;
    // This smoke is the Operational proof for a newly writable imported
    // target. Keep validation enabled so layout/barrier regressions are
    // reflected by the backend counter-stability assertion below.
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = false;

    auto app = std::make_unique<UvViewSmokeApp>();
    UvViewSmokeApp* appPtr = app.get();
    auto engine            = std::make_unique<Engine>(config);
    Intrinsic::Tests::AddRuntimeTestModule(*engine, std::move(app));
    engine->Initialize();

    const auto inputs = GetVulkanDeviceOperationalInputs(&engine->GetDevice());
    if (!inputs.LogicalDeviceReady || !inputs.SwapchainReady ||
        !inputs.CommandSyncReady)
    {
        engine->Shutdown();
        return UvViewSmokeBootstrap{
            .EnginePtr  = nullptr,
            .App        = nullptr,
            .Skipped    = true,
            .SkipReason = "Promoted Vulkan did not reach "
                          "logical-device/swapchain/command-sync readiness on this host.",
        };
    }

    return UvViewSmokeBootstrap{
        .EnginePtr = std::move(engine),
        .App = appPtr,
        .Skipped = false,
        .SkipReason = {},
    };
}

class EngineShutdownGuard
{
public:
    explicit EngineShutdownGuard(Engine& engine) noexcept : m_Engine(&engine) {}
    ~EngineShutdownGuard()
    {
        if (m_Engine != nullptr)
            m_Engine->Shutdown();
    }

    EngineShutdownGuard(const EngineShutdownGuard&) = delete;
    EngineShutdownGuard& operator=(const EngineShutdownGuard&) = delete;

private:
    Engine* m_Engine;
};

struct Rgba8Pixel
{
    std::uint8_t R = 0u;
    std::uint8_t G = 0u;
    std::uint8_t B = 0u;
    std::uint8_t A = 0u;
};

[[nodiscard]] Rgba8Pixel PixelAt(const std::span<const std::uint8_t> pixels,
                                 const std::uint32_t x,
                                 const std::uint32_t y)
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * kTargetWidth + x) * kPixelBytes;
    return Rgba8Pixel{
        .R = pixels[offset + 0u],
        .G = pixels[offset + 1u],
        .B = pixels[offset + 2u],
        .A = pixels[offset + 3u],
    };
}

[[nodiscard]] bool PixelNear(const Rgba8Pixel actual,
                             const Rgba8Pixel expected,
                             const int tolerance = 4) noexcept
{
    const auto near = [tolerance](const std::uint8_t a,
                                  const std::uint8_t b) noexcept
    {
        return std::abs(static_cast<int>(a) - static_cast<int>(b)) <=
               tolerance;
    };
    return near(actual.R, expected.R) && near(actual.G, expected.G) &&
           near(actual.B, expected.B) && near(actual.A, expected.A);
}

struct TargetPixel
{
    std::uint32_t X = 0u;
    std::uint32_t Y = 0u;
};

[[nodiscard]] TargetPixel TargetPixelForUv(const glm::vec2 uv) noexcept
{
    // The request bounds stay inside [0,1], so UvView fits the included unit
    // square around center .5 with its production 5% padding (half extent
    // .525). Vulkan's negative-height viewport maps positive UV Y upward.
    constexpr float kUvCenter = 0.5f;
    constexpr float kUvHalfExtent = 0.525f;
    const float ndcX = (uv.x - kUvCenter) / kUvHalfExtent;
    const float ndcY = (uv.y - kUvCenter) / kUvHalfExtent;
    const long pixelX = std::lround(
        (ndcX + 1.0f) * 0.5f * static_cast<float>(kTargetWidth) -
        0.5f);
    const long pixelY = std::lround(
        (1.0f - ndcY) * 0.5f * static_cast<float>(kTargetHeight) -
        0.5f);
    return TargetPixel{
        .X = static_cast<std::uint32_t>(std::clamp(
            pixelX, 0l, static_cast<long>(kTargetWidth - 1u))),
        .Y = static_cast<std::uint32_t>(std::clamp(
            pixelY, 0l, static_cast<long>(kTargetHeight - 1u))),
    };
}

[[nodiscard]] constexpr Rgba8Pixel PixelFromTextureRgba(
    const TextureRgba& color) noexcept
{
    return Rgba8Pixel{
        .R = color[0],
        .G = color[1],
        .B = color[2],
        .A = color[3],
    };
}
} // namespace

TEST(UvViewGpuSmoke, RetainedBackgroundModesReadBackOnOperationalVulkan)
{
    auto bootstrap = BootstrapUvViewSmoke();
    if (bootstrap.Skipped)
        GTEST_SKIP() << bootstrap.SkipReason;

    Engine& engine = *bootstrap.EnginePtr;
    UvViewSmokeApp& app = *bootstrap.App;
    EngineShutdownGuard shutdownGuard{engine};

    const Counters::Snapshot before =
        ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    engine.Run();
    const Counters::Snapshot after =
        ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());

    const auto operationalStatus =
        EvaluateVulkanDeviceOperationalStatus(&engine.GetDevice());
    if (!engine.GetDevice().IsOperational())
    {
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during "
                         "UV-view frames: status="
                      << ToString(operationalStatus.Code)
                      << " reason=" << ToString(operationalStatus.Reason);
        return;
    }
    EXPECT_EQ(
        operationalStatus.Code,
        Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(operationalStatus.Reason,
              Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

    ASSERT_TRUE(app.Error().empty()) << app.Error();
    EXPECT_FALSE(app.TimedOut())
        << "UV-view smoke exceeded its " << kMaximumFrames << "-frame bound.";
    EXPECT_GE(app.Frames(), kMinimumFrames);
    EXPECT_LE(app.Frames(), kMaximumFrames);

    const Extrinsic::Graphics::UvViewOutput finalOutput =
        engine.GetRenderer().GetUvViewOutput();
    ASSERT_TRUE(finalOutput.IsGpuReady()) << finalOutput.Diagnostic;
    EXPECT_EQ(finalOutput.RequestToken,
              kRequestTokens[ModeIndex(UvViewSmokeMode::Texture)]);
    EXPECT_EQ(finalOutput.ActiveBackground,
              Extrinsic::Graphics::UvViewBackgroundMode::Texture);

    EXPECT_EQ(app.ReadbackCopyCount(),
              static_cast<std::uint32_t>(kUvViewSmokeModeCount));
    const auto assertCapturedMode =
        [&](const UvViewSmokeMode mode, const bool heatmapExpected)
        {
            const std::size_t modeIndex = ModeIndex(mode);
            const Extrinsic::Graphics::UvViewOutput& output =
                app.CapturedOutput(mode);
            EXPECT_TRUE(output.IsGpuReady()) << output.Diagnostic;
            EXPECT_EQ(output.Status, Extrinsic::Graphics::UvViewStatus::Ready);
            EXPECT_EQ(output.ActiveMode,
                      Extrinsic::Graphics::UvViewActiveMode::GpuShaded);
            EXPECT_EQ(output.RequestToken, kRequestTokens[modeIndex]);
            EXPECT_EQ(output.RequestedBackground,
                      kBackgroundModes[modeIndex]);
            EXPECT_EQ(output.ActiveBackground, kBackgroundModes[modeIndex]);
            EXPECT_EQ(output.HeatmapActive, heatmapExpected);
            EXPECT_TRUE(output.Texture.IsValid());
            EXPECT_NE(output.BindlessIndex,
                      Extrinsic::RHI::kInvalidBindlessIndex);
            EXPECT_EQ(output.Width, kTargetWidth);
            EXPECT_EQ(output.Height, kTargetHeight);
            EXPECT_GE(output.TargetGeneration, 1u);
            EXPECT_GE(output.RecordedPassCount, 2u);
            EXPECT_TRUE(app.ReadbackBuffer(mode).IsValid());
            EXPECT_EQ(app.CopiedTexture(mode), output.Texture);
        };
    assertCapturedMode(UvViewSmokeMode::CheckerHeatmap, true);
    assertCapturedMode(UvViewSmokeMode::TexelDensity, false);
    assertCapturedMode(UvViewSmokeMode::Texture, false);

    EXPECT_NE(app.TextureBackgroundBindlessIndex(),
              Extrinsic::RHI::kInvalidBindlessIndex);
    EXPECT_LT(
        app.CapturedOutput(UvViewSmokeMode::CheckerHeatmap).RecordedPassCount,
        app.CapturedOutput(UvViewSmokeMode::TexelDensity).RecordedPassCount);
    EXPECT_LT(
        app.CapturedOutput(UvViewSmokeMode::TexelDensity).RecordedPassCount,
        app.CapturedOutput(UvViewSmokeMode::Texture).RecordedPassCount);
    EXPECT_EQ(
        app.CapturedOutput(UvViewSmokeMode::CheckerHeatmap).Texture,
        app.CapturedOutput(UvViewSmokeMode::TexelDensity).Texture);
    EXPECT_EQ(
        app.CapturedOutput(UvViewSmokeMode::TexelDensity).Texture,
        app.CapturedOutput(UvViewSmokeMode::Texture).Texture);

    const auto& stats = engine.GetRenderer().GetLastRenderGraphStats();
    ASSERT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    ASSERT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);
    const auto* uvPass = FindCommandPass(stats, "UvViewPass");
    ASSERT_NE(uvPass, nullptr)
        << "Enabled UV view was absent from the compiled default recipe.";
    EXPECT_EQ(uvPass->Status, RenderCommandPassStatus::Recorded)
        << "UvViewPass did not record on the operational Vulkan command stream.";

    engine.GetDevice().WaitIdle();
    const auto readCapturedPixels =
        [&](const UvViewSmokeMode mode)
        {
            std::vector<std::uint8_t> pixels(
                static_cast<std::size_t>(kReadbackBytes), 0u);
            engine.GetDevice().ReadBuffer(
                app.ReadbackBuffer(mode),
                pixels.data(),
                kReadbackBytes,
                0u);
            return pixels;
        };
    const std::vector<std::uint8_t> checkerPixels =
        readCapturedPixels(UvViewSmokeMode::CheckerHeatmap);
    const std::vector<std::uint8_t> texelDensityPixels =
        readCapturedPixels(UvViewSmokeMode::TexelDensity);
    const std::vector<std::uint8_t> texturePixels =
        readCapturedPixels(UvViewSmokeMode::Texture);

    constexpr std::array<Rgba8Pixel, kHeatmapTriangleCount>
        kExpectedHeatmapColors{{
        {20u, 97u, 242u, 255u},
        {245u, 224u, 31u, 255u},
        {224u, 15u, 10u, 255u},
        {122u, 20u, 158u, 255u},
    }};
    for (std::size_t triangleIndex = 0u;
         triangleIndex < kHeatmapUvTriangles.size();
         ++triangleIndex)
    {
        const auto& triangle = kHeatmapUvTriangles[triangleIndex];
        const glm::vec2 centroid =
            (triangle[0] + triangle[1] + triangle[2]) / 3.0f;
        const TargetPixel probe = TargetPixelForUv(centroid);
        const Rgba8Pixel pixel =
            PixelAt(checkerPixels, probe.X, probe.Y);
        EXPECT_TRUE(PixelNear(pixel, kExpectedHeatmapColors[triangleIndex], 6))
            << "Heatmap triangle " << triangleIndex
            << " centroid UV=(" << centroid.x << "," << centroid.y
            << ") target pixel=(" << probe.X << "," << probe.Y
            << ") read RGBA=(" << static_cast<int>(pixel.R) << ","
            << static_cast<int>(pixel.G) << ","
            << static_cast<int>(pixel.B) << ","
            << static_cast<int>(pixel.A)
            << "); expected the low/mid/high/invalid face-aligned ramp color.";
    }

    constexpr Rgba8Pixel kCheckerDark{31u, 33u, 41u, 255u};
    constexpr Rgba8Pixel kCheckerLight{71u, 77u, 87u, 255u};
    constexpr std::array<std::array<std::uint32_t, 2u>, 4u> kCornerProbes{{
        {{4u, 4u}},
        {{kTargetWidth - 5u, 4u}},
        {{4u, kTargetHeight - 5u}},
        {{kTargetWidth - 5u, kTargetHeight - 5u}},
    }};
    std::uint32_t darkProbeCount = 0u;
    std::uint32_t lightProbeCount = 0u;
    for (const auto& probe : kCornerProbes)
    {
        const Rgba8Pixel pixel =
            PixelAt(checkerPixels, probe[0], probe[1]);
        const bool dark = PixelNear(pixel, kCheckerDark);
        const bool light = PixelNear(pixel, kCheckerLight);
        EXPECT_TRUE(dark || light)
            << "Checker background probe at (" << probe[0] << "," << probe[1]
            << ") read RGBA=(" << static_cast<int>(pixel.R) << ","
            << static_cast<int>(pixel.G) << ","
            << static_cast<int>(pixel.B) << ","
            << static_cast<int>(pixel.A) << ").";
        darkProbeCount += dark ? 1u : 0u;
        lightProbeCount += light ? 1u : 0u;
    }
    EXPECT_EQ(darkProbeCount, 2u);
    EXPECT_EQ(lightProbeCount, 2u);

    std::uint64_t checkerPixelCount = 0u;
    std::uint64_t geometryPixelCount = 0u;
    for (std::uint32_t y = 0u; y < kTargetHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kTargetWidth; ++x)
        {
            const Rgba8Pixel pixel = PixelAt(checkerPixels, x, y);
            if (PixelNear(pixel, kCheckerDark) ||
                PixelNear(pixel, kCheckerLight))
            {
                ++checkerPixelCount;
            }
            else
            {
                ++geometryPixelCount;
            }
        }
    }
    constexpr std::uint64_t kPixelCount =
        static_cast<std::uint64_t>(kTargetWidth) * kTargetHeight;
    EXPECT_GT(checkerPixelCount, kPixelCount / 5u)
        << "Retained target did not preserve a substantial checker background.";
    EXPECT_GT(geometryPixelCount, kPixelCount / 16u)
        << "Retained target did not contain the asymmetric shaded-triangle "
           "footprint.";

    // Texel-density mode is a high-frequency blue/orange reference pattern.
    // Count both color families only in the target border, outside every
    // authored UV triangle, so mesh fill cannot satisfy the assertion.
    std::uint64_t texelDensityBluePixels = 0u;
    std::uint64_t texelDensityOrangePixels = 0u;
    constexpr std::uint32_t kBackgroundBorderWidth = 10u;
    for (std::uint32_t y = 0u; y < kTargetHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kTargetWidth; ++x)
        {
            const bool inBackgroundBorder =
                x < kBackgroundBorderWidth ||
                x >= kTargetWidth - kBackgroundBorderWidth ||
                y < kBackgroundBorderWidth ||
                y >= kTargetHeight - kBackgroundBorderWidth;
            if (!inBackgroundBorder)
            {
                continue;
            }

            const Rgba8Pixel pixel = PixelAt(texelDensityPixels, x, y);
            if (static_cast<int>(pixel.B) >
                    static_cast<int>(pixel.R) + 50 &&
                static_cast<int>(pixel.B) >
                    static_cast<int>(pixel.G) + 20)
            {
                ++texelDensityBluePixels;
            }
            if (static_cast<int>(pixel.R) >
                    static_cast<int>(pixel.B) + 50 &&
                static_cast<int>(pixel.R) >
                    static_cast<int>(pixel.G) + 20)
            {
                ++texelDensityOrangePixels;
            }
        }
    }
    EXPECT_GT(texelDensityBluePixels, kPixelCount / 32u)
        << "Texel-density background did not render its blue cell family outside "
           "the mesh.";
    EXPECT_GT(texelDensityOrangePixels, kPixelCount / 32u)
        << "Texel-density background did not render its orange cell family "
           "outside the mesh.";

    // Texture mode samples a real uploaded RGBA8 texture through a real
    // bindless slot. Probe known UVs in all four colored quadrants; their pane
    // locations make U/V flips, swaps, and slot-zero fallback observable.
    struct TextureQuadrantProbe
    {
        glm::vec2 Uv{0.0f};
        Rgba8Pixel Expected{};
        const char* Label = nullptr;
    };
    const std::array<TextureQuadrantProbe, 4u> textureQuadrantProbes{{
        {{0.04f, 0.96f},
         PixelFromTextureRgba(kTextureLowUHighV),
         "top-left / low-U high-V"},
        {{0.96f, 0.96f},
         PixelFromTextureRgba(kTextureHighUHighV),
         "top-right / high-U high-V"},
        {{0.04f, 0.04f},
         PixelFromTextureRgba(kTextureLowULowV),
         "bottom-left / low-U low-V"},
        {{0.96f, 0.04f},
         PixelFromTextureRgba(kTextureHighULowV),
         "bottom-right / high-U low-V"},
    }};
    for (const TextureQuadrantProbe& quadrant : textureQuadrantProbes)
    {
        const TargetPixel probe = TargetPixelForUv(quadrant.Uv);
        const Rgba8Pixel pixel =
            PixelAt(texturePixels, probe.X, probe.Y);
        EXPECT_TRUE(PixelNear(pixel, quadrant.Expected))
            << "Real texture quadrant " << quadrant.Label << " at UV=("
            << quadrant.Uv.x << "," << quadrant.Uv.y
            << ") target pixel=(" << probe.X << "," << probe.Y
            << ") read RGBA=("
            << static_cast<int>(pixel.R) << ","
            << static_cast<int>(pixel.G) << ","
            << static_cast<int>(pixel.B) << ","
            << static_cast<int>(pixel.A) << ").";
    }
    std::uint64_t realTexturePixelCount = 0u;
    for (std::uint32_t y = 0u; y < kTargetHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kTargetWidth; ++x)
        {
            const Rgba8Pixel pixel = PixelAt(texturePixels, x, y);
            const bool matchesAQuadrant = std::ranges::any_of(
                textureQuadrantProbes,
                [pixel](const TextureQuadrantProbe& quadrant)
                {
                    return PixelNear(pixel, quadrant.Expected);
                });
            if (matchesAQuadrant)
            {
                ++realTexturePixelCount;
            }
        }
    }
    EXPECT_GT(realTexturePixelCount, kPixelCount / 3u)
        << "Real four-quadrant bindless texture did not cover a substantial "
           "retained-target region.";

    EXPECT_TRUE(Counters::IsStable(before, after))
        << "Vulkan operational counters changed across UV-view rendering: "
        << "fallbackToNull " << before.FallbackToNull << " -> "
        << after.FallbackToNull << ", initFailure " << before.InitFailure
        << " -> " << after.InitFailure << ", validationError "
        << before.ValidationError << " -> " << after.ValidationError
        << ", gateFailure " << before.OperationalGateFailure << " -> "
        << after.OperationalGateFailure;
}
