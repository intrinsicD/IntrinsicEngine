module;
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream> // For panic logging if needed

module Core.Window;
import Core.Logging;

namespace Core::Windowing
{
    static bool s_GLFWInitialized = false;

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

        if (!s_GLFWInitialized)
        {
            int success = glfwInit();
            if (!success)
            {
                Log::Error("Could not initialize GLFW!");
                return; // TODO: Handle fatal error better
            }
            glfwSetErrorCallback(GLFWErrorCallback);
            s_GLFWInitialized = true;
        }

        // Hint: We are using Vulkan, so NO OpenGL context
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);


        auto m_glfw = glfwCreateWindow(m_Data.Width, m_Data.Height, m_Data.Title.c_str(), nullptr, nullptr);

        m_Window = m_glfw;
        // Store a pointer to our data struct inside the GLFW window so callbacks can find it
        glfwSetWindowUserPointer(m_glfw, &m_Data);

        // --- Setup Callbacks ---

        // 1. Resize
        glfwSetFramebufferSizeCallback(m_glfw, [](GLFWwindow* window, int width, int height)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            data.Width = width;
            data.Height = height;

            Event e;
            e.Type = EventType::WindowResize;
            e.Width = width;
            e.Height = height;

            if (data.Callback) data.Callback(e);
        });

        // 2. Close
        glfwSetWindowCloseCallback(m_glfw, [](GLFWwindow* window)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);
            Event e;
            e.Type = EventType::WindowClose;
            if (data.Callback) data.Callback(e);
        });

        // 3. Key
        glfwSetKeyCallback(m_glfw, [](GLFWwindow* window, int key, [[maybe_unused]] int scancode, int action,
                                      [[maybe_unused]] int mods)
        {
            WindowData& data = *(WindowData*)glfwGetWindowUserPointer(window);

            Event e;
            if (action == GLFW_PRESS) e.Type = EventType::KeyPressed;
            else if (action == GLFW_RELEASE) e.Type = EventType::KeyReleased;
            else return; // Ignore repeat for now

            e.KeyCode = key;
            if (data.Callback) data.Callback(e);
        });
    }

    void Window::Shutdown()
    {
        auto m_glfw = static_cast<GLFWwindow*>(m_Window);
        glfwDestroyWindow(m_glfw);
        // Note: We generally don't terminate GLFW here in case we have multiple windows,
        // but for a game engine, usually closing the main window kills the app.
    }

    void Window::OnUpdate()
    {
        glfwPollEvents();
    }

    bool Window::ShouldClose() const
    {
        auto m_glfw = static_cast<GLFWwindow*>(m_Window);
        return glfwWindowShouldClose(m_glfw);
    }

    bool Window::CreateSurface(void* instance, void* allocator, void* surfaceOut) {
        VkInstance vkInst = static_cast<VkInstance>(instance);
        VkAllocationCallbacks* vkAlloc = static_cast<VkAllocationCallbacks*>(allocator);
        VkSurfaceKHR* vkSurf = static_cast<VkSurfaceKHR*>(surfaceOut);
        auto m_glfw = static_cast<GLFWwindow*>(m_Window);
        VkResult result = glfwCreateWindowSurface(vkInst, m_glfw, vkAlloc, vkSurf);
        if (result != VK_SUCCESS) {
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
