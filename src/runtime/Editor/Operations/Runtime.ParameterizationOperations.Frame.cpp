module;
#include <functional>
#include <memory>

module Extrinsic.Runtime.ParameterizationOperations;

import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
EditorParameterizationPreparedFrame PrepareEditorParameterizationFrame(const EditorWorkspaceAttachment& attachment)
{
    EditorParameterizationPreparedFrame prepared{};
    const auto state = EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
    if (!state) return prepared;
    (void)state->Session.VisitPreparedFrame([&](EditorFeatureDetail::EditorWorkspacePreparedFrame frame)
    {
        prepared.Commands = BindEditorProcessingCommands(frame.Geometry);
        if (frame.Results.ParameterizationUvViewCommands)
            prepared.UvViewCommands = *frame.Results.ParameterizationUvViewCommands;
        if (frame.Results.ParameterizationResultSinks)
            prepared.ResultSinks = *frame.Results.ParameterizationResultSinks;
        if (frame.Results.ParameterizationResults)
            prepared.Results = *frame.Results.ParameterizationResults;
    });
    return prepared;
}

} // namespace Extrinsic::Runtime
