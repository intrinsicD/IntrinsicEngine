module;
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

module Core.Input;

namespace Core::Input
{
    void Context::Initialize(void* windowHandle)
    {
        m_WindowHandle = static_cast<GLFWwindow*>(windowHandle);

        // Initialize cached state to current GLFW values to avoid a false 'just pressed' on frame 0.
        Update();
        m_PrevMouse = m_CurrMouse;
    }

    bool Context::IsKeyPressed(int keycode) const
    {
        if (!m_WindowHandle) return false;
        int state = glfwGetKey((GLFWwindow*)m_WindowHandle, keycode);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Context::IsMouseButtonPressed(int button) const
    {
        if (!m_WindowHandle) return false;
        if (button < 0 || button >= kMouseButtons) return false;
        return m_CurrMouse[button] != 0;
    }

    bool Context::IsMouseButtonJustPressed(int button) const
    {
        if (!m_WindowHandle) return false;
        if (button < 0 || button >= kMouseButtons) return false;
        return (m_CurrMouse[button] != 0) && (m_PrevMouse[button] == 0);
    }

    glm::vec2 Context::GetMousePosition() const
    {
        if (!m_WindowHandle) return {0.0f, 0.0f};
        double x, y;
        glfwGetCursorPos((GLFWwindow*)m_WindowHandle, &x, &y);
        return {static_cast<float>(x), static_cast<float>(y)};
    }

    void Context::Update()
    {
        if (!m_WindowHandle) return;

        m_PrevMouse = m_CurrMouse;
        for (int b = 0; b < kMouseButtons; ++b)
        {
            const int state = glfwGetMouseButton((GLFWwindow*)m_WindowHandle, b);
            m_CurrMouse[b] = static_cast<uint8_t>(state == GLFW_PRESS);
        }
    }
}
