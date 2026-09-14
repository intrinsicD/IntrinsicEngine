module;
#include <functional>
#include <memory>

module Extrinsic.Runtime.PointConstructionOperations;

import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
EditorPointConstructionPreparedFrame PrepareEditorPointConstructionFrame(const EditorWorkspaceAttachment& attachment)
{
    EditorPointConstructionPreparedFrame prepared{};
    const auto state = EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
    if (!state) return prepared;
    (void)state->Session.VisitPreparedFrame([&](EditorFeatureDetail::EditorWorkspacePreparedFrame frame)
    {
        prepared.Commands = BindEditorProcessingCommands(frame.Geometry);
        if (frame.Results.PointConstructionResultSinks) prepared.ResultSinks = *frame.Results.PointConstructionResultSinks;
        if (frame.Results.PointConstructionResults) prepared.Results = *frame.Results.PointConstructionResults;
    });
    return prepared;
}

} // namespace Extrinsic::Runtime
