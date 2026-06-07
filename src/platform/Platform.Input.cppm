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
    }

    class Context {
    public:
        struct XY
        {
            float x{0.0f}, y{0.0f};
        };

        void Initialize(void* windowHandle);

        [[nodiscard]] bool IsKeyPressed(int keycode) const;
        [[nodiscard]] bool IsKeyJustPressed(int keycode) const;
        [[nodiscard]] bool IsMouseButtonPressed(int button) const;
        [[nodiscard]] bool IsMouseButtonJustPressed(int button) const;

        [[nodiscard]] XY GetMousePosition() const { return m_MousePosition; }
        [[nodiscard]] XY GetScrollDelta() const { return {m_FrameScrollX, m_FrameScrollY}; }
        [[nodiscard]] XY GetScrollAccum() const { return {m_ScrollAccumX, m_ScrollAccumY}; }

        void AccumulateScroll(float xOffset, float yOffset);
        void SetKeyState(int keycode, bool pressed);
        void SetMouseButtonState(int button, bool pressed);
        void SetMousePosition(float x, float y);
        void BeginFrame();
        void Update();

    private:
        void* m_WindowHandle = nullptr;

        static constexpr int kMouseButtons = 8;
        std::array<std::uint8_t, kMouseButtons> m_PrevMouse{};
        std::array<std::uint8_t, kMouseButtons> m_CurrMouse{};

        static constexpr int kMaxTrackedKeys = 400;
        std::array<std::uint8_t, kMaxTrackedKeys> m_PrevKeys{};
        std::array<std::uint8_t, kMaxTrackedKeys> m_CurrKeys{};

        float m_ScrollAccumX = 0.0f;
        float m_ScrollAccumY = 0.0f;
        float m_FrameScrollX = 0.0f;
        float m_FrameScrollY = 0.0f;
        XY m_MousePosition{};
    };
}
