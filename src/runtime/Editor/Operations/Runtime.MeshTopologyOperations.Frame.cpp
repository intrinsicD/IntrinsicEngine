module;
#include <functional>
#include <memory>

module Extrinsic.Runtime.MeshTopologyOperations;

import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
EditorMeshTopologyPreparedFrame PrepareEditorMeshTopologyFrame(const EditorWorkspaceAttachment& attachment)
{
    EditorMeshTopologyPreparedFrame prepared{};
    const auto state = EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
    if (!state) return prepared;
    (void)state->Session.VisitPreparedFrame([&](EditorFeatureDetail::EditorWorkspacePreparedFrame frame)
    {
        prepared.Commands = BindEditorProcessingCommands(frame.Geometry);
        if (frame.Results.MeshTopologyResultSinks) prepared.ResultSinks = *frame.Results.MeshTopologyResultSinks;
        if (frame.Results.MeshTopologyResults) prepared.Results = *frame.Results.MeshTopologyResults;
    });
    return prepared;
}

} // namespace Extrinsic::Runtime
