module;
#include <functional>
#include <memory>

module Extrinsic.Runtime.PointFieldOperations;

import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
EditorPointFieldPreparedFrame PrepareEditorPointFieldFrame(const EditorWorkspaceAttachment& attachment)
{
    EditorPointFieldPreparedFrame prepared{};
    const auto state = EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
    if (!state) return prepared;
    (void)state->Session.VisitPreparedFrame([&](EditorFeatureDetail::EditorWorkspacePreparedFrame frame)
    {
        prepared.Commands = BindEditorProcessingCommands(frame.Geometry);
        if (frame.Results.OperationResults)
            prepared.Results = frame.Results.OperationResults->Copy();
        if (frame.Results.DismissOperationResult)
            prepared.DismissResult = *frame.Results.DismissOperationResult;
    });
    return prepared;
}

} // namespace Extrinsic::Runtime
