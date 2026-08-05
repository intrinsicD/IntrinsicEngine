module;

#include <memory>

export module Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.Private.EditorFeatures;

export namespace Extrinsic::Runtime::EditorFeatureDetail
{
    struct EditorWorkspaceAttachmentState final
    {
        EditorWorkspaceSession Session{};
    };

    [[nodiscard]] inline std::shared_ptr<EditorWorkspaceAttachmentState>
    ResolveEditorWorkspaceAttachmentState(
        const EditorWorkspaceAttachment& attachment) noexcept
    {
        return std::static_pointer_cast<EditorWorkspaceAttachmentState>(
            attachment.FeatureBindingState());
    }
}
