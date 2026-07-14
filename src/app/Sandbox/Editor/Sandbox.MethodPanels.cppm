module;

#include <memory>

export module Extrinsic.Sandbox.Editor.MethodPanels;

import Extrinsic.Sandbox.Editor.Shell;

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

        void Register(EditorShell& editorShell);
        void Unregister();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
