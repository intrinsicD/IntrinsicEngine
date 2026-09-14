module;
#include <functional>
#include <memory>

module Extrinsic.Runtime.PointAnalysisOperations;

import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
EditorPointAnalysisPreparedFrame PrepareEditorPointAnalysisFrame(const EditorWorkspaceAttachment& attachment)
{
    EditorPointAnalysisPreparedFrame prepared{};
    const auto state = EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
    if (!state) return prepared;
    (void)state->Session.VisitPreparedFrame([&](EditorFeatureDetail::EditorWorkspacePreparedFrame frame)
    {
        prepared.Commands = BindEditorProcessingCommands(frame.Geometry);
        if (frame.Results.PointAnalysisResultSinks) prepared.ResultSinks = *frame.Results.PointAnalysisResultSinks;
        if (frame.Results.PointAnalysisResults) prepared.Results = *frame.Results.PointAnalysisResults;
    });
    return prepared;
}

} // namespace Extrinsic::Runtime
