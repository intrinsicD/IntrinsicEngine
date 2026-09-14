module;
#include <functional>
#include <memory>

module Extrinsic.Runtime.MeshFieldOperations;

import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
EditorMeshFieldPreparedFrame PrepareEditorMeshFieldFrame(const EditorWorkspaceAttachment& attachment)
{
    EditorMeshFieldPreparedFrame prepared{};
    const auto state = EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
    if (!state) return prepared;
    (void)state->Session.VisitPreparedFrame([&](EditorFeatureDetail::EditorWorkspacePreparedFrame frame)
    {
        prepared.Commands = BindEditorProcessingCommands(frame.Geometry);
        if (frame.Results.MeshFieldResultSinks) prepared.ResultSinks = *frame.Results.MeshFieldResultSinks;
        if (frame.Results.MeshFieldResults) prepared.Results = *frame.Results.MeshFieldResults;
    });
    return prepared;
}

} // namespace Extrinsic::Runtime
