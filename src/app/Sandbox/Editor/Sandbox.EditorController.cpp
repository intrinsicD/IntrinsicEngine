module;

#include <memory>

module Extrinsic.Sandbox.Editor.Controller;

import Extrinsic.Sandbox.Editor.DomainPanels;
import Extrinsic.Sandbox.Editor.Shell;
import Extrinsic.Sandbox.Editor.MeshProcessingPanels;
import Extrinsic.Sandbox.Editor.MethodPanels;

namespace Extrinsic::Sandbox::Editor
{
    struct SandboxEditorController::Impl
    {
        EditorShell Shell{};
        MethodPanels Method{};
        MeshProcessingPanels MeshProcessing{};
        DomainPanels Domain{};

        void Attach(Runtime::Engine& engine)
        {
            Detach();
            Shell.Attach(engine);
            Method.Register(Shell);
            MeshProcessing.Register(Shell);
            Domain.Register(Shell);
        }

        void Detach()
        {
            Domain.Unregister();
            MeshProcessing.Unregister();
            Method.Unregister();
            Shell.Detach();
        }
    };

    SandboxEditorController::SandboxEditorController()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    SandboxEditorController::~SandboxEditorController()
    {
        Detach();
    }

    void SandboxEditorController::Attach(Runtime::Engine& engine)
    {
        m_Impl->Attach(engine);
    }

    void SandboxEditorController::Detach()
    {
        m_Impl->Detach();
    }

    bool SandboxEditorController::IsAttached() const noexcept
    {
        return m_Impl->Shell.IsAttached();
    }
}
