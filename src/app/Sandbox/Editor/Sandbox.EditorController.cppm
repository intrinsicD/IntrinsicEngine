module;

#include <memory>

export module Extrinsic.Sandbox.Editor.Controller;

import Extrinsic.Runtime.Engine;

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

        void Attach(Runtime::Engine& engine);
        void Detach();

        [[nodiscard]] bool IsAttached() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
