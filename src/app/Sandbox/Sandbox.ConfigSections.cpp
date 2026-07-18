module;

#include <exception>
#include <utility>

module Extrinsic.Sandbox.ConfigSections;

import Extrinsic.Runtime.SandboxConfigSections;

namespace Extrinsic::Sandbox
{
    Runtime::EngineConfigSectionRegistry CreateSandboxConfigSectionRegistry(
        SandboxConfigSectionCallbacks callbacks)
    {
        Runtime::EngineConfigSectionRegistry registry{};
        if (!registry.Register(
                Runtime::MakeProgressivePoissonConfigSectionRegistration(
                    std::move(callbacks.ProgressivePoisson))) ||
            !registry.Register(
                Runtime::MakeParameterizationConfigSectionRegistration(
                    std::move(callbacks.Parameterization))))
        {
            std::terminate();
        }
        return registry;
    }
}
