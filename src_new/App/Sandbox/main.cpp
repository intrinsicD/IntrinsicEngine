import Extrinsic.Runtime.Engine;
import Extrinsic.Core.Config;

import <memory>;
import Sandbox;

int main()
{
    Extrinsic::Runtime::EngineConfig config{};
    config.Window.Title = "Modular Vulkan Engine";
    config.Window.Width = 1600;
    config.Window.Height = 900;
    config.Render.Backend = Extrinsic::Core::GraphicsBackend::Vulkan;
    config.Render.EnableValidation = true;
    config.Render.EnableVSync = true;
    config.Render.FramesInFlight = 2;

    auto app = std::make_unique<Sandbox::App>();

    Extrinsic::Runtime::Engine engine{config, std::move(app)};
    engine.Initialize();
    engine.Run();
    engine.Shutdown();

    return 0;
}
