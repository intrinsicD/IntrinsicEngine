module;

#include <glm/glm.hpp>
#include <array>

export module Core.Input;

export namespace Core::Input {
    // Common Key Codes (Subset)
    namespace Key {
        constexpr int W = 87;
        constexpr int A = 65;
        constexpr int S = 83;
        constexpr int D = 68;
        constexpr int F = 70;
        constexpr int R = 82;
        constexpr int Space = 32;
        constexpr int Escape = 256;
        constexpr int LeftShift = 340;
    }

    class Context {
    public:
        void Initialize(void* windowHandle); // GLFWwindow*

        [[nodiscard]] bool IsKeyPressed(int keycode) const;
        [[nodiscard]] bool IsKeyJustPressed(int keycode) const;
        [[nodiscard]] bool IsMouseButtonPressed(int button) const;
        [[nodiscard]] bool IsMouseButtonJustPressed(int button) const;
        [[nodiscard]] glm::vec2 GetMousePosition() const;
        [[nodiscard]] glm::vec2 GetScrollDelta() const;

        // Accumulate scroll input (called from window scroll callback).
        void AccumulateScroll(float xOffset, float yOffset);

        // Call once per frame to update cached mouse/key transitions and latch scroll.
        void Update();

    private:
        void* m_WindowHandle = nullptr;

        // GLFW supports mouse buttons 0..7 by default.
        static constexpr int kMouseButtons = 8;
        std::array<uint8_t, kMouseButtons> m_PrevMouse{};
        std::array<uint8_t, kMouseButtons> m_CurrMouse{};

        // Key state tracking for just-pressed detection.
        // Covers GLFW key codes 0..kMaxTrackedKeys-1.
        static constexpr int kMaxTrackedKeys = 400;
        std::array<uint8_t, kMaxTrackedKeys> m_PrevKeys{};
        std::array<uint8_t, kMaxTrackedKeys> m_CurrKeys{};

        // Scroll accumulator (event-driven, latched each frame).
        float m_ScrollAccumX = 0.0f;
        float m_ScrollAccumY = 0.0f;
        float m_FrameScrollX = 0.0f;
        float m_FrameScrollY = 0.0f;
    };
}