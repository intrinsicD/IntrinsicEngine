module;

#include <exception>
#include <utility>

module Extrinsic.Sandbox.ConfigSections;

import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.ProgressivePoissonConfig;

namespace Extrinsic::Sandbox
{
    Runtime::RuntimeEngineConfigSectionRegistry CreateSandboxConfigSectionRegistry(
        SandboxConfigSectionCallbacks callbacks)
    {
        Runtime::RuntimeEngineConfigSectionRegistry registry{};
        if (!registry.Register(
                Runtime::MakeClusteringConfigSectionRegistration(
                    std::move(callbacks.Clustering))) ||
            !registry.Register(
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
