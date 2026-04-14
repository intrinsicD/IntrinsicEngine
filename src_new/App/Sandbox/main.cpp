import Extrinsic.Runtime.Engine;
import Extrinsic.Core.Application;
import Extrinsic.Core.Config;

import <memory>;
import Sandbox;

int main()
{
    Extrinsic::EngineConfig config{};
    config.Window.Title = "Modular Vulkan Engine";
    config.Window.Width = 1600;
    config.Window.Height = 900;
    config.Render.Backend = Engine::Core::GraphicsBackend::Vulkan;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = true;
    config.Render.FramesInFlight = 2;

    auto app = std::make_unique<SandboxApp>();

    Engine::Core::Engine engine{config, std::move(app)};
    engine.Initialize();
    engine.Run();
    engine.Shutdown();

    return 0;
}