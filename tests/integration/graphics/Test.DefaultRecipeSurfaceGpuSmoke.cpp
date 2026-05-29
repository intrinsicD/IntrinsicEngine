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
import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Graphics.RenderWorld;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.FrameHandle;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.Runtime.Engine;

namespace
{
namespace Readback = Extrinsic::Tests::Support::MinimalTriangleReadback;
namespace Counters = Extrinsic::Tests::Support::OperationalCounterStability;

using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
using Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs;
using Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::ToString;
using Extrinsic::Core::Config::FrameRecipeKind;
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

// GRAPHICS-076 Slice D — bounded `engine.Run()` driver mirroring the
// MinimalDebug fixture's `ExitAfterFramesApp`. The smoke drives a small fixed
// number of frames so the test cannot hang on a misconfigured swapchain loop
// even when the operational Vulkan gate flips green.
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

// Default-recipe equivalent of `BootstrapEngineForMinimalDebug` in
// `Test.MinimalDebugSurfaceGpuSmoke.cpp`. The MinimalDebug bootstrap pinned
// `config.Render.FrameRecipe = FrameRecipeKind::MinimalDebug`; this helper
// leaves the field at the constructor default
// (`FrameRecipeKind::Default` per `Core.Config.Render.cppm`) so the canonical
// default recipe is what reaches the executor.
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
    // Match the MinimalDebug fixture's small fixed framebuffer so backbuffer
    // sizing/format negotiation paths are exercised identically across both
    // recipes on this host.
    config.Window.Width = Readback::kFramebufferWidth;
    config.Window.Height = Readback::kFramebufferHeight;
    config.Window.Resizable = false;
    config.Render.EnableValidation = false;
    config.Render.EnableVSync = false;
    // GRAPHICS-076 Slice D — explicit no-op assignment for reviewer clarity;
    // `Core.Config.Render.cppm` already defaults `FrameRecipe` to `Default`,
    // and `CreateReferenceEngineConfig()` does not override it. Pinning the
    // field here protects this fixture against any future change to either
    // of those defaults that would silently re-route this smoke onto a
    // non-canonical recipe.
    config.Render.FrameRecipe = FrameRecipeKind::Default;

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
    Extrinsic::Core::Config::FrameRecipeKind FrameRecipe{Extrinsic::Core::Config::FrameRecipeKind::Default};
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
    capture.FrameRecipe = engine.GetRenderer().GetFrameRecipe();
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
        capture.FrameRecipe = renderer.GetFrameRecipe();
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
    capture.FrameRecipe = renderer.GetFrameRecipe();
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
} // namespace

// GRAPHICS-076 Slice D — recipe-selector smoke for the canonical default
// recipe. Mirrors `MinimalDebugSurfaceGpuSmoke.RecipeSelectorReachesOperationalVulkanCommandStream`
// in spirit: drives one operational frame through the bounded `engine.Run()`
// helper and asserts that the executor reached the operational Vulkan command
// stream, that the canonical `"Present"` pass recorded its bind+draw, that
// the recipe selector did NOT silently fall back to `MinimalDebug`, and that
// the Vulkan fallback / init-failure / validation-error / operational-gate
// counters did not increment across the operational frame.
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

    EXPECT_EQ(run.FrameRecipe, FrameRecipeKind::Default);
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

    // The recipe selector must not have silently fallen back to MinimalDebug.
    // Both minimal-recipe execution counters are MinimalDebug-only and must
    // stay zero under the default recipe.
    EXPECT_EQ(run.Stats.MinimalSurfacePassExecutions, 0u);
    EXPECT_EQ(run.Stats.MinimalPresentPassExecutions, 0u);
    EXPECT_EQ(run.Stats.MinimalRecipeMissingPrerequisiteCount, 0u);
    // The MinimalDebug readback hook must remain dormant under the default
    // recipe because this fixture does not arm
    // `SetMinimalDebugBackbufferReadbackBuffer(...)`.
    EXPECT_EQ(run.Stats.MinimalDebugBackbufferReadbackCopyCount, 0u);

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
    EXPECT_EQ(run.FrameRecipe, FrameRecipeKind::Default);
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
    EXPECT_EQ(stats.MinimalDebugBackbufferReadbackCopyCount, 0u)
        << "Default-recipe readback must not reuse the MinimalDebug diagnostic counter.";
    EXPECT_GE(stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Default-recipe readback triplet did not record on any operational frame.";

    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback counters incremented across an operational default-recipe readback frame: "
        << "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
        << ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
        << ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
        << ", gateFailure " << run.Before.OperationalGateFailure << " -> " << run.After.OperationalGateFailure;

    static_assert(Readback::kSamplePoints.size() == 4u,
                  "GRAPHICS-076E pixel readback requires exactly four deterministic sample points");

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

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);

    engine.Shutdown();
}




