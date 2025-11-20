module;
#include <string>
#include <functional>
#include <utility>

export module Core.Window;

namespace Core::Windowing
{
    export struct WindowProps
    {
        std::string Title = "Intrinsic Engine";
        int Width = 1280;
        int Height = 720;
    };

    // Simple Event Types for now
    export enum class EventType
    {
        WindowClose,
        WindowResize,
        KeyPressed,
        KeyReleased
        // Mouse events to follow later
    };

    export struct Event
    {
        EventType Type{};
        // Minimal payload for now. In a real engine, this would be a variant or union.
        int KeyCode = 0;
        int Width = 0;
        int Height = 0;
    };

    using EventCallbackFn = std::function<void(const Event&)>;

    export class Window
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
        [[nodiscard]] int GetWidth() const { return m_Data.Width; }
        [[nodiscard]] int GetHeight() const { return m_Data.Height; }

        // Set the function that the Window calls when something happens
        void SetEventCallback(const EventCallbackFn& callback) { m_Data.Callback = callback; }

        // We use void* to avoid including vulkan.h in the module interface
        // instance is a VkInstance, allocator is VkAllocationCallbacks*
        [[nodiscard]] bool CreateSurface(void* instance, void* allocator, void* surfaceOut);

    private:
        void* m_Window = nullptr;

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
