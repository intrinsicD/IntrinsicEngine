module;

#include <memory>

export module Extrinsic.Sandbox;

import Extrinsic.Runtime.Engine;
import Extrinsic.Runtime.SandboxEditorUi;

namespace Extrinsic::Sandbox
{
    class App final : public Runtime::IApplication
    {
    public:
        void OnInitialize(Runtime::Engine& engine) override
        {
            m_EditorUi.Attach(engine);
        }

        void OnSimTick(Runtime::Engine& engine, double fixedDt) override
        {
            (void)engine;
            (void)fixedDt;
        }

        void OnVariableTick(Runtime::Engine& engine,
                            double alpha, double dt) override
        {
            (void)engine;
            (void)alpha;
            (void)dt;
        }

        void OnShutdown(Runtime::Engine& engine) override
        {
            (void)engine;
            m_EditorUi.Detach();
        }

    private:
        Runtime::SandboxEditorUi m_EditorUi{};
    };
}

export namespace Extrinsic::Sandbox
{
    [[nodiscard]] std::unique_ptr<Runtime::IApplication> CreateSandboxApp();
}
