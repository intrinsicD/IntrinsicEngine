module;

#include <memory>
#include <string_view>

export module Extrinsic.Runtime.EditorUiModule;

export import Extrinsic.Runtime.EditorUiHost;

import Extrinsic.Core.Error;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.ServiceRegistry;

namespace Extrinsic::Runtime
{
    export class EditorUiModule final : public IRuntimeModule
    {
    public:
        EditorUiModule();
        ~EditorUiModule() override;

        EditorUiModule(const EditorUiModule&) = delete;
        EditorUiModule& operator=(const EditorUiModule&) = delete;

        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        [[nodiscard]] Core::Result OnResolve(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

    private:
        struct Impl;

        void RunUiBegin(RuntimeFrameHookContext& context);
        void RunUiBuild(RuntimeFrameHookContext& context);
        void RunUiEndCapture(RuntimeFrameHookContext& context);
        void ShutdownAndReset(ServiceRegistry* services) noexcept;

        std::unique_ptr<Impl> m_Impl{};
    };
}
