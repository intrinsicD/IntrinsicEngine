// RUNTIME-090 Slice A — contract coverage for the runtime-side Dear ImGui
// adapter (`Extrinsic.Runtime.ImGuiAdapter`). These cases drive the adapter
// against a `FakeWindow` test double so the produce path (context lifecycle,
// Platform::Event -> ImGui IO pump, ImDrawData -> ImGuiOverlayFrame walk,
// editor hook, diagnostics) is verified on the CPU/null gate with no GLFW or
// Vulkan dependency. `imgui.h` is included directly only to synthesize a real
// panel draw in the draw-list test.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>
#include <imgui.h>

import Extrinsic.Graphics.ImGuiOverlaySystem;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.ImGuiAdapter;

namespace Plat = Extrinsic::Platform;
namespace Runtime = Extrinsic::Runtime;

using Extrinsic::Graphics::ImGuiOverlaySystem;
using Extrinsic::Runtime::ImGuiAdapter;

namespace
{
    // Minimal in-process IWindow used to drive the adapter deterministically:
    // fixed window/framebuffer extents and a queue of synthesized events drained
    // exactly once. It needs no GLFW backend.
    class FakeWindow final : public Plat::IWindow
    {
    public:
        FakeWindow(const int width, const int height)
            : m_Extent{width, height}
            , m_Framebuffer{width, height}
        {
        }

        void QueueEvent(Plat::Event event) { m_Pending.push_back(std::move(event)); }

        // Simulate a resize / DPI change: the window port reports the new logical
        // window and framebuffer extents (as GLFW's callbacks update them before
        // events are drained).
        void SetExtents(const Plat::Extent2D window, const Plat::Extent2D framebuffer)
        {
            m_Extent = window;
            m_Framebuffer = framebuffer;
        }

        void PollEvents() override {}
        [[nodiscard]] bool ShouldClose() const override { return false; }
        [[nodiscard]] bool IsMinimized() const override { return false; }
        [[nodiscard]] bool WasResized() const override { return false; }
        void AcknowledgeResize() override {}
        [[nodiscard]] bool ConsumeInputActivity() override { return false; }
        [[nodiscard]] Plat::Extent2D GetWindowExtent() const override { return m_Extent; }
        [[nodiscard]] Plat::Extent2D GetFramebufferExtent() const override { return m_Framebuffer; }
        [[nodiscard]] void* GetNativeHandle() const override { return nullptr; }

        void Listen(EventCallbackFn) override {}
        [[nodiscard]] std::vector<Plat::Event> DrainEvents() override
        {
            std::vector<Plat::Event> drained;
            drained.swap(m_Pending);
            return drained;
        }

        void OnUpdate() override {}
        void WaitForEventsTimeout(double) override {}

        void SetClipboardText(std::string_view text) override { m_Clipboard = std::string(text); }
        [[nodiscard]] std::string GetClipboardText() const override { return m_Clipboard; }
        void SetCursorMode(Plat::CursorMode mode) override { m_CursorMode = mode; }
        [[nodiscard]] Plat::CursorMode GetCursorMode() const override { return m_CursorMode; }

    private:
        Plat::Extent2D           m_Extent{};
        Plat::Extent2D           m_Framebuffer{};
        std::vector<Plat::Event> m_Pending{};
        std::string              m_Clipboard{};
        Plat::CursorMode         m_CursorMode{Plat::CursorMode::Normal};
    };

    constexpr double kFrameDelta = 1.0 / 60.0;
}

// --- lifecycle + empty frame --------------------------------------------------

TEST(ImGuiAdapter, InitializeProducesZeroListFrameForEmptyWindow)
{
    FakeWindow         window(1280, 720);
    ImGuiOverlaySystem overlay;
    ImGuiAdapter       adapter(window, overlay);

    ASSERT_TRUE(adapter.Initialize());
    EXPECT_TRUE(adapter.IsInitialized());
    EXPECT_TRUE(overlay.IsInitialized());

    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();

    const auto& diag = adapter.GetDiagnostics();
    EXPECT_TRUE(diag.Initialized);
    EXPECT_EQ(diag.FramesProduced, 1u);
    EXPECT_EQ(diag.LastDrawListCount, 0u); // no panels -> no draw lists
    EXPECT_EQ(diag.LastVertexCount, 0u);
    EXPECT_EQ(diag.LastIndexCount, 0u);
    EXPECT_FALSE(diag.LastFrameUsedUserTexture);
    EXPECT_EQ(diag.DisplayWidth, 1280u);
    EXPECT_EQ(diag.DisplayHeight, 720u);
}

TEST(ImGuiAdapter, InitializeRejectsDoubleInitialize)
{
    FakeWindow         window(640, 480);
    ImGuiOverlaySystem overlay;
    ImGuiAdapter       adapter(window, overlay);

    ASSERT_TRUE(adapter.Initialize());
    EXPECT_FALSE(adapter.Initialize()); // second Initialize is a no-op
    EXPECT_TRUE(adapter.IsInitialized());
}

TEST(ImGuiAdapter, InitializeLoadsBundledRobotoAndBuildsLegacyFontAtlas)
{
    FakeWindow         window(1280, 720);
    ImGuiOverlaySystem overlay;
    ImGuiAdapter       adapter(window, overlay);

    ASSERT_TRUE(adapter.Initialize());

    ImGuiIO& io = ImGui::GetIO();
    EXPECT_EQ(io.BackendFlags & ImGuiBackendFlags_RendererHasTextures, 0);
    ASSERT_GT(io.Fonts->Fonts.Size, 0);
    const ImFont* font = io.Fonts->Fonts[0];
    ASSERT_NE(font, nullptr);
    EXPECT_NE(std::string_view(font->GetDebugName()).find("Roboto-Medium.ttf"),
              std::string_view::npos);

    unsigned char* fontPixels = nullptr;
    int fontWidth = 0;
    int fontHeight = 0;
    int fontBytesPerPixel = 0;
    io.Fonts->GetTexDataAsAlpha8(
        &fontPixels,
        &fontWidth,
        &fontHeight,
        &fontBytesPerPixel);
    ASSERT_NE(fontPixels, nullptr);
    EXPECT_GT(fontWidth, 0);
    EXPECT_GT(fontHeight, 0);
    EXPECT_EQ(fontBytesPerPixel, 1);

    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();

    const auto* frame = overlay.GetCurrentFrame();
    ASSERT_NE(frame, nullptr);
    EXPECT_TRUE(frame->FontAtlas.Valid);
    EXPECT_EQ(frame->FontAtlas.Width, static_cast<std::uint32_t>(fontWidth));
    EXPECT_EQ(frame->FontAtlas.Height, static_cast<std::uint32_t>(fontHeight));
    EXPECT_EQ(frame->FontAtlas.BytesPerPixel, 1u);
    EXPECT_FALSE(frame->FontAtlas.Pixels.empty());
}

TEST(ImGuiAdapter, FontAtlasPayloadIsCopiedOnlyWhenDirty)
{
    FakeWindow         window(1280, 720);
    ImGuiOverlaySystem overlay;
    ImGuiAdapter       adapter(window, overlay);

    ASSERT_TRUE(adapter.Initialize());

    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();
    {
        const auto& diag = adapter.GetDiagnostics();
        EXPECT_EQ(diag.FontAtlasCopyCount, 1u);
        EXPECT_EQ(diag.FontAtlasReuseCount, 0u);
        EXPECT_TRUE(diag.LastFrameFontAtlasCopied);
        EXPECT_GT(diag.LastFontAtlasByteCount, 0u);
        EXPECT_EQ(diag.LastFrameFontAtlasCopyBytes,
                  diag.LastFontAtlasByteCount);
    }
    const auto* firstFrame = overlay.GetCurrentFrame();
    ASSERT_NE(firstFrame, nullptr);
    ASSERT_FALSE(firstFrame->FontAtlas.Pixels.empty());
    const std::vector<std::byte> firstPixels = firstFrame->FontAtlas.Pixels;

    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();

    const auto& diag = adapter.GetDiagnostics();
    EXPECT_EQ(diag.FontAtlasCopyCount, 1u);
    EXPECT_EQ(diag.FontAtlasReuseCount, 1u);
    EXPECT_FALSE(diag.LastFrameFontAtlasCopied);
    EXPECT_EQ(diag.LastFontAtlasByteCount, firstPixels.size());
    EXPECT_EQ(diag.LastFrameFontAtlasCopyBytes, 0u);

    const auto* retainedFrame = overlay.GetCurrentFrame();
    ASSERT_NE(retainedFrame, nullptr);
    EXPECT_EQ(retainedFrame->FontAtlas.Pixels, firstPixels);
    EXPECT_TRUE(overlay.GetDiagnostics().FontAtlasRetained);
    EXPECT_EQ(overlay.GetDiagnostics().FontAtlasRetainCount, 1u);
}

// --- ImDrawData -> ImGuiOverlayFrame walk ------------------------------------

TEST(ImGuiAdapter, EditorPanelDrawProducesNonEmptyDrawList)
{
    FakeWindow         window(1280, 720);
    ImGuiOverlaySystem overlay;
    ImGuiAdapter       adapter(window, overlay);

    ASSERT_TRUE(adapter.Initialize());
    // Explicit pos/size so the window is not auto-fitting (an auto-fitting
    // window is rendered hidden on its first appearing frame while ImGui
    // measures it).
    adapter.SetEditorCallback(
        []
        {
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
            ImGui::SetNextWindowSize(ImVec2(220.0f, 120.0f));
            ImGui::Begin("RUNTIME-090 Panel");
            ImGui::Text("hello imgui");
            ImGui::End();
        });

    // Drive a warm-up frame followed by the asserted frame so window geometry is
    // produced regardless of ImGui's first-appearance handling.
    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();
    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();

    const auto& diag = adapter.GetDiagnostics();
    EXPECT_EQ(diag.FramesProduced, 2u);
    EXPECT_EQ(diag.EditorCallbackInvocations, 2u);
    EXPECT_GE(diag.LastDrawListCount, 1u);
    EXPECT_GT(diag.LastVertexCount, 0u);
    EXPECT_GT(diag.LastIndexCount, 0u);
    EXPECT_GE(diag.LastCommandCount, 1u);
    EXPECT_FALSE(diag.LastFrameUsedUserTexture); // a text panel only uses the font atlas
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
    EXPECT_GE(diag.LastEndFrameMicros, diag.LastEditorCallbackMicros);
    EXPECT_GE(diag.LastEndFrameMicros, diag.LastImGuiRenderMicros);
    EXPECT_GE(diag.LastEndFrameMicros, diag.LastDrawDataCopyMicros);
}

TEST(ImGuiAdapter, ImageDrawPreservesUserTextureBindlessCommand)
{
    FakeWindow         window(1280, 720);
    ImGuiOverlaySystem overlay;
    ImGuiAdapter       adapter(window, overlay);

    ASSERT_TRUE(adapter.Initialize());
    adapter.SetEditorCallback(
        []
        {
            ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f));
            ImGui::SetNextWindowSize(ImVec2(220.0f, 120.0f));
            ImGui::Begin("GRAPHICS-079 User Texture Panel");
            ImGui::Image(static_cast<ImTextureID>(77u), ImVec2(16.0f, 16.0f));
            ImGui::End();
        });

    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();
    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();

    const auto& diag = adapter.GetDiagnostics();
    EXPECT_EQ(diag.FramesProduced, 2u);
    EXPECT_TRUE(diag.LastFrameUsedUserTexture);

    const auto* frame = overlay.GetCurrentFrame();
    ASSERT_NE(frame, nullptr);
    bool foundUserTextureCommand = false;
    for (const Extrinsic::Graphics::ImGuiOverlayDrawList& drawList : frame->DrawLists)
    {
        for (const Extrinsic::Graphics::ImGuiOverlayDrawCommand& command : drawList.Commands)
        {
            if (command.UsesUserTexture && command.TextureBindlessIndex == 77u)
            {
                foundUserTextureCommand = true;
            }
        }
    }
    EXPECT_TRUE(foundUserTextureCommand);
}

// --- editor hook cadence ------------------------------------------------------

TEST(ImGuiAdapter, EditorCallbackInvokedOncePerFramePair)
{
    FakeWindow         window(800, 600);
    ImGuiOverlaySystem overlay;
    ImGuiAdapter       adapter(window, overlay);

    ASSERT_TRUE(adapter.Initialize());
    std::uint32_t calls = 0u;
    adapter.SetEditorCallback([&calls] { ++calls; });

    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();
    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();

    EXPECT_EQ(calls, 2u);
    EXPECT_EQ(adapter.GetDiagnostics().EditorCallbackInvocations, 2u);
    EXPECT_EQ(adapter.GetDiagnostics().FramesProduced, 2u);
}

// --- input pump + resize ------------------------------------------------------

// Input events pump into ImGui IO, and a resize on a HiDPI display reports
// framebuffer pixels in the overlay frame without double-scaling. The GLFW
// backend emits WindowResizeEvent in framebuffer pixels while io.DisplaySize
// must stay logical, so display metrics are sourced from the window port each
// frame, never from the resize payload.
TEST(ImGuiAdapter, PumpsInputAndResizeReportsFramebufferPixelsWithoutDoubleScaling)
{
    FakeWindow window(400, 300);
    window.SetExtents(Plat::Extent2D{400, 300}, Plat::Extent2D{800, 600}); // HiDPI, scale 2
    ImGuiOverlaySystem overlay;
    ImGuiAdapter       adapter(window, overlay);

    ASSERT_TRUE(adapter.Initialize());

    window.QueueEvent(Plat::CursorEvent{10.0, 20.0});
    window.QueueEvent(Plat::MouseButtonEvent{0, true});
    window.QueueEvent(Plat::ScrollEvent{0.0, 1.0});
    window.QueueEvent(Plat::CharEvent{static_cast<unsigned int>('A')});

    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();
    {
        const auto& diag = adapter.GetDiagnostics();
        EXPECT_EQ(diag.PumpedEventCount, 4u);
        // 400 logical * 2 scale = 800 framebuffer pixels.
        EXPECT_EQ(diag.DisplayWidth, 800u);
        EXPECT_EQ(diag.DisplayHeight, 600u);
    }

    // Resize on the HiDPI display: the window port reports the new logical and
    // framebuffer extents, and GLFW emits a framebuffer-pixel WindowResizeEvent.
    window.SetExtents(Plat::Extent2D{800, 600}, Plat::Extent2D{1600, 1200});
    window.QueueEvent(Plat::WindowResizeEvent{1600, 1200});

    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();
    {
        const auto& diag = adapter.GetDiagnostics();
        EXPECT_EQ(diag.PumpedEventCount, 5u);
        // Framebuffer pixels (1600x1200), NOT double-scaled (3200x2400) from
        // writing the framebuffer-pixel resize payload into io.DisplaySize.
        EXPECT_EQ(diag.DisplayWidth, 1600u);
        EXPECT_EQ(diag.DisplayHeight, 1200u);
    }
}

TEST(ImGuiAdapter, SnapshotsCaptureStateOncePerCompletedEditorFrame)
{
    FakeWindow         window(400, 300);
    ImGuiOverlaySystem overlay;
    ImGuiAdapter       adapter(window, overlay);

    ASSERT_TRUE(adapter.Initialize());
    EXPECT_FALSE(adapter.CaptureSnapshot().CapturedMouse);
    EXPECT_FALSE(adapter.CaptureSnapshot().CapturedKeyboard);
    EXPECT_EQ(adapter.GetDiagnostics().CaptureSnapshots, 0u);

    ImGui::SetNextFrameWantCaptureMouse(true);
    ImGui::SetNextFrameWantCaptureKeyboard(true);

    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();

    const Runtime::EditorInputCaptureSnapshot snapshot =
        adapter.CaptureSnapshot();
    EXPECT_TRUE(snapshot.CapturedMouse);
    EXPECT_TRUE(snapshot.CapturedKeyboard);
    EXPECT_TRUE(snapshot.CapturesViewportInput());
    EXPECT_EQ(adapter.GetDiagnostics().CaptureSnapshots, 1u);
}

TEST(ImGuiAdapter, HiddenEditorClearsStaleCaptureState)
{
    FakeWindow         window(400, 300);
    ImGuiOverlaySystem overlay;
    ImGuiAdapter       adapter(window, overlay);
    std::uint32_t      editorCallbacks = 0u;

    ASSERT_TRUE(adapter.Initialize());
    adapter.SetEditorCallback([&editorCallbacks] { ++editorCallbacks; });
    ImGui::SetNextFrameWantCaptureMouse(true);
    ImGui::SetNextFrameWantCaptureKeyboard(true);
    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();
    ASSERT_TRUE(adapter.CaptureSnapshot().CapturesViewportInput());
    ASSERT_EQ(editorCallbacks, 1u);

    adapter.SetEditorVisible(false);
    EXPECT_FALSE(adapter.IsEditorVisible());
    EXPECT_FALSE(adapter.CaptureSnapshot().CapturesViewportInput());

    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();
    EXPECT_FALSE(adapter.CaptureSnapshot().CapturedMouse);
    EXPECT_FALSE(adapter.CaptureSnapshot().CapturedKeyboard);
    EXPECT_FALSE(adapter.CaptureSnapshot().WidgetsActive);
    EXPECT_EQ(editorCallbacks, 1u);
}

// --- DPI / font rebuild -------------------------------------------------------

TEST(ImGuiAdapter, RebuildForDisplayChangeCyclesOverlayExactlyOnce)
{
    FakeWindow         window(1024, 768);
    ImGuiOverlaySystem overlay;
    ImGuiAdapter       adapter(window, overlay);

    ASSERT_TRUE(adapter.Initialize());
    adapter.RebuildForDisplayChange();

    EXPECT_EQ(adapter.GetDiagnostics().ContextRebuilds, 1u);
    EXPECT_TRUE(adapter.IsInitialized());
    EXPECT_TRUE(overlay.IsInitialized());

    // The adapter still produces frames after a rebuild.
    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();
    EXPECT_EQ(adapter.GetDiagnostics().FramesProduced, 1u);
}

TEST(ImGuiAdapter, RebuildBeforeInitializeIsUncounted)
{
    FakeWindow         window(1024, 768);
    ImGuiOverlaySystem overlay;
    ImGuiAdapter       adapter(window, overlay);

    adapter.RebuildForDisplayChange(); // no-op while uninitialized
    EXPECT_EQ(adapter.GetDiagnostics().ContextRebuilds, 0u);
    EXPECT_FALSE(adapter.IsInitialized());
}
