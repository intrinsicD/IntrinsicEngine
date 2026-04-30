module;

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

export module Extrinsic.Platform.Backend.Null;

import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Window;

namespace Extrinsic::Platform::Backends::Null
{
    export class NullWindow final : public Platform::IWindow
    {
    public:
        explicit NullWindow(const Core::Config::WindowConfig& config)
            : m_WindowExtent{.Width = config.Width, .Height = config.Height}
            , m_FramebufferExtent{.Width = config.Width, .Height = config.Height}
        {
            GetInput().Initialize(nullptr);
        }

        void QueueEvent(Platform::Event event)
        {
            m_PendingEvents.push_back(std::move(event));
        }

        void QueueResize(int width, int height)
        {
            QueueEvent(Platform::WindowResizeEvent{.Width = width, .Height = height});
        }

        void QueueClose()
        {
            QueueEvent(Platform::WindowCloseEvent{});
        }

        void QueueKey(int keycode, bool pressed)
        {
            QueueEvent(Platform::KeyEvent{.KeyCode = keycode, .IsPressed = pressed});
        }

        void QueueMouseButton(int button, bool pressed)
        {
            QueueEvent(Platform::MouseButtonEvent{.ButtonCode = button, .IsPressed = pressed});
        }

        void QueueScroll(double xOffset, double yOffset)
        {
            QueueEvent(Platform::ScrollEvent{.XOffset = xOffset, .YOffset = yOffset});
        }

        void QueueCursor(double x, double y)
        {
            QueueEvent(Platform::CursorEvent{.XPos = x, .YPos = y});
        }

        void PollEvents() override
        {
            GetInput().BeginFrame();
            for (auto& event : m_PendingEvents)
            {
                ApplyEvent(event);
                m_Events.push_back(std::move(event));
                if (m_Callback)
                {
                    m_Callback(m_Events.back());
                }
            }
            m_PendingEvents.clear();
            GetInput().Update();
        }

        [[nodiscard]] bool ShouldClose() const override { return m_ShouldClose; }
        [[nodiscard]] bool IsMinimized() const override { return m_FramebufferExtent.Width == 0 || m_FramebufferExtent.Height == 0; }
        [[nodiscard]] bool WasResized() const override { return m_WasResized; }

        void AcknowledgeResize() override
        {
            m_WasResized = false;
        }

        [[nodiscard]] bool ConsumeInputActivity() override
        {
            const bool hadActivity = m_InputActivity;
            m_InputActivity = false;
            return hadActivity;
        }

        [[nodiscard]] Platform::Extent2D GetWindowExtent() const override { return m_WindowExtent; }
        [[nodiscard]] Platform::Extent2D GetFramebufferExtent() const override { return m_FramebufferExtent; }
        [[nodiscard]] void* GetNativeHandle() const override { return nullptr; }

        void Listen(EventCallbackFn callback) override
        {
            m_Callback = std::move(callback);
        }

        [[nodiscard]] std::vector<Platform::Event> DrainEvents() override
        {
            auto drained = std::move(m_Events);
            m_Events.clear();
            return drained;
        }

        void OnUpdate() override
        {
            PollEvents();
        }

        void WaitForEventsTimeout(double timeoutSeconds) override
        {
            (void)timeoutSeconds;
            PollEvents();
        }

        void SetClipboardText(std::string_view text) override
        {
            m_Clipboard.assign(text.begin(), text.end());
        }

        [[nodiscard]] std::string GetClipboardText() const override
        {
            return m_Clipboard;
        }

        void SetCursorMode(Platform::CursorMode mode) override
        {
            m_CursorMode = mode;
        }

        [[nodiscard]] Platform::CursorMode GetCursorMode() const override
        {
            return m_CursorMode;
        }

    private:
        void ApplyEvent(const Platform::Event& event)
        {
            std::visit([this](const auto& value)
            {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, Platform::WindowCloseEvent>)
                {
                    m_ShouldClose = true;
                    m_InputActivity = true;
                }
                else if constexpr (std::is_same_v<T, Platform::WindowResizeEvent>)
                {
                    m_WindowExtent = {.Width = value.Width, .Height = value.Height};
                    m_FramebufferExtent = {.Width = value.Width, .Height = value.Height};
                    m_WasResized = true;
                    m_InputActivity = true;
                }
                else if constexpr (std::is_same_v<T, Platform::KeyEvent>)
                {
                    GetInput().SetKeyState(value.KeyCode, value.IsPressed);
                    m_InputActivity = true;
                }
                else if constexpr (std::is_same_v<T, Platform::MouseButtonEvent>)
                {
                    GetInput().SetMouseButtonState(value.ButtonCode, value.IsPressed);
                    m_InputActivity = true;
                }
                else if constexpr (std::is_same_v<T, Platform::ScrollEvent>)
                {
                    GetInput().AccumulateScroll(static_cast<float>(value.XOffset), static_cast<float>(value.YOffset));
                    m_InputActivity = true;
                }
                else if constexpr (std::is_same_v<T, Platform::CursorEvent>)
                {
                    GetInput().SetMousePosition(static_cast<float>(value.XPos), static_cast<float>(value.YPos));
                }
                else if constexpr (std::is_same_v<T, Platform::CharEvent> || std::is_same_v<T, Platform::WindowDropEvent>)
                {
                    m_InputActivity = true;
                }
            }, event);
        }

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

    export std::unique_ptr<Platform::IWindow> CreateWindow(const Core::Config::WindowConfig& config)
    {
        return std::make_unique<NullWindow>(config);
    }
}

