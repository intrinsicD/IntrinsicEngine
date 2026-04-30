module;

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <variant>

export module Extrinsic.Platform.Window;

import Extrinsic.Core.Config.Window;
import Extrinsic.Platform.Input;

namespace Extrinsic::Platform
{
    export struct Extent2D
    {
        int Width{0};
        int Height{0};
    };

    export struct WindowCloseEvent
    {
    };

    export struct WindowResizeEvent
    {
        int Width;
        int Height;
    };

    export struct KeyEvent
    {
        int KeyCode;
        bool IsPressed; // true = pressed, false = released
    };

    export struct MouseButtonEvent
    {
        int ButtonCode;
        bool IsPressed; // true = pressed, false = released
    };

    export struct ScrollEvent
    {
        double XOffset;
        double YOffset;
    };

    export struct CursorEvent
    {
        double XPos;
        double YPos;
    };

    export struct CharEvent
    {
        unsigned int Character;
    };

    export struct WindowDropEvent
    {
        std::vector<std::string> Paths{};
    };

    // Type-safe variant
    export using Event = std::variant<
        WindowCloseEvent,
        WindowResizeEvent,
        KeyEvent,
        MouseButtonEvent,
        ScrollEvent,
        CursorEvent,
        CharEvent,
        WindowDropEvent
    >;

    export enum class CursorMode
    {
        Normal,
        Hidden,
        Disabled,
    };

    export class IWindow
    {
    public:
        virtual ~IWindow() = default;

        virtual void PollEvents() = 0;
        [[nodiscard]] virtual bool ShouldClose() const = 0;
        [[nodiscard]] virtual bool IsMinimized() const = 0;
        [[nodiscard]] virtual bool WasResized() const = 0;
        virtual void AcknowledgeResize() = 0;
        [[nodiscard]] virtual bool ConsumeInputActivity() = 0;
        [[nodiscard]] virtual Extent2D GetWindowExtent() const = 0;
        [[nodiscard]] virtual Extent2D GetFramebufferExtent() const = 0;
        [[nodiscard]] virtual void* GetNativeHandle() const = 0;

        using EventCallbackFn = std::function<void(const Event&)>;
        virtual void Listen(EventCallbackFn callback) = 0;
        [[nodiscard]] virtual std::vector<Event> DrainEvents() = 0;

        virtual void OnUpdate() = 0;
        virtual void WaitForEventsTimeout(double timeoutSeconds) = 0;

        virtual void SetClipboardText(std::string_view text) = 0;
        [[nodiscard]] virtual std::string GetClipboardText() const = 0;
        virtual void SetCursorMode(CursorMode mode) = 0;
        [[nodiscard]] virtual CursorMode GetCursorMode() const = 0;


        [[nodiscard]] const Input::Context& GetInput() const { return m_InputContext; }

    protected:
        [[nodiscard]] Input::Context& GetInput() { return m_InputContext; }
    private:
        Input::Context m_InputContext{};
    };

    export std::unique_ptr<IWindow> CreateWindow(const Core::Config::WindowConfig& config);
}
