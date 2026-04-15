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

        void Initialize(void* windowHandle); // GLFWwindow*

        [[nodiscard]] bool IsKeyPressed(int keycode) const;
        [[nodiscard]] bool IsKeyJustPressed(int keycode) const;
        [[nodiscard]] bool IsMouseButtonPressed(int button) const;
        [[nodiscard]] bool IsMouseButtonJustPressed(int button) const;
        [[nodiscard]] XY GetMousePosition() const;
        [[nodiscard]] XY GetScrollDelta() const;
        [[nodiscard]] XY GetScrollAccum() const;

        // Accumulate scroll input (called from window scroll callback).
        void AccumulateScroll(float xOffset, float yOffset);

        // Call once per frame to update cached mouse/key transitions and latch scroll.
        void Update();

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
    };
}