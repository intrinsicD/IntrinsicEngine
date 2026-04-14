export module Extrinsic.Core.Engine;

import <cstdint>;
import <memory>;
import Extrinsic.Core.Config;
import Extrinsic.Core.Application;
import Extrinsic.RHI.Device;
import Extrinsic.Platform.Window;
import Extrinsic.Render.Renderer;

namespace Extrinsic::Runtime
{

    export struct EngineConfig
    {
        Core::RenderConfig Render;
        Core::WindowConfig Window;
    };

    export class Engine
    {
    public:
        Engine(EngineConfig config, std::unique_ptr<Core::IApplication> application);
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
        [[nodiscard]] Render::IRenderer& GetRenderer() noexcept;

    private:
        void HandleResize();
        void Tick(double deltaSeconds);

    private:
        EngineConfig m_Config;
        std::unique_ptr<Core::IApplication> m_Application;
        std::unique_ptr<Platform::IWindow> m_Window;
        std::unique_ptr<RHI::IDevice> m_Device;
        std::unique_ptr<Render::IRenderer> m_Renderer;

        bool m_Initialized{false};
        bool m_Running{false};
    };
}