#pragma once

// Include-only Engine module glue. Include after the required graphics,
// platform, and runtime module imports in Runtime.Engine.cppm.

namespace Extrinsic::Runtime
{
    class ImGuiEditorBridge
    {
    public:
        ImGuiEditorBridge() = default;
        ~ImGuiEditorBridge();

        ImGuiEditorBridge(const ImGuiEditorBridge&) = delete;
        ImGuiEditorBridge& operator=(const ImGuiEditorBridge&) = delete;
        ImGuiEditorBridge(ImGuiEditorBridge&&) = delete;
        ImGuiEditorBridge& operator=(ImGuiEditorBridge&&) = delete;

        void Initialize(Platform::IWindow& window, Graphics::IRenderer& renderer);
        void Shutdown(Graphics::IRenderer* renderer) noexcept;

        void SetEditorCallback(std::function<void()> callback);
        void SetEditorVisible(bool visible) noexcept;
        void BeginFrame(double deltaSeconds);
        void EndFrame();

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] EditorInputCaptureSnapshot CaptureSnapshot() const noexcept;
        [[nodiscard]] const ImGuiAdapterDiagnostics* Diagnostics() const noexcept;
        [[nodiscard]] const ImGuiAdapter& Adapter() const noexcept;

    private:
        Graphics::ImGuiOverlaySystem m_Overlay{};
        std::function<void()> m_EditorCallback{};
        std::unique_ptr<ImGuiAdapter> m_Adapter{};
        bool m_EditorVisible{true};
    };
}
