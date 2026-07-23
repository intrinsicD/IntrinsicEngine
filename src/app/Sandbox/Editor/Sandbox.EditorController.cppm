module;

#include <memory>

export module Extrinsic.Sandbox.Editor.Controller;

import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldRegistry;

export namespace Extrinsic::Sandbox::Editor
{
    class SandboxEditorController final
    {
    public:
        SandboxEditorController();
        ~SandboxEditorController();

        SandboxEditorController(const SandboxEditorController&) = delete;
        SandboxEditorController& operator=(const SandboxEditorController&) = delete;
        SandboxEditorController(SandboxEditorController&&) = delete;
        SandboxEditorController& operator=(SandboxEditorController&&) = delete;

        void Attach(Runtime::WorldRegistry& worlds, Runtime::ServiceRegistry& services);
        void Detach();

        [[nodiscard]] bool IsAttached() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
