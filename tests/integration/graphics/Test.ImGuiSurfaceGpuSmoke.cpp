#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#include <gtest/gtest.h>
#include <imgui.h>

#include "OperationalCounterStability.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.Render;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.ImGuiAdapter;

namespace
{
namespace Counters = Extrinsic::Tests::Support::OperationalCounterStability;

using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
using Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs;
using Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::ToString;
using Extrinsic::Core::Config::FrameRecipeKind;
using Extrinsic::Graphics::RenderCommandPassStatus;
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

struct ImGuiSmokeBootstrap
{
    std::unique_ptr<Engine> EnginePtr;
    bool Skipped{false};
    std::string SkipReason;
};

[[nodiscard]] ImGuiSmokeBootstrap BootstrapOperationalDefaultRecipe(
    const std::uint32_t targetFrames = 4u)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        return ImGuiSmokeBootstrap{
            .EnginePtr = nullptr,
            .Skipped = true,
            .SkipReason = "GLFW could not initialize in this environment; gpu;vulkan ImGui smoke is opt-in.",
        };
    }

    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
    config.Window.Title = "Intrinsic ImGui gpu;vulkan smoke";
    config.Window.Width = 256u;
    config.Window.Height = 256u;
    config.Window.Resizable = false;
    config.Render.EnableValidation = false;
    config.Render.EnableVSync = false;
    config.Render.FrameRecipe = FrameRecipeKind::Default;

    auto enginePtr = std::make_unique<Engine>(
        config, std::make_unique<ExitAfterFramesApp>(targetFrames));
    enginePtr->Initialize();

    const auto initInputs = GetVulkanDeviceOperationalInputs(&enginePtr->GetDevice());
    if (!initInputs.LogicalDeviceReady || !initInputs.SwapchainReady || !initInputs.CommandSyncReady)
    {
        enginePtr->Shutdown();
        return ImGuiSmokeBootstrap{
            .EnginePtr = nullptr,
            .Skipped = true,
            .SkipReason = "Promoted Vulkan did not reach logical-device/swapchain/command-sync readiness on this host.",
        };
    }

    return ImGuiSmokeBootstrap{.EnginePtr = std::move(enginePtr), .Skipped = false, .SkipReason = {}};
}

[[nodiscard]] const Extrinsic::Graphics::RenderGraphCommandPassStats* FindCommandPass(
    const Extrinsic::Graphics::RenderGraphFrameStats& stats,
    const std::string_view passName) noexcept
{
    const auto it = std::find_if(
        stats.CommandRecords.Passes.begin(),
        stats.CommandRecords.Passes.end(),
        [passName](const auto& pass) { return pass.Name == passName; });
    return it == stats.CommandRecords.Passes.end() ? nullptr : &*it;
}
} // namespace

TEST(ImGuiSurfaceGpuSmoke, UserTextureImageRecordsOnOperationalVulkanCommandStream)
{
    auto bootstrap = BootstrapOperationalDefaultRecipe();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    engine.SetImGuiEditorCallback(
        []
        {
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
            ImGui::SetNextWindowSize(ImVec2(128.0f, 96.0f));
            ImGui::Begin("GRAPHICS-079 Vulkan ImGui");
            ImGui::Image(static_cast<ImTextureID>(77u), ImVec2(16.0f, 16.0f));
            ImGui::End();
        });

    const Counters::Snapshot before =
        ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    engine.Run();
    const Counters::Snapshot after =
        ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());

    const auto status = EvaluateVulkanDeviceOperationalStatus(&engine.GetDevice());
    if (!engine.GetDevice().IsOperational())
    {
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during ImGui frame: status="
                      << ToString(status.Code) << " reason=" << ToString(status.Reason);
        return;
    }

    EXPECT_EQ(status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

    const auto& diag = engine.GetImGuiAdapter().GetDiagnostics();
    EXPECT_GE(diag.FramesProduced, 1u);
    EXPECT_TRUE(diag.LastFrameUsedUserTexture);
    EXPECT_GE(diag.LastDrawListCount, 1u);
    EXPECT_GT(diag.LastVertexCount, 0u);
    EXPECT_GT(diag.LastIndexCount, 0u);
    EXPECT_GE(diag.LastCommandCount, 1u);

    const auto& stats = engine.GetRenderer().GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    const auto* pass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(pass, nullptr)
        << "Default recipe omitted ImGuiPass despite adapter-produced overlay work.";
    EXPECT_EQ(pass->Status, RenderCommandPassStatus::Recorded)
        << "ImGuiPass did not record on the operational Vulkan command stream.";

    EXPECT_TRUE(Counters::IsStable(before, after))
        << "Vulkan fallback counters incremented across the ImGui frame: "
        << "fallbackToNull " << before.FallbackToNull << " -> " << after.FallbackToNull
        << ", initFailure " << before.InitFailure << " -> " << after.InitFailure
        << ", validationError " << before.ValidationError << " -> " << after.ValidationError
        << ", gateFailure " << before.OperationalGateFailure << " -> " << after.OperationalGateFailure;

    engine.Shutdown();
}
