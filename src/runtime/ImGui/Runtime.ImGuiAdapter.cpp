module;

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <imgui.h>

module Extrinsic.Runtime.ImGuiAdapter;

import Extrinsic.Platform.Window;
import Extrinsic.Graphics.ImGuiOverlaySystem;

namespace Extrinsic::Runtime
{
    namespace
    {
        // Apply the window's logical size and HiDPI scale onto ImGui IO. ImGui
        // expects `DisplaySize` in window coordinates and `DisplayFramebufferScale`
        // as framebuffer-pixels-per-window-unit, so the overlay frame can report
        // true pixel dimensions to the renderer.
        void ApplyDisplayMetrics(ImGuiIO& io, const Platform::Extent2D win, const Platform::Extent2D fb)
        {
            const float winW = win.Width > 0 ? static_cast<float>(win.Width) : 0.0f;
            const float winH = win.Height > 0 ? static_cast<float>(win.Height) : 0.0f;
            io.DisplaySize = ImVec2(winW, winH);

            float scaleX = 1.0f;
            float scaleY = 1.0f;
            if (win.Width > 0 && fb.Width > 0)
                scaleX = static_cast<float>(fb.Width) / winW;
            if (win.Height > 0 && fb.Height > 0)
                scaleY = static_cast<float>(fb.Height) / winH;
            io.DisplayFramebufferScale = ImVec2(scaleX, scaleY);
        }

        [[nodiscard]] std::uint32_t ToPixelDimension(const float value)
        {
            return value > 0.0f ? static_cast<std::uint32_t>(value + 0.5f) : 0u;
        }
    }

    ImGuiAdapter::ImGuiAdapter(Platform::IWindow& window, Graphics::ImGuiOverlaySystem& overlaySystem)
        : m_Window(window)
        , m_OverlaySystem(overlaySystem)
    {
    }

    ImGuiAdapter::~ImGuiAdapter()
    {
        Shutdown();
    }

    bool ImGuiAdapter::Initialize()
    {
        if (m_Context != nullptr)
            return false;

        IMGUI_CHECKVERSION();
        m_Context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_Context);
        ConfigureIo();
        m_OverlaySystem.Initialize();

        m_Diagnostics.Initialized = true;
        m_FrameStarted = false;
        return true;
    }

    void ImGuiAdapter::ConfigureIo()
    {
        ImGui::SetCurrentContext(m_Context);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // the engine owns persistence; never write imgui.ini
        io.LogFilename = nullptr;
        io.BackendPlatformName = "Extrinsic.Runtime.ImGuiAdapter";
        io.BackendRendererName = "Extrinsic.Graphics.ImGuiOverlaySystem";
        // 1.92 dynamic texture system: ImGui owns the font atlas lifecycle, so
        // NewFrame()/Render() run headless without a pre-built atlas or
        // SetTexID(). The graphics-owned retained atlas upload is GRAPHICS-079.
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        io.BackendFlags |= ImGuiBackendFlags_HasSetMousePos;

        ImGuiPlatformIO& platformIo = ImGui::GetPlatformIO();
        platformIo.Platform_ClipboardUserData = this;
        platformIo.Platform_GetClipboardTextFn = &ImGuiAdapter::ClipboardGet;
        platformIo.Platform_SetClipboardTextFn = &ImGuiAdapter::ClipboardSet;

        ApplyDisplayMetrics(io, m_Window.GetWindowExtent(), m_Window.GetFramebufferExtent());
    }

    void ImGuiAdapter::BeginFrame(double deltaSeconds)
    {
        if (m_Context == nullptr)
            return;

        ImGui::SetCurrentContext(m_Context);
        ImGuiIO& io = ImGui::GetIO();
        // Refresh from the window first; queued resize events pumped below win.
        ApplyDisplayMetrics(io, m_Window.GetWindowExtent(), m_Window.GetFramebufferExtent());
        io.DeltaTime = deltaSeconds > 0.0 ? static_cast<float>(deltaSeconds) : (1.0f / 60.0f);

        PumpEvents();

        ImGui::NewFrame();
        m_FrameStarted = true;
    }

    void ImGuiAdapter::PumpEvents()
    {
        ImGuiIO& io = ImGui::GetIO();
        const std::vector<Platform::Event> events = m_Window.DrainEvents();
        for (const Platform::Event& event : events)
        {
            ++m_Diagnostics.PumpedEventCount;
            std::visit(
                [&io](const auto& e)
                {
                    using T = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<T, Platform::CursorEvent>)
                    {
                        io.AddMousePosEvent(static_cast<float>(e.XPos), static_cast<float>(e.YPos));
                    }
                    else if constexpr (std::is_same_v<T, Platform::MouseButtonEvent>)
                    {
                        if (e.ButtonCode >= 0 && e.ButtonCode < ImGuiMouseButton_COUNT)
                            io.AddMouseButtonEvent(e.ButtonCode, e.IsPressed);
                    }
                    else if constexpr (std::is_same_v<T, Platform::ScrollEvent>)
                    {
                        io.AddMouseWheelEvent(static_cast<float>(e.XOffset), static_cast<float>(e.YOffset));
                    }
                    else if constexpr (std::is_same_v<T, Platform::CharEvent>)
                    {
                        io.AddInputCharacter(e.Character);
                    }
                    // WindowResizeEvent is intentionally NOT written into
                    // io.DisplaySize. Display metrics are refreshed every frame
                    // from the authoritative window port (logical
                    // GetWindowExtent + framebuffer GetFramebufferExtent) at the
                    // top of BeginFrame, keeping io.DisplaySize logical and
                    // io.DisplayFramebufferScale carrying the pixel ratio. The
                    // resize payload is in backend-specific framebuffer pixels
                    // (the GLFW backend emits framebuffer width/height for both
                    // the resize and content-scale callbacks), so writing it into
                    // io.DisplaySize would double-scale the overlay frame on
                    // HiDPI displays (DisplaySize * DisplayFramebufferScale).
                    // WindowCloseEvent / KeyEvent / WindowDropEvent are likewise
                    // counted as pumped but not translated: GLFW key-code ->
                    // ImGuiKey mapping is owned by the editor input-binding slice
                    // (Slice A non-goal), and close/drop have no ImGui IO
                    // equivalent.
                },
                event);
        }
    }

    void ImGuiAdapter::EndFrame()
    {
        if (m_Context == nullptr || !m_FrameStarted)
            return;

        ImGui::SetCurrentContext(m_Context);
        if (m_EditorCallback)
        {
            m_EditorCallback();
            ++m_Diagnostics.EditorCallbackInvocations;
        }

        ImGui::Render();
        const ImDrawData* drawData = ImGui::GetDrawData();
        // The font atlas texture reference. User-texture detection compares each
        // draw command's TexRef against this without calling GetTexID(): under
        // ImGuiBackendFlags_RendererHasTextures the atlas is not yet uploaded to
        // a GPU id (that upload is GRAPHICS-079), so GetTexID() would assert.
        const ImTextureData* atlasTexData = ImGui::GetIO().Fonts->TexRef._TexData;

        Graphics::ImGuiOverlayFrame frame;
        frame.Enabled = true;

        std::uint32_t drawListCount = 0u;
        std::uint32_t vertexCount = 0u;
        std::uint32_t indexCount = 0u;
        std::uint32_t commandCount = 0u;
        bool usesUserTexture = false;
        std::uint32_t pixelWidth = 0u;
        std::uint32_t pixelHeight = 0u;

        if (drawData != nullptr && drawData->Valid)
        {
            pixelWidth = ToPixelDimension(drawData->DisplaySize.x * drawData->FramebufferScale.x);
            pixelHeight = ToPixelDimension(drawData->DisplaySize.y * drawData->FramebufferScale.y);
            frame.DrawLists.reserve(static_cast<std::size_t>(drawData->CmdListsCount));
            for (int i = 0; i < drawData->CmdListsCount; ++i)
            {
                const ImDrawList* cmdList = drawData->CmdLists[i];
                if (cmdList == nullptr)
                    continue;

                Graphics::ImGuiOverlayDrawList overlayList;
                overlayList.CommandCount = static_cast<std::uint32_t>(cmdList->CmdBuffer.Size);
                overlayList.VertexCount = static_cast<std::uint32_t>(cmdList->VtxBuffer.Size);
                overlayList.IndexCount = static_cast<std::uint32_t>(cmdList->IdxBuffer.Size);
                for (int c = 0; c < cmdList->CmdBuffer.Size; ++c)
                {
                    // A command referencing a texture other than the font atlas
                    // is a user texture (e.g. ImGui::Image).
                    if (cmdList->CmdBuffer[c].TexRef._TexData != atlasTexData)
                    {
                        overlayList.UsesUserTexture = true;
                        usesUserTexture = true;
                    }
                }

                drawListCount += 1u;
                vertexCount += overlayList.VertexCount;
                indexCount += overlayList.IndexCount;
                commandCount += overlayList.CommandCount;
                frame.DrawLists.push_back(overlayList);
            }
        }

        frame.DisplayWidth = pixelWidth;
        frame.DisplayHeight = pixelHeight;
        m_OverlaySystem.SubmitFrame(frame);

        m_Diagnostics.FramesProduced += 1u;
        m_Diagnostics.LastDrawListCount = drawListCount;
        m_Diagnostics.LastVertexCount = vertexCount;
        m_Diagnostics.LastIndexCount = indexCount;
        m_Diagnostics.LastCommandCount = commandCount;
        m_Diagnostics.LastFrameUsedUserTexture = usesUserTexture;
        m_Diagnostics.DisplayWidth = pixelWidth;
        m_Diagnostics.DisplayHeight = pixelHeight;
        m_FrameStarted = false;
    }

    void ImGuiAdapter::Shutdown()
    {
        if (m_Context == nullptr)
            return;

        m_OverlaySystem.Shutdown();
        DestroyContext();
        m_Diagnostics.Initialized = false;
        m_FrameStarted = false;
    }

    void ImGuiAdapter::RebuildForDisplayChange()
    {
        if (m_Context == nullptr)
            return;

        // DPI / font rebuild: tear the overlay system + context down and back up
        // exactly once. The overlay system reallocates its (graphics-owned) font
        // atlas on Initialize() once GRAPHICS-079 lands; here it is a CPU no-op.
        m_OverlaySystem.Shutdown();
        DestroyContext();

        m_Context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_Context);
        ConfigureIo();
        m_OverlaySystem.Initialize();

        m_FrameStarted = false;
        m_Diagnostics.ContextRebuilds += 1u;
    }

    void ImGuiAdapter::DestroyContext()
    {
        if (m_Context == nullptr)
            return;
        ImGui::DestroyContext(m_Context);
        m_Context = nullptr;
    }

    void ImGuiAdapter::SetEditorCallback(std::function<void()> callback)
    {
        m_EditorCallback = std::move(callback);
    }

    const char* ImGuiAdapter::ClipboardGet(ImGuiContext* /*ctx*/)
    {
        auto* self = static_cast<ImGuiAdapter*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
        if (self == nullptr)
            return "";
        self->m_ClipboardScratch = self->m_Window.GetClipboardText();
        return self->m_ClipboardScratch.c_str();
    }

    void ImGuiAdapter::ClipboardSet(ImGuiContext* /*ctx*/, const char* text)
    {
        auto* self = static_cast<ImGuiAdapter*>(ImGui::GetPlatformIO().Platform_ClipboardUserData);
        if (self == nullptr || text == nullptr)
            return;
        self->m_Window.SetClipboardText(text);
    }
}
