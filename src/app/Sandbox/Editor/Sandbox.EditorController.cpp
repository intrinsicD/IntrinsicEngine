module;

#include <memory>

module Extrinsic.Sandbox.Editor.Controller;

import Extrinsic.Sandbox.Editor.DomainPanels;
import Extrinsic.Sandbox.Editor.MeshProcessingPanels;
import Extrinsic.Sandbox.Editor.MethodPanels;
import Extrinsic.Runtime.SandboxEditorUi;

namespace Extrinsic::Sandbox::Editor
{
    struct SandboxEditorController::Impl
    {
        Runtime::SandboxEditorUi EditorUi{};
        MethodPanels Method{};
        MeshProcessingPanels MeshProcessing{};
        DomainPanels Domain{};

        void Attach(Runtime::Engine& engine)
        {
            Detach();
            EditorUi.Attach(engine);
            Method.Register(EditorUi);
            MeshProcessing.Register(EditorUi);
            Domain.Register(EditorUi);
        }

        void Detach()
        {
            Domain.Unregister();
            MeshProcessing.Unregister();
            Method.Unregister();
            EditorUi.Detach();
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
        return m_Impl->EditorUi.IsAttached();
    }
}
