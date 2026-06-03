module;

#include <cstdint>
#include <functional>
#include <string>

// Opaque forward declaration of the Dear ImGui context. Keep this in the
// global module fragment so it denotes the same global-module entity declared
// by `imgui.h` in the matching implementation unit, without including `imgui.h`
// in this module interface (RUNTIME-090).
struct ImGuiContext;

export module Extrinsic.Runtime.ImGuiAdapter;

import Extrinsic.Platform.Window;
import Extrinsic.Graphics.ImGuiOverlaySystem;

export namespace Extrinsic::Runtime
{
    // Testable observables for the adapter. The adapter has no graphics or
    // GPU output of its own (it only produces `ImGuiOverlayFrame` records for
    // the overlay system), so contract tests assert the produce path through
    // these counters without including `imgui.h`.
    struct ImGuiAdapterDiagnostics
    {
        bool          Initialized{false};
        std::uint32_t FramesProduced{0u};            // EndFrame submissions to the overlay system
        std::uint32_t LastDrawListCount{0u};         // CmdLists in the most recent frame
        std::uint32_t LastVertexCount{0u};           // summed vertices in the most recent frame
        std::uint32_t LastIndexCount{0u};            // summed indices in the most recent frame
        std::uint32_t LastCommandCount{0u};          // summed draw commands in the most recent frame
        bool          LastFrameUsedUserTexture{false};
        std::uint32_t PumpedEventCount{0u};          // platform events forwarded to ImGui IO (cumulative)
        std::uint32_t ContextRebuilds{0u};           // RebuildForDisplayChange cycles performed
        std::uint32_t EditorCallbackInvocations{0u}; // editor hook invocations (cumulative)
        std::uint32_t DisplayWidth{0u};              // reported overlay-frame pixel width
        std::uint32_t DisplayHeight{0u};             // reported overlay-frame pixel height
    };

    // Runtime-side Dear ImGui platform/renderer adapter (RUNTIME-090, the
    // producer half declared by GRAPHICS-013CQ). Owns the ImGui context
    // lifecycle, translates `Platform::Event` variants into ImGui IO, walks
    // `ImDrawData` after `ImGui::Render()`, and submits `ImGuiOverlayFrame`
    // records to the graphics-owned `ImGuiOverlaySystem`. It is intentionally
    // backend-agnostic: it does not link `imgui_impl_glfw`, so it drives the
    // `Null` platform window as well as the GLFW backend. Graphics never sees
    // ImGui types; the GPU font-atlas upload and `Pass.ImGui` execution are
    // owned by GRAPHICS-079.
    class ImGuiAdapter
    {
    public:
        ImGuiAdapter(Platform::IWindow& window, Graphics::ImGuiOverlaySystem& overlaySystem);
        ~ImGuiAdapter();

        ImGuiAdapter(const ImGuiAdapter&)            = delete;
        ImGuiAdapter& operator=(const ImGuiAdapter&) = delete;
        ImGuiAdapter(ImGuiAdapter&&)                 = delete;
        ImGuiAdapter& operator=(ImGuiAdapter&&)      = delete;

        // Creates the ImGui context, initializes the overlay system, and
        // configures ImGui IO (display size, framebuffer scale, clipboard,
        // backend flags). Returns false and changes nothing if already
        // initialized.
        bool Initialize();

        // Pumps the window's drained events into ImGui IO and calls
        // `ImGui::NewFrame()`. No-op when not initialized.
        void BeginFrame(double deltaSeconds);

        // Invokes the editor hook (between begin and end), calls
        // `ImGui::Render()`, walks `ImDrawData`, builds one `ImGuiOverlayFrame`,
        // and submits it to the overlay system. No-op when not initialized or
        // when no frame was started.
        void EndFrame();

        // Destroys the ImGui context and shuts down the overlay system. Safe to
        // call when not initialized.
        void Shutdown();

        // DPI / font rebuild: one `ImGuiOverlaySystem::Shutdown()` +
        // `Initialize()` cycle paired with an ImGui context recreate, counted
        // once in diagnostics. No-op (and uncounted) when not initialized.
        void RebuildForDisplayChange();

        // Editor-facing hook called once per frame between BeginFrame and
        // EndFrame so future editor code can issue ImGui panel draws without
        // modifying the adapter.
        void SetEditorCallback(std::function<void()> callback);

        [[nodiscard]] bool IsInitialized() const noexcept { return m_Context != nullptr; }
        [[nodiscard]] const ImGuiAdapterDiagnostics& GetDiagnostics() const noexcept { return m_Diagnostics; }

    private:
        void PumpEvents();          // translate Platform::Event -> ImGui IO
        void ConfigureIo();         // apply display size / scale / backend flags
        void DestroyContext();      // tear down the ImGui context only

        // ImGui clipboard hooks routed through the platform window. Declared as
        // static members (free-function-pointer compatible) so they can reach
        // the owning adapter via `ImGuiPlatformIO::Platform_ClipboardUserData`.
        static const char* ClipboardGet(ImGuiContext* ctx);
        static void        ClipboardSet(ImGuiContext* ctx, const char* text);

        Platform::IWindow&            m_Window;
        Graphics::ImGuiOverlaySystem& m_OverlaySystem;
        ImGuiContext*                 m_Context{nullptr};
        std::function<void()>         m_EditorCallback{};
        ImGuiAdapterDiagnostics       m_Diagnostics{};
        std::string                   m_ClipboardScratch{}; // backing store for ImGui clipboard reads
        bool                          m_FrameStarted{false};
    };
}
