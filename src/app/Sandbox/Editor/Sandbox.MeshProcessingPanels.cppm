// Declares the app-owned geometry-processing panels that present runtime views
// and command surfaces without taking ownership of geometry operations.
module;

#include <memory>
#include "Sandbox.EditorFwd.hpp"

export module Extrinsic.Sandbox.Editor.MeshProcessingPanels;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.NormalOperations;

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
        // Test seam: the Smooth Property window drives this GPU transaction (Accept / Discard /
        // Stop) as if it had started it, so the panel's transaction state is exercised without
        // a device.
        void InjectPropertySmoothingTransactionForTest(Runtime::EditorPropertySmoothingTransactionHandle transaction);
        // The same seam for the Normal Estimation window's Vulkan run (Accept / Discard).
        void InjectNormalTransactionForTest(Runtime::EditorNormalTransactionHandle transaction);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
