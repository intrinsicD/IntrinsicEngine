module;

#include <string_view>

export module Extrinsic.Runtime.AsyncWorkModule;

import Extrinsic.Core.Error;
import Extrinsic.Runtime.Module;

namespace Extrinsic::Runtime
{
    export class AsyncWorkModule final : public IRuntimeModule
    {
    public:
        [[nodiscard]] std::string_view Name() const noexcept override;
        [[nodiscard]] Core::Result OnRegister(EngineSetup& setup) override;
        void OnShutdown(RuntimeModuleShutdownContext& context) override;

    private:
        bool m_Registered{false};
    };
}
