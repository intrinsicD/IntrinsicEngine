module;

#include <cstdint>

module Extrinsic.Platform.Input;

namespace Extrinsic::Platform::Input
{
    bool Context::IsKeyPressed(int keycode) const
    {
        if (keycode < 0 || keycode >= kMaxTrackedKeys) return false;
        return m_CurrKeys[keycode] != 0;
    }

    bool Context::IsKeyJustPressed(int keycode) const
    {
        if (keycode < 0 || keycode >= kMaxTrackedKeys) return false;
        return m_PressedKeys[keycode] != 0;
    }

    bool Context::IsMouseButtonPressed(int button) const
    {
        if (button < 0 || button >= kMouseButtons) return false;
        return m_CurrMouse[button] != 0;
    }

    bool Context::IsMouseButtonJustPressed(int button) const
    {
        if (button < 0 || button >= kMouseButtons) return false;
        return m_PressedMouse[button] != 0;
    }

    void Context::AccumulateScroll(float xOffset, float yOffset)
    {
        m_ScrollAccumX += xOffset;
        m_ScrollAccumY += yOffset;
    }

    void Context::SetKeyState(int keycode, bool pressed)
    {
        if (keycode < 0 || keycode >= kMaxTrackedKeys) return;
        if (pressed && !m_CurrKeys[keycode]) m_PressedKeys[keycode] = 1;
        m_CurrKeys[keycode] = static_cast<std::uint8_t>(pressed);
    }

    void Context::SetMouseButtonState(int button, bool pressed)
    {
        if (button < 0 || button >= kMouseButtons) return;
        if (pressed && !m_CurrMouse[button]) m_PressedMouse[button] = 1;
        m_CurrMouse[button] = static_cast<std::uint8_t>(pressed);
    }

    void Context::SetMousePosition(float x, float y)
    {
        m_MousePosition = XY{x, y};
    }

    void Context::BeginFrame()
    {
        m_PressedMouse.fill(0);
        m_PressedKeys.fill(0);
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
