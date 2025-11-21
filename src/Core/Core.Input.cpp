module;
#include <GLFW/glfw3.h>

module Core.Input;

namespace Core::Input
{
    static GLFWwindow* s_Window = nullptr;

    void Initialize(void* windowHandle)
    {
        s_Window = static_cast<GLFWwindow*>(windowHandle);
    }

    bool IsKeyPressed(int keycode)
    {
        if (!s_Window) return false;
        int state = glfwGetKey(s_Window, keycode);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool IsMouseButtonPressed(int button)
    {
        if (!s_Window) return false;
        int state = glfwGetMouseButton(s_Window, button);
        return state == GLFW_PRESS;
    }

    double GetMouseX()
    {
        if (!s_Window) return 0.0;
        double x, y;
        glfwGetCursorPos(s_Window, &x, &y);
        return x;
    }

    double GetMouseY()
    {
        if (!s_Window) return 0.0;
        double x, y;
        glfwGetCursorPos(s_Window, &x, &y);
        return y;
    }
}
