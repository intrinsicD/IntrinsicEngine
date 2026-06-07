module;

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

module Extrinsic.Platform.Backend.Glfw;

import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Logging;

namespace Extrinsic::Platform::Backends::Glfw
{
    namespace
    {
        bool s_GLFWInitialized = false;

        void GLFWErrorCallback(int error, const char* description)
        {
            Core::Log::Error("GLFW Error ({0}): {1}", error, description ? description : "<null>");
        }

        [[nodiscard]] bool HasImGuiGlfwBackend() noexcept
        {
            return ImGui::GetCurrentContext() != nullptr &&
                   ImGui::GetIO().BackendPlatformUserData != nullptr;
        }

        class GLFWLifetime
        {
        public:
            static GLFWLifetime& Instanciate()
            {
                static GLFWLifetime instance;
                if (!s_GLFWInitialized)
                {
                    glfwSetErrorCallback(GLFWErrorCallback);
                    s_GLFWInitialized = glfwInit() == GLFW_TRUE;
                }
                return instance;
            }

            ~GLFWLifetime()
            {
                if (s_GLFWInitialized)
                {
                    glfwTerminate();
                    s_GLFWInitialized = false;
                }
            }

            GLFWLifetime(const GLFWLifetime&) = delete;
            GLFWLifetime& operator=(const GLFWLifetime&) = delete;

            static bool IsInitialized() noexcept { return s_GLFWInitialized; }

        private:
            GLFWLifetime() = default;
        };
    }

    bool CanInitialize()
    {
        GLFWLifetime::Instanciate();
        return GLFWLifetime::IsInitialized();
    }

    Window::Window(const Core::Config::WindowConfig& config)
        : m_WindowExtent{.Width = config.Width, .Height = config.Height}
        , m_FramebufferExtent{.Width = config.Width, .Height = config.Height}
    {
        GLFWLifetime::Instanciate();
        if (!GLFWLifetime::IsInitialized())
        {
            m_ShouldClose = true;
            return;
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, config.Resizable ? GLFW_TRUE : GLFW_FALSE);

        m_Window = glfwCreateWindow(
            m_WindowExtent.Width,
            m_WindowExtent.Height,
            config.Title.c_str(),
            nullptr,
            nullptr);

        if (!m_Window)
        {
            Core::Log::Error("Failed to create GLFW window.");
            m_ShouldClose = true;
            return;
        }

        glfwSetWindowUserPointer(m_Window, this);
        GetInput().Initialize(m_Window);
        SeedCachedDimensions();
        SetupCallbacks();
        m_IsValid = true;
    }

    Window::~Window()
    {
        if (m_Window)
        {
            glfwDestroyWindow(m_Window);
            m_Window = nullptr;
        }
    }

    void Window::PollEvents()
    {
        OnUpdate();
    }

    bool Window::ShouldClose() const
    {
        return m_ShouldClose || (m_Window && glfwWindowShouldClose(m_Window));
    }

    void Window::AcknowledgeResize()
    {
        m_WasResized = false;
    }

    bool Window::ConsumeInputActivity()
    {
        const bool hadActivity = m_InputActivity;
        m_InputActivity = false;
        return hadActivity;
    }

    void Window::Listen(EventCallbackFn callback)
    {
        m_Callback = std::move(callback);
    }

    std::vector<Platform::Event> Window::DrainEvents()
    {
        auto drained = std::move(m_Events);
        m_Events.clear();
        return drained;
    }

    void Window::OnUpdate()
    {
        if (!m_IsValid) return;

        GetInput().BeginFrame();
        glfwPollEvents();
        SeedCachedDimensions();
        GetInput().Update();
    }

    void Window::WaitForEventsTimeout(double timeoutSeconds)
    {
        if (!m_IsValid) return;

        GetInput().BeginFrame();
        glfwWaitEventsTimeout(timeoutSeconds);
        SeedCachedDimensions();
        GetInput().Update();
    }

    void Window::SetClipboardText(std::string_view text)
    {
        if (!m_Window) return;
        m_ClipboardScratch.assign(text.begin(), text.end());
        glfwSetClipboardString(m_Window, m_ClipboardScratch.c_str());
    }

    std::string Window::GetClipboardText() const
    {
        if (!m_Window) return {};
        const char* text = glfwGetClipboardString(m_Window);
        return text ? std::string{text} : std::string{};
    }

    void Window::SetCursorMode(Platform::CursorMode mode)
    {
        m_CursorMode = mode;
        if (!m_Window) return;

        int glfwMode = GLFW_CURSOR_NORMAL;
        switch (mode)
        {
        case Platform::CursorMode::Normal:
            glfwMode = GLFW_CURSOR_NORMAL;
            break;
        case Platform::CursorMode::Hidden:
            glfwMode = GLFW_CURSOR_HIDDEN;
            break;
        case Platform::CursorMode::Disabled:
            glfwMode = GLFW_CURSOR_DISABLED;
            break;
        }
        glfwSetInputMode(m_Window, GLFW_CURSOR, glfwMode);
    }

    void Window::SeedCachedDimensions()
    {
        if (!m_Window) return;
        glfwGetWindowSize(m_Window, &m_WindowExtent.Width, &m_WindowExtent.Height);
        glfwGetFramebufferSize(m_Window, &m_FramebufferExtent.Width, &m_FramebufferExtent.Height);
    }

    void Window::Emit(Platform::Event event)
    {
        ApplyEvent(event);
        m_Events.push_back(std::move(event));
        if (m_Callback)
        {
            m_Callback(m_Events.back());
        }
    }

    void Window::ApplyEvent(const Platform::Event& event)
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

    void Window::SetupCallbacks()
    {
        glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
        {
            auto& self = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            self.Emit(Platform::WindowResizeEvent{.Width = width, .Height = height});
        });

        glfwSetWindowContentScaleCallback(m_Window, [](GLFWwindow* window, float, float)
        {
            auto& self = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            int fbWidth = self.m_FramebufferExtent.Width;
            int fbHeight = self.m_FramebufferExtent.Height;
            glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

            if (fbWidth == self.m_FramebufferExtent.Width && fbHeight == self.m_FramebufferExtent.Height)
                return;

            self.Emit(Platform::WindowResizeEvent{.Width = fbWidth, .Height = fbHeight});
        });

        glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
        {
            auto& self = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            self.Emit(Platform::WindowCloseEvent{});
        });

        glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, int scancode, int action, int mods)
        {
            if (HasImGuiGlfwBackend())
                ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
            if (action != GLFW_PRESS && action != GLFW_RELEASE) return;

            auto& self = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            self.Emit(Platform::KeyEvent{.KeyCode = key, .IsPressed = action == GLFW_PRESS});
        });

        glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
        {
            if (HasImGuiGlfwBackend())
                ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
            if (action != GLFW_PRESS && action != GLFW_RELEASE) return;

            auto& self = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            self.Emit(Platform::MouseButtonEvent{.ButtonCode = button, .IsPressed = action == GLFW_PRESS});
        });

        glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xoffset, double yoffset)
        {
            if (HasImGuiGlfwBackend())
                ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
            auto& self = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            self.Emit(Platform::ScrollEvent{.XOffset = xoffset, .YOffset = yoffset});
        });

        glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xpos, double ypos)
        {
            if (HasImGuiGlfwBackend())
                ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
            auto& self = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            self.Emit(Platform::CursorEvent{.XPos = xpos, .YPos = ypos});
        });

        glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int c)
        {
            if (HasImGuiGlfwBackend())
                ImGui_ImplGlfw_CharCallback(window, c);
            auto& self = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            self.Emit(Platform::CharEvent{.Character = c});
        });

        glfwSetDropCallback(m_Window, [](GLFWwindow* window, int count, const char** paths)
        {
            auto& self = *static_cast<Window*>(glfwGetWindowUserPointer(window));
            Platform::WindowDropEvent event;
            for (int i = 0; i < count; ++i)
            {
                event.Paths.emplace_back(paths[i]);
            }
            self.Emit(std::move(event));
        });
    }

    std::unique_ptr<Platform::IWindow> CreateWindow(const Core::Config::WindowConfig& config)
    {
        return std::make_unique<Window>(config);
    }
}
