module;

#include <exception>
#include <utility>

module Extrinsic.Sandbox.ConfigSections;

import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.CurvatureSegmentationConfig;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.PhysicsModule;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
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
                Runtime::MakeCurvatureSegmentationConfigSectionRegistration(
                    std::move(callbacks.CurvatureSegmentation))) ||
            !registry.Register(
                Runtime::MakeProgressivePoissonConfigSectionRegistration(
                    std::move(callbacks.ProgressivePoisson))) ||
            !registry.Register(
                Runtime::MakeParameterizationConfigSectionRegistration(
                    std::move(callbacks.Parameterization))) ||
            !registry.Register(
                Runtime::
                    MakePointCloudConsolidationConfigSectionRegistration(
                        std::move(callbacks.PointCloudConsolidation))) ||
            !registry.Register(
                Runtime::MakePhysicsModuleConfigSectionRegistration(
                    std::move(callbacks.Physics))))
        {
            std::terminate();
        }
        return registry;
    }
}
