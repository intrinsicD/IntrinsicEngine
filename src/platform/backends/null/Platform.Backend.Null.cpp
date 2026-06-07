module;

#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

module Extrinsic.Platform.Backend.Null;

namespace Extrinsic::Platform::Backends::Null
{
    NullWindow::NullWindow(const Core::Config::WindowConfig& config)
        : m_WindowExtent{.Width = config.Width, .Height = config.Height}
        , m_FramebufferExtent{.Width = config.Width, .Height = config.Height}
    {
        GetInput().Initialize(nullptr);
    }

    void NullWindow::QueueEvent(Platform::Event event)
    {
        m_PendingEvents.push_back(std::move(event));
    }

    void NullWindow::QueueResize(int width, int height)
    {
        QueueEvent(Platform::WindowResizeEvent{.Width = width, .Height = height});
    }

    void NullWindow::QueueClose()
    {
        QueueEvent(Platform::WindowCloseEvent{});
    }

    void NullWindow::QueueKey(int keycode, bool pressed)
    {
        QueueEvent(Platform::KeyEvent{.KeyCode = keycode, .IsPressed = pressed});
    }

    void NullWindow::QueueMouseButton(int button, bool pressed)
    {
        QueueEvent(Platform::MouseButtonEvent{.ButtonCode = button, .IsPressed = pressed});
    }

    void NullWindow::QueueScroll(double xOffset, double yOffset)
    {
        QueueEvent(Platform::ScrollEvent{.XOffset = xOffset, .YOffset = yOffset});
    }

    void NullWindow::QueueCursor(double x, double y)
    {
        QueueEvent(Platform::CursorEvent{.XPos = x, .YPos = y});
    }

    void NullWindow::PollEvents()
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

    void NullWindow::AcknowledgeResize()
    {
        m_WasResized = false;
    }

    bool NullWindow::ConsumeInputActivity()
    {
        const bool hadActivity = m_InputActivity;
        m_InputActivity = false;
        return hadActivity;
    }

    void NullWindow::Listen(EventCallbackFn callback)
    {
        m_Callback = std::move(callback);
    }

    std::vector<Platform::Event> NullWindow::DrainEvents()
    {
        auto drained = std::move(m_Events);
        m_Events.clear();
        return drained;
    }

    void NullWindow::OnUpdate()
    {
        PollEvents();
    }

    void NullWindow::WaitForEventsTimeout(double timeoutSeconds)
    {
        (void)timeoutSeconds;
        PollEvents();
    }

    void NullWindow::SetClipboardText(std::string_view text)
    {
        m_Clipboard.assign(text.begin(), text.end());
    }

    std::string NullWindow::GetClipboardText() const
    {
        return m_Clipboard;
    }

    void NullWindow::SetCursorMode(Platform::CursorMode mode)
    {
        m_CursorMode = mode;
    }

    void NullWindow::ApplyEvent(const Platform::Event& event)
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

    std::unique_ptr<Platform::IWindow> CreateWindow(const Core::Config::WindowConfig& config)
    {
        return std::make_unique<NullWindow>(config);
    }
}
