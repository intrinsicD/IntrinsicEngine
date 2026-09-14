module;
#include <functional>
#include <memory>

module Extrinsic.Runtime.PointSetOperations;

import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
EditorPointSetPreparedFrame PrepareEditorPointSetFrame(const EditorWorkspaceAttachment& attachment)
{
    EditorPointSetPreparedFrame prepared{};
    const auto state = EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
    if (!state) return prepared;
    (void)state->Session.VisitPreparedFrame([&](EditorFeatureDetail::EditorWorkspacePreparedFrame frame)
    {
        prepared.Commands = BindEditorProcessingCommands(frame.Geometry);
        if (frame.Results.PointSetResultSinks) prepared.ResultSinks = *frame.Results.PointSetResultSinks;
        if (frame.Results.PointSetResults) prepared.Results = *frame.Results.PointSetResults;
    });
    return prepared;
}

} // namespace Extrinsic::Runtime
