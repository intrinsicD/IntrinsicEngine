module;

#include <memory>

export module Extrinsic.Sandbox.Editor.MethodPanels;

import Extrinsic.Runtime.SandboxEditorUi;

export namespace Extrinsic::Sandbox::Editor
{
    class MethodPanels final
    {
    public:
        MethodPanels();
        ~MethodPanels();

        MethodPanels(const MethodPanels&) = delete;
        MethodPanels& operator=(const MethodPanels&) = delete;
        MethodPanels(MethodPanels&&) = delete;
        MethodPanels& operator=(MethodPanels&&) = delete;

        void Register(Runtime::SandboxEditorUi& editorUi);
        void Unregister();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
