module;

export module Extrinsic.Sandbox.ConfigSections;

import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.ProgressivePoissonConfig;

export namespace Extrinsic::Sandbox
{
    struct SandboxConfigSectionCallbacks
    {
        Runtime::RuntimeEngineConfigSectionChangedCallback Clustering{};
        Runtime::RuntimeEngineConfigSectionChangedCallback ProgressivePoisson{};
        Runtime::RuntimeEngineConfigSectionChangedCallback Parameterization{};
        Runtime::RuntimeEngineConfigSectionChangedCallback
            PointCloudConsolidation{};
    };

    [[nodiscard]] Runtime::RuntimeEngineConfigSectionRegistry
    CreateSandboxConfigSectionRegistry(
        SandboxConfigSectionCallbacks callbacks = {});
}
