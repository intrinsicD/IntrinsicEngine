// Private command diagnostics, import preflight and render-hint comparisons.
// C++ declarations share the compiled owner Runtime.EditorFeatureContextAdapters.cpp.
#pragma once

// Requires SceneEditingOperations, Asset.ImportRouter, Core.Error and
// Graphics.Component.RenderGeometry. Provide <array>, <optional>, <string> and
// <string_view> in the global module fragment.

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

    [[nodiscard]] bool SameRenderHintComponent(
        const std::optional<Graphics::Components::RenderSurface>& lhs,
        const std::optional<Graphics::Components::RenderSurface>& rhs);
    [[nodiscard]] bool SameRenderHintComponent(
        const std::optional<Graphics::Components::RenderEdges>& lhs,
        const std::optional<Graphics::Components::RenderEdges>& rhs);
    [[nodiscard]] bool SameRenderHintComponent(
        const std::optional<Graphics::Components::RenderPoints>& lhs,
        const std::optional<Graphics::Components::RenderPoints>& rhs);

} // namespace Extrinsic::Runtime::EditorFeatureDetail

} // extern "C++"
