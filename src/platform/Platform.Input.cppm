// Stores polled keyboard, mouse, and cursor state independently of the window backend.
module;

#include <array>
#include <cstdint>

export module Extrinsic.Platform.Input;

export namespace Extrinsic::Platform::Input {
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
        constexpr int RightShift = 344;
        constexpr int RightControl = 345;
    }

    class Context {
    public:
        struct XY
        {
            float x{0.0f}, y{0.0f};
        };

        [[nodiscard]] bool IsKeyPressed(int keycode) const;
        // True if a press transition occurred since BeginFrame, even after release.
        [[nodiscard]] bool IsKeyJustPressed(int keycode) const;
        [[nodiscard]] bool IsMouseButtonPressed(int button) const;
        // True if a press transition occurred since BeginFrame, even after release.
        [[nodiscard]] bool IsMouseButtonJustPressed(int button) const;

        [[nodiscard]] XY GetMousePosition() const { return m_MousePosition; }
        [[nodiscard]] XY GetScrollDelta() const { return {m_FrameScrollX, m_FrameScrollY}; }

        void AccumulateScroll(float xOffset, float yOffset);
        void SetKeyState(int keycode, bool pressed);
        void SetMouseButtonState(int button, bool pressed);
        void SetMousePosition(float x, float y);
        void BeginFrame();
        void Update();

    private:
        static constexpr int kMouseButtons = 8;
        std::array<std::uint8_t, kMouseButtons> m_PressedMouse{};
        std::array<std::uint8_t, kMouseButtons> m_CurrMouse{};

        static constexpr int kMaxTrackedKeys = 400;
        std::array<std::uint8_t, kMaxTrackedKeys> m_PressedKeys{};
        std::array<std::uint8_t, kMaxTrackedKeys> m_CurrKeys{};

        float m_ScrollAccumX = 0.0f;
        float m_ScrollAccumY = 0.0f;
        float m_FrameScrollX = 0.0f;
        float m_FrameScrollY = 0.0f;
        XY m_MousePosition{};
    };
}
