module;
#include <functional>
#include <memory>

module Extrinsic.Runtime.GeometryProcessingOperations;

import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
EditorProcessingCommands PrepareEditorProcessingCommands(
    const EditorWorkspaceAttachment& attachment)
{
    EditorProcessingCommands commands{};
    const auto state = EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
    if (!state) return commands;
    (void)state->Session.VisitPreparedFrame([&](EditorFeatureDetail::EditorWorkspacePreparedFrame frame)
    {
        commands = BindEditorProcessingCommands(frame.Geometry);
    });
    return commands;
}

} // namespace Extrinsic::Runtime
