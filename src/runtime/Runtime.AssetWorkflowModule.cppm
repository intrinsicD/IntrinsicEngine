module;

#include <memory>
#include <string_view>

export module Extrinsic.Runtime.AssetWorkflowModule;

import Extrinsic.Core.Error;
import Extrinsic.Core.FrameLoop;
import Extrinsic.Runtime.Module;

namespace Extrinsic::Runtime
{
    export class AssetWorkflowModule final
        : public IRuntimeModule
        , public Core::IAssetFrameHooks
    {
    public:
        AssetWorkflowModule();
        ~AssetWorkflowModule() override;

        AssetWorkflowModule(const AssetWorkflowModule&) = delete;
        AssetWorkflowModule& operator=(const AssetWorkflowModule&) = delete;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

    private:
        void TickAssets() override;

        struct Impl;
        std::unique_ptr<Impl> m_Impl{};
    };
}
