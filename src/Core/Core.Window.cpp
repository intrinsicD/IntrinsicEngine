module;
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <iostream> // For panic logging if needed
#include <imgui_impl_glfw.h>

module Core:Window.Impl;
import :Logging;
import :Window;

namespace Core::Windowing
{
    static bool s_GLFWInitialized = false;

    class GLFWLifetime
    {
    public:
        static GLFWLifetime& Instance()
        {
            static GLFWLifetime instance;
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

    private:
        GLFWLifetime() = default;
        GLFWLifetime(const GLFWLifetime&) = delete;
        GLFWLifetime& operator=(const GLFWLifetime&) = delete;
    };


    static void GLFWErrorCallback(int error, const char* description)
    {
        Log::Error("GLFW Error ({0}): {1}", error, description);
    }

    Window::Window(const WindowProps& props)
    {
        Init(props);
    }

    Window::~Window()
    {
        Shutdown();
    }

    void Window::Init(const WindowProps& props)
    {
        m_Data.Title = props.Title;
        m_Data.WindowWidth = props.WindowWidth;
        m_Data.WindowHeight = props.WindowHeight;

        Log::Info("Creating Window {0} ({1}x{2})", props.Title, props.WindowWidth, props.WindowHeight);

        [[maybe_unused]] auto& lifetime = GLFWLifetime::Instance();

        if (!s_GLFWInitialized)
        {
            int success = glfwInit();
            if (!success)
            {
                Log::Error("Could not initialize GLFW!");
                m_IsValid = false;
                return;
            }
            glfwSetErrorCallback(GLFWErrorCallback);
            s_GLFWInitialized = true;
        }

        // Hint: We are using Vulkan, so NO OpenGL context
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);


        auto glfwWindow = glfwCreateWindow(m_Data.WindowWidth, m_Data.WindowHeight, m_Data.Title.c_str(), nullptr,
                                           nullptr);

        if (!glfwWindow)
        {
            Log::Error("Failed to create GLFW window!");
            m_IsValid = false;
            return;
        }

        m_Window = glfwWindow;
        m_IsValid = true;
        m_InputContext.Initialize(m_Window);

        // Store a pointer to our data struct inside the GLFW window so callbacks can find it
        glfwSetWindowUserPointer(glfwWindow, &m_Data);

        // --- Setup Callbacks ---

        // 1. Resize
        glfwSetFramebufferSizeCallback(glfwWindow, [](GLFWwindow* window, int width, int height)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (data.Callback)
            {
                data.Callback(WindowResizeEvent{width, height});
            }
        });

        // 2. Close
        glfwSetWindowCloseCallback(glfwWindow, [](GLFWwindow* window)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (data.Callback)
            {
                data.Callback(WindowCloseEvent{});
            }
        });

        // 3. Key
        glfwSetKeyCallback(glfwWindow, [](GLFWwindow* window, int key, [[maybe_unused]] int scancode, int action,
                                          [[maybe_unused]] int mods)
        {
            ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

            if (action == GLFW_PRESS || action == GLFW_RELEASE)
            {
                if (data.Callback)
                {
                    data.Callback(KeyEvent{key, action == GLFW_PRESS});
                }
            }
        });

        glfwSetMouseButtonCallback(glfwWindow, [](GLFWwindow* window, int button, int action, int mods)
        {
            ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

            if (action == GLFW_PRESS || action == GLFW_RELEASE)
            {
                if (data.Callback)
                {
                    data.Callback(MouseButtonEvent{button, action == GLFW_PRESS});
                }
            }
        });

        glfwSetScrollCallback(glfwWindow, [](GLFWwindow* window, double xoffset, double yoffset)
        {
            ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (data.Callback)
            {
                data.Callback(ScrollEvent{xoffset, yoffset});
            }
        });

        glfwSetCursorPosCallback(glfwWindow, [](GLFWwindow* window, double xpos, double ypos)
        {
            ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (data.Callback)
            {
                data.Callback(CursorEvent{xpos, ypos});
            }
        });

        glfwSetCharCallback(glfwWindow, [](GLFWwindow* window, unsigned int c)
        {
            ImGui_ImplGlfw_CharCallback(window, c);
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
            if (data.Callback)
            {
                data.Callback(CharEvent{c});
            }
        });

        glfwSetDropCallback(glfwWindow, [](GLFWwindow* window, int count, const char** paths)
        {
            WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));

            WindowDropEvent event;
            // Convert C-strings to std::vector<std::string>
            for (int i = 0; i < count; i++)
            {
                event.Paths.emplace_back(paths[i]);
            }

            if (data.Callback)
            {
                data.Callback(event);
            }
        });
    }

    void Window::Shutdown()
    {
        if (m_Window)
        {
            auto glfwWindow = static_cast<GLFWwindow*>(m_Window);
            glfwDestroyWindow(glfwWindow);
        }
        // Note: We generally don't terminate GLFW here in case we have multiple windows,
        // but for a game engine, usually closing the main window kills the app.
    }

    void Window::OnUpdate()
    {
        if (!m_IsValid) return;
        glfwPollEvents();
        glfwGetWindowSize(static_cast<GLFWwindow*>(m_Window), &m_Data.WindowWidth, &m_Data.WindowHeight);
        glfwGetFramebufferSize(static_cast<GLFWwindow*>(m_Window), &m_Data.FramebufferWidth, &m_Data.FramebufferHeight);
    }

    int Window::GetWindowWidth() const
    {
        return m_Data.WindowWidth;
    }

    int Window::GetWindowHeight() const
    {
        return m_Data.WindowHeight;
    }

    int Window::GetFramebufferWidth() const
    {
        return m_Data.FramebufferWidth;
    }

    int Window::GetFramebufferHeight() const
    {
        return m_Data.FramebufferHeight;
    }

    bool Window::ShouldClose() const
    {
        if (!m_IsValid) return true;
        auto glfwWindow = static_cast<GLFWwindow*>(m_Window);
        return glfwWindowShouldClose(glfwWindow);
    }

    bool Window::CreateSurface(void* instance, void* allocator, void* surfaceOut)
    {
        auto vkInst = static_cast<VkInstance>(instance);
        auto vkAlloc = static_cast<VkAllocationCallbacks*>(allocator);
        auto vkSurf = static_cast<VkSurfaceKHR*>(surfaceOut);
        auto glfwWindow = static_cast<GLFWwindow*>(m_Window);
        VkResult result = glfwCreateWindowSurface(vkInst, glfwWindow, vkAlloc, vkSurf);
        if (result != VK_SUCCESS)
        {
            Log::Error("Failed to create Window Surface! Error: {}", (int)result);
            return false;
        }
        return true;
    }

    void Window::SetTitle(const std::string& title) const
    {
        glfwSetWindowTitle(static_cast<GLFWwindow*>(GetNativeHandle()), title.c_str());
    }
}
