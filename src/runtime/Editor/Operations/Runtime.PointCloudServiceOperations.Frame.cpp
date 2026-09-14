module;
#include <functional>
#include <memory>

module Extrinsic.Runtime.PointCloudServiceOperations;

import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
EditorPointCloudServicePreparedFrame PrepareEditorPointCloudServiceFrame(
    const EditorWorkspaceAttachment& attachment)
{
    EditorPointCloudServicePreparedFrame prepared{};
    const auto state = EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
    if (!state) return prepared;
    (void)state->Session.VisitPreparedFrame([&](EditorFeatureDetail::EditorWorkspacePreparedFrame frame)
    {
        prepared.Commands = BindEditorProcessingCommands(frame.Geometry);
        if (frame.Results.PointCloudServices)
        {
            prepared.Clustering = frame.Results.PointCloudServices->Clustering;
            prepared.PointCloudConsolidation =
                frame.Results.PointCloudServices->PointCloudConsolidation;
        }
        if (frame.Results.PointCloudServiceResultSinks) prepared.ResultSinks = *frame.Results.PointCloudServiceResultSinks;
        if (frame.Results.PointCloudServiceResults) prepared.Results = *frame.Results.PointCloudServiceResults;
        prepared.ClusteringAvailable =
            IsEditorClusteringAvailable(prepared.Commands, prepared.Clustering);
        prepared.PointCloudConsolidationAvailable = IsEditorPointCloudConsolidationAvailable(
            prepared.Commands, prepared.PointCloudConsolidation);
    });
    return prepared;
}

} // namespace Extrinsic::Runtime
