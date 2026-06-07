module;

#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;

export module Extrinsic.Platform.Backend.Glfw;

import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Window;

namespace Extrinsic::Platform::Backends::Glfw
{
    export bool CanInitialize();

    export class Window final : public Platform::IWindow
    {
    public:
        explicit Window(const Core::Config::WindowConfig& config);
        ~Window() override;

        void PollEvents() override;
        [[nodiscard]] bool ShouldClose() const override;
        [[nodiscard]] bool IsMinimized() const override { return m_FramebufferExtent.Width == 0 || m_FramebufferExtent.Height == 0; }
        [[nodiscard]] bool WasResized() const override { return m_WasResized; }
        void AcknowledgeResize() override;
        [[nodiscard]] bool ConsumeInputActivity() override;
        [[nodiscard]] Platform::Extent2D GetWindowExtent() const override { return m_WindowExtent; }
        [[nodiscard]] Platform::Extent2D GetFramebufferExtent() const override { return m_FramebufferExtent; }
        [[nodiscard]] void* GetNativeHandle() const override { return m_Window; }

        void Listen(EventCallbackFn callback) override;
        [[nodiscard]] std::vector<Platform::Event> DrainEvents() override;

        void OnUpdate() override;
        void WaitForEventsTimeout(double timeoutSeconds) override;

        void SetClipboardText(std::string_view text) override;
        [[nodiscard]] std::string GetClipboardText() const override;
        void SetCursorMode(Platform::CursorMode mode) override;
        [[nodiscard]] Platform::CursorMode GetCursorMode() const override { return m_CursorMode; }

    private:
        void SeedCachedDimensions();
        void Emit(Platform::Event event);
        void ApplyEvent(const Platform::Event& event);
        void SetupCallbacks();

        Platform::Extent2D m_WindowExtent{};
        Platform::Extent2D m_FramebufferExtent{};
        bool m_ShouldClose{false};
        bool m_WasResized{false};
        bool m_InputActivity{false};
        bool m_IsValid{false};
        Platform::CursorMode m_CursorMode{Platform::CursorMode::Normal};
        GLFWwindow* m_Window{nullptr};
        EventCallbackFn m_Callback{};
        std::vector<Platform::Event> m_Events{};
        std::string m_ClipboardScratch{};
    };

    export std::unique_ptr<Platform::IWindow> CreateWindow(const Core::Config::WindowConfig& config);
}
