module;
#include <string>
#include <functional>
#include <variant>
#include <vector>

export module Core.Window;

export namespace Core::Windowing
{
    struct WindowProps
    {
        std::string Title = "Intrinsic Engine";
        int Width = 1280;
        int Height = 720;
    };

    struct WindowCloseEvent
    {
    };

    struct WindowResizeEvent
    {
        int Width;
        int Height;
    };

    struct KeyEvent
    {
        int KeyCode;
        bool IsPressed; // true = pressed, false = released
    };

    struct MouseButtonEvent
    {
        int ButtonCode;
        bool IsPressed; // true = pressed, false = released
    };

    struct ScrollEvent
    {
        double XOffset;
        double YOffset;
    };

    struct CursorEvent
    {
        double XPos;
        double YPos;
    };

    struct CharEvent
    {
        unsigned int Character;
    };

    struct WindowDropEvent
    {
        std::vector<std::string> Paths;
    };

    // Type-safe variant
    using Event = std::variant<
        WindowCloseEvent,
        WindowResizeEvent,
        KeyEvent,
        MouseButtonEvent,
        ScrollEvent,
        CursorEvent,
        CharEvent,
        WindowDropEvent
    >;

    using EventCallbackFn = std::function<void(const Event&)>;

    class Window
    {
    public:
        explicit Window(const WindowProps& props);
        ~Window();

        // Deleted copy, allow move (Window is a unique resource)
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;

        void OnUpdate(); // Call this every frame (Polls events)

        [[nodiscard]] bool ShouldClose() const;
        [[nodiscard]] void* GetNativeHandle() const { return m_Window; } // Returns GLFWwindow* void*
        [[nodiscard]] int GetWidth() const;
        [[nodiscard]] int GetHeight() const;
        [[nodiscard]] bool IsValid() const { return m_IsValid; }

        // Set the function that the Window calls when something happens
        void SetEventCallback(const EventCallbackFn& callback) { m_Data.Callback = callback; }

        void SetTitle(const std::string& title) const;

        // We use void* to avoid including vulkan.h in the module interface
        // instance is a VkInstance, allocator is VkAllocationCallbacks*
        [[nodiscard]] bool CreateSurface(void* instance, void* allocator, void* surfaceOut);

    private:
        void* m_Window = nullptr;
        bool m_IsValid = false;

        struct WindowData
        {
            std::string Title;
            int Width;
            int Height;
            EventCallbackFn Callback;
        };

        WindowData m_Data;

        // Internal init helper
        void Init(const WindowProps& props);
        void Shutdown();
    };
}
