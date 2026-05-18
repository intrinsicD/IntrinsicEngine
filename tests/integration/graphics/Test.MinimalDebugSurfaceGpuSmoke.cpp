#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "MinimalTriangleReadback.hpp"
#include "OperationalCounterStability.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Descriptors;
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
using Extrinsic::Runtime::Engine;
using Extrinsic::Runtime::IApplication;

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

// GRAPHICS-033D — the operational gate accepts both RGBA and BGRA swizzles
// for the swapchain image. Normalise the four-byte texel into the harness's
// canonical RGBA order before tolerance comparison.
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

// IEC 61966-2-1 sRGB → linear EOTF, inverted into 8-bit space so the harness's
// linear `kTriangleR/G/B/A` quantization can be compared directly against
// pixels the GPU gamma-encoded on the way to an sRGB swapchain image.
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

// GRAPHICS-032D / GRAPHICS-033D — shared engine bring-up for the MinimalDebug
// `gpu;vulkan` smokes. Both the pixel-readback fixture
// (`ReferenceTriangleRecordsOnOperationalPromotedVulkan`) and the
// recipe-selector fixture (`RecipeSelectorReachesOperationalVulkanCommandStream`)
// drive the runtime through this one helper so the bounded `engine.Run()`
// driver loop and the bootstrap skip checks are not duplicated across the two
// tests.
struct MinimalDebugBootstrap
{
    std::unique_ptr<Engine> EnginePtr;
    bool Skipped{false};
    std::string SkipReason;
};

[[nodiscard]] MinimalDebugBootstrap BootstrapEngineForMinimalDebug(
    const std::uint32_t targetFrames = 4u,
    const char* const windowTitle = "Intrinsic MinimalDebug gpu;vulkan smoke")
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        return MinimalDebugBootstrap{
            .EnginePtr = nullptr,
            .Skipped = true,
            .SkipReason = "GLFW could not initialize in this environment; gpu;vulkan visible-triangle smoke is opt-in.",
        };
    }

    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
    config.Window.Title = windowTitle;
    config.Window.Width = Readback::kFramebufferWidth;
    config.Window.Height = Readback::kFramebufferHeight;
    config.Window.Resizable = false;
    config.Render.EnableValidation = false;
    config.Render.EnableVSync = false;
    config.Render.FrameRecipe = FrameRecipeKind::MinimalDebug;

    auto enginePtr = std::make_unique<Engine>(
        config, std::make_unique<ExitAfterFramesApp>(targetFrames));
    enginePtr->Initialize();

    const auto initInputs = GetVulkanDeviceOperationalInputs(&enginePtr->GetDevice());
    if (!initInputs.LogicalDeviceReady || !initInputs.SwapchainReady || !initInputs.CommandSyncReady)
    {
        enginePtr->Shutdown();
        return MinimalDebugBootstrap{
            .EnginePtr = nullptr,
            .Skipped = true,
            .SkipReason = "Promoted Vulkan did not reach logical-device/swapchain/command-sync readiness on this host.",
        };
    }

    return MinimalDebugBootstrap{.EnginePtr = std::move(enginePtr), .Skipped = false, .SkipReason = {}};
}

struct MinimalDebugRunCapture
{
    Counters::Snapshot Before{};
    Counters::Snapshot After{};
    Extrinsic::Backends::Vulkan::VulkanOperationalStatus Status{};
    Extrinsic::Graphics::RenderGraphFrameStats Stats{};
    Extrinsic::Core::Config::FrameRecipeKind FrameRecipe{Extrinsic::Core::Config::FrameRecipeKind::Default};
    bool DeviceOperational{false};
};

// Bounded `engine.Run()` plus before/after operational-counter snapshots and
// post-frame status/stats capture. Callers wire `SetMinimalDebugBackbufferReadbackBuffer`
// before invoking when they want the per-frame readback triplet recorded.
[[nodiscard]] MinimalDebugRunCapture DriveOneFrameAndCapture(Engine& engine)
{
    MinimalDebugRunCapture capture;
    capture.Before = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    engine.Run();
    capture.Status = EvaluateVulkanDeviceOperationalStatus(&engine.GetDevice());
    capture.DeviceOperational = engine.GetDevice().IsOperational();
    capture.Stats = engine.GetRenderer().GetLastRenderGraphStats();
    capture.FrameRecipe = engine.GetRenderer().GetFrameRecipe();
    capture.After = ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    return capture;
}
} // namespace

TEST(MinimalDebugSurfaceGpuSmoke, ReferenceTriangleRecordsOnOperationalPromotedVulkan)
{
    auto bootstrap = BootstrapEngineForMinimalDebug();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    // GRAPHICS-033D readback wiring: allocate a host-visible backend buffer
    // sized for a full mip-0 copy of the backbuffer image and arm the
    // renderer's opt-in readback hook. This smoke allocates directly through
    // IDevice instead of BufferManager because BufferManager is intentionally
    // fail-closed while IDevice::IsOperational() is false; the readback buffer
    // must already exist for the frame that proves the Vulkan operational gate
    // flips. The readback path is a no-op on every other test/engine config
    // (the smoke is the only caller of SetMinimalDebugBackbufferReadbackBuffer),
    // so wiring it here cannot affect the default CPU gate.
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
        .DebugName = "MinimalDebug.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan smoke is opt-in.";
    }
    renderer.SetMinimalDebugBackbufferReadbackBuffer(readbackBuffer);

    const auto run = DriveOneFrameAndCapture(engine);

    if (!run.DeviceOperational)
    {
        renderer.SetMinimalDebugBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        GTEST_SKIP() << "Promoted Vulkan operational gate did not flip on this host: status="
                     << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason);
    }

    EXPECT_EQ(run.Status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(run.Status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

    const auto& stats = run.Stats;
    EXPECT_EQ(run.FrameRecipe, FrameRecipeKind::MinimalDebug);
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);
    EXPECT_EQ(stats.MinimalSurfacePassExecutions, 1u);
    EXPECT_EQ(stats.MinimalPresentPassExecutions, 1u);
    EXPECT_EQ(stats.MinimalRecipeMissingPrerequisiteCount, 0u);

    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback counters incremented across an operational frame: "
        << "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
        << ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
        << ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
        << ", gateFailure " << run.Before.OperationalGateFailure << " -> " << run.After.OperationalGateFailure;

    // GRAPHICS-033D pixel readback: the four-sample assertion runs against the
    // reusable harness sample-point table now that the backbuffer-to-host
    // readback seam (RHI::ICommandContext::CopyTextureToBuffer +
    // RHI::IDevice::ReadBuffer + the renderer's opt-in hook) is wired. The
    // compile-time harness invariants are also verified at the contract-test
    // layer (tests/contract/graphics/Test.MinimalTriangleReadbackHarness.cpp),
    // so the sample-point table contract is locked in for the sibling
    // GRAPHICS-032D recipe-selector fixture and the canonical
    // GRAPHICS-076/081 default-recipe smoke.
    static_assert(Readback::kSamplePoints.size() == 4u,
                  "GRAPHICS-033D pixel readback requires exactly four deterministic sample points");

    // The renderer increments this counter exactly once per operational frame
    // in which the readback triplet recorded. ExitAfterFramesApp runs 4
    // frames; gate on >=1 so a single operational frame still counts and the
    // remaining frames don't gate on bootstrap timing.
    EXPECT_GE(stats.MinimalDebugBackbufferReadbackCopyCount, 1u)
        << "MinimalDebug readback triplet did not record on any operational frame.";

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
            << " backbuffer format=" << static_cast<int>(backbufferFormat);
    }

    // Drop the readback wiring before destroying the backend buffer so the
    // renderer cannot hold a stale handle across Shutdown(). Without this the
    // GRAPHICS-033F-style backend-readiness predicate would observe a dangling
    // readback handle across Shutdown().
    renderer.SetMinimalDebugBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);

    engine.Shutdown();
}

// GRAPHICS-032D — sibling recipe-selector smoke. Drives one operational frame
// of `FrameRecipe::MinimalDebugSurface` through the shared bootstrap +
// `engine.Run()` driver helper and asserts that the recipe selector reached
// the operational Vulkan command stream by way of the renderer's per-frame
// minimal-recipe counters. No pixel readback, no readback-buffer wiring: that
// path is exclusively owned by the GRAPHICS-033D fixture above.
TEST(MinimalDebugSurfaceGpuSmoke, RecipeSelectorReachesOperationalVulkanCommandStream)
{
    auto bootstrap = BootstrapEngineForMinimalDebug(
        4u, "Intrinsic MinimalDebug gpu;vulkan recipe-selector smoke");
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    const auto run = DriveOneFrameAndCapture(engine);

    if (!run.DeviceOperational)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Promoted Vulkan operational gate did not flip on this host: status="
                     << ToString(run.Status.Code) << " reason=" << ToString(run.Status.Reason);
    }

    EXPECT_EQ(run.Status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(run.Status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

    EXPECT_EQ(run.FrameRecipe, FrameRecipeKind::MinimalDebug);
    EXPECT_TRUE(run.Stats.Compile.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.Succeeded) << run.Stats.Diagnostic;
    EXPECT_TRUE(run.Stats.Execute.DeviceOperational);

    // Recipe-selector contract per GRAPHICS-032 / GRAPHICS-032D: the selector
    // reached the operational Vulkan command stream iff the surface and
    // present pass bodies both recorded exactly once and no per-frame
    // prerequisite gap was reported.
    EXPECT_EQ(run.Stats.MinimalSurfacePassExecutions, 1u);
    EXPECT_EQ(run.Stats.MinimalPresentPassExecutions, 1u);
    EXPECT_EQ(run.Stats.MinimalRecipeMissingPrerequisiteCount, 0u);

    // The readback path is not armed by this fixture, so the readback counter
    // must remain at its post-Initialize default. Asserting zero locks the
    // recipe-selector path against silently regressing into the readback
    // wiring (which would imply the fixtures are no longer separable).
    EXPECT_EQ(run.Stats.MinimalDebugBackbufferReadbackCopyCount, 0u);

    EXPECT_TRUE(Counters::IsStable(run.Before, run.After))
        << "Vulkan fallback counters incremented across an operational frame: "
        << "fallbackToNull " << run.Before.FallbackToNull << " -> " << run.After.FallbackToNull
        << ", initFailure " << run.Before.InitFailure << " -> " << run.After.InitFailure
        << ", validationError " << run.Before.ValidationError << " -> " << run.After.ValidationError
        << ", gateFailure " << run.Before.OperationalGateFailure << " -> " << run.After.OperationalGateFailure;

    engine.Shutdown();
}
