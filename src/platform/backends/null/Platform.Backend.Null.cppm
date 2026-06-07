module;

#include <memory>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Platform.Backend.Null;

import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Window;

namespace Extrinsic::Platform::Backends::Null
{
    export class NullWindow final : public Platform::IWindow
    {
    public:
        explicit NullWindow(const Core::Config::WindowConfig& config);

        void QueueEvent(Platform::Event event);
        void QueueResize(int width, int height);
        void QueueClose();
        void QueueKey(int keycode, bool pressed);
        void QueueMouseButton(int button, bool pressed);
        void QueueScroll(double xOffset, double yOffset);
        void QueueCursor(double x, double y);

        void PollEvents() override;

        [[nodiscard]] bool ShouldClose() const override { return m_ShouldClose; }
        [[nodiscard]] bool IsMinimized() const override { return m_FramebufferExtent.Width == 0 || m_FramebufferExtent.Height == 0; }
        [[nodiscard]] bool WasResized() const override { return m_WasResized; }

        void AcknowledgeResize() override;
        [[nodiscard]] bool ConsumeInputActivity() override;

        [[nodiscard]] Platform::Extent2D GetWindowExtent() const override { return m_WindowExtent; }
        [[nodiscard]] Platform::Extent2D GetFramebufferExtent() const override { return m_FramebufferExtent; }
        [[nodiscard]] void* GetNativeHandle() const override { return nullptr; }

        void Listen(EventCallbackFn callback) override;
        [[nodiscard]] std::vector<Platform::Event> DrainEvents() override;

        void OnUpdate() override;
        void WaitForEventsTimeout(double timeoutSeconds) override;

        void SetClipboardText(std::string_view text) override;
        [[nodiscard]] std::string GetClipboardText() const override;
        void SetCursorMode(Platform::CursorMode mode) override;
        [[nodiscard]] Platform::CursorMode GetCursorMode() const override { return m_CursorMode; }

    private:
        void ApplyEvent(const Platform::Event& event);

        Platform::Extent2D m_WindowExtent{};
        Platform::Extent2D m_FramebufferExtent{};
        bool m_ShouldClose{false};
        bool m_WasResized{false};
        bool m_InputActivity{false};
        Platform::CursorMode m_CursorMode{Platform::CursorMode::Normal};
        std::string m_Clipboard{};
        EventCallbackFn m_Callback{};
        std::vector<Platform::Event> m_PendingEvents{};
        std::vector<Platform::Event> m_Events{};
    };

    export std::unique_ptr<Platform::IWindow> CreateWindow(const Core::Config::WindowConfig& config);
}
