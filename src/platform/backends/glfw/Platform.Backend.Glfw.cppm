module;

#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

export module Extrinsic.Platform.Backend.Glfw;

import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Logging;
import Extrinsic.Platform.Window;

namespace Extrinsic::Platform::Backends::Glfw
{
    namespace
    {
        bool s_GLFWInitialized = false;

        void GLFWErrorCallback(int error, const char* description)
        {
            Core::Log::Error("GLFW Error ({0}): {1}", error, description ? description : "<null>");
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

    export bool CanInitialize()
    {
        GLFWLifetime::Instanciate();
        return GLFWLifetime::IsInitialized();
    }

    export class Window final : public Platform::IWindow
    {
    public:
        explicit Window(const Core::Config::WindowConfig& config)
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

        ~Window() override
        {
            if (m_Window)
            {
                glfwDestroyWindow(m_Window);
                m_Window = nullptr;
            }
        }

        void PollEvents() override
        {
            OnUpdate();
        }

        [[nodiscard]] bool ShouldClose() const override
        {
            return m_ShouldClose || (m_Window && glfwWindowShouldClose(m_Window));
        }

        [[nodiscard]] bool IsMinimized() const override
        {
            return m_FramebufferExtent.Width == 0 || m_FramebufferExtent.Height == 0;
        }

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
        [[nodiscard]] void* GetNativeHandle() const override { return m_Window; }

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
            if (!m_IsValid) return;

            GetInput().BeginFrame();
            glfwPollEvents();
            SeedCachedDimensions();
            GetInput().Update();
        }

        void WaitForEventsTimeout(double timeoutSeconds) override
        {
            if (!m_IsValid) return;

            GetInput().BeginFrame();
            glfwWaitEventsTimeout(timeoutSeconds);
            SeedCachedDimensions();
            GetInput().Update();
        }

        void SetClipboardText(std::string_view text) override
        {
            if (!m_Window) return;
            m_ClipboardScratch.assign(text.begin(), text.end());
            glfwSetClipboardString(m_Window, m_ClipboardScratch.c_str());
        }

        [[nodiscard]] std::string GetClipboardText() const override
        {
            if (!m_Window) return {};
            const char* text = glfwGetClipboardString(m_Window);
            return text ? std::string{text} : std::string{};
        }

        void SetCursorMode(Platform::CursorMode mode) override
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

        [[nodiscard]] Platform::CursorMode GetCursorMode() const override
        {
            return m_CursorMode;
        }

    private:
        void SeedCachedDimensions()
        {
            if (!m_Window) return;
            glfwGetWindowSize(m_Window, &m_WindowExtent.Width, &m_WindowExtent.Height);
            glfwGetFramebufferSize(m_Window, &m_FramebufferExtent.Width, &m_FramebufferExtent.Height);
        }

        void Emit(Platform::Event event)
        {
            ApplyEvent(event);
            m_Events.push_back(std::move(event));
            if (m_Callback)
            {
                m_Callback(m_Events.back());
            }
        }

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

        void SetupCallbacks()
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
                ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
                if (action != GLFW_PRESS && action != GLFW_RELEASE) return;

                auto& self = *static_cast<Window*>(glfwGetWindowUserPointer(window));
                self.Emit(Platform::KeyEvent{.KeyCode = key, .IsPressed = action == GLFW_PRESS});
            });

            glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
            {
                ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
                if (action != GLFW_PRESS && action != GLFW_RELEASE) return;

                auto& self = *static_cast<Window*>(glfwGetWindowUserPointer(window));
                self.Emit(Platform::MouseButtonEvent{.ButtonCode = button, .IsPressed = action == GLFW_PRESS});
            });

            glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xoffset, double yoffset)
            {
                ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
                auto& self = *static_cast<Window*>(glfwGetWindowUserPointer(window));
                self.Emit(Platform::ScrollEvent{.XOffset = xoffset, .YOffset = yoffset});
            });

            glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xpos, double ypos)
            {
                ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
                auto& self = *static_cast<Window*>(glfwGetWindowUserPointer(window));
                self.Emit(Platform::CursorEvent{.XPos = xpos, .YPos = ypos});
            });

            glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int c)
            {
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

    export std::unique_ptr<Platform::IWindow> CreateWindow(const Core::Config::WindowConfig& config)
    {
        return std::make_unique<Window>(config);
    }
}

