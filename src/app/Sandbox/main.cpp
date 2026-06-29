#include <cstddef>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

import Extrinsic.Runtime.Engine;

import Extrinsic.Sandbox;

int main(int argc, char** argv)
{
    std::vector<std::string_view> args{};
    args.reserve(static_cast<std::size_t>(argc));
    for (int index = 0; index < argc; ++index)
    {
        args.emplace_back(argv[index]);
    }

    auto boot = Extrinsic::Runtime::ResolveEngineConfigForBoot(args);
    auto config = std::move(boot.Config);

    auto app = Extrinsic::Sandbox::CreateSandboxApp();

    Extrinsic::Runtime::Engine engine{config, std::move(app)};
    engine.Initialize();
    engine.Run();
    engine.Shutdown();

    return 0;
}
