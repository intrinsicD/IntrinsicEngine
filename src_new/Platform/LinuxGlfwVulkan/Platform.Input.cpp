module;

#include <GLFW/glfw3.h>

module Extrinsic.Platform.Input;

namespace Extrinsic::Platform::Input
{
    void Context::Initialize(void* windowHandle)
    {
        m_WindowHandle = static_cast<GLFWwindow*>(windowHandle);

        // Initialize cached state to current GLFW values to avoid a false 'just pressed' on frame 0.
        Update();
        m_PrevMouse = m_CurrMouse;
        m_PrevKeys = m_CurrKeys;
    }

    bool Context::IsKeyPressed(int keycode) const
    {
        if (!m_WindowHandle) return false;
        int state = glfwGetKey((GLFWwindow*)m_WindowHandle, keycode);
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Context::IsKeyJustPressed(int keycode) const
    {
        if (!m_WindowHandle) return false;
        if (keycode < 0 || keycode >= kMaxTrackedKeys) return false;
        return (m_CurrKeys[keycode] != 0) && (m_PrevKeys[keycode] == 0);
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

    Context::XY Context::GetMousePosition() const
    {
        if (!m_WindowHandle) return {0.0f, 0.0f};
        double x, y;
        glfwGetCursorPos((GLFWwindow*)m_WindowHandle, &x, &y);
        return {static_cast<float>(x), static_cast<float>(y)};
    }

    Context::XY Context::GetScrollDelta() const
    {
        return {m_FrameScrollX, m_FrameScrollY};
    }

    Context::XY Context::GetScrollAccum() const
    {
        return {m_ScrollAccumX, m_ScrollAccumY};
    }

    void Context::AccumulateScroll(float xOffset, float yOffset)
    {
        m_ScrollAccumX += xOffset;
        m_ScrollAccumY += yOffset;
    }

    void Context::Update()
    {
        if (!m_WindowHandle) return;

        // Latch scroll delta for this frame, then reset accumulator.
        m_FrameScrollX = m_ScrollAccumX;
        m_FrameScrollY = m_ScrollAccumY;
        m_ScrollAccumX = 0.0f;
        m_ScrollAccumY = 0.0f;

        // Mouse button transitions.
        m_PrevMouse = m_CurrMouse;
        for (int b = 0; b < kMouseButtons; ++b)
        {
            const int state = glfwGetMouseButton((GLFWwindow*)m_WindowHandle, b);
            m_CurrMouse[b] = static_cast<uint8_t>(state == GLFW_PRESS);
        }

        // Key state transitions (for IsKeyJustPressed).
        // IMPORTANT: GLFW key space is sparse. Calling glfwGetKey() with unsupported
        // key values triggers GLFW_ERROR_INVALID_VALUE (65539) spam.
        // We track only the valid GLFW key range and leave the rest at 0.
        m_PrevKeys = m_CurrKeys;
        m_CurrKeys.fill(0);

        constexpr int kFirstGlfwKey = GLFW_KEY_SPACE; // first printable key
        constexpr int kLastGlfwKey = GLFW_KEY_LAST;   // last supported key

        static_assert(kLastGlfwKey < kMaxTrackedKeys,
            "kMaxTrackedKeys must cover GLFW_KEY_LAST for cached key transitions");

        for (int k = kFirstGlfwKey; k <= kLastGlfwKey; ++k)
        {
            const int state = glfwGetKey((GLFWwindow*)m_WindowHandle, k);
            m_CurrKeys[k] = static_cast<uint8_t>(state == GLFW_PRESS || state == GLFW_REPEAT);
        }
    }
}
