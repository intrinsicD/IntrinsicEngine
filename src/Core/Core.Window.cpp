module;
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <iostream> // For panic logging if needed
#include <imgui_impl_glfw.h>

module Core.Window;
import Core.Logging;

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
        m_Data.Width = props.Width;
        m_Data.Height = props.Height;

        Log::Info("Creating Window {0} ({1}x{2})", props.Title, props.Width, props.Height);

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


        auto m_glfw = glfwCreateWindow(m_Data.Width, m_Data.Height, m_Data.Title.c_str(), nullptr, nullptr);

        if (!m_glfw)
        {
            Log::Error("Failed to create GLFW window!");
            m_IsValid = false;
            return;
        }

        m_Window = m_glfw;
        m_IsValid = true;

        // Store a pointer to our data struct inside the GLFW window so callbacks can find it
        glfwSetWindowUserPointer(m_glfw, &m_Data);

        // --- Setup Callbacks ---

        // 1. Resize
        glfwSetFramebufferSizeCallback(m_glfw, [](GLFWwindow* window, int width, int height)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            if (data.Callback)
            {
                data.Callback(WindowResizeEvent{width, height});
            }
        });

        // 2. Close
        glfwSetWindowCloseCallback(m_glfw, [](GLFWwindow* window)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            if (data.Callback)
            {
                data.Callback(WindowCloseEvent{});
            }
        });

        // 3. Key
        glfwSetKeyCallback(m_glfw, [](GLFWwindow* window, int key, [[maybe_unused]] int scancode, int action,
                                      [[maybe_unused]] int mods)
        {
            ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            if (action == GLFW_PRESS || action == GLFW_RELEASE)
            {
                if (data.Callback)
                {
                    data.Callback(KeyEvent{key, action == GLFW_PRESS});
                }
            }
        });

        glfwSetMouseButtonCallback(m_glfw, [](GLFWwindow* window, int button, int action, int mods)
        {
            ImGui_ImplGlfw_MouseButtonCallback(window, button, action, mods);
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            if (action == GLFW_PRESS || action == GLFW_RELEASE)
            {
                if (data.Callback)
                {
                    data.Callback(MouseButtonEvent{button, action == GLFW_PRESS});
                }
            }
        });

        glfwSetScrollCallback(m_glfw, [](GLFWwindow* window, double xoffset, double yoffset)
        {
            ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            if (data.Callback)
            {
                data.Callback(ScrollEvent{xoffset, yoffset});
            }
        });

        glfwSetCursorPosCallback(m_glfw, [](GLFWwindow* window, double xpos, double ypos)
        {
            ImGui_ImplGlfw_CursorPosCallback(window, xpos, ypos);
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            if (data.Callback)
            {
                data.Callback(ScrollEvent{xpos, ypos});
            }
        });

        glfwSetCharCallback(m_glfw, [](GLFWwindow* window, unsigned int c)
        {
            ImGui_ImplGlfw_CharCallback(window, c);
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            if (data.Callback)
            {
                data.Callback(CharEvent{c});
            }
        });

        glfwSetDropCallback(m_glfw, [](GLFWwindow* window, int count, const char** paths)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);


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
            auto m_glfw = static_cast<GLFWwindow*>(m_Window);
            glfwDestroyWindow(m_glfw);
        }
        // Note: We generally don't terminate GLFW here in case we have multiple windows,
        // but for a game engine, usually closing the main window kills the app.
    }

    void Window::OnUpdate()
    {
        if (!m_IsValid) return;
        glfwPollEvents();
    }

    bool Window::ShouldClose() const
    {
        if (!m_IsValid) return true;
        auto m_glfw = static_cast<GLFWwindow*>(m_Window);
        return glfwWindowShouldClose(m_glfw);
    }

    bool Window::CreateSurface(void* instance, void* allocator, void* surfaceOut)
    {
        VkInstance vkInst = static_cast<VkInstance>(instance);
        VkAllocationCallbacks* vkAlloc = static_cast<VkAllocationCallbacks*>(allocator);
        VkSurfaceKHR* vkSurf = static_cast<VkSurfaceKHR*>(surfaceOut);
        auto m_glfw = static_cast<GLFWwindow*>(m_Window);
        VkResult result = glfwCreateWindowSurface(vkInst, m_glfw, vkAlloc, vkSurf);
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
