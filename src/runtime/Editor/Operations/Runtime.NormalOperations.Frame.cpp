module;
#include <functional>
#include <memory>

module Extrinsic.Runtime.NormalOperations;

import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
EditorNormalPreparedFrame PrepareEditorNormalFrame(const EditorWorkspaceAttachment& attachment)
{
    EditorNormalPreparedFrame prepared{};
    const auto state = EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
    if (!state) return prepared;
    (void)state->Session.VisitPreparedFrame([&](EditorFeatureDetail::EditorWorkspacePreparedFrame frame)
    {
        prepared.Commands = BindEditorProcessingCommands(frame.Geometry);
        if (frame.Results.NormalResultSinks) prepared.ResultSinks = *frame.Results.NormalResultSinks;
        if (frame.Results.NormalResults) prepared.Results = *frame.Results.NormalResults;
    });
    return prepared;
}

} // namespace Extrinsic::Runtime
