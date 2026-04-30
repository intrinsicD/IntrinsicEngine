module;

#include <cstdint>
#include <array>

export module Extrinsic.Platform.Input;

export namespace Extrinsic::Platform::Input {
    // Common Key Codes (Subset)
    namespace Key {
        constexpr int A = 65;
        constexpr int C = 67;
        constexpr int D = 68;
        constexpr int E = 69;
        constexpr int F = 70;
        constexpr int G = 71;
        constexpr int Q = 81;
        constexpr int R = 82;
        constexpr int S = 83;
        constexpr int T = 84;
        constexpr int W = 87;
        constexpr int X = 88;
        constexpr int Space = 32;
        constexpr int Escape = 256;
        constexpr int LeftShift = 340;
        constexpr int LeftControl = 341;
    }

    class Context {
    public:
        struct XY
        {
            float x{0.0f}, y{0.0f};
        };

        void Initialize(void* windowHandle) { m_WindowHandle = windowHandle; }

        [[nodiscard]] bool IsKeyPressed(int keycode) const
        {
            if (keycode < 0 || keycode >= kMaxTrackedKeys) return false;
            return m_CurrKeys[keycode] != 0;
        }

        [[nodiscard]] bool IsKeyJustPressed(int keycode) const
        {
            if (keycode < 0 || keycode >= kMaxTrackedKeys) return false;
            return (m_CurrKeys[keycode] != 0) && (m_PrevKeys[keycode] == 0);
        }

        [[nodiscard]] bool IsMouseButtonPressed(int button) const
        {
            if (button < 0 || button >= kMouseButtons) return false;
            return m_CurrMouse[button] != 0;
        }

        [[nodiscard]] bool IsMouseButtonJustPressed(int button) const
        {
            if (button < 0 || button >= kMouseButtons) return false;
            return (m_CurrMouse[button] != 0) && (m_PrevMouse[button] == 0);
        }

        [[nodiscard]] XY GetMousePosition() const { return m_MousePosition; }
        [[nodiscard]] XY GetScrollDelta() const { return {m_FrameScrollX, m_FrameScrollY}; }
        [[nodiscard]] XY GetScrollAccum() const { return {m_ScrollAccumX, m_ScrollAccumY}; }

        // Accumulate scroll input (called from window scroll callback).
        void AccumulateScroll(float xOffset, float yOffset)
        {
            m_ScrollAccumX += xOffset;
            m_ScrollAccumY += yOffset;
        }

        // Backend event adapters. Key/button codes outside the tracked ranges are ignored.
        void SetKeyState(int keycode, bool pressed)
        {
            if (keycode < 0 || keycode >= kMaxTrackedKeys) return;
            m_CurrKeys[keycode] = static_cast<std::uint8_t>(pressed);
        }

        void SetMouseButtonState(int button, bool pressed)
        {
            if (button < 0 || button >= kMouseButtons) return;
            m_CurrMouse[button] = static_cast<std::uint8_t>(pressed);
        }

        void SetMousePosition(float x, float y) { m_MousePosition = XY{x, y}; }

        // Call before polling a frame's event source to snapshot transition baselines.
        void BeginFrame()
        {
            m_PrevMouse = m_CurrMouse;
            m_PrevKeys = m_CurrKeys;
            m_FrameScrollX = 0.0f;
            m_FrameScrollY = 0.0f;
        }

        // Call once per frame to update cached mouse/key transitions and latch scroll.
        void Update()
        {
            m_FrameScrollX = m_ScrollAccumX;
            m_FrameScrollY = m_ScrollAccumY;
            m_ScrollAccumX = 0.0f;
            m_ScrollAccumY = 0.0f;
        }

    private:
        void* m_WindowHandle = nullptr;

        // GLFW supports mouse buttons 0..7 by default.
        static constexpr int kMouseButtons = 8;
        std::array<std::uint8_t, kMouseButtons> m_PrevMouse{};
        std::array<std::uint8_t, kMouseButtons> m_CurrMouse{};

        // Key state tracking for just-pressed detection.
        // Covers GLFW key codes 0..kMaxTrackedKeys-1.
        static constexpr int kMaxTrackedKeys = 400;
        std::array<std::uint8_t, kMaxTrackedKeys> m_PrevKeys{};
        std::array<std::uint8_t, kMaxTrackedKeys> m_CurrKeys{};

        // Scroll accumulator (event-driven, latched each frame).
        float m_ScrollAccumX = 0.0f;
        float m_ScrollAccumY = 0.0f;
        float m_FrameScrollX = 0.0f;
        float m_FrameScrollY = 0.0f;
        XY m_MousePosition{};
    };
}