module;

#include <memory>

export module Extrinsic.Runtime.Engine;

import Extrinsic.Core.Config.Engine;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Window;
import Extrinsic.Graphics.Renderer;

namespace Extrinsic::Runtime
{
    export class Engine;

    export class IApplication
    {
    public:
        virtual ~IApplication() = default;

        virtual void OnInitialize(Engine& engine) = 0;
        virtual void OnUpdate(Engine& engine, double deltaSeconds) = 0;
        virtual void OnShutdown(Engine& engine) = 0;
    };

    class Engine
    {
    public:
        Engine(Core::Config::EngineConfig config, std::unique_ptr<IApplication> application);
        ~Engine();

        Engine(const Engine&) = delete;
        Engine& operator=(const Engine&) = delete;

        void Initialize();
        void Run();
        void Shutdown();

        [[nodiscard]] bool IsRunning() const noexcept;
        void RequestExit() noexcept;

        [[nodiscard]] Platform::IWindow& GetWindow() noexcept;
        [[nodiscard]] RHI::IDevice& GetDevice() noexcept;
        [[nodiscard]] Graphics::IRenderer& GetRenderer() noexcept;

    private:
        void HandleResize();
        void Tick(double deltaSeconds);

    private:
        Core::Config::EngineConfig m_Config;
        std::unique_ptr<IApplication> m_Application;
        std::unique_ptr<Platform::IWindow> m_Window;
        std::unique_ptr<RHI::IDevice> m_Device;
        std::unique_ptr<Graphics::IRenderer> m_Renderer;

        bool m_Initialized{false};
        bool m_Running{false};
    };
}
