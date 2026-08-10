module;

export module Extrinsic.Sandbox.ConfigSections;

import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.CurvatureSegmentationConfig;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.PhysicsModule;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.ProgressivePoissonConfig;

export namespace Extrinsic::Sandbox
{
    struct SandboxConfigSectionCallbacks
    {
        Runtime::RuntimeEngineConfigSectionChangedCallback Clustering{};
        Runtime::RuntimeEngineConfigSectionChangedCallback
            CurvatureSegmentation{};
        Runtime::RuntimeEngineConfigSectionChangedCallback ProgressivePoisson{};
        Runtime::RuntimeEngineConfigSectionChangedCallback Parameterization{};
        Runtime::RuntimeEngineConfigSectionChangedCallback
            PointCloudConsolidation{};
        Runtime::PhysicsModuleConfigChangedCallback Physics{};
    };

    [[nodiscard]] Runtime::RuntimeEngineConfigSectionRegistry
    CreateSandboxConfigSectionRegistry(
        SandboxConfigSectionCallbacks callbacks = {});
}
