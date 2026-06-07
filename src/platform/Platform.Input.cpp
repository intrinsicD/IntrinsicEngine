module;

#include <cstdint>

module Extrinsic.Platform.Input;

namespace Extrinsic::Platform::Input
{
    void Context::Initialize(void* windowHandle)
    {
        m_WindowHandle = windowHandle;
    }

    bool Context::IsKeyPressed(int keycode) const
    {
        if (keycode < 0 || keycode >= kMaxTrackedKeys) return false;
        return m_CurrKeys[keycode] != 0;
    }

    bool Context::IsKeyJustPressed(int keycode) const
    {
        if (keycode < 0 || keycode >= kMaxTrackedKeys) return false;
        return (m_CurrKeys[keycode] != 0) && (m_PrevKeys[keycode] == 0);
    }

    bool Context::IsMouseButtonPressed(int button) const
    {
        if (button < 0 || button >= kMouseButtons) return false;
        return m_CurrMouse[button] != 0;
    }

    bool Context::IsMouseButtonJustPressed(int button) const
    {
        if (button < 0 || button >= kMouseButtons) return false;
        return (m_CurrMouse[button] != 0) && (m_PrevMouse[button] == 0);
    }

    void Context::AccumulateScroll(float xOffset, float yOffset)
    {
        m_ScrollAccumX += xOffset;
        m_ScrollAccumY += yOffset;
    }

    void Context::SetKeyState(int keycode, bool pressed)
    {
        if (keycode < 0 || keycode >= kMaxTrackedKeys) return;
        m_CurrKeys[keycode] = static_cast<std::uint8_t>(pressed);
    }

    void Context::SetMouseButtonState(int button, bool pressed)
    {
        if (button < 0 || button >= kMouseButtons) return;
        m_CurrMouse[button] = static_cast<std::uint8_t>(pressed);
    }

    void Context::SetMousePosition(float x, float y)
    {
        m_MousePosition = XY{x, y};
    }

    void Context::BeginFrame()
    {
        m_PrevMouse = m_CurrMouse;
        m_PrevKeys = m_CurrKeys;
        m_FrameScrollX = 0.0f;
        m_FrameScrollY = 0.0f;
    }

    void Context::Update()
    {
        m_FrameScrollX = m_ScrollAccumX;
        m_FrameScrollY = m_ScrollAccumY;
        m_ScrollAccumX = 0.0f;
        m_ScrollAccumY = 0.0f;
    }
}
