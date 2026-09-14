// Registers method windows without exposing panel state or runtime operation models.
module;
#include <memory>
#include "Sandbox.EditorFwd.hpp"
export module Extrinsic.Sandbox.Editor.MethodPanels;
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
