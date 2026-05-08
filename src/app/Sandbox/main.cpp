#include <memory>

import Extrinsic.Runtime.Engine;

import Extrinsic.Sandbox;

int main()
{
    auto config = Extrinsic::Runtime::CreateReferenceEngineConfig();

    auto app = std::make_unique<Extrinsic::Sandbox::App>();

    Extrinsic::Runtime::Engine engine{config, std::move(app)};
    engine.Initialize();
    engine.Run();
    engine.Shutdown();

    return 0;
}
