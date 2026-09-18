// Terminal failure status and diagnostics shared by queued editor methods.
// Include after EditorCommon, Core.Error and JobService imports; the including
// global module fragment supplies string and string_view.
#pragma once

extern "C++"
{
namespace Extrinsic::Runtime::GeometryProcessingDetail::MeshSupport
{
    struct UnpublishedEditorJobFailure
    {
        EditorCommandStatus Status{EditorCommandStatus::GeometryProcessingFailed};
        Core::ErrorCode Error{Core::ErrorCode::Unknown};
        std::string Message{};
    };

    // Worker detail is copied only for Current; stale/cancelled reasons stand alone.
    [[nodiscard]] UnpublishedEditorJobFailure BuildUnpublishedEditorJobFailure(
        JobApplyValidation validation,
        std::string_view label,
        std::string_view detail = {});

}
}
