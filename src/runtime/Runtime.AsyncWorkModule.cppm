module;

#include <cstdint>
#include <memory>
#include <string_view>

export module Extrinsic.Runtime.AsyncWorkModule;

import Extrinsic.Core.Error;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.StreamingExecutor;
import Extrinsic.Runtime.WorldHandle;

namespace Extrinsic::Runtime
{
    export class AsyncWorkModule final
        : public IRuntimeModule
        , public Core::IStreamingFrameHooks
    {
    public:
        AsyncWorkModule();
        ~AsyncWorkModule() override;

        AsyncWorkModule(const AsyncWorkModule&) = delete;
        AsyncWorkModule& operator=(const AsyncWorkModule&) = delete;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

    private:
        void DrainCompletions() override;
        void ApplyMainThreadResults() override;
        void SubmitFrameWork() override;
        void PumpBackground(std::uint32_t maxLaunches) override;

        [[nodiscard]] std::uint32_t ApplyMainThreadResults(
            std::uint32_t maxApplyCount);
        void RetireWorld(WorldHandle world);
        void ShutdownAndReset(ServiceRegistry* services);

        std::unique_ptr<StreamingExecutor> m_StreamingExecutor{};
        std::unique_ptr<DerivedJobRegistry> m_DerivedJobRegistry{};
        KernelEventSubscription m_WorldRetirementSubscription{};
    };
}
