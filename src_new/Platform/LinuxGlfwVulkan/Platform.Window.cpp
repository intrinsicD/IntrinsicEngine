module;

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#include <memory>

module Extrinsic.Platform.Window;
import Extrinsic.Core.Logging;

namespace Extrinsic::Platform
{
    static bool s_GLFWInitialized = false;

    static void GLFWErrorCallback(int error, const char* description)
    {
        Core::Log::Error("GLFW Error ({0}): {1}", error, description);
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
                glfwInit();
                s_GLFWInitialized = true;
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

    class LinuxGlfwVulkanWindow final : public IWindow
    {
    public:
        explicit LinuxGlfwVulkanWindow(const Core::Config::WindowConfig& config)
            : m_WindowExtent{.Width = config.Width, .Height = config.Height}
        {
            GLFWLifetime::Instanciate();

            if (!GLFWLifetime::IsInitialized())
            {
                m_IsValid = false;
                return;
            }

            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);


            auto glfwWindow = glfwCreateWindow(m_WindowExtent.Width, m_WindowExtent.Height, config.Title.c_str(),
                                               nullptr,
                                               nullptr);

            if (!glfwWindow)
            {
                Core::Log::Error("Failed to create GLFW window!");
                m_IsValid = false;
                return;
            }

            m_Window = glfwWindow;
            m_IsValid = true;

            SeedCachedDimensions();
            SetupCallbacks();

            glfwSetWindowUserPointer(glfwWindow, this);
        }

        ~LinuxGlfwVulkanWindow() override
        {
            if (m_Window)
            {
                glfwDestroyWindow(m_Window);
            }
            // Note: We generally don't terminate GLFW here in case we have multiple windows,
            // but for a game engine, usually closing the main window kills the app.
        }

        void SeedCachedDimensions()
        {
            glfwGetWindowSize(m_Window, &m_WindowExtent.Width, &m_WindowExtent.Height);
            glfwGetFramebufferSize(m_Window, &m_FramebufferExtent.Width, &m_FramebufferExtent.Height);
        }

        void SetupCallbacks()
        {
            // 1. Resize
            glfwSetFramebufferSizeCallback(m_Window, [](GLFWwindow* window, int width, int height)
            {
                auto& self = *static_cast<LinuxGlfwVulkanWindow*>(glfwGetWindowUserPointer(window));
                self.m_FramebufferExtent = {width, height};
                self.m_InputActivity = true;
                if (self.Callback)
                {
                    self.Callback(WindowResizeEvent{width, height});
                }
            });

            // 1b. Content-scale / monitor-DPI change.
            // Moving a window between monitors can change framebuffer size without a meaningful logical
            // window-size change. Re-query the framebuffer extent here so the engine can recreate the
            // swapchain and all framebuffer-sized render targets immediately.
            glfwSetWindowContentScaleCallback(m_Window, [](GLFWwindow* window, float, float)
            {
                auto& self = *static_cast<LinuxGlfwVulkanWindow*>(glfwGetWindowUserPointer(window));
                int fbWidth = self.m_FramebufferExtent.Width;
                int fbHeight = self.m_FramebufferExtent.Height;
                glfwGetFramebufferSize(window, &fbWidth, &fbHeight);

                if (fbWidth == self.m_FramebufferExtent.Width && fbHeight == self.m_FramebufferExtent.Height)
                    return;

                self.m_FramebufferExtent.Width = fbWidth;
                self.m_FramebufferExtent.Height = fbHeight;
                if (self.Callback)
                {
                    self.Callback(WindowResizeEvent{fbWidth, fbHeight});
                }
            });

            // 2. Close
            glfwSetWindowCloseCallback(m_Window, [](GLFWwindow* window)
            {
                auto& data = *static_cast<LinuxGlfwVulkanWindow*>(glfwGetWindowUserPointer(window));
                if (data.Callback)
                {
                    data.Callback(WindowCloseEvent{});
                }
            });

            // 3. Key
            glfwSetKeyCallback(m_Window, [](GLFWwindow* window, int key, [[maybe_unused]] int scancode, int action,
                                            [[maybe_unused]] int mods)
            {
                ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
                auto& data = *static_cast<LinuxGlfwVulkanWindow*>(glfwGetWindowUserPointer(window));
                data.m_InputActivity = true;

                if (action == GLFW_PRESS || action == GLFW_RELEASE)
                {
                    if (data.Callback)
                    {
                        data.Callback(KeyEvent{key, action == GLFW_PRESS});
                    }
                }
            });

            glfwSetMouseButtonCallback(m_Window, [](GLFWwindow* window, int button, int action, int mods)
            {
                ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
                auto& data = *static_cast<LinuxGlfwVulkanWindow*>(glfwGetWindowUserPointer(window));
                data.m_InputActivity = true;

                if (action == GLFW_PRESS || action == GLFW_RELEASE)
                {
                    if (data.Callback)
                    {
                        data.Callback(MouseButtonEvent{button, action == GLFW_PRESS});
                    }
                }
            });

            glfwSetScrollCallback(m_Window, [](GLFWwindow* window, double xoffset, double yoffset)
            {
                ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
                auto& self = *static_cast<LinuxGlfwVulkanWindow*>(glfwGetWindowUserPointer(window));
                self.m_InputActivity = true;

                self.GetInput().AccumulateScroll(static_cast<float>(xoffset), static_cast<float>(yoffset));

                if (self.Callback)
                {
                    self.Callback(ScrollEvent{xoffset, yoffset});
                }
            });

            glfwSetCursorPosCallback(m_Window, [](GLFWwindow* window, double xpos, double ypos)
            {
                ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
                auto& self = *static_cast<LinuxGlfwVulkanWindow*>(glfwGetWindowUserPointer(window));
                // Cursor motion alone is NOT treated as user activity for idle
                // throttling — the cursor drifting over the viewport while the user
                // works in another app should not prevent idle pacing. Clicks,
                // scrolls, and key presses are the meaningful activity signals.
                if (self.Callback)
                {
                    self.Callback(CursorEvent{xpos, ypos});
                }
            });

            glfwSetCharCallback(m_Window, [](GLFWwindow* window, unsigned int c)
            {
                ImGui_ImplGlfw_CharCallback(window, c);
                auto& self = *static_cast<LinuxGlfwVulkanWindow*>(glfwGetWindowUserPointer(window));
                self.m_InputActivity = true;
                if (self.Callback)
                {
                    self.Callback(CharEvent{c});
                }
            });

            glfwSetDropCallback(m_Window, [](GLFWwindow* window, int count, const char** paths)
            {
                auto& self = *static_cast<LinuxGlfwVulkanWindow*>(glfwGetWindowUserPointer(window));
                self.m_InputActivity = true;

                WindowDropEvent event;
                // Convert C-strings to std::vector<std::string>
                for (int i = 0; i < count; i++)
                {
                    event.Paths.emplace_back(paths[i]);
                }

                if (self.Callback)
                {
                    self.Callback(event);
                }
            });
        }

        void PollEvents() override
        {
        }

        [[nodiscard]] bool ShouldClose() const override
        {
            return m_ShouldClose;
        }

        [[nodiscard]] bool IsMinimized() const override
        {
            return m_WindowExtent.Width == 0 || m_WindowExtent.Height == 0;
        }

        [[nodiscard]] bool WasResized() const override
        {
            return m_WasResized;
        }

        [[nodiscard]] bool ConsumeInputActivity() override
        {
            const bool had = m_InputActivity;
            m_InputActivity = false;
            return had;
        }

        void AcknowledgeResize() override
        {
            m_WasResized = false;
        }

        [[nodiscard]] Extent2D GetWindowExtent() const override
        {
            return m_WindowExtent;
        }

        [[nodiscard]] Extent2D GetFramebufferExtent() const override
        {
            return m_FramebufferExtent;
        }

        [[nodiscard]] void* GetNativeHandle() const override
        {
            return m_Window;
        }

        void Listen(EventCallbackFn callback) override
        {
            Callback = callback;
        }

        void OnUpdate() override
        {
            if (!m_IsValid) return;
            glfwPollEvents();

            // Keep per-frame input transitions in sync with the polled event stream.
            GetInput().Update();

            SeedCachedDimensions();
        }

        void WaitForEventsTimeout(double timeoutSeconds) override
        {
            if (!m_IsValid) return;

            glfwWaitEventsTimeout(timeoutSeconds);
            GetInput().Update();

            // Keep cached window/framebuffer dimensions current after the wait, since the wake-up
            // event may have been a restore or resize rather than a normal per-frame pump.
            SeedCachedDimensions();
        }

        //TODO: Create the VulkanSurface...

    private:
        Extent2D m_WindowExtent{};
        Extent2D m_FramebufferExtent{};
        bool m_ShouldClose{false};
        bool m_WasResized{false};
        bool m_InputActivity{false};
        bool m_IsValid{false};
        GLFWwindow* m_Window{nullptr};
        EventCallbackFn Callback{nullptr};
    };

    std::unique_ptr<IWindow> CreateWindow(const Core::Config::WindowConfig& config)
    {
        return std::make_unique<LinuxGlfwVulkanWindow>(config);
    }
}
