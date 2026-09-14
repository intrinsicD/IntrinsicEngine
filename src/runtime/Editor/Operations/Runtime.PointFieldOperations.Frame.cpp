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
        if (frame.Results.PointFieldResultSinks) prepared.ResultSinks = *frame.Results.PointFieldResultSinks;
        if (frame.Results.PointFieldResults) prepared.Results = *frame.Results.PointFieldResults;
    });
    return prepared;
}

} // namespace Extrinsic::Runtime
