module;

#include <memory>

module Extrinsic.Runtime.EditorWorkspaceAttachment;

import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime
{
    struct EditorWorkspaceAttachment::Impl final
    {
        std::shared_ptr<EditorFeatureDetail::EditorWorkspaceAttachmentState>
            State{std::make_shared<
                EditorFeatureDetail::EditorWorkspaceAttachmentState>()};
    };

    EditorWorkspaceAttachment::EditorWorkspaceAttachment()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    EditorWorkspaceAttachment::~EditorWorkspaceAttachment()
    {
        Detach();
    }

    void EditorWorkspaceAttachment::Attach(
        WorldRegistry& worlds,
        ServiceRegistry& services)
    {
        m_Impl->State->Session.Attach(worlds, services);
    }

    void EditorWorkspaceAttachment::Detach()
    {
        if (m_Impl != nullptr && m_Impl->State != nullptr)
            m_Impl->State->Session.Detach();
    }

    bool EditorWorkspaceAttachment::IsAttached() const noexcept
    {
        return m_Impl != nullptr &&
            m_Impl->State != nullptr &&
            m_Impl->State->Session.IsAttached();
    }

    std::shared_ptr<void>
    EditorWorkspaceAttachment::FeatureBindingState() const noexcept
    {
        return m_Impl != nullptr
            ? std::static_pointer_cast<void>(m_Impl->State)
            : std::shared_ptr<void>{};
    }
}
