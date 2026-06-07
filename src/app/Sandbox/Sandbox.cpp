module;

#include <memory>

module Extrinsic.Sandbox;

namespace Extrinsic::Sandbox
{
    std::unique_ptr<Runtime::IApplication> CreateSandboxApp()
    {
        return std::make_unique<App>();
    }
}
