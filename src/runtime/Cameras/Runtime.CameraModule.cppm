module;

#include <string_view>

export module Extrinsic.Runtime.CameraModule;

import Extrinsic.Core.Error;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;

namespace Extrinsic::Runtime
{
    export class CameraModule final : public IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

    private:
        void RunViewportInput(RuntimeViewportInputHookContext& context);
        void ShutdownAndReset(
            KernelEventBus* events,
            ServiceRegistry* services) noexcept;

        CameraControllerRegistry m_Registry{};
        KernelEventSubscription m_ActiveWorldChangedSubscription{};
        KernelEventSubscription m_WorldDestroyedSubscription{};
        bool m_Published{false};
    };
}
