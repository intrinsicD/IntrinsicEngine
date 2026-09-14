// Minimal runtime module lifecycle; setup and frame composition stay in Runtime.Module.
module;
#include <string_view>
export module Extrinsic.Runtime.ModuleLifecycle;
import Extrinsic.Core.Error;
export namespace Extrinsic::Runtime
{
    extern "C++"
    {
        class EngineSetup;
        struct RuntimeModuleShutdownContext;
    }
    using RuntimeModuleResult = Core::Result;

    [[nodiscard]] inline RuntimeModuleResult RuntimeModuleOk() { return Core::Ok(); }

    class IRuntimeModule
    {
    public:
        virtual ~IRuntimeModule() = default;

        [[nodiscard]] virtual std::string_view Name() const noexcept = 0;
        [[nodiscard]] virtual RuntimeModuleResult OnRegister(EngineSetup& setup) = 0;
        [[nodiscard]] virtual RuntimeModuleResult OnResolve(EngineSetup&)
        {
            return RuntimeModuleOk();
        }
        virtual void OnShutdown(RuntimeModuleShutdownContext& context) = 0;
    };
}
