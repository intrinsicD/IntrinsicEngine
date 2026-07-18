module;

export module Extrinsic.Sandbox.ConfigSections;

import Extrinsic.Runtime.SandboxConfigSections;

export namespace Extrinsic::Sandbox
{
    struct SandboxConfigSectionCallbacks
    {
        Runtime::EngineConfigSectionChangedCallback ProgressivePoisson{};
        Runtime::EngineConfigSectionChangedCallback Parameterization{};
    };

    [[nodiscard]] Runtime::EngineConfigSectionRegistry
    CreateSandboxConfigSectionRegistry(
        SandboxConfigSectionCallbacks callbacks = {});
}
