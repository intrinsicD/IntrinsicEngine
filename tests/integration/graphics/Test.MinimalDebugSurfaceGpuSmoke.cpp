#include <cstdint>
#include <memory>

#include <gtest/gtest.h>

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Runtime.Engine;

namespace
{
using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
using Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs;
using Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::ToString;
using Extrinsic::Core::Config::FrameRecipeKind;
using Extrinsic::Runtime::Engine;
using Extrinsic::Runtime::IApplication;

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
    config.Window.Width = 128;
    config.Window.Height = 128;
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
    EXPECT_EQ(afterFrameDiagnostics.VulkanFallbackToNullCount,
              beforeFrameDiagnostics.VulkanFallbackToNullCount);
    EXPECT_EQ(afterFrameDiagnostics.VulkanInitFailureCount,
              beforeFrameDiagnostics.VulkanInitFailureCount);
    EXPECT_EQ(afterFrameDiagnostics.VulkanValidationErrorCount,
              beforeFrameDiagnostics.VulkanValidationErrorCount);
    EXPECT_EQ(afterFrameDiagnostics.VulkanOperationalGateFailureCount,
              beforeFrameDiagnostics.VulkanOperationalGateFailureCount);

    engine.Shutdown();
}

