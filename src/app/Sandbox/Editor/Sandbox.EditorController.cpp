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

        void Attach(Runtime::WorldRegistry& worlds, Runtime::ServiceRegistry& services)
        {
            Detach();
            Shell.Attach(worlds, services);
            if (!Shell.IsAttached())
                return;
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

    void SandboxEditorController::Attach(Runtime::WorldRegistry& worlds,
                                         Runtime::ServiceRegistry& services)
    {
        m_Impl->Attach(worlds, services);
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
