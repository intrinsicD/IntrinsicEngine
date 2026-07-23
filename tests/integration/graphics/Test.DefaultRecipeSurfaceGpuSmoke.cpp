#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include "MinimalTriangleReadback.hpp"
#include "OperationalCounterStability.hpp"

#include "RuntimeTestModule.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Telemetry;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Profiler;
import Extrinsic.RHI.QueueAffinity;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;
import Extrinsic.Runtime.RenderArtifactPublication;

namespace
{
namespace Readback = Extrinsic::Tests::Support::MinimalTriangleReadback;
namespace Counters = Extrinsic::Tests::Support::OperationalCounterStability;

using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
using Extrinsic::Backends::Vulkan::GetVulkanBootstrapDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs;
using Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::ToString;
using Extrinsic::Graphics::RenderCommandPassStatus;
using Extrinsic::Runtime::Engine;

[[nodiscard]] Readback::ExpectedPixel ReorderToRgba(
    const Extrinsic::RHI::Format format,
    const std::uint8_t b0,
    const std::uint8_t b1,
    const std::uint8_t b2,
    const std::uint8_t b3) noexcept
{
    switch (format)
    {
    case Extrinsic::RHI::Format::BGRA8_UNORM:
    case Extrinsic::RHI::Format::BGRA8_SRGB:
        return Readback::ExpectedPixel{.R = b2, .G = b1, .B = b0, .A = b3};
    case Extrinsic::RHI::Format::RGBA8_UNORM:
    case Extrinsic::RHI::Format::RGBA8_SRGB:
    default:
        return Readback::ExpectedPixel{.R = b0, .G = b1, .B = b2, .A = b3};
    }
}

[[nodiscard]] constexpr bool IsSrgbFormat(const Extrinsic::RHI::Format format) noexcept
{
    return format == Extrinsic::RHI::Format::RGBA8_SRGB ||
           format == Extrinsic::RHI::Format::BGRA8_SRGB;
}

[[nodiscard]] std::uint8_t SrgbByteToLinearByte(const std::uint8_t srgb) noexcept
{
    const float s = static_cast<float>(srgb) / 255.0f;
    const float linear = (s <= 0.04045f)
                             ? (s / 12.92f)
                             : std::pow((s + 0.055f) / 1.055f, 2.4f);
    const float clamped = linear < 0.0f ? 0.0f : (linear > 1.0f ? 1.0f : linear);
    return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
}

[[nodiscard]] Readback::ExpectedPixel SrgbToLinearPixel(
    const Extrinsic::RHI::Format format,
    const Readback::ExpectedPixel& srgbPixel) noexcept
{
    if (!IsSrgbFormat(format))
    {
        return srgbPixel;
    }
    return Readback::ExpectedPixel{
        .R = SrgbByteToLinearByte(srgbPixel.R),
        .G = SrgbByteToLinearByte(srgbPixel.G),
        .B = SrgbByteToLinearByte(srgbPixel.B),
        .A = srgbPixel.A,
    };
}

// GRAPHICS-076 Slice D — bounded `engine.Run()` driver. The smoke drives a
// small fixed number of frames so the test cannot hang on a misconfigured
// swapchain loop even when the operational Vulkan gate flips green.
class ExitAfterFramesApp final : public Intrinsic::Tests::RuntimeTestModule
{
public:
    explicit ExitAfterFramesApp(const std::uint32_t targetFrames) noexcept
        : m_TargetFrames(targetFrames)
    {
    }

    void Resolve() override {}

    void Frame(double, double) override
    {
        auto& engine = Kernel();
        ++m_Frames;
        if (m_Frames >= m_TargetFrames)
        {
            engine.RequestExit();
        }
    }

    void Shutdown() override {}

private:
    std::uint32_t m_TargetFrames{1u};
    std::uint32_t m_Frames{0u};
};

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

struct DefaultRecipeBootstrap
{
    std::unique_ptr<Engine> EnginePtr;
    bool Skipped{false};
    std::string SkipReason;
};

[[nodiscard]] DefaultRecipeBootstrap BootstrapEngineForDefaultRecipe(
    const std::uint32_t targetFrames = 4u,
    const char* const windowTitle = "Intrinsic Default-recipe gpu;vulkan smoke",
    const bool enableValidation = false,
    const bool enableGpuProfiling = false)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        return DefaultRecipeBootstrap{
            .EnginePtr  = nullptr,
            .Skipped    = true,
            .SkipReason = "GLFW could not initialize in this environment; "
                          "gpu;vulkan default-recipe smoke is opt-in.",
        };
    }

    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
    config.Window.Title = windowTitle;
    // Fixed framebuffer keeps backbuffer sizing and format negotiation stable
    // for readback sample points across hosts.
    config.Window.Width = Readback::kFramebufferWidth;
    config.Window.Height = Readback::kFramebufferHeight;
    config.Window.Resizable = false;
    config.Render.EnableValidation = enableValidation;
    config.Render.EnableVSync = false;
    config.Render.EnableGpuProfiling = enableGpuProfiling;
    auto enginePtr                   = std::make_unique<Engine>(config);
    Intrinsic::Tests::AddRuntimeTestModule(*enginePtr,
                                           std::make_unique<ExitAfterFramesApp>(targetFrames));
    enginePtr->Initialize();

    const auto initInputs = GetVulkanDeviceOperationalInputs(&enginePtr->GetDevice());
    if (!initInputs.LogicalDeviceReady || !initInputs.SwapchainReady || !initInputs.CommandSyncReady)
    {
        enginePtr->Shutdown();
        return DefaultRecipeBootstrap{
            .EnginePtr  = nullptr,
            .Skipped    = true,
            .SkipReason = "Promoted Vulkan did not reach "
                          "logical-device/swapchain/command-sync readiness on this host.",
        };
    }

    return DefaultRecipeBootstrap{.EnginePtr = std::move(enginePtr), .Skipped = false, .SkipReason = {}};
}

struct DefaultRecipeRunCapture
{
    Counters::Snapshot Before{};
    Counters::Snapshot After{};
    Extrinsic::Backends::Vulkan::VulkanOperationalStatus Status{};
    Extrinsic::Graphics::RenderGraphFrameStats Stats{};
    bool DeviceOperational{false};
    bool ProfileCandidateSubmitted{false};
    std::uint64_t ProfileCandidateFrameNumber{0u};
    std::uint32_t ProfileCandidateFrameSlot{0u};
};

[[nodiscard]] DefaultRecipeRunCapture DriveDefaultRecipeAndCapture(Engine& engine)
{
    DefaultRecipeRunCapture capture;
    capture.Before = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    engine.Run();
    capture.Status = EvaluateVulkanDeviceOperationalStatus(&engine.GetDevice());
    capture.DeviceOperational = engine.GetDevice().IsOperational();
    capture.Stats = engine.GetRenderer().GetLastRenderGraphStats();
    capture.After = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    return capture;
}

[[nodiscard]] bool SubmitReadbackTriangle(Extrinsic::Graphics::IRenderer& renderer);

[[nodiscard]] DefaultRecipeRunCapture DriveDefaultRecipeDebugViewFrameAndCapture(
    Engine& engine,
    const bool seedReadbackTriangle = false,
    const bool enableGpuProfiling = false)
{
    DefaultRecipeRunCapture capture;
    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();

    renderer.SetDebugViewRequestedResourceName("SceneColorHDR");
    capture.Before = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());

    Extrinsic::RHI::FrameHandle frame{};
    capture.ProfileCandidateFrameNumber = device.GetGlobalFrameNumber();
    if (!renderer.BeginFrame(frame))
    {
        capture.Status = EvaluateVulkanDeviceOperationalStatus(&device);
        capture.DeviceOperational = device.IsOperational();
        capture.Stats = renderer.GetLastRenderGraphStats();
        capture.After = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
        return capture;
    }
    capture.ProfileCandidateFrameSlot = frame.FrameIndex;

    if (seedReadbackTriangle)
    {
        (void)SubmitReadbackTriangle(renderer);
    }

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = Readback::kFramebufferWidth,
                     .Height = Readback::kFramebufferHeight},
        .DebugOverlayEnabled = true,
        .EnableGpuProfiling = enableGpuProfiling,
    };
    Extrinsic::Graphics::RenderWorld world = renderer.ExtractRenderWorld(input);
    renderer.PrepareFrame(world);
    renderer.ExecuteFrame(frame, world);
    const std::uint64_t completedFrameNumber =
        renderer.EndFrame(frame);
    capture.ProfileCandidateSubmitted =
        completedFrameNumber > capture.ProfileCandidateFrameNumber;
    device.Present(frame);

    capture.Status = EvaluateVulkanDeviceOperationalStatus(&device);
    capture.DeviceOperational = device.IsOperational();
    capture.Stats = renderer.GetLastRenderGraphStats();
    capture.After = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    return capture;
}

[[nodiscard]] bool SubmitReadbackTriangle(Extrinsic::Graphics::IRenderer& renderer)
{
    static const std::array<Extrinsic::Graphics::DebugTrianglePacket, 1> kTriangles{{
        Extrinsic::Graphics::DebugTrianglePacket{
            .A = glm::vec3{Readback::kTriangleNdc[0][0], Readback::kTriangleNdc[0][1], 0.0f},
            .B = glm::vec3{Readback::kTriangleNdc[1][0], Readback::kTriangleNdc[1][1], 0.0f},
            .C = glm::vec3{Readback::kTriangleNdc[2][0], Readback::kTriangleNdc[2][1], 0.0f},
            .Color = glm::vec4{Readback::kTriangleR,
                               Readback::kTriangleG,
                               Readback::kTriangleB,
                               Readback::kTriangleA},
            .DepthTested = false,
        },
    }};
    renderer.SubmitRuntimeSnapshots(Extrinsic::Graphics::RuntimeRenderSnapshotBatch{
        .DebugTriangles = std::span<const Extrinsic::Graphics::DebugTrianglePacket>{kTriangles.data(), kTriangles.size()},
    });
    return true;
}

[[nodiscard]] RenderCommandPassStatus FindPassStatus(
    const Extrinsic::Graphics::RenderGraphFrameStats& stats,
    const std::string_view passName) noexcept
{
    for (const auto& pass : stats.CommandRecords.Passes)
    {
        if (pass.Name == passName)
        {
            return pass.Status;
        }
    }
    // The executor only enters `CommandRecords.Passes` for passes the recipe
    // declared. Returning `SkippedNonOperational` here cannot collide with a
    // "really skipped" entry because a missing pass is a recipe shape miss,
    // not an executor decision; the caller asserts the canonical pass name is
    // present before checking the status.
    return RenderCommandPassStatus::SkippedNonOperational;
}

[[nodiscard]] bool ContainsPass(
    const Extrinsic::Graphics::RenderGraphFrameStats& stats,
    const std::string_view passName) noexcept
{
    return std::any_of(
        stats.CommandRecords.Passes.begin(),
        stats.CommandRecords.Passes.end(),
        [passName](const auto& pass) { return pass.Name == passName; });
}

[[nodiscard]] const Extrinsic::Graphics::RenderArtifactMetadata* FindDeclaredArtifact(
    const Extrinsic::Graphics::RenderGraphFrameStats& stats,
    const std::string_view purpose) noexcept
{
    const auto found = std::find_if(
        stats.Contract.DeclaredArtifacts.begin(),
        stats.Contract.DeclaredArtifacts.end(),
        [purpose](const Extrinsic::Graphics::RenderArtifactMetadata& artifact)
        {
            return artifact.Purpose == purpose;
        });
    return found != stats.Contract.DeclaredArtifacts.end() ? &*found : nullptr;
}

[[nodiscard]] std::string BuildPassStatusSummary(
    const Extrinsic::Graphics::RenderGraphFrameStats& stats)
{
    std::string summary;
    for (const auto& pass : stats.CommandRecords.Passes)
    {
        if (!summary.empty())
        {
            summary += ", ";
        }
        summary += pass.Name;
        summary += "=";
        switch (pass.Status)
        {
        case RenderCommandPassStatus::Recorded:
            summary += "Recorded";
            break;
        case RenderCommandPassStatus::SkippedNonOperational:
            summary += "SkippedNonOperational";
            break;
        case RenderCommandPassStatus::SkippedUnavailable:
            summary += "SkippedUnavailable";
            break;
        }
    }
    return summary;
}

template <typename DeviceT>
void ExpectMinimalHarnessReadbackSamples(
    DeviceT& device,
    const Extrinsic::RHI::BufferHandle readbackBuffer,
    const std::uint64_t readbackSize,
    const std::uint32_t bytesPerPixel,
    const Extrinsic::RHI::Format backbufferFormat,
    const Extrinsic::Graphics::RenderGraphFrameStats& stats)
{
    static_assert(Readback::kSamplePoints.size() == 4u,
                  "gpu;vulkan pixel readback requires exactly four deterministic "
                  "sample points");

    std::vector<std::uint8_t> readbackBytes(static_cast<std::size_t>(readbackSize), 0u);
    device.ReadBuffer(readbackBuffer, readbackBytes.data(), readbackSize, 0u);

    const std::uint64_t rowStride =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(Readback::kFramebufferWidth);

    for (const Readback::SamplePoint& sample : Readback::kSamplePoints)
    {
        const std::uint64_t pixelOffset =
            static_cast<std::uint64_t>(sample.PixelY) * rowStride +
            static_cast<std::uint64_t>(sample.PixelX) * static_cast<std::uint64_t>(bytesPerPixel);
        ASSERT_LE(pixelOffset + 4u, readbackSize)
            << "Sample point " << sample.Label << " is outside the readback buffer.";

        const std::uint8_t b0 = readbackBytes[static_cast<std::size_t>(pixelOffset + 0u)];
        const std::uint8_t b1 = readbackBytes[static_cast<std::size_t>(pixelOffset + 1u)];
        const std::uint8_t b2 = readbackBytes[static_cast<std::size_t>(pixelOffset + 2u)];
        const std::uint8_t b3 = readbackBytes[static_cast<std::size_t>(pixelOffset + 3u)];

        const Readback::ExpectedPixel actualSrgb = ReorderToRgba(backbufferFormat, b0, b1, b2, b3);
        const Readback::ExpectedPixel actualLinear = SrgbToLinearPixel(backbufferFormat, actualSrgb);
        const Readback::ExpectedPixel expected = Readback::ExpectedAt(sample);
        EXPECT_TRUE(Readback::ChannelsWithinTolerance(expected, actualLinear))
            << "Sample " << sample.Label
            << " (pixel " << sample.PixelX << "," << sample.PixelY
            << ", inside=" << (sample.InsideTriangle ? "true" : "false") << ")"
            << " expected linear RGBA=("
            << static_cast<int>(expected.R) << ","
            << static_cast<int>(expected.G) << ","
            << static_cast<int>(expected.B) << ","
            << static_cast<int>(expected.A) << ")"
            << " actual linear RGBA=("
            << static_cast<int>(actualLinear.R) << ","
            << static_cast<int>(actualLinear.G) << ","
            << static_cast<int>(actualLinear.B) << ","
            << static_cast<int>(actualLinear.A) << ")"
            << " raw bytes=("
            << static_cast<int>(b0) << ","
            << static_cast<int>(b1) << ","
            << static_cast<int>(b2) << ","
            << static_cast<int>(b3) << ")"
            << " backbuffer format=" << static_cast<int>(backbufferFormat)
            << " pass statuses=[" << BuildPassStatusSummary(stats) << "]"
            << "\nrender graph debug dump:\n" << stats.DebugDump;
    }
}

[[nodiscard]] std::vector<std::uint8_t> ReadBackbufferBytes(
    Extrinsic::RHI::IDevice& device,
    const Extrinsic::RHI::BufferHandle readbackBuffer,
    const std::uint64_t readbackSize)
{
    std::vector<std::uint8_t> readbackBytes(static_cast<std::size_t>(readbackSize), 0u);
    device.ReadBuffer(readbackBuffer, readbackBytes.data(), readbackSize, 0u);
    return readbackBytes;
}

void ExpectReadbackImagesEqual(
    const std::span<const std::uint8_t> fallbackBytes,
    const std::span<const std::uint8_t> aliasBytes,
    const std::uint32_t bytesPerPixel,
    const std::string_view fallbackDebugDump,
    const std::string_view aliasDebugDump)
{
    ASSERT_EQ(fallbackBytes.size(), aliasBytes.size());
    for (std::size_t i = 0; i < fallbackBytes.size(); ++i)
    {
        if (fallbackBytes[i] == aliasBytes[i])
        {
            continue;
        }
        const std::size_t pixelIndex = bytesPerPixel == 0u ? 0u : i / bytesPerPixel;
        ADD_FAILURE() << "Aliasing-on readback differs from aliasing-off readback at byte "
                      << i << " (pixel index " << pixelIndex << ", channel "
                      << (bytesPerPixel == 0u ? 0u : i % bytesPerPixel)
                      << "): off=" << static_cast<int>(fallbackBytes[i])
                      << " on=" << static_cast<int>(aliasBytes[i])
                      << "\naliasing-off debug dump:\n" << fallbackDebugDump
                      << "\naliasing-on debug dump:\n" << aliasDebugDump;
        return;
    }
}

void ExpectDefaultRecipeDebugViewReadbackRecorded(
    const Extrinsic::Graphics::RenderGraphFrameStats& stats)
{
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);
    EXPECT_EQ(FindPassStatus(stats, "DebugViewPass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(stats);
    EXPECT_EQ(FindPassStatus(stats, "DepthPrepass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(stats);
    EXPECT_EQ(FindPassStatus(stats, "SurfacePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(stats);
    EXPECT_EQ(FindPassStatus(stats, "TransientDebugSurfacePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(stats);
    EXPECT_EQ(FindPassStatus(stats, "Present"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(stats);
    EXPECT_GE(stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Default-recipe readback triplet did not record on any operational "
           "frame.";
}

[[nodiscard]] Extrinsic::Graphics::FrameRecipeOverride MakeGraphicsOnlyFrameRecipeOverride()
{
    return Extrinsic::Graphics::FrameRecipeOverride{
        .Recipe = Extrinsic::Graphics::RenderRecipeDescriptor{
            .RecipeId = "graphics-119.parallel-vulkan-smoke",
        },
        .DisabledExtensionSlots = {"lighting", "postprocess"},
        .SourceId = "GRAPHICS-119",
    };
}

struct ReadbackRunCapture
{
    DefaultRecipeRunCapture Run{};
    std::vector<std::uint8_t> Bytes{};
};

[[nodiscard]] ReadbackRunCapture CaptureDefaultRecipeReadbackFrame(
    Engine& engine,
    const Extrinsic::RHI::BufferHandle readbackBuffer,
    const std::uint64_t readbackSize,
    const bool parallelRecordingEnabled,
    const bool enableGpuProfiling = false)
{
    auto& renderer = engine.GetRenderer();
    renderer.SetParallelRenderGraphRecordingEnabled(parallelRecordingEnabled);
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    ReadbackRunCapture capture{};
    capture.Run = DriveDefaultRecipeDebugViewFrameAndCapture(
        engine,
        true,
        enableGpuProfiling);
    if (capture.Run.DeviceOperational)
    {
        capture.Bytes = ReadBackbufferBytes(engine.GetDevice(), readbackBuffer, readbackSize);
    }
    return capture;
}

[[nodiscard]] const Extrinsic::Graphics::RenderGraphGpuProfilePassStats*
FindGpuProfilePass(
    const Extrinsic::Graphics::RenderGraphGpuProfileStats& profile,
    const std::string_view passName) noexcept
{
    const auto found = std::find_if(
        profile.Passes.begin(),
        profile.Passes.end(),
        [passName](
            const Extrinsic::Graphics::RenderGraphGpuProfilePassStats& pass)
        {
            return pass.Name == passName;
        });
    return found != profile.Passes.end() ? &*found : nullptr;
}

[[nodiscard]] std::string BuildGpuProfileSummary(
    const Extrinsic::Graphics::RenderGraphGpuProfileStats& profile)
{
    std::string summary =
        "status=" +
        std::to_string(static_cast<std::uint32_t>(profile.Status)) +
        " source=" +
        std::to_string(static_cast<std::uint32_t>(profile.Source)) +
        " fresh=" + (profile.Fresh ? "true" : "false") +
        " stale=" + (profile.Stale ? "true" : "false") +
        " resolvedFrame=" +
        std::to_string(profile.ResolvedSubmittedFrameNumber) +
        " resolvedSlot=" + std::to_string(profile.ResolvedFrameSlot) +
        " age=" + std::to_string(profile.SampleAgeFrames) +
        " diagnostic=" + profile.Diagnostic + " passes=[";
    for (const auto& pass : profile.Passes)
    {
        if (summary.back() != '[')
        {
            summary += ", ";
        }
        summary += pass.Name + "@" +
            std::string{Extrinsic::RHI::QueueAffinityName(pass.Queue)} +
            ":" +
            std::to_string(static_cast<std::uint32_t>(pass.Source));
        if (pass.DurationNs.has_value())
        {
            summary += "=" + std::to_string(*pass.DurationNs) + "ns";
        }
        else
        {
            summary += "=unavailable";
        }
    }
    summary += "]";
    return summary;
}

void ExpectGpuProfileRowsUnique(
    const Extrinsic::Graphics::RenderGraphGpuProfileStats& profile)
{
    for (const auto& pass : profile.Passes)
    {
        EXPECT_TRUE(pass.Id.IsValid())
            << "Native GPU profile row has no compiled pass identity: "
            << pass.Name;
    }
    for (std::size_t left = 0u; left < profile.Passes.size(); ++left)
    {
        for (std::size_t right = left + 1u;
             right < profile.Passes.size();
             ++right)
        {
            const auto& first = profile.Passes[left];
            const auto& second = profile.Passes[right];
            EXPECT_FALSE(first.Name == second.Name);
            EXPECT_NE(first.Id, second.Id);
        }
    }
}

void ExpectResolvedNativeGpuProfile(
    const DefaultRecipeRunCapture& capture,
    const Extrinsic::RHI::IDevice& device,
    const std::uint64_t submittedFrameNumber,
    const std::uint32_t submittedFrameSlot)
{
    const auto& profile = capture.Stats.GpuProfile;
    ASSERT_TRUE(capture.DeviceOperational);
    EXPECT_TRUE(profile.Fresh) << BuildGpuProfileSummary(profile);
    EXPECT_FALSE(profile.Stale) << BuildGpuProfileSummary(profile);
    EXPECT_EQ(profile.Source, Extrinsic::RHI::GpuTimestampSource::NativeGpu)
        << BuildGpuProfileSummary(profile);
    EXPECT_TRUE(profile.HasResolvedFrame) << BuildGpuProfileSummary(profile);
    EXPECT_EQ(profile.ResolvedSubmittedFrameNumber, submittedFrameNumber)
        << BuildGpuProfileSummary(profile);
    EXPECT_EQ(profile.ResolvedFrameSlot, submittedFrameSlot)
        << BuildGpuProfileSummary(profile);
    EXPECT_LT(
        profile.ResolvedSubmittedFrameNumber,
        device.GetGlobalFrameNumber());
    EXPECT_GE(profile.SampleAgeFrames, device.GetFramesInFlight())
        << "The sample resolved before its cyclic frame slot was reused.";
    ExpectGpuProfileRowsUnique(profile);
}

[[nodiscard]] DefaultRecipeRunCapture DriveUntilGpuProfileResolvesFrame(
    Engine& engine,
    const std::uint64_t submittedFrameNumber,
    const std::uint32_t submittedFrameSlot)
{
    DefaultRecipeRunCapture capture{};
    const std::uint32_t frameBudget =
        engine.GetDevice().GetFramesInFlight() + 1u;
    for (std::uint32_t frameIndex = 0u;
         frameIndex < frameBudget;
         ++frameIndex)
    {
        capture = DriveDefaultRecipeDebugViewFrameAndCapture(
            engine,
            true,
            true);
        EXPECT_TRUE(Counters::IsStable(capture.Before, capture.After))
            << "Vulkan fallback/validation counters changed while resolving "
               "a submitted native GPU profile.";
        if (!capture.DeviceOperational)
        {
            return capture;
        }
        const auto& profile = capture.Stats.GpuProfile;
        if (profile.HasResolvedFrame &&
            profile.ResolvedSubmittedFrameNumber ==
                submittedFrameNumber &&
            profile.ResolvedFrameSlot == submittedFrameSlot)
        {
            return capture;
        }
    }
    return capture;
}

void ExpectGpuProfileScopeParity(
    const Extrinsic::Graphics::RenderGraphGpuProfileStats& serial,
    const Extrinsic::Graphics::RenderGraphGpuProfileStats& parallel)
{
    ExpectGpuProfileRowsUnique(serial);
    ExpectGpuProfileRowsUnique(parallel);
    ASSERT_EQ(serial.Passes.size(), parallel.Passes.size())
        << "Serial profile: " << BuildGpuProfileSummary(serial)
        << "\nParallel profile: " << BuildGpuProfileSummary(parallel);
    for (std::size_t index = 0u; index < serial.Passes.size(); ++index)
    {
        EXPECT_EQ(serial.Passes[index].Name, parallel.Passes[index].Name);
        EXPECT_EQ(serial.Passes[index].Id, parallel.Passes[index].Id);
        EXPECT_EQ(serial.Passes[index].Queue, parallel.Passes[index].Queue);
        EXPECT_EQ(
            serial.Passes[index].CommandStatus,
            parallel.Passes[index].CommandStatus);
    }
}
} // namespace

// GRAPHICS-076 Slice D — canonical default-recipe smoke. It drives one
// operational frame through the bounded `engine.Run()` helper and asserts that
// the executor reached the operational Vulkan command stream, that the
// canonical `"Present"` pass recorded its bind+draw, and that the Vulkan
// fallback / init-failure / validation-error / operational-gate counters did
// not increment across the operational frame.
//
// This fixture is the default-recipe leg of GRAPHICS-076 Slice D. The
// pixel-readback parity path remains a separate GRAPHICS-076E test below so
// the command-stream proof cannot be weakened by readback harness changes.
TEST(DefaultRecipeSurfaceGpuSmoke, RecipeSelectorReachesOperationalVulkanCommandStream)
{
    auto bootstrap = BootstrapEngineForDefaultRecipe();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const auto run = DriveDefaultRecipeAndCapture(engine);

    if (!run.DeviceOperational)
    {
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip after "
                         "running the default recipe: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". Host capability checks passed, so this is a "
                         "GRAPHICS-076 Slice D regression, not a skip condition.";
        return;
    }

    EXPECT_EQ(run.Status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(run.Status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.DeviceOperational);

    // The default recipe's executor branch for the canonical `"Present"` pass
    // landed in GRAPHICS-076 Slice A. Slice D's operational-gate proof is that
    // this branch records on a real Vulkan device rather than soft-skipping
    // with `SkippedUnavailable`. The pass MUST appear in
    // `CommandRecords.Passes` for the canonical recipe; assert presence before
    // status so a missing pass shows as a clear "recipe shape regression"
    // rather than a status mismatch.
    ASSERT_TRUE(ContainsPass(run.Stats, "Present"))
        << "Canonical default recipe did not emit a \"Present\" command record; "
        << "the recipe shape itself regressed.";
    EXPECT_EQ(FindPassStatus(run.Stats, "Present"), RenderCommandPassStatus::Recorded)
        << "Canonical default recipe \"Present\" pass did not record on the "
           "operational "
        << "Vulkan command stream.";

    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback counters incremented across an operational "
           "default-recipe frame: "
        << "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
        << ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
        << ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
        << ", gateFailure " << run.Before.OperationalGateFailure << " -> "
        << run.After.OperationalGateFailure;

    engine.Shutdown();
}

TEST(DefaultRecipeSurfaceGpuSmoke,
     NativeGpuTimestampsResolveNamedPassesAfterSlotReuse)
{
    constexpr std::uint32_t kTargetFrames = 8u;
    auto bootstrap = BootstrapEngineForDefaultRecipe(
        kTargetFrames,
        "Intrinsic native GPU timestamp slot-reuse smoke",
        false,
        true);
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;
    auto& device = engine.GetDevice();
    Extrinsic::RHI::IProfiler* profiler = device.GetProfiler();
    ASSERT_NE(profiler, nullptr);

    const std::uint64_t firstSubmittedFrameNumber =
        device.GetGlobalFrameNumber();
    const auto run = DriveDefaultRecipeAndCapture(engine);
    const std::uint64_t completedFrameNumber =
        device.GetGlobalFrameNumber();
    const std::uint64_t successfulFrameCount =
        completedFrameNumber - firstSubmittedFrameNumber;

    if (!run.DeviceOperational)
    {
        engine.Shutdown();
        ADD_FAILURE()
            << "Promoted Vulkan operational gate did not remain active "
               "during native timestamp slot-reuse smoke: status="
            << ToString(run.Status.Code)
            << " reason=" << ToString(run.Status.Reason)
            << ". Host readiness passed before the run, so this is a "
               "GRAPHICS-127 regression rather than a capability skip.";
        return;
    }

    const std::uint32_t framesInFlight = device.GetFramesInFlight();
    ASSERT_GT(framesInFlight, 0u);
    EXPECT_GE(
        successfulFrameCount,
        2u * static_cast<std::uint64_t>(framesInFlight) + 1u)
        << "The smoke did not cross two complete query-slot reuse windows.";
    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback/validation counters changed during native "
           "timestamp profiling.";

    const Extrinsic::RHI::ProfilerStatusSnapshot profilerStatus =
        profiler->GetStatus();
    RecordProperty("SuccessfulFrames", std::to_string(successfulFrameCount));
    RecordProperty("FramesInFlight", std::to_string(framesInFlight));
    RecordProperty(
        "ProfilerBackendStatus",
        std::to_string(
            static_cast<std::uint32_t>(profilerStatus.Status)));
    RecordProperty("ProfilerDiagnostic", profilerStatus.Diagnostic);
    const auto& profile = run.Stats.GpuProfile;
    RecordProperty(
        "RenderGraphGpuProfileStatus",
        std::to_string(static_cast<std::uint32_t>(profile.Status)));

    if (profile.Status ==
        Extrinsic::Graphics::RenderGraphGpuProfileStatus::Unsupported)
    {
        EXPECT_EQ(
            profile.Source,
            Extrinsic::RHI::GpuTimestampSource::Unavailable);
        EXPECT_FALSE(profile.Fresh);
        EXPECT_FALSE(profile.HasResolvedFrame);
        EXPECT_TRUE(profile.QueueEnvelopes.empty());
        EXPECT_TRUE(profile.Passes.empty());
        EXPECT_TRUE(
            Extrinsic::Core::Telemetry::TelemetrySystem::Get()
                .GetPassTimings()
                .empty());
        engine.Shutdown();
        return;
    }

    ASSERT_EQ(
        profilerStatus.Status,
        Extrinsic::RHI::ProfilerBackendStatus::Ready)
        << profilerStatus.Diagnostic;
    ASSERT_TRUE(profilerStatus.NativeTimestampsAvailable())
        << profilerStatus.Diagnostic;
    for (const std::string_view field :
         {"selectedDevice=\"",
          "physicalDeviceApi=",
          "loaderInstanceApi=",
          "engineRequestedApi=1.3.0",
          "driverName=\"",
          "driverInfo=\"",
          "driverVersion=",
          "deviceUUID=",
          "timestampPeriodNs=",
          "graphicsFamily=",
          "graphicsValidBits=",
          "asyncAvailable=",
          "asyncFamily=",
          "asyncValidBits="})
    {
        EXPECT_NE(
            profilerStatus.Diagnostic.find(field),
            std::string::npos)
            << "Selected-device profiler diagnostic omitted " << field
            << ": " << profilerStatus.Diagnostic;
    }

    ASSERT_TRUE(profile.Fresh) << BuildGpuProfileSummary(profile);
    EXPECT_FALSE(profile.Stale) << BuildGpuProfileSummary(profile);
    EXPECT_EQ(
        profile.Source,
        Extrinsic::RHI::GpuTimestampSource::NativeGpu)
        << BuildGpuProfileSummary(profile);
    ASSERT_TRUE(profile.HasResolvedFrame)
        << BuildGpuProfileSummary(profile);
    EXPECT_LT(
        profile.ResolvedSubmittedFrameNumber,
        completedFrameNumber);
    EXPECT_LT(profile.ResolvedFrameSlot, framesInFlight);
    EXPECT_GE(profile.SampleAgeFrames, framesInFlight)
        << "Native timestamps were published before the submitted query "
           "slot's reuse proof.";
    ExpectGpuProfileRowsUnique(profile);

    const auto* surfacePass = FindGpuProfilePass(profile, "SurfacePass");
    ASSERT_NE(surfacePass, nullptr) << BuildGpuProfileSummary(profile);
    EXPECT_EQ(
        surfacePass->CommandStatus,
        RenderCommandPassStatus::Recorded);
    EXPECT_EQ(
        surfacePass->Source,
        Extrinsic::RHI::GpuTimestampSource::NativeGpu);
    ASSERT_TRUE(surfacePass->DurationNs.has_value());
    EXPECT_GT(*surfacePass->DurationNs, 0u);
    EXPECT_TRUE(std::isfinite(
        static_cast<double>(*surfacePass->DurationNs)));

    const auto graphicsEnvelope = std::find_if(
        profile.QueueEnvelopes.begin(),
        profile.QueueEnvelopes.end(),
        [](const auto& envelope)
        {
            return envelope.Queue ==
                Extrinsic::RHI::QueueAffinity::Graphics;
        });
    ASSERT_NE(graphicsEnvelope, profile.QueueEnvelopes.end());
    EXPECT_EQ(
        graphicsEnvelope->Source,
        Extrinsic::RHI::GpuTimestampSource::NativeGpu);
    ASSERT_TRUE(graphicsEnvelope->DurationNs.has_value());
    EXPECT_GT(*graphicsEnvelope->DurationNs, 0u);

    const auto& telemetry =
        Extrinsic::Core::Telemetry::TelemetrySystem::Get()
            .GetPassTimings();
    const auto surfaceTelemetry = std::find_if(
        telemetry.begin(),
        telemetry.end(),
        [](const auto& timing)
        {
            return timing.Name == "SurfacePass";
        });
    ASSERT_NE(surfaceTelemetry, telemetry.end());
    EXPECT_EQ(
        surfaceTelemetry->GpuTimeNs,
        *surfacePass->DurationNs);
    EXPECT_EQ(surfaceTelemetry->CpuTimeNs, 0u);

    RecordProperty(
        "ResolvedSubmittedFrameNumber",
        std::to_string(profile.ResolvedSubmittedFrameNumber));
    RecordProperty(
        "ResolvedFrameSlot",
        std::to_string(profile.ResolvedFrameSlot));
    RecordProperty(
        "SampleAgeFrames",
        std::to_string(profile.SampleAgeFrames));
    RecordProperty(
        "SurfacePassGpuTimeNs",
        std::to_string(*surfacePass->DurationNs));

    engine.Shutdown();
}

TEST(DefaultRecipeSurfaceGpuSmoke, ReferenceTriangleDebugViewReadbackMatchesMinimalHarnessSamples)
{
    auto bootstrap = BootstrapEngineForDefaultRecipe(
        4u, "Intrinsic Default-recipe gpu;vulkan readback smoke");
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const auto warmup = DriveDefaultRecipeAndCapture(engine);
    if (!warmup.DeviceOperational)
    {
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during "
                         "default-recipe readback warmup: status="
                      << ToString(warmup.Status.Code)
                      << " reason=" << ToString(warmup.Status.Reason)
                      << ". Host capability checks passed, so this is a "
                         "GRAPHICS-076E regression, not a skip condition.";
        return;
    }

    auto& renderer = engine.GetRenderer();
    auto& device   = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    if (bytesPerPixel == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format has no host-uploadable layout on this "
                        "host; readback skipped.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(Readback::kFramebufferWidth) *
        static_cast<std::uint64_t>(Readback::kFramebufferHeight);
    Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "DefaultRecipe.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveDefaultRecipeDebugViewFrameAndCapture(engine, true);

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip after "
                         "running the default recipe readback smoke: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". Host capability checks passed, so this is a "
                         "GRAPHICS-076E regression, not a skip condition.";
        return;
    }

    EXPECT_EQ(run.Status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(run.Status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

    const auto& stats = run.Stats;
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);
    EXPECT_EQ(FindPassStatus(stats, "DebugViewPass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(stats);
    EXPECT_EQ(FindPassStatus(stats, "DepthPrepass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(stats);
    EXPECT_EQ(FindPassStatus(stats, "SurfacePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(stats);
    EXPECT_EQ(FindPassStatus(stats, "TransientDebugSurfacePass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(stats);
    EXPECT_EQ(FindPassStatus(stats, "Present"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(stats);
    EXPECT_GE(stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Default-recipe readback triplet did not record on any operational "
           "frame.";

    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback counters incremented across an operational "
           "default-recipe readback frame: "
        << "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
        << ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
        << ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
        << ", gateFailure " << run.Before.OperationalGateFailure << " -> "
        << run.After.OperationalGateFailure;

    ExpectMinimalHarnessReadbackSamples(device,
                                        readbackBuffer,
                                        readbackSize,
                                        bytesPerPixel,
                                        backbufferFormat,
                                        stats);

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);

    engine.Shutdown();
}

TEST(DefaultRecipeSurfaceGpuSmoke, ParallelRecordingMatchesSerialReadbackWithValidation)
{
    auto bootstrap = BootstrapEngineForDefaultRecipe(
        4u,
        "Intrinsic Default-recipe gpu;vulkan parallel-recording smoke",
        true,
        true);
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;
    const auto bootstrapDiagnostics = GetVulkanBootstrapDiagnosticsSnapshot();
    if (!bootstrapDiagnostics.ValidationEnabled || !bootstrapDiagnostics.DebugUtilsEnabled)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Vulkan validation layer/debug-utils is unavailable; "
                        "parallel-recording validation smoke is opt-in.";
    }

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::IProfiler* profiler = device.GetProfiler();
    ASSERT_NE(profiler, nullptr);
    const Extrinsic::RHI::ProfilerStatusSnapshot profilerStatus =
        profiler->GetStatus();
    if (profilerStatus.Status ==
        Extrinsic::RHI::ProfilerBackendStatus::Unsupported)
    {
        engine.Shutdown();
        GTEST_SKIP()
            << "Native timestamps are unavailable before the graphics "
               "serial/parallel profiling run: "
            << profilerStatus.Diagnostic;
    }
    ASSERT_EQ(
        profilerStatus.Status,
        Extrinsic::RHI::ProfilerBackendStatus::Ready)
        << profilerStatus.Diagnostic;
    renderer.SetActiveFrameRecipeOverride(
        std::make_optional(MakeGraphicsOnlyFrameRecipeOverride()));

    const auto warmup = DriveDefaultRecipeAndCapture(engine);
    if (!warmup.DeviceOperational)
    {
        renderer.ClearActiveFrameRecipeOverride();
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during "
                         "parallel-recording smoke warmup: status="
                      << ToString(warmup.Status.Code)
                      << " reason=" << ToString(warmup.Status.Reason)
                      << ". Host capability checks passed, so this is a "
                         "GRAPHICS-119 Slice D regression, not a skip condition.";
        return;
    }

    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    if (bytesPerPixel == 0u)
    {
        renderer.ClearActiveFrameRecipeOverride();
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format has no host-uploadable layout on this "
                        "host; parallel-recording smoke skipped.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(Readback::kFramebufferWidth) *
        static_cast<std::uint64_t>(Readback::kFramebufferHeight);
    Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "DefaultRecipe.ParallelRecordingReadback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan "
                        "parallel-recording smoke is opt-in.";
    }

    renderer.SetRenderGraphDebugDumpEnabled(true);

    const ReadbackRunCapture serial = CaptureDefaultRecipeReadbackFrame(
        engine,
        readbackBuffer,
        readbackSize,
        false,
        true);
    if (!serial.Run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        renderer.ClearActiveFrameRecipeOverride();
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate dropped during serial "
                         "baseline frame: status="
                      << ToString(serial.Run.Status.Code)
                      << " reason=" << ToString(serial.Run.Status.Reason);
        return;
    }

    const auto& serialStats = serial.Run.Stats;
    ExpectDefaultRecipeDebugViewReadbackRecorded(serialStats);
    EXPECT_TRUE(serialStats.FrameRecipeOverrideActive);
    EXPECT_TRUE(serialStats.FrameRecipeOverrideApplied);
    EXPECT_EQ(serialStats.FrameRecipeOverrideDiagnosticCount, 0u);
    EXPECT_FALSE(serialStats.Execute.ParallelRecordingRequested);
    EXPECT_FALSE(serialStats.Execute.ParallelRecordingAccepted);
    EXPECT_FALSE(serialStats.Execute.SerialFallbackUsed);
    EXPECT_EQ(serialStats.Execute.ParallelCommandContextCount, 0u);
    EXPECT_EQ(serialStats.AsyncComputeUtilizedFrames, 0u)
        << "The GRAPHICS-119 Vulkan smoke disables async-capable optional recipe "
           "slots so the Vulkan path exercises a graphics-only context plan.";
    EXPECT_TRUE(Counters::IsStable(serial.Run.Before, serial.Run.After))
        << "Vulkan fallback/validation counters changed across serial baseline "
           "frame.";
    ExpectMinimalHarnessReadbackSamples(device,
                                        readbackBuffer,
                                        readbackSize,
                                        bytesPerPixel,
                                        backbufferFormat,
                                        serialStats);
    ASSERT_TRUE(serial.Run.ProfileCandidateSubmitted);
    const DefaultRecipeRunCapture resolvedSerial =
        DriveUntilGpuProfileResolvesFrame(
            engine,
            serial.Run.ProfileCandidateFrameNumber,
            serial.Run.ProfileCandidateFrameSlot);
    ExpectResolvedNativeGpuProfile(
        resolvedSerial,
        device,
        serial.Run.ProfileCandidateFrameNumber,
        serial.Run.ProfileCandidateFrameSlot);

    const ReadbackRunCapture parallel = CaptureDefaultRecipeReadbackFrame(
        engine,
        readbackBuffer,
        readbackSize,
        true,
        true);
    if (!parallel.Run.DeviceOperational)
    {
        renderer.SetParallelRenderGraphRecordingEnabled(false);
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        renderer.ClearActiveFrameRecipeOverride();
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate dropped during "
                         "parallel-recording frame: status="
                      << ToString(parallel.Run.Status.Code)
                      << " reason=" << ToString(parallel.Run.Status.Reason);
        return;
    }

    const auto& parallelStats = parallel.Run.Stats;
    ExpectDefaultRecipeDebugViewReadbackRecorded(parallelStats);
    EXPECT_TRUE(parallelStats.FrameRecipeOverrideActive);
    EXPECT_TRUE(parallelStats.FrameRecipeOverrideApplied);
    EXPECT_EQ(parallelStats.FrameRecipeOverrideDiagnosticCount, 0u);
    EXPECT_TRUE(parallelStats.Execute.ParallelRecordingRequested);
    EXPECT_TRUE(parallelStats.Execute.ParallelRecordingAccepted);
    EXPECT_FALSE(parallelStats.Execute.SerialFallbackUsed);
    EXPECT_GT(parallelStats.Execute.ParallelCommandContextCount, 0u);
    EXPECT_EQ(parallelStats.Execute.ParallelRecordedPassCount,
              parallelStats.Execute.ParallelCommandContextCount);
    EXPECT_EQ(parallelStats.Execute.ParallelRecordWorkerTaskCount +
                  parallelStats.Execute.ParallelRecordCallerRecordCount,
              parallelStats.Execute.ParallelCommandContextCount);
    EXPECT_EQ(parallelStats.AsyncComputeUtilizedFrames, 0u);
    EXPECT_TRUE(Counters::IsStable(parallel.Run.Before, parallel.Run.After))
        << "Vulkan fallback/validation counters changed across "
           "parallel-recording frame.";
    ExpectMinimalHarnessReadbackSamples(device,
                                        readbackBuffer,
                                        readbackSize,
                                        bytesPerPixel,
                                        backbufferFormat,
                                        parallelStats);
    ExpectReadbackImagesEqual(
        serial.Bytes,
        parallel.Bytes,
        bytesPerPixel,
        serialStats.DebugDump,
        parallelStats.DebugDump);
    ASSERT_TRUE(parallel.Run.ProfileCandidateSubmitted);
    const DefaultRecipeRunCapture resolvedParallel =
        DriveUntilGpuProfileResolvesFrame(
            engine,
            parallel.Run.ProfileCandidateFrameNumber,
            parallel.Run.ProfileCandidateFrameSlot);
    ExpectResolvedNativeGpuProfile(
        resolvedParallel,
        device,
        parallel.Run.ProfileCandidateFrameNumber,
        parallel.Run.ProfileCandidateFrameSlot);
    ExpectGpuProfileScopeParity(
        resolvedSerial.Stats.GpuProfile,
        resolvedParallel.Stats.GpuProfile);
    for (const auto& pass : resolvedParallel.Stats.GpuProfile.Passes)
    {
        EXPECT_EQ(pass.Queue, Extrinsic::RHI::QueueAffinity::Graphics)
            << pass.Name;
    }
    for (const auto& envelope :
         resolvedParallel.Stats.GpuProfile.QueueEnvelopes)
    {
        EXPECT_EQ(
            envelope.Queue,
            Extrinsic::RHI::QueueAffinity::Graphics);
    }

    renderer.SetParallelRenderGraphRecordingEnabled(false);
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    renderer.ClearActiveFrameRecipeOverride();
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

TEST(DefaultRecipeSurfaceGpuSmoke, ParallelRecordingMatchesSerialAsyncComputeReadbackWithValidation)
{
    auto bootstrap = BootstrapEngineForDefaultRecipe(
        4u,
        "Intrinsic Default-recipe gpu;vulkan parallel async-compute smoke",
        true,
        true);
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;
    const auto bootstrapDiagnostics = GetVulkanBootstrapDiagnosticsSnapshot();
    if (!bootstrapDiagnostics.ValidationEnabled || !bootstrapDiagnostics.DebugUtilsEnabled)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Vulkan validation layer/debug-utils is unavailable; async "
                        "parallel-recording validation smoke is opt-in.";
    }

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    if (!device.GetQueueCapabilityProfile().SupportsAsyncCompute)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Promoted Vulkan framegraph async-compute queue is "
                        "unavailable; GRAPHICS-119 async smoke is opt-in.";
    }
    const Extrinsic::RHI::IProfiler* profiler = device.GetProfiler();
    ASSERT_NE(profiler, nullptr);
    const Extrinsic::RHI::ProfilerStatusSnapshot profilerStatus =
        profiler->GetStatus();
    if (profilerStatus.Status ==
        Extrinsic::RHI::ProfilerBackendStatus::Unsupported)
    {
        engine.Shutdown();
        GTEST_SKIP()
            << "Native timestamps are unavailable before the async-compute "
               "serial/parallel profiling run: "
            << profilerStatus.Diagnostic;
    }
    ASSERT_EQ(
        profilerStatus.Status,
        Extrinsic::RHI::ProfilerBackendStatus::Ready)
        << profilerStatus.Diagnostic;

    const auto warmup = DriveDefaultRecipeAndCapture(engine);
    if (!warmup.DeviceOperational)
    {
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during async "
                         "parallel-recording smoke warmup: status="
                      << ToString(warmup.Status.Code)
                      << " reason=" << ToString(warmup.Status.Reason)
                      << ". Host capability checks passed, so this is a GRAPHICS-119 Slice "
                         "C.11 regression, not a skip condition.";
        return;
    }

    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    if (bytesPerPixel == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format has no host-uploadable layout on this "
                        "host; async parallel-recording smoke skipped.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(Readback::kFramebufferWidth) *
        static_cast<std::uint64_t>(Readback::kFramebufferHeight);
    Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "DefaultRecipe.ParallelAsyncRecordingReadback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan async "
                        "parallel-recording smoke is opt-in.";
    }

    renderer.SetRenderGraphDebugDumpEnabled(true);

    const ReadbackRunCapture serial = CaptureDefaultRecipeReadbackFrame(
        engine,
        readbackBuffer,
        readbackSize,
        false,
        true);
    if (!serial.Run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate dropped during async "
                         "serial baseline frame: status="
                      << ToString(serial.Run.Status.Code)
                      << " reason=" << ToString(serial.Run.Status.Reason);
        return;
    }

    const auto& serialStats = serial.Run.Stats;
    ExpectDefaultRecipeDebugViewReadbackRecorded(serialStats);
    EXPECT_EQ(FindPassStatus(serialStats, "PostProcessHistogramPass"),
              RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(serialStats);
    EXPECT_GE(serialStats.AsyncComputeUtilizedFrames, 1u)
        << "Serial baseline did not accept the async-compute queue-submit plan.";
    EXPECT_FALSE(serialStats.Execute.ParallelRecordingRequested);
    EXPECT_FALSE(serialStats.Execute.ParallelRecordingAccepted);
    EXPECT_FALSE(serialStats.Execute.SerialFallbackUsed);
    EXPECT_EQ(serialStats.Execute.ParallelCommandContextCount, 0u);
    EXPECT_TRUE(Counters::IsStable(serial.Run.Before, serial.Run.After))
        << "Vulkan fallback/validation counters changed across async serial "
           "baseline frame.";
    ExpectMinimalHarnessReadbackSamples(device,
                                        readbackBuffer,
                                        readbackSize,
                                        bytesPerPixel,
                                        backbufferFormat,
                                        serialStats);
    ASSERT_TRUE(serial.Run.ProfileCandidateSubmitted);
    const DefaultRecipeRunCapture resolvedSerial =
        DriveUntilGpuProfileResolvesFrame(
            engine,
            serial.Run.ProfileCandidateFrameNumber,
            serial.Run.ProfileCandidateFrameSlot);
    ExpectResolvedNativeGpuProfile(
        resolvedSerial,
        device,
        serial.Run.ProfileCandidateFrameNumber,
        serial.Run.ProfileCandidateFrameSlot);

    const ReadbackRunCapture parallel = CaptureDefaultRecipeReadbackFrame(
        engine,
        readbackBuffer,
        readbackSize,
        true,
        true);
    if (!parallel.Run.DeviceOperational)
    {
        renderer.SetParallelRenderGraphRecordingEnabled(false);
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate dropped during async "
                         "parallel-recording frame: status="
                      << ToString(parallel.Run.Status.Code)
                      << " reason=" << ToString(parallel.Run.Status.Reason);
        return;
    }

    const auto& parallelStats = parallel.Run.Stats;
    ExpectDefaultRecipeDebugViewReadbackRecorded(parallelStats);
    EXPECT_EQ(FindPassStatus(parallelStats, "PostProcessHistogramPass"),
              RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(parallelStats);
    EXPECT_GE(parallelStats.AsyncComputeUtilizedFrames, 1u)
        << "Parallel frame did not accept the async-compute queue-submit plan.";
    EXPECT_TRUE(parallelStats.Execute.ParallelRecordingRequested);
    EXPECT_TRUE(parallelStats.Execute.ParallelRecordingAccepted);
    EXPECT_FALSE(parallelStats.Execute.SerialFallbackUsed);
    EXPECT_GT(parallelStats.Execute.ParallelCommandContextCount, 0u);
    EXPECT_EQ(parallelStats.Execute.ParallelRecordedPassCount,
              parallelStats.Execute.ParallelCommandContextCount);
    EXPECT_EQ(parallelStats.Execute.ParallelRecordWorkerTaskCount +
                  parallelStats.Execute.ParallelRecordCallerRecordCount,
              parallelStats.Execute.ParallelCommandContextCount);
    EXPECT_TRUE(Counters::IsStable(parallel.Run.Before, parallel.Run.After))
        << "Vulkan fallback/validation counters changed across async "
           "parallel-recording frame.";
    ExpectMinimalHarnessReadbackSamples(device,
                                        readbackBuffer,
                                        readbackSize,
                                        bytesPerPixel,
                                        backbufferFormat,
                                        parallelStats);
    ExpectReadbackImagesEqual(
        serial.Bytes,
        parallel.Bytes,
        bytesPerPixel,
        serialStats.DebugDump,
        parallelStats.DebugDump);
    ASSERT_TRUE(parallel.Run.ProfileCandidateSubmitted);
    const DefaultRecipeRunCapture resolvedParallel =
        DriveUntilGpuProfileResolvesFrame(
            engine,
            parallel.Run.ProfileCandidateFrameNumber,
            parallel.Run.ProfileCandidateFrameSlot);
    ExpectResolvedNativeGpuProfile(
        resolvedParallel,
        device,
        parallel.Run.ProfileCandidateFrameNumber,
        parallel.Run.ProfileCandidateFrameSlot);
    ExpectGpuProfileScopeParity(
        resolvedSerial.Stats.GpuProfile,
        resolvedParallel.Stats.GpuProfile);

    const auto* histogramProfile = FindGpuProfilePass(
        resolvedParallel.Stats.GpuProfile,
        "PostProcessHistogramPass");
    ASSERT_NE(histogramProfile, nullptr)
        << BuildGpuProfileSummary(
               resolvedParallel.Stats.GpuProfile);
    EXPECT_EQ(
        histogramProfile->Queue,
        Extrinsic::RHI::QueueAffinity::AsyncCompute);
    EXPECT_EQ(
        histogramProfile->CommandStatus,
        RenderCommandPassStatus::Recorded);
    EXPECT_EQ(
        histogramProfile->Source,
        Extrinsic::RHI::GpuTimestampSource::NativeGpu);
    ASSERT_TRUE(histogramProfile->DurationNs.has_value());

    const auto* surfaceProfile = FindGpuProfilePass(
        resolvedParallel.Stats.GpuProfile,
        "SurfacePass");
    ASSERT_NE(surfaceProfile, nullptr)
        << BuildGpuProfileSummary(
               resolvedParallel.Stats.GpuProfile);
    EXPECT_EQ(
        surfaceProfile->Queue,
        Extrinsic::RHI::QueueAffinity::Graphics);
    EXPECT_EQ(
        surfaceProfile->Source,
        Extrinsic::RHI::GpuTimestampSource::NativeGpu);

    const auto graphicsEnvelope = std::find_if(
        resolvedParallel.Stats.GpuProfile.QueueEnvelopes.begin(),
        resolvedParallel.Stats.GpuProfile.QueueEnvelopes.end(),
        [](const auto& envelope)
        {
            return envelope.Queue ==
                Extrinsic::RHI::QueueAffinity::Graphics;
        });
    const auto asyncEnvelope = std::find_if(
        resolvedParallel.Stats.GpuProfile.QueueEnvelopes.begin(),
        resolvedParallel.Stats.GpuProfile.QueueEnvelopes.end(),
        [](const auto& envelope)
        {
            return envelope.Queue ==
                Extrinsic::RHI::QueueAffinity::AsyncCompute;
        });
    ASSERT_NE(
        graphicsEnvelope,
        resolvedParallel.Stats.GpuProfile.QueueEnvelopes.end());
    ASSERT_NE(
        asyncEnvelope,
        resolvedParallel.Stats.GpuProfile.QueueEnvelopes.end());
    EXPECT_EQ(
        graphicsEnvelope->Source,
        Extrinsic::RHI::GpuTimestampSource::NativeGpu);
    EXPECT_EQ(
        asyncEnvelope->Source,
        Extrinsic::RHI::GpuTimestampSource::NativeGpu);

    renderer.SetParallelRenderGraphRecordingEnabled(false);
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

TEST(DefaultRecipeSurfaceGpuSmoke, TransientAliasingMatchesFallbackReadbackAndReducesMemory)
{
    auto bootstrap = BootstrapEngineForDefaultRecipe(
        4u,
        "Intrinsic Default-recipe gpu;vulkan transient-aliasing smoke",
        true);
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;
    const auto bootstrapDiagnostics = GetVulkanBootstrapDiagnosticsSnapshot();
    if (!bootstrapDiagnostics.ValidationEnabled || !bootstrapDiagnostics.DebugUtilsEnabled)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Vulkan validation layer/debug-utils is unavailable; "
                        "transient-aliasing validation smoke is opt-in.";
    }

    const auto warmup = DriveDefaultRecipeAndCapture(engine);
    if (!warmup.DeviceOperational)
    {
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during "
                         "transient-aliasing smoke warmup: status="
                      << ToString(warmup.Status.Code)
                      << " reason=" << ToString(warmup.Status.Reason)
                      << ". Host capability checks passed, so this is a "
                         "GRAPHICS-118 Slice D.2 regression, not a skip condition.";
        return;
    }

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    if (bytesPerPixel == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format has no host-uploadable layout on this "
                        "host; transient-aliasing smoke skipped.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(Readback::kFramebufferWidth) *
        static_cast<std::uint64_t>(Readback::kFramebufferHeight);
    Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "DefaultRecipe.TransientAliasingReadback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan "
                        "transient-aliasing smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);
    renderer.SetRenderGraphDebugDumpEnabled(true);

    renderer.SetTransientAliasingEnabled(false);
    EXPECT_FALSE(renderer.IsTransientAliasingEnabled());
    const auto fallbackRun = DriveDefaultRecipeDebugViewFrameAndCapture(engine, true);
    if (!fallbackRun.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate dropped during "
                         "aliasing-off baseline frame: status="
                      << ToString(fallbackRun.Status.Code)
                      << " reason=" << ToString(fallbackRun.Status.Reason);
        return;
    }
    const auto& fallbackStats = fallbackRun.Stats;
    ExpectDefaultRecipeDebugViewReadbackRecorded(fallbackStats);
    RecordProperty("AliasingOffTransientMemoryBytes",
                   fallbackStats.Compile.TransientMemoryEstimateBytes);
    EXPECT_EQ(fallbackStats.Compile.TransientPlacedPeakMemoryEstimateBytes,
              fallbackStats.Compile.TransientNaiveMemoryEstimateBytes);
    EXPECT_EQ(fallbackStats.Compile.TransientMemoryEstimateBytes,
              fallbackStats.Compile.TransientNaiveMemoryEstimateBytes);
    EXPECT_TRUE(Counters::IsStable(fallbackRun.Before, fallbackRun.After))
        << "Vulkan counters changed across aliasing-off baseline frame.";
    ExpectMinimalHarnessReadbackSamples(device,
                                        readbackBuffer,
                                        readbackSize,
                                        bytesPerPixel,
                                        backbufferFormat,
                                        fallbackStats);
    const std::vector<std::uint8_t> fallbackBytes =
        ReadBackbufferBytes(device, readbackBuffer, readbackSize);

    renderer.SetTransientAliasingEnabled(true);
    EXPECT_TRUE(renderer.IsTransientAliasingEnabled());
    const auto aliasRun = DriveDefaultRecipeDebugViewFrameAndCapture(engine, true);
    if (!aliasRun.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate dropped during "
                         "aliasing-on frame: status="
                      << ToString(aliasRun.Status.Code)
                      << " reason=" << ToString(aliasRun.Status.Reason);
        return;
    }
    const auto& aliasStats = aliasRun.Stats;
    ExpectDefaultRecipeDebugViewReadbackRecorded(aliasStats);
    RecordProperty("AliasingOnTransientMemoryBytes",
                   aliasStats.Compile.TransientMemoryEstimateBytes);
    RecordProperty("AliasingNaiveTransientMemoryBytes",
                   aliasStats.Compile.TransientNaiveMemoryEstimateBytes);
    RecordProperty("AliasingPlacedPeakTransientMemoryBytes",
                   aliasStats.Compile.TransientPlacedPeakMemoryEstimateBytes);
    EXPECT_LT(aliasStats.Compile.TransientPlacedPeakMemoryEstimateBytes,
              aliasStats.Compile.TransientNaiveMemoryEstimateBytes);
    EXPECT_EQ(aliasStats.Compile.TransientMemoryEstimateBytes,
              aliasStats.Compile.TransientPlacedPeakMemoryEstimateBytes);
    EXPECT_LT(aliasStats.Compile.TransientMemoryEstimateBytes,
              fallbackStats.Compile.TransientMemoryEstimateBytes)
        << "aliasing-off bytes=" << fallbackStats.Compile.TransientMemoryEstimateBytes
        << " aliasing-on bytes=" << aliasStats.Compile.TransientMemoryEstimateBytes;
    EXPECT_TRUE(Counters::IsStable(aliasRun.Before, aliasRun.After))
        << "Vulkan fallback/validation counters changed across aliasing-on "
           "frame.";
    ExpectMinimalHarnessReadbackSamples(device,
                                        readbackBuffer,
                                        readbackSize,
                                        bytesPerPixel,
                                        backbufferFormat,
                                        aliasStats);
    const std::vector<std::uint8_t> aliasBytes =
        ReadBackbufferBytes(device, readbackBuffer, readbackSize);
    ExpectReadbackImagesEqual(
        fallbackBytes,
        aliasBytes,
        bytesPerPixel,
        fallbackStats.DebugDump,
        aliasStats.DebugDump);

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}

TEST(DefaultRecipeSurfaceGpuSmoke, VulkanRenderContractPublishesDeclaredArtifactMetadata)
{
    auto bootstrap = BootstrapEngineForDefaultRecipe(
        4u, "Intrinsic Vulkan render-contract artifact smoke");
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const auto warmup = DriveDefaultRecipeAndCapture(engine);
    if (!warmup.DeviceOperational)
    {
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during "
                         "render-contract warmup: status="
                      << ToString(warmup.Status.Code)
                      << " reason=" << ToString(warmup.Status.Reason)
                      << ". Host capability checks passed, so this is a "
                         "GRAPHICS-103 regression, not a skip condition.";
        return;
    }

    auto& renderer = engine.GetRenderer();
    auto& device   = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    if (bytesPerPixel == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format has no host-uploadable layout on this "
                        "host; render-contract smoke skipped.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(Readback::kFramebufferWidth) *
        static_cast<std::uint64_t>(Readback::kFramebufferHeight);
    Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "DefaultRecipe.RenderContractReadback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveDefaultRecipeDebugViewFrameAndCapture(engine, true);

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip after "
                         "running the render-contract smoke: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". Host capability checks passed, so this is a "
                         "GRAPHICS-103 regression, not a skip condition.";
        return;
    }

    const auto& stats = run.Stats;
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);
    EXPECT_TRUE(stats.Contract.Evaluated);
    EXPECT_TRUE(stats.Contract.ContractCompatible);
    EXPECT_TRUE(stats.Contract.SharedProductsCompatible);
    EXPECT_TRUE(stats.Contract.ArtifactMetadataValid);
    EXPECT_EQ(stats.Contract.UnsupportedProductDiagnosticCount, 0u);
    EXPECT_EQ(stats.Contract.MissingOutputDiagnosticCount, 0u);
    EXPECT_EQ(stats.Contract.ArtifactPublicationFailureDiagnosticCount, 0u);
    EXPECT_GE(stats.DefaultRecipeBackbufferReadbackCopyCount, 1u);

    const Extrinsic::Graphics::RenderArtifactMetadata* colorArtifact =
        FindDeclaredArtifact(stats, "color");
    ASSERT_NE(colorArtifact, nullptr);
    EXPECT_EQ(colorArtifact->Status, Extrinsic::Graphics::RenderArtifactStatus::Available);
    EXPECT_FALSE(colorArtifact->SourceRevisions.empty());

    const Extrinsic::Graphics::RenderArtifactMetadata* readbackArtifact =
        FindDeclaredArtifact(stats, "readback");
    ASSERT_NE(readbackArtifact, nullptr);
    EXPECT_EQ(readbackArtifact->Status, Extrinsic::Graphics::RenderArtifactStatus::Available);

    Extrinsic::Runtime::RenderArtifactRegistry registry;
    const Extrinsic::Runtime::RenderArtifactOperationResult registered =
        registry.RegisterArtifact(Extrinsic::Runtime::RenderArtifactDeclaration{
            .Metadata = *colorArtifact,
            .Kind = Extrinsic::Runtime::RenderArtifactPublicationKind::PreviewOnly,
            .PayloadUri = "memory://vulkan/default-recipe/color",
            .ProducerLabel = "DefaultRecipeSurfaceGpuSmoke",
        });
    ASSERT_TRUE(registered.Succeeded());
    EXPECT_EQ(registered.Status,
              Extrinsic::Runtime::RenderArtifactOperationStatus::Registered);
    EXPECT_EQ(registry.Size(), 1u);

    ExpectMinimalHarnessReadbackSamples(device,
                                        readbackBuffer,
                                        readbackSize,
                                        bytesPerPixel,
                                        backbufferFormat,
                                        stats);

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);

    engine.Shutdown();
}

TEST(DefaultRecipeSurfaceGpuSmoke, VulkanResourceSlotsRecycleAfterRetirementWindow)
{
    auto bootstrap = BootstrapEngineForDefaultRecipe(
        4u,
        "Intrinsic Vulkan resource slot recycling smoke");
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }

    Engine& engine = *bootstrap.EnginePtr;
    auto initial = DriveDefaultRecipeAndCapture(engine);
    if (!initial.DeviceOperational)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Promoted Vulkan device did not become operational; "
                        "resource slot recycling smoke is opt-in.";
    }

    auto& device = engine.GetDevice();
    const Extrinsic::RHI::BufferDesc bufferDesc{
        .SizeBytes = 256u,
        .Usage = Extrinsic::RHI::BufferUsage::Storage |
                 Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = false,
        .DebugName = "BUG-035.RecycleBuffer",
    };
    const Extrinsic::RHI::TextureDesc textureDesc{
        .Width = 4u,
        .Height = 4u,
        .DepthOrArrayLayers = 1u,
        .MipLevels = 1u,
        .Fmt = Extrinsic::RHI::Format::RGBA8_UNORM,
        .Dimension = Extrinsic::RHI::TextureDimension::Tex2D,
        .Usage = Extrinsic::RHI::TextureUsage::Sampled |
                 Extrinsic::RHI::TextureUsage::TransferDst,
        .DebugName = "BUG-035.RecycleTexture",
    };

    const Extrinsic::RHI::BufferHandle firstBuffer = device.CreateBuffer(bufferDesc);
    const Extrinsic::RHI::TextureHandle firstTexture = device.CreateTexture(textureDesc);
    ASSERT_TRUE(firstBuffer.IsValid());
    ASSERT_TRUE(firstTexture.IsValid());

    device.DestroyBuffer(firstBuffer);
    device.DestroyTexture(firstTexture);

    for (std::uint32_t i = 0; i < device.GetFramesInFlight() + 2u; ++i)
    {
        auto capture = DriveDefaultRecipeDebugViewFrameAndCapture(engine);
        ASSERT_TRUE(capture.DeviceOperational);
    }

    std::vector<Extrinsic::RHI::BufferHandle> allocatedBuffers{};
    std::vector<Extrinsic::RHI::TextureHandle> allocatedTextures{};
    bool sawRecycledBuffer = false;
    bool sawRecycledTexture = false;
    constexpr std::uint32_t kRecycleProbeSlack = 256u;

    const std::uint32_t maxBufferAttempts = std::max(firstBuffer.Index + 8u, kRecycleProbeSlack);
    for (std::uint32_t i = 0; i < maxBufferAttempts; ++i)
    {
        const Extrinsic::RHI::BufferHandle handle = device.CreateBuffer(bufferDesc);
        if (!handle.IsValid())
        {
            ADD_FAILURE() << "Failed to allocate probe buffer " << i;
            break;
        }
        allocatedBuffers.push_back(handle);
        if (handle.Index == firstBuffer.Index)
        {
            sawRecycledBuffer = true;
            EXPECT_GT(handle.Generation, firstBuffer.Generation);
            break;
        }
    }

    const std::uint32_t maxTextureAttempts = std::max(firstTexture.Index + 8u, kRecycleProbeSlack);
    for (std::uint32_t i = 0; i < maxTextureAttempts; ++i)
    {
        const Extrinsic::RHI::TextureHandle handle = device.CreateTexture(textureDesc);
        if (!handle.IsValid())
        {
            ADD_FAILURE() << "Failed to allocate probe texture " << i;
            break;
        }
        allocatedTextures.push_back(handle);
        if (handle.Index == firstTexture.Index)
        {
            sawRecycledTexture = true;
            EXPECT_GT(handle.Generation, firstTexture.Generation);
            break;
        }
    }

    for (const Extrinsic::RHI::BufferHandle handle : allocatedBuffers)
    {
        device.DestroyBuffer(handle);
    }
    for (const Extrinsic::RHI::TextureHandle handle : allocatedTextures)
    {
        device.DestroyTexture(handle);
    }

    EXPECT_TRUE(sawRecycledBuffer) << "The destroyed buffer slot was not "
                                      "returned to the ResourcePool free queue.";
    EXPECT_TRUE(sawRecycledTexture) << "The destroyed texture slot was not "
                                       "returned to the ResourcePool free queue.";

    engine.Shutdown();
}

TEST(DefaultRecipeSurfaceGpuSmoke, AsyncComputeHistogramQueueReadbackMatchesMinimalHarnessSamples)
{
    auto bootstrap = BootstrapEngineForDefaultRecipe(
        4u, "Intrinsic Default-recipe gpu;vulkan async-compute smoke");
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    auto& renderer = engine.GetRenderer();
    auto& device   = engine.GetDevice();
    if (!device.GetQueueCapabilityProfile().SupportsAsyncCompute)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Promoted Vulkan device has no async-compute queue; "
                        "GRAPHICS-037D smoke is opt-in.";
    }

    const auto warmup = DriveDefaultRecipeAndCapture(engine);
    if (!warmup.DeviceOperational)
    {
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during "
                         "async-compute smoke warmup: status="
                      << ToString(warmup.Status.Code)
                      << " reason=" << ToString(warmup.Status.Reason)
                      << ". Host capability checks passed, so this is a "
                         "GRAPHICS-037D regression, not a skip condition.";
        return;
    }

    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    if (bytesPerPixel == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format has no host-uploadable layout on this "
                        "host; async-compute smoke skipped.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(Readback::kFramebufferWidth) *
        static_cast<std::uint64_t>(Readback::kFramebufferHeight);
    Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "DefaultRecipe.AsyncComputeReadback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveDefaultRecipeDebugViewFrameAndCapture(engine, true);

    if (!run.DeviceOperational)
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip after "
                         "running the async-compute smoke: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". Host capability checks passed, so this is a "
                         "GRAPHICS-037D regression, not a skip condition.";
        return;
    }

    EXPECT_EQ(run.Status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(run.Status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

    const auto& stats = run.Stats;
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);
    EXPECT_EQ(FindPassStatus(stats, "PostProcessHistogramPass"), RenderCommandPassStatus::Recorded)
        << BuildPassStatusSummary(stats);
    EXPECT_GE(stats.AsyncComputeUtilizedFrames, 1u)
        << "Default recipe did not accept a multi-queue submit plan with an "
           "async-compute batch.";
    EXPECT_GE(stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Default-recipe readback triplet did not record on the async-compute "
           "smoke frame.";

    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback counters incremented across an operational "
           "async-compute frame: "
        << "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
        << ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
        << ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
        << ", gateFailure " << run.Before.OperationalGateFailure << " -> "
        << run.After.OperationalGateFailure;

    ExpectMinimalHarnessReadbackSamples(device,
                                        readbackBuffer,
                                        readbackSize,
                                        bytesPerPixel,
                                        backbufferFormat,
                                        stats);

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);

    engine.Shutdown();
}
