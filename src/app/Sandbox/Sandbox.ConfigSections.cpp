module;

#include <exception>
#include <utility>

module Extrinsic.Sandbox.ConfigSections;

import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.GeodesicsConfig;
import Extrinsic.Runtime.RegistrationConfig;
import Extrinsic.Runtime.NormalEstimationConfig;
import Extrinsic.Runtime.OutlierAnalysisConfig;
import Extrinsic.Runtime.KernelDensityConfig;
import Extrinsic.Runtime.PointSpacingConfig;
import Extrinsic.Runtime.BilateralFilterConfig;
import Extrinsic.Runtime.KeypointAnalysisConfig;
import Extrinsic.Runtime.DescriptorAnalysisConfig;
import Extrinsic.Runtime.SelectionController;
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
        if (!registry.Register(Runtime::MakeSelectionConfigSectionRegistration()) ||
            !registry.Register(Runtime::MakeGeodesicsConfigSectionRegistration()) ||
            !registry.Register(Runtime::MakeRegistrationConfigSectionRegistration()) ||
            !registry.Register(Runtime::MakeNormalEstimationConfigSectionRegistration()) ||
            !registry.Register(Runtime::MakeOutlierAnalysisConfigSectionRegistration()) ||
            !registry.Register(Runtime::MakeKernelDensityConfigSectionRegistration()) ||
            !registry.Register(Runtime::MakePointSpacingConfigSectionRegistration()) ||
            !registry.Register(Runtime::MakeBilateralFilterConfigSectionRegistration()) ||
            !registry.Register(Runtime::MakeKeypointAnalysisConfigSectionRegistration()) ||
            !registry.Register(Runtime::MakeDescriptorAnalysisConfigSectionRegistration()) ||
            !registry.Register(
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
