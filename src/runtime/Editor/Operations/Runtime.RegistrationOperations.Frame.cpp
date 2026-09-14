module;
#include <functional>
#include <memory>

module Extrinsic.Runtime.RegistrationOperations;

import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace Extrinsic::Runtime {
EditorRegistrationPreparedFrame PrepareEditorRegistrationFrame(const EditorWorkspaceAttachment& attachment)
{
    EditorRegistrationPreparedFrame prepared{};
    const auto state = EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
    if (!state) return prepared;
    (void)state->Session.VisitPreparedFrame([&](EditorFeatureDetail::EditorWorkspacePreparedFrame frame)
    {
        prepared.Commands = BindEditorProcessingCommands(frame.Geometry);
        if (frame.Results.RegistrationResultSinks) prepared.ResultSinks = *frame.Results.RegistrationResultSinks;
        if (frame.Results.RegistrationResults) prepared.Results = *frame.Results.RegistrationResults;
    });
    return prepared;
}

} // namespace Extrinsic::Runtime
