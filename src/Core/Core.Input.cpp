module;
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

module Core:Input.Impl;
import :Input;

namespace Core::Input
{
    void Context::Initialize(void* windowHandle)
    {
        m_WindowHandle = static_cast<GLFWwindow*>(windowHandle);
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
        int state = glfwGetMouseButton((GLFWwindow*)m_WindowHandle, button);
        return state == GLFW_PRESS;
    }

    glm::vec2 Context::GetMousePosition() const
    {
        if (!m_WindowHandle) return {0.0f, 0.0f};
        double x, y;
        glfwGetCursorPos((GLFWwindow*)m_WindowHandle, &x, &y);
        return {static_cast<float>(x), static_cast<float>(y)};
    }
}
