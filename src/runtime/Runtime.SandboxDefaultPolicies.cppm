module;

#include <vector>

export module Extrinsic.Runtime.SandboxDefaultPolicies;

import Extrinsic.Runtime.AssetImportPipeline;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.Engine;

export namespace Extrinsic::Runtime
{
    struct RuntimeSandboxDefaultPolicyRegistration
    {
        std::vector<RuntimePostImportProcessorHandle> PostImportProcessors{};
        std::vector<RuntimeImportEntityAuthoringPolicyHandle>
            ImportEntityAuthoringPolicies{};
        std::vector<RuntimeImportCompletedHandlerHandle>
            ImportCompletedHandlers{};
        std::vector<RuntimeInputActionHandle> InputActions{};

        [[nodiscard]] bool IsEmpty() const noexcept
        {
            return PostImportProcessors.empty() &&
                   ImportEntityAuthoringPolicies.empty() &&
                   ImportCompletedHandlers.empty() &&
                   InputActions.empty();
        }
    };

    [[nodiscard]] RuntimeSandboxDefaultPolicyRegistration
        RegisterSandboxDefaultRuntimePolicies(
            Engine& engine,
            CameraControllerRegistry* cameraControllers);

    void UnregisterSandboxDefaultRuntimePolicies(
        Engine& engine,
        RuntimeSandboxDefaultPolicyRegistration& registration);
}
