#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

#include "MinimalTriangleReadback.hpp"
#include "OperationalCounterStability.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Backend.Glfw;
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
} // namespace

TEST(MinimalDebugSurfaceGpuSmoke, ReferenceTriangleRecordsOnOperationalPromotedVulkan)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        GTEST_SKIP() << "GLFW could not initialize in this environment; gpu;vulkan visible-triangle smoke is opt-in.";
    }

    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
    config.Window.Title = "Intrinsic MinimalDebug gpu;vulkan smoke";
    config.Window.Width = Readback::kFramebufferWidth;
    config.Window.Height = Readback::kFramebufferHeight;
    config.Window.Resizable = false;
    config.Render.EnableValidation = false;
    config.Render.EnableVSync = false;
    config.Render.FrameRecipe = FrameRecipeKind::MinimalDebug;

    Engine engine(config, std::make_unique<ExitAfterFramesApp>(4u));
    engine.Initialize();

    const auto initInputs = GetVulkanDeviceOperationalInputs(&engine.GetDevice());
    if (!initInputs.LogicalDeviceReady || !initInputs.SwapchainReady || !initInputs.CommandSyncReady)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Promoted Vulkan did not reach logical-device/swapchain/command-sync readiness on this host.";
    }

    const auto beforeFrameDiagnostics = GetVulkanOperationalDiagnosticsSnapshot();
    const auto beforeCounters = ToCounterSnapshot(beforeFrameDiagnostics);

    engine.Run();

    const auto status = EvaluateVulkanDeviceOperationalStatus(&engine.GetDevice());
    if (!engine.GetDevice().IsOperational())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Promoted Vulkan operational gate did not flip on this host: status="
                     << ToString(status.Code) << " reason=" << ToString(status.Reason);
    }

    EXPECT_EQ(status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

    const auto& stats = engine.GetRenderer().GetLastRenderGraphStats();
    EXPECT_EQ(engine.GetRenderer().GetFrameRecipe(), FrameRecipeKind::MinimalDebug);
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);
    EXPECT_EQ(stats.MinimalSurfacePassExecutions, 1u);
    EXPECT_EQ(stats.MinimalPresentPassExecutions, 1u);
    EXPECT_EQ(stats.MinimalRecipeMissingPrerequisiteCount, 0u);

    const auto afterFrameDiagnostics = GetVulkanOperationalDiagnosticsSnapshot();
    const auto afterCounters = ToCounterSnapshot(afterFrameDiagnostics);
    EXPECT_TRUE(Counters::IsStable(beforeCounters, afterCounters))
        << "Vulkan fallback counters incremented across an operational frame: "
        << "fallbackToNull " << beforeCounters.FallbackToNull << " -> " << afterCounters.FallbackToNull
        << ", initFailure " << beforeCounters.InitFailure << " -> " << afterCounters.InitFailure
        << ", validationError " << beforeCounters.ValidationError << " -> " << afterCounters.ValidationError
        << ", gateFailure " << beforeCounters.OperationalGateFailure << " -> " << afterCounters.OperationalGateFailure;

    // GRAPHICS-033D pixel readback: the four-sample assertion runs against the
    // reusable harness sample-point table once the backbuffer-to-host readback
    // seam lands (tracked as the remaining bullet in
    // tasks/active/GRAPHICS-033D-gpu-vulkan-visible-triangle-smoke.md). The
    // harness is exercised at compile time below so the byte-identical contract
    // shared with the sibling GRAPHICS-032D recipe-selector fixture and the
    // canonical GRAPHICS-076/081 default-recipe smoke is locked in here.
    static_assert(Readback::kSamplePoints.size() == 4u,
                  "GRAPHICS-033D pixel readback requires exactly four deterministic sample points");
    static_assert(Readback::ExpectedAt(Readback::kSamplePoints[0]).R == Readback::Quantize8(Readback::kTriangleR),
                  "Interior sample point must expect the reference-triangle color");
    static_assert(Readback::ExpectedAt(Readback::kSamplePoints[1]).R == Readback::Quantize8(Readback::kClearR),
                  "Exterior sample point must expect the clear color");

    engine.Shutdown();
}

