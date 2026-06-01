// RUNTIME-090 Slice A — contract coverage for the runtime-side Dear ImGui
// adapter (`Extrinsic.Runtime.ImGuiAdapter`). These cases drive the adapter
// against a `FakeWindow` test double so the produce path (context lifecycle,
// Platform::Event -> ImGui IO pump, ImDrawData -> ImGuiOverlayFrame walk,
// editor hook, diagnostics) is verified on the CPU/null gate with no GLFW or
// Vulkan dependency. `imgui.h` is included directly only to synthesize a real
// panel draw in the draw-list test.

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

TEST(ImGuiAdapter, PumpsInputEventsAndResizeUpdatesDisplaySize)
{
    FakeWindow         window(100, 100);
    ImGuiOverlaySystem overlay;
    ImGuiAdapter       adapter(window, overlay);

    ASSERT_TRUE(adapter.Initialize());

    window.QueueEvent(Plat::CursorEvent{10.0, 20.0});
    window.QueueEvent(Plat::MouseButtonEvent{0, true});
    window.QueueEvent(Plat::ScrollEvent{0.0, 1.0});
    window.QueueEvent(Plat::CharEvent{static_cast<unsigned int>('A')});
    window.QueueEvent(Plat::WindowResizeEvent{800, 600});

    adapter.BeginFrame(kFrameDelta);
    adapter.EndFrame();

    const auto& diag = adapter.GetDiagnostics();
    EXPECT_EQ(diag.PumpedEventCount, 5u);
    EXPECT_EQ(diag.DisplayWidth, 800u);  // resize event overrides the window extent
    EXPECT_EQ(diag.DisplayHeight, 600u);
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
