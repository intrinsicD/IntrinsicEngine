#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include <gtest/gtest.h>

#include "MinimalTriangleReadback.hpp"
#include "OperationalCounterStability.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.QueueAffinity;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.RenderArtifactPublication;

namespace
{
namespace Readback = Extrinsic::Tests::Support::MinimalTriangleReadback;
namespace Counters = Extrinsic::Tests::Support::OperationalCounterStability;

using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
using Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs;
using Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::ToString;
using Extrinsic::Graphics::RenderCommandPassStatus;
using Extrinsic::Runtime::Engine;
using Extrinsic::Runtime::IApplication;

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
class ExitAfterFramesApp final : public IApplication
{
public:
    explicit ExitAfterFramesApp(const std::uint32_t targetFrames) noexcept
        : m_TargetFrames(targetFrames)
    {
    }

    void OnInitialize(Engine&) override {}
    void OnSimTick(Engine&, double) override {}

    void OnVariableTick(Engine& engine, double, double) override
    {
        ++m_Frames;
        if (m_Frames >= m_TargetFrames)
        {
            engine.RequestExit();
        }
    }

    void OnShutdown(Engine&) override {}

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
    const char* const windowTitle = "Intrinsic Default-recipe gpu;vulkan smoke")
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        return DefaultRecipeBootstrap{
            .EnginePtr = nullptr,
            .Skipped = true,
            .SkipReason = "GLFW could not initialize in this environment; gpu;vulkan default-recipe smoke is opt-in.",
        };
    }

    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
    config.Window.Title = windowTitle;
    // Fixed framebuffer keeps backbuffer sizing and format negotiation stable
    // for readback sample points across hosts.
    config.Window.Width = Readback::kFramebufferWidth;
    config.Window.Height = Readback::kFramebufferHeight;
    config.Window.Resizable = false;
    config.Render.EnableValidation = false;
    config.Render.EnableVSync = false;
    auto enginePtr = std::make_unique<Engine>(
        config, std::make_unique<ExitAfterFramesApp>(targetFrames));
    enginePtr->Initialize();

    const auto initInputs = GetVulkanDeviceOperationalInputs(&enginePtr->GetDevice());
    if (!initInputs.LogicalDeviceReady || !initInputs.SwapchainReady || !initInputs.CommandSyncReady)
    {
        enginePtr->Shutdown();
        return DefaultRecipeBootstrap{
            .EnginePtr = nullptr,
            .Skipped = true,
            .SkipReason = "Promoted Vulkan did not reach logical-device/swapchain/command-sync readiness on this host.",
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
    const bool seedReadbackTriangle = false)
{
    DefaultRecipeRunCapture capture;
    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();

    renderer.SetDebugViewRequestedResourceName("SceneColorHDR");
    capture.Before = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());

    Extrinsic::RHI::FrameHandle frame{};
    if (!renderer.BeginFrame(frame))
    {
        capture.Status = EvaluateVulkanDeviceOperationalStatus(&device);
        capture.DeviceOperational = device.IsOperational();
        capture.Stats = renderer.GetLastRenderGraphStats();
        capture.After = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
        return capture;
    }

    if (seedReadbackTriangle)
    {
        (void)SubmitReadbackTriangle(renderer);
    }

    const Extrinsic::Graphics::RenderFrameInput input{
        .Viewport = {.Width = Readback::kFramebufferWidth,
                     .Height = Readback::kFramebufferHeight},
        .DebugOverlayEnabled = true,
    };
    Extrinsic::Graphics::RenderWorld world = renderer.ExtractRenderWorld(input);
    renderer.PrepareFrame(world);
    renderer.ExecuteFrame(frame, world);
    (void)renderer.EndFrame(frame);
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
                  "gpu;vulkan pixel readback requires exactly four deterministic sample points");

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
            << " pass statuses=[" << BuildPassStatusSummary(stats) << "]";
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
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip after running the default recipe: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". Host capability checks passed, so this is a GRAPHICS-076 Slice D regression, not a skip condition.";
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
        << "Canonical default recipe \"Present\" pass did not record on the operational "
        << "Vulkan command stream.";

    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback counters incremented across an operational default-recipe frame: "
        << "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
        << ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
        << ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
        << ", gateFailure " << run.Before.OperationalGateFailure << " -> " << run.After.OperationalGateFailure;

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
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during default-recipe readback warmup: status="
                      << ToString(warmup.Status.Code) << " reason=" << ToString(warmup.Status.Reason)
                      << ". Host capability checks passed, so this is a GRAPHICS-076E regression, not a skip condition.";
        return;
    }

    auto& renderer = engine.GetRenderer();
    auto& device   = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    if (bytesPerPixel == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format has no host-uploadable layout on this host; readback skipped.";
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
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip after running the default recipe readback smoke: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". Host capability checks passed, so this is a GRAPHICS-076E regression, not a skip condition.";
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
        << "Default-recipe readback triplet did not record on any operational frame.";

    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback counters incremented across an operational default-recipe readback frame: "
        << "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
        << ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
        << ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
        << ", gateFailure " << run.Before.OperationalGateFailure << " -> " << run.After.OperationalGateFailure;

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
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during render-contract warmup: status="
                      << ToString(warmup.Status.Code) << " reason=" << ToString(warmup.Status.Reason)
                      << ". Host capability checks passed, so this is a GRAPHICS-103 regression, not a skip condition.";
        return;
    }

    auto& renderer = engine.GetRenderer();
    auto& device   = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    if (bytesPerPixel == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format has no host-uploadable layout on this host; render-contract smoke skipped.";
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
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip after running the render-contract smoke: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". Host capability checks passed, so this is a GRAPHICS-103 regression, not a skip condition.";
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
        GTEST_SKIP() << "Promoted Vulkan device did not become operational; resource slot recycling smoke is opt-in.";
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

    EXPECT_TRUE(sawRecycledBuffer)
        << "The destroyed buffer slot was not returned to the ResourcePool free queue.";
    EXPECT_TRUE(sawRecycledTexture)
        << "The destroyed texture slot was not returned to the ResourcePool free queue.";

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
        GTEST_SKIP() << "Promoted Vulkan device has no async-compute queue; GRAPHICS-037D smoke is opt-in.";
    }

    const auto warmup = DriveDefaultRecipeAndCapture(engine);
    if (!warmup.DeviceOperational)
    {
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during async-compute smoke warmup: status="
                      << ToString(warmup.Status.Code) << " reason=" << ToString(warmup.Status.Reason)
                      << ". Host capability checks passed, so this is a GRAPHICS-037D regression, not a skip condition.";
        return;
    }

    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    if (bytesPerPixel == 0u)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format has no host-uploadable layout on this host; async-compute smoke skipped.";
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
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip after running the async-compute smoke: status="
                      << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason)
                      << ". Host capability checks passed, so this is a GRAPHICS-037D regression, not a skip condition.";
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
        << "Default recipe did not accept a multi-queue submit plan with an async-compute batch.";
    EXPECT_GE(stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Default-recipe readback triplet did not record on the async-compute smoke frame.";

    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback counters incremented across an operational async-compute frame: "
        << "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
        << ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
        << ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
        << ", gateFailure " << run.Before.OperationalGateFailure << " -> " << run.After.OperationalGateFailure;

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
