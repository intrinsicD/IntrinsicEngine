// Private command diagnostics and import preflight for editor actions.
// C++ declarations share the compiled owner Runtime.EditorFeatureContextAdapters.cpp.
#pragma once

// Requires SceneEditingOperations, Asset.ImportRouter and Core.Error.
// Provide <array>, <string> and <string_view> in the global module fragment.

extern "C++"
{
namespace Extrinsic::Runtime::EditorFeatureDetail
{
    using namespace Extrinsic::Runtime;

    [[nodiscard]] std::string BuildImportSuccessMessage(
        const EditorFileImportCommand& command,
        const EditorFileImportResult& result);

    [[nodiscard]] std::string BuildImportPendingMessage(
        const EditorFileImportCommand& command,
        const Assets::AssetPayloadKind payloadKind);

    [[nodiscard]] std::string BuildImportFailureMessage(
        const Core::ErrorCode error);

    [[nodiscard]] std::string BuildSceneFileSuccessMessage(
        const EditorSceneFileCommand& command,
        const EditorSceneFileResult& result);

    [[nodiscard]] std::string BuildSceneFileFailureMessage(
        const EditorSceneFileOperation operation,
        const Core::ErrorCode error);

    [[nodiscard]] std::string BuildSceneFilePendingMessage(
        const EditorSceneFileCommand& command,
        const EditorSceneFileOperation operation);

    struct FileImportPrerequisiteEvaluation
    {
        bool CanChoosePayloadHint{false};
        bool CanImport{false};
        Assets::AssetPayloadKind ResolvedPayloadKind{
            Assets::AssetPayloadKind::Unknown};
        std::array<EditorFileImportPayloadOption, 6> PayloadOptions{};
        std::string PayloadHintDisabledReason{};
        std::string ImportDisabledReason{};
        Core::ErrorCode Error{Core::ErrorCode::Success};
    };

    [[nodiscard]] FileImportPrerequisiteEvaluation
    EvaluateFileImportPrerequisites(
        const bool commandSurfaceAvailable,
        const std::string_view path,
        const Assets::AssetPayloadKind selectedPayloadKind);

} // namespace Extrinsic::Runtime::EditorFeatureDetail

} // extern "C++"
