#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <imgui.h>

#include "OperationalCounterStability.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.ImGuiAdapter;

namespace
{
namespace Counters = Extrinsic::Tests::Support::OperationalCounterStability;

using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
using Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs;
using Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::ToString;
using Extrinsic::Graphics::RenderCommandPassStatus;
using Extrinsic::Runtime::Engine;
using Extrinsic::Runtime::IApplication;

inline constexpr std::uint32_t kReadbackWidth = 256u;
inline constexpr std::uint32_t kReadbackHeight = 256u;
inline constexpr std::uint32_t kImGuiProbeX = 64u;
inline constexpr std::uint32_t kImGuiProbeY = 40u;

struct CpuFontAtlasProbe
{
    std::uint32_t Width{0u};
    std::uint32_t Height{0u};
    std::uint32_t X{0u};
    std::uint32_t Y{0u};
    std::uint8_t Alpha{0u};
    float DrawVertexU{0.0f};
    float DrawVertexV{0.0f};
    std::uint32_t DrawVertexColor{0u};
};

CpuFontAtlasProbe g_LastCpuFontAtlasProbe{};

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
    config.Window.Width = kReadbackWidth;
    config.Window.Height = kReadbackHeight;
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
        return ImGuiSmokeBootstrap{
            .EnginePtr = nullptr,
            .Skipped = true,
            .SkipReason = "Promoted Vulkan did not reach logical-device/swapchain/command-sync readiness on this host.",
        };
    }

    return ImGuiSmokeBootstrap{.EnginePtr = std::move(enginePtr), .Skipped = false, .SkipReason = {}};
}

struct Rgba8Pixel
{
    std::uint8_t R{0u};
    std::uint8_t G{0u};
    std::uint8_t B{0u};
    std::uint8_t A{0u};
};

[[nodiscard]] Rgba8Pixel ReorderToRgba(
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
        return Rgba8Pixel{.R = b2, .G = b1, .B = b0, .A = b3};
    case Extrinsic::RHI::Format::RGBA8_UNORM:
    case Extrinsic::RHI::Format::RGBA8_SRGB:
    default:
        return Rgba8Pixel{.R = b0, .G = b1, .B = b2, .A = b3};
    }
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

TEST(ImGuiSurfaceGpuSmoke, DrawListPixelsReachBackbufferOnOperationalVulkan)
{
    auto bootstrap = BootstrapOperationalDefaultRecipe();
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;

    auto& renderer = engine.GetRenderer();
    auto& device = engine.GetDevice();
    const Extrinsic::RHI::Format backbufferFormat = device.GetBackbufferFormat();
    const std::uint32_t bytesPerPixel = Extrinsic::RHI::BytesPerBlock(backbufferFormat);
    if (bytesPerPixel < 4u)
    {
        engine.Shutdown();
        GTEST_SKIP() << "Backbuffer format cannot support rgba-style ImGui pixel readback.";
    }

    const std::uint64_t readbackSize =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(kReadbackWidth) *
        static_cast<std::uint64_t>(kReadbackHeight);
    const Extrinsic::RHI::BufferHandle readbackBuffer = device.CreateBuffer(Extrinsic::RHI::BufferDesc{
        .SizeBytes = readbackSize,
        .Usage = Extrinsic::RHI::BufferUsage::TransferDst,
        .HostVisible = true,
        .DebugName = "ImGuiSurface.VisiblePixel.Readback",
    });
    if (!readbackBuffer.IsValid())
    {
        engine.Shutdown();
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan ImGui smoke is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    engine.SetImGuiEditorCallback(
        []
        {
            unsigned char* pixels = nullptr;
            int width = 0;
            int height = 0;
            int bytesPerPixel = 0;
            ImGui::GetIO().Fonts->GetTexDataAsAlpha8(&pixels, &width, &height, &bytesPerPixel);
            const ImVec2 whiteUv = ImGui::GetFontTexUvWhitePixel();
            CpuFontAtlasProbe probe{};
            probe.Width = width > 0 ? static_cast<std::uint32_t>(width) : 0u;
            probe.Height = height > 0 ? static_cast<std::uint32_t>(height) : 0u;
            if (pixels != nullptr && width > 0 && height > 0 && bytesPerPixel == 1)
            {
                const auto x = static_cast<int>(whiteUv.x * static_cast<float>(width));
                const auto y = static_cast<int>(whiteUv.y * static_cast<float>(height));
                const int clampedX = std::clamp(x, 0, width - 1);
                const int clampedY = std::clamp(y, 0, height - 1);
                probe.X = static_cast<std::uint32_t>(clampedX);
                probe.Y = static_cast<std::uint32_t>(clampedY);
                probe.Alpha = pixels[static_cast<std::size_t>(clampedY * width + clampedX)];
            }
            g_LastCpuFontAtlasProbe = probe;

            ImDrawList* foreground = ImGui::GetForegroundDrawList();
            const int firstNewVertex = foreground->VtxBuffer.Size;
            foreground->AddRectFilled(
                ImVec2(32.0f, 32.0f),
                ImVec2(96.0f, 96.0f),
                IM_COL32(255, 0, 0, 255));
            if (firstNewVertex >= 0 && firstNewVertex < foreground->VtxBuffer.Size)
            {
                const ImDrawVert& vertex = foreground->VtxBuffer[firstNewVertex];
                g_LastCpuFontAtlasProbe.DrawVertexU = vertex.uv.x;
                g_LastCpuFontAtlasProbe.DrawVertexV = vertex.uv.y;
                g_LastCpuFontAtlasProbe.DrawVertexColor = vertex.col;
            }
        });

    engine.Run();

    const auto status = EvaluateVulkanDeviceOperationalStatus(&device);
    if (!device.IsOperational())
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during ImGui visible-pixel frame: status="
                      << ToString(status.Code) << " reason=" << ToString(status.Reason);
        return;
    }

    const auto& stats = renderer.GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);
    EXPECT_EQ(stats.DefaultRecipeBackbufferReadbackCopyCount, 1u)
        << "Visible ImGui pixel smoke requires one backbuffer readback copy.";
    const auto* pass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(pass, nullptr)
        << "Default recipe omitted ImGuiPass despite adapter-produced overlay work.";
    EXPECT_EQ(pass->Status, RenderCommandPassStatus::Recorded)
        << "ImGuiPass did not record on the operational Vulkan command stream.";

    const auto& diag = engine.GetImGuiAdapter().GetDiagnostics();
    EXPECT_GE(diag.FramesProduced, 1u);
    EXPECT_GT(diag.LastVertexCount, 0u);
    EXPECT_GT(diag.LastIndexCount, 0u);
    EXPECT_GT(diag.LastCommandCount, 0u);

    std::vector<std::uint8_t> readbackBytes(static_cast<std::size_t>(readbackSize), 0u);
    device.ReadBuffer(readbackBuffer, readbackBytes.data(), readbackSize, 0u);

    const std::uint64_t rowStride =
        static_cast<std::uint64_t>(bytesPerPixel) *
        static_cast<std::uint64_t>(kReadbackWidth);
    const std::uint64_t pixelOffset =
        static_cast<std::uint64_t>(kImGuiProbeY) * rowStride +
        static_cast<std::uint64_t>(kImGuiProbeX) * static_cast<std::uint64_t>(bytesPerPixel);
    ASSERT_LE(pixelOffset + 4u, readbackSize);

    std::uint32_t redPixelCount = 0u;
    std::uint32_t firstRedX = 0u;
    std::uint32_t firstRedY = 0u;
    for (std::uint32_t y = 0u; y < kReadbackHeight; ++y)
    {
        for (std::uint32_t x = 0u; x < kReadbackWidth; ++x)
        {
            const std::uint64_t currentOffset =
                static_cast<std::uint64_t>(y) * rowStride +
                static_cast<std::uint64_t>(x) * static_cast<std::uint64_t>(bytesPerPixel);
            const Rgba8Pixel pixel = ReorderToRgba(
                backbufferFormat,
                readbackBytes[static_cast<std::size_t>(currentOffset + 0u)],
                readbackBytes[static_cast<std::size_t>(currentOffset + 1u)],
                readbackBytes[static_cast<std::size_t>(currentOffset + 2u)],
                readbackBytes[static_cast<std::size_t>(currentOffset + 3u)]);
            if (pixel.R > 200u && pixel.G < 96u && pixel.B < 96u)
            {
                if (redPixelCount == 0u)
                {
                    firstRedX = x;
                    firstRedY = y;
                }
                ++redPixelCount;
            }
        }
    }

    const std::uint32_t flippedProbeY = kReadbackHeight - kImGuiProbeY;
    const std::uint64_t flippedPixelOffset =
        static_cast<std::uint64_t>(flippedProbeY) * rowStride +
        static_cast<std::uint64_t>(kImGuiProbeX) * static_cast<std::uint64_t>(bytesPerPixel);
    ASSERT_LE(flippedPixelOffset + 4u, readbackSize);
    const Rgba8Pixel flippedProbe = ReorderToRgba(
        backbufferFormat,
        readbackBytes[static_cast<std::size_t>(flippedPixelOffset + 0u)],
        readbackBytes[static_cast<std::size_t>(flippedPixelOffset + 1u)],
        readbackBytes[static_cast<std::size_t>(flippedPixelOffset + 2u)],
        readbackBytes[static_cast<std::size_t>(flippedPixelOffset + 3u)]);

    const Rgba8Pixel actual = ReorderToRgba(
        backbufferFormat,
        readbackBytes[static_cast<std::size_t>(pixelOffset + 0u)],
        readbackBytes[static_cast<std::size_t>(pixelOffset + 1u)],
        readbackBytes[static_cast<std::size_t>(pixelOffset + 2u)],
        readbackBytes[static_cast<std::size_t>(pixelOffset + 3u)]);
    EXPECT_GT(actual.R, 200u)
        << "Expected the ImGui red rectangle to reach the backbuffer at "
        << kImGuiProbeX << "," << kImGuiProbeY << "; actual RGBA=("
        << static_cast<int>(actual.R) << ","
        << static_cast<int>(actual.G) << ","
        << static_cast<int>(actual.B) << ","
        << static_cast<int>(actual.A) << "), redPixelCount=" << redPixelCount
        << ", firstRed=(" << firstRedX << "," << firstRedY << ")"
        << ", flippedProbeY=" << flippedProbeY << " flippedProbeRGBA=("
        << static_cast<int>(flippedProbe.R) << ","
        << static_cast<int>(flippedProbe.G) << ","
        << static_cast<int>(flippedProbe.B) << ","
        << static_cast<int>(flippedProbe.A) << ")"
        << ", cpuFontWhitePixel=(" << g_LastCpuFontAtlasProbe.X << ","
        << g_LastCpuFontAtlasProbe.Y << ") atlas="
        << g_LastCpuFontAtlasProbe.Width << "x" << g_LastCpuFontAtlasProbe.Height
        << " alpha=" << static_cast<int>(g_LastCpuFontAtlasProbe.Alpha)
        << ", cpuDrawVertexUV=(" << g_LastCpuFontAtlasProbe.DrawVertexU << ","
        << g_LastCpuFontAtlasProbe.DrawVertexV << ") color=0x"
        << std::hex << g_LastCpuFontAtlasProbe.DrawVertexColor << std::dec;
    EXPECT_LT(actual.G, 96u);
    EXPECT_LT(actual.B, 96u);

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    engine.Shutdown();
}
