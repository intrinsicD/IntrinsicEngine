// Declares the app-owned geometry-processing panels that present runtime views
// and command surfaces without taking ownership of geometry operations.
module;

#include <memory>

export module Extrinsic.Sandbox.Editor.MeshProcessingPanels;

import Extrinsic.Sandbox.Editor.Shell;

export namespace Extrinsic::Sandbox::Editor
{
    class MeshProcessingPanels final
    {
    public:
        MeshProcessingPanels();
        ~MeshProcessingPanels();

        MeshProcessingPanels(const MeshProcessingPanels&) = delete;
        MeshProcessingPanels& operator=(const MeshProcessingPanels&) = delete;
        MeshProcessingPanels(MeshProcessingPanels&&) = delete;
        MeshProcessingPanels& operator=(MeshProcessingPanels&&) = delete;

        void Register(EditorShell& editorShell);
        void Unregister();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
