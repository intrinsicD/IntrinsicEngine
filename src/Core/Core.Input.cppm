module;

export module Core.Input;

export namespace Core::Input {

    // We need to set this context when the Window is created
    void Initialize(void* windowHandle);

    // Polling API
    [[nodiscard]] bool IsKeyPressed(int keycode);
    [[nodiscard]] bool IsMouseButtonPressed(int button);
    [[nodiscard]] double GetMouseX();
    [[nodiscard]] double GetMouseY();
    
    // Common Key Codes (Subset)
    namespace Key {
        constexpr int W = 87;
        constexpr int A = 65;
        constexpr int S = 83;
        constexpr int D = 68;
        constexpr int Space = 32;
        constexpr int Escape = 256;
        constexpr int LeftShift = 340;
    }
}