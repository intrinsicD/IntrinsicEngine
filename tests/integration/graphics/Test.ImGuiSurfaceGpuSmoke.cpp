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

#include "RuntimeTestModule.hpp"

import Extrinsic.Backends.Vulkan;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Graphics.Renderer;
import Extrinsic.Platform.Backend.Glfw;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureUpload;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorUiModule;
import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.EngineConfigBoot;

namespace
{
namespace Counters = Extrinsic::Tests::Support::OperationalCounterStability;

using Extrinsic::Backends::Vulkan::EvaluateVulkanDeviceOperationalStatus;
using Extrinsic::Backends::Vulkan::GetVulkanDeviceOperationalInputs;
using Extrinsic::Backends::Vulkan::GetVulkanOperationalDiagnosticsSnapshot;
using Extrinsic::Backends::Vulkan::ToString;
using Extrinsic::Graphics::RenderCommandPassStatus;
using Extrinsic::Runtime::Engine;

inline constexpr std::uint32_t kReadbackWidth = 256u;
inline constexpr std::uint32_t kReadbackHeight = 256u;
inline constexpr std::uint32_t kImGuiProbeX = 64u;
inline constexpr std::uint32_t kImGuiProbeY = 40u;
inline constexpr std::uint32_t kImGuiClippedProbeY = 80u;

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

class ExitAfterFramesApp final : public Intrinsic::Tests::RuntimeTestModule
{
public:
    explicit ExitAfterFramesApp(const std::uint32_t targetFrames) noexcept
        : m_TargetFrames(targetFrames)
    {
    }

    void Resolve() override {}
    void Simulate(double) override {}

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

struct ImGuiSmokeBootstrap
{
    std::unique_ptr<Engine> EnginePtr;
    Extrinsic::Runtime::EditorUiHost* EditorUi{nullptr};
    bool Skipped{false};
    std::string SkipReason;
};

[[nodiscard]] ImGuiSmokeBootstrap BootstrapOperationalDefaultRecipe(
    const std::uint32_t targetFrames = 4u)
{
    if (!Extrinsic::Platform::Backends::Glfw::CanInitialize())
    {
        return ImGuiSmokeBootstrap{
            .EnginePtr  = nullptr,
            .Skipped    = true,
            .SkipReason = "GLFW could not initialize in this environment; "
                          "gpu;vulkan ImGui smoke is opt-in.",
        };
    }

    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();
    config.Window.Title = "Intrinsic ImGui gpu;vulkan smoke";
    config.Window.Width = kReadbackWidth;
    config.Window.Height = kReadbackHeight;
    config.Window.Resizable = false;
    config.Render.EnableValidation = false;
    config.Render.EnableVSync = false;

    auto enginePtr = std::make_unique<Engine>(config);
    Intrinsic::Tests::AddRuntimeTestModule(*enginePtr,
                                           std::make_unique<ExitAfterFramesApp>(targetFrames));
    enginePtr->EmplaceModule<Extrinsic::Runtime::EditorUiModule>();
    enginePtr->Initialize();
    auto* const editorUi =
        enginePtr->Services().Find<Extrinsic::Runtime::EditorUiHost>();

    const auto initInputs = GetVulkanDeviceOperationalInputs(&enginePtr->GetDevice());
    if (!initInputs.LogicalDeviceReady || !initInputs.SwapchainReady || !initInputs.CommandSyncReady)
    {
        enginePtr->Shutdown();
        return ImGuiSmokeBootstrap{
            .EnginePtr  = nullptr,
            .Skipped    = true,
            .SkipReason = "Promoted Vulkan did not reach "
                          "logical-device/swapchain/command-sync readiness on this host.",
        };
    }

    return ImGuiSmokeBootstrap{
        .EnginePtr = std::move(enginePtr),
        .EditorUi = editorUi,
        .Skipped = false,
        .SkipReason = {},
    };
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

void DrawLargeSelectedEntityInspectorPayload()
{
    ImGui::SetNextWindowPos(ImVec2(6.0f, 18.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(244.0f, 228.0f), ImGuiCond_Always);
    ImGui::Begin("GRAPHICS-114 Selected Entity Inspector");
    ImGui::TextUnformatted("Selected entity: ReferenceTriangle");
    ImGui::TextUnformatted("Stable id: 101");
    ImGui::Separator();
    for (int section = 0; section < 4; ++section)
    {
        ImGui::PushID(section);
        if (ImGui::TreeNodeEx("Component group", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (int row = 0; row < 5; ++row)
            {
                ImGui::PushID(row);
                bool enabled = ((section + row) % 2) == 0;
                float values[3] = {
                    static_cast<float>(section),
                    static_cast<float>(row),
                    static_cast<float>(section * row) * 0.25f,
                };
                ImGui::Checkbox("enabled", &enabled);
                ImGui::SameLine();
                ImGui::DragFloat3("value", values, 0.01f);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::End();

    ImDrawList* foreground = ImGui::GetForegroundDrawList();
    for (int y = 0; y < 12; ++y)
    {
        for (int x = 0; x < 20; ++x)
        {
            const float left = 8.0f + static_cast<float>(x) * 12.0f;
            const float top = 8.0f + static_cast<float>(y) * 10.0f;
            foreground->AddRectFilled(
                ImVec2(left, top),
                ImVec2(left + 7.0f, top + 5.0f),
                IM_COL32(40 + (x * 7) % 180, 70 + (y * 11) % 160, 210, 180));
        }
    }
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
    ASSERT_NE(bootstrap.EditorUi, nullptr);
    ASSERT_TRUE(bootstrap.EditorUi->IsOperational());

    const auto contribution =
        bootstrap.EditorUi->RegisterFrameContribution(
        []
        {
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
            ImGui::SetNextWindowSize(ImVec2(128.0f, 96.0f));
            ImGui::Begin("GRAPHICS-079 Vulkan ImGui");
            ImGui::Image(static_cast<ImTextureID>(77u), ImVec2(16.0f, 16.0f));
            ImGui::End();
        });
    ASSERT_TRUE(contribution.IsValid());

    const Counters::Snapshot before =
        ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    engine.Run();
    const Counters::Snapshot after =
        ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());

    const auto status = EvaluateVulkanDeviceOperationalStatus(&engine.GetDevice());
    if (!engine.GetDevice().IsOperational())
    {
        EXPECT_TRUE(
            bootstrap.EditorUi->UnregisterFrameContribution(contribution));
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during "
                         "ImGui frame: status="
                      << ToString(status.Code) << " reason=" << ToString(status.Reason);
        return;
    }

    EXPECT_EQ(status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

    const auto& diag = bootstrap.EditorUi->GetDiagnostics();
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
    ASSERT_NE(pass, nullptr) << "Default recipe omitted ImGuiPass despite "
                                "adapter-produced overlay work.";
    EXPECT_EQ(pass->Status, RenderCommandPassStatus::Recorded)
        << "ImGuiPass did not record on the operational Vulkan command stream.";

    EXPECT_TRUE(Counters::IsStable(before, after))
        << "Vulkan fallback counters incremented across the ImGui frame: "
        << "fallbackToNull " << before.FallbackToNull << " -> " << after.FallbackToNull
        << ", initFailure " << before.InitFailure << " -> " << after.InitFailure
        << ", validationError " << before.ValidationError << " -> " << after.ValidationError
        << ", gateFailure " << before.OperationalGateFailure << " -> " << after.OperationalGateFailure;

    EXPECT_TRUE(
        bootstrap.EditorUi->UnregisterFrameContribution(contribution));
    engine.Shutdown();
}

TEST(ImGuiSurfaceGpuSmoke, LargeSelectedEntityPayloadRetainsAtlasOnOperationalVulkan)
{
    auto bootstrap = BootstrapOperationalDefaultRecipe(6u);
    if (bootstrap.Skipped)
    {
        GTEST_SKIP() << bootstrap.SkipReason;
    }
    Engine& engine = *bootstrap.EnginePtr;
    ASSERT_NE(bootstrap.EditorUi, nullptr);
    ASSERT_TRUE(bootstrap.EditorUi->IsOperational());

    const auto contribution =
        bootstrap.EditorUi->RegisterFrameContribution(
            DrawLargeSelectedEntityInspectorPayload);
    ASSERT_TRUE(contribution.IsValid());

    const Counters::Snapshot before =
        ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());
    engine.Run();
    const Counters::Snapshot after =
        ToCounterSnapshot(GetVulkanOperationalDiagnosticsSnapshot());

    const auto status = EvaluateVulkanDeviceOperationalStatus(&engine.GetDevice());
    if (!engine.GetDevice().IsOperational())
    {
        EXPECT_TRUE(
            bootstrap.EditorUi->UnregisterFrameContribution(contribution));
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during "
                         "large selected-entity ImGui frame: status="
                      << ToString(status.Code) << " reason=" << ToString(status.Reason);
        return;
    }

    EXPECT_EQ(status.Code, Extrinsic::Backends::Vulkan::VulkanOperationalStatusCode::Operational);
    EXPECT_EQ(status.Reason, Extrinsic::Backends::Vulkan::VulkanOperationalReason::None);

    const auto& diag = bootstrap.EditorUi->GetDiagnostics();
    EXPECT_GE(diag.FramesProduced, 6u);
    EXPECT_EQ(diag.FontAtlasCopyCount, 1u);
    EXPECT_GE(diag.FontAtlasReuseCount, 5u);
    EXPECT_FALSE(diag.LastFrameFontAtlasCopied);
    EXPECT_GT(diag.LastFontAtlasByteCount, 0u);
    EXPECT_EQ(diag.LastFrameFontAtlasCopyBytes, 0u);
    EXPECT_GE(diag.LastDrawListCount, 1u);
    EXPECT_GE(diag.LastVertexCount, 900u);
    EXPECT_GE(diag.LastIndexCount, 1200u);
    EXPECT_GE(diag.LastCommandCount, 1u);
    EXPECT_EQ(diag.LastFrameVertexCopyBytes,
              static_cast<std::uint64_t>(diag.LastVertexCount) *
                  sizeof(Extrinsic::Graphics::ImGuiOverlayVertex));
    EXPECT_EQ(diag.LastFrameIndexCopyBytes,
              static_cast<std::uint64_t>(diag.LastIndexCount) *
                  sizeof(std::uint32_t));
    EXPECT_EQ(diag.LastFrameCommandCopyBytes,
              static_cast<std::uint64_t>(diag.LastCommandCount) *
                  sizeof(Extrinsic::Graphics::ImGuiOverlayDrawCommand));
    EXPECT_EQ(diag.LastFrameOverlayCopyBytes,
              diag.LastFrameFontAtlasCopyBytes +
                  diag.LastFrameVertexCopyBytes +
                  diag.LastFrameIndexCopyBytes +
                  diag.LastFrameCommandCopyBytes);

    const auto& stats = engine.GetRenderer().GetLastRenderGraphStats();
    EXPECT_TRUE(stats.Compile.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.Succeeded) << stats.Diagnostic;
    EXPECT_TRUE(stats.Execute.DeviceOperational);

    const auto* pass = FindCommandPass(stats, "ImGuiPass");
    ASSERT_NE(pass, nullptr) << "Default recipe omitted ImGuiPass despite large "
                                "selected-entity overlay work.";
    EXPECT_EQ(pass->Status, RenderCommandPassStatus::Recorded)
        << "ImGuiPass did not record the large selected-entity overlay payload.";

    EXPECT_TRUE(Counters::IsStable(before, after))
        << "Vulkan fallback counters incremented across the large "
           "selected-entity ImGui frame: "
        << "fallbackToNull " << before.FallbackToNull << " -> " << after.FallbackToNull
        << ", initFailure " << before.InitFailure << " -> " << after.InitFailure
        << ", validationError " << before.ValidationError << " -> " << after.ValidationError
        << ", gateFailure " << before.OperationalGateFailure << " -> "
        << after.OperationalGateFailure;

    EXPECT_TRUE(
        bootstrap.EditorUi->UnregisterFrameContribution(contribution));
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
    ASSERT_NE(bootstrap.EditorUi, nullptr);
    ASSERT_TRUE(bootstrap.EditorUi->IsOperational());

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
        GTEST_SKIP() << "Readback buffer allocation failed; gpu;vulkan ImGui smoke "
                        "is opt-in.";
    }
    renderer.SetDefaultRecipeBackbufferReadbackBuffer(readbackBuffer);

    const auto contribution =
        bootstrap.EditorUi->RegisterFrameContribution(
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
            foreground->PushClipRect(
                ImVec2(32.0f, 32.0f),
                ImVec2(96.0f, 64.0f),
                true);
            foreground->AddRectFilled(
                ImVec2(32.0f, 32.0f),
                ImVec2(96.0f, 96.0f),
                IM_COL32(255, 0, 0, 255));
            foreground->PopClipRect();
            if (firstNewVertex >= 0 && firstNewVertex < foreground->VtxBuffer.Size)
            {
                const ImDrawVert& vertex = foreground->VtxBuffer[firstNewVertex];
                g_LastCpuFontAtlasProbe.DrawVertexU = vertex.uv.x;
                g_LastCpuFontAtlasProbe.DrawVertexV = vertex.uv.y;
                g_LastCpuFontAtlasProbe.DrawVertexColor = vertex.col;
            }
        });
    ASSERT_TRUE(contribution.IsValid());

    engine.Run();

    const auto status = EvaluateVulkanDeviceOperationalStatus(&device);
    if (!device.IsOperational())
    {
        renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
        device.DestroyBuffer(readbackBuffer);
        EXPECT_TRUE(
            bootstrap.EditorUi->UnregisterFrameContribution(contribution));
        engine.Shutdown();
        ADD_FAILURE() << "Promoted Vulkan operational gate did not flip during "
                         "ImGui visible-pixel frame: status="
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
    ASSERT_NE(pass, nullptr) << "Default recipe omitted ImGuiPass despite "
                                "adapter-produced overlay work.";
    EXPECT_EQ(pass->Status, RenderCommandPassStatus::Recorded)
        << "ImGuiPass did not record on the operational Vulkan command stream.";

    const auto& diag = bootstrap.EditorUi->GetDiagnostics();
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

    const std::uint64_t clippedPixelOffset =
        static_cast<std::uint64_t>(kImGuiClippedProbeY) * rowStride +
        static_cast<std::uint64_t>(kImGuiProbeX) *
            static_cast<std::uint64_t>(bytesPerPixel);
    ASSERT_LE(clippedPixelOffset + 4u, readbackSize);
    const Rgba8Pixel clipped = ReorderToRgba(
        backbufferFormat,
        readbackBytes[static_cast<std::size_t>(clippedPixelOffset + 0u)],
        readbackBytes[static_cast<std::size_t>(clippedPixelOffset + 1u)],
        readbackBytes[static_cast<std::size_t>(clippedPixelOffset + 2u)],
        readbackBytes[static_cast<std::size_t>(clippedPixelOffset + 3u)]);
    EXPECT_FALSE(clipped.R > 200u && clipped.G < 96u && clipped.B < 96u)
        << "Expected the ImGui command scissor to reject the red rectangle at "
        << kImGuiProbeX << "," << kImGuiClippedProbeY << "; actual RGBA=("
        << static_cast<int>(clipped.R) << ","
        << static_cast<int>(clipped.G) << ","
        << static_cast<int>(clipped.B) << ","
        << static_cast<int>(clipped.A) << ")";

    renderer.SetDefaultRecipeBackbufferReadbackBuffer(Extrinsic::RHI::BufferHandle{});
    device.DestroyBuffer(readbackBuffer);
    EXPECT_TRUE(
        bootstrap.EditorUi->UnregisterFrameContribution(contribution));
    engine.Shutdown();
}
