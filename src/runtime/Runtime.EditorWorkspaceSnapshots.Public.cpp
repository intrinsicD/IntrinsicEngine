module;

#include <functional>
#include <memory>
#include <optional>
#include <utility>

module Extrinsic.Runtime.EditorWorkspaceSnapshots;

import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.Private.EditorFeatures;

namespace {
template <typename T>
[[nodiscard]] std::optional<T> CopyOptional(const T *value) {
  return value != nullptr ? std::optional<T>{*value} : std::nullopt;
}
} // namespace

namespace Extrinsic::Runtime {
struct EditorWorkspaceSnapshotQueries::State {
  explicit State(EditorWorkspaceSnapshotContext context)
      : Context(std::move(context)) {}

  EditorWorkspaceSnapshotContext Context{};
};

EditorWorkspaceSnapshotQueries::EditorWorkspaceSnapshotQueries(
    std::shared_ptr<const State> state)
    : m_State(std::move(state)) {}

bool EditorWorkspaceSnapshotQueries::IsBound() const noexcept {
  return m_State != nullptr && (!m_State->Context.Scene.AttachmentActive ||
                                m_State->Context.Scene.AttachmentActive());
}

EditorWorkspaceSnapshotQueries::
operator const EditorWorkspaceSnapshotContext &() const noexcept {
  static const EditorWorkspaceSnapshotContext empty{};
  return m_State != nullptr ? m_State->Context : empty;
}

EditorWorkspaceSnapshotQueries
BindEditorWorkspaceSnapshotQueries(EditorWorkspaceSnapshotContext context) {
  return EditorWorkspaceSnapshotQueries{
      std::make_shared<EditorWorkspaceSnapshotQueries::State>(
          std::move(context))};
}

struct EditorWorkspaceSession::Impl {
  EditorFeatureDetail::EditorWorkspaceSession Session{};
};

EditorWorkspaceSnapshot
BuildEditorWorkspaceSnapshot(const EditorWorkspaceSnapshotContext &context) {
  return EditorFeatureDetail::BuildEditorWorkspaceSnapshotImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
}

EditorWorkspaceSnapshot
BuildEditorWorkspaceSnapshot(const EditorWorkspaceSnapshotContext &context,
                             const EditorWorkspaceSnapshotRequest &request) {
  return EditorFeatureDetail::BuildEditorWorkspaceSnapshotImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), request);
}

EditorDomainWindowModel
BuildEditorDomainWindowModel(const EditorWorkspaceSnapshotContext &context,
                             EditorDomainWindowKind kind) {
  return EditorFeatureDetail::BuildEditorDomainWindowModelImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), kind);
}

EditorWorkspaceSession::EditorWorkspaceSession()
    : m_Impl(std::make_unique<Impl>()) {}

EditorWorkspaceSession::~EditorWorkspaceSession() = default;

void EditorWorkspaceSession::Attach(WorldRegistry &worlds,
                                    ServiceRegistry &services) {
  m_Impl->Session.Attach(worlds, services);
}

void EditorWorkspaceSession::Detach() { m_Impl->Session.Detach(); }

bool EditorWorkspaceSession::PrepareFrame(
    const EditorWorkspaceSnapshotRequest &request,
    std::string pendingAssetImportPath,
    EditorAssetPayloadKind pendingAssetImportPayloadKind,
    std::string pendingSceneFilePath) {
  return m_Impl->Session.PrepareFrame(
      request, std::move(pendingAssetImportPath), pendingAssetImportPayloadKind,
      std::move(pendingSceneFilePath));
}

bool EditorWorkspaceSession::VisitPreparedFrame(
    const EditorWorkspacePreparedFrameVisitor &visitor) {
  if (!visitor)
    return false;

  return m_Impl->Session.VisitPreparedFrame(
      [&visitor](EditorFeatureDetail::EditorWorkspacePreparedFrame frame) {
        const EditorFeatureDetail::EditorFeatureBindings &bindings =
            frame.Context;
        const EditorGeometryProcessingContext geometry =
            EditorFeatureDetail::MakeEditorGeometryProcessingContext(bindings);
        const EditorSceneEditingContext scene =
            EditorFeatureDetail::MakeEditorSceneEditingContext(bindings);
        const EditorVisualizationEditingContext visualization =
            EditorFeatureDetail::MakeEditorVisualizationEditingContext(
                bindings);
        const EditorRenderRecipeEditingContext renderRecipe =
            EditorFeatureDetail::MakeEditorRenderRecipeEditingContext(bindings);

        EditorDocumentCommandSurface documentCommands{};
        if (bindings.CommandHistory != nullptr) {
          EditorCommandHistory *history = bindings.CommandHistory;
          const std::function<bool()> attachmentActive =
              bindings.AttachmentActive;
          documentCommands.Undo = [history, attachmentActive]() {
            if (attachmentActive && !attachmentActive())
              return EditorCommandHistoryResult{};
            return history->Undo();
          };
          documentCommands.Redo = [history, attachmentActive]() {
            if (attachmentActive && !attachmentActive())
              return EditorCommandHistoryResult{};
            return history->Redo();
          };
        }

        visitor(EditorWorkspacePreparedFrame{
            .Frame = frame.Frame,
            .SceneCommands = BindEditorSceneEditingCommands(scene),
            .GeometryCommands = BindEditorGeometryProcessingCommands(geometry),
            .VisualizationCommands =
                BindEditorVisualizationEditingCommands(visualization),
            .RenderRecipeCommands =
                BindEditorRenderRecipeEditingCommands(renderRecipe),
            .SnapshotQueries = BindEditorWorkspaceSnapshotQueries(
                EditorWorkspaceSnapshotContext{
                    .Scene = scene,
                    .Geometry = geometry,
                    .Visualization = visualization,
                    .RenderRecipe = renderRecipe,
                    .SelectedModelCache = bindings.SelectedModelCache,
                }),
            .AssetImportQueueCommands = scene.AssetImportQueueCommands,
            .DocumentCommands = std::move(documentCommands),
            .MethodResultSinks = bindings.MethodResultSinks,
            .GeometryResults =
                EditorGeometryProcessingResultsSnapshot{
                    .LastKMeansResult = CopyOptional(bindings.LastKMeansResult),
                    .LastMeshDenoiseResult =
                        CopyOptional(bindings.LastMeshDenoiseResult),
                    .LastMeshCurvatureResult =
                        CopyOptional(bindings.LastMeshCurvatureResult),
                    .LastMeshRemeshResult =
                        CopyOptional(bindings.LastMeshRemeshResult),
                    .LastMeshSubdivideResult =
                        CopyOptional(bindings.LastMeshSubdivideResult),
                    .LastMeshSimplifyResult =
                        CopyOptional(bindings.LastMeshSimplifyResult),
                    .LastMeshVertexNormalsResult =
                        CopyOptional(bindings.LastMeshVertexNormalsResult),
                    .LastGraphVertexNormalsResult =
                        CopyOptional(bindings.LastGraphVertexNormalsResult),
                    .LastPointCloudVertexNormalsResult = CopyOptional(
                        bindings.LastPointCloudVertexNormalsResult),
                    .LastPointCloudOutlierRemovalResult = CopyOptional(
                        bindings.LastPointCloudOutlierRemovalResult),
                    .LastUvRegenerationResult =
                        CopyOptional(bindings.LastUvRegenerationResult),
                    .LastParameterizationResult =
                        CopyOptional(bindings.LastParameterizationResult),
                    .LastProgressivePoissonResult =
                        CopyOptional(bindings.LastProgressivePoissonResult),
                    .LastRegistrationResult =
                        CopyOptional(bindings.LastRegistrationResult),
                },
            .RenderRecipeDraft =
                EditorRenderRecipeDraftSnapshot{
                    .DraftDocument =
                        renderRecipe.RenderRecipeEditorState != nullptr
                            ? renderRecipe.RenderRecipeEditorState
                                  ->DraftDocument
                            : std::string{},
                    .DraftState =
                        renderRecipe.RenderRecipeEditorState != nullptr
                            ? renderRecipe.RenderRecipeEditorState->DraftState
                            : EditorRenderRecipeDraftState::InactiveDraft,
                    .DraftRevision =
                        renderRecipe.RenderRecipeEditorState != nullptr
                            ? renderRecipe.RenderRecipeEditorState
                                  ->DraftRevision
                            : 0u,
                },
            .SceneAvailable = scene.Scene != nullptr,
            .GeometryConfigCommandsAvailable =
                AreEditorGeometryConfigCommandsAvailable(geometry),
            .ClusteringAvailable = IsEditorClusteringAvailable(geometry),
            .RenderRecipeCommandsAvailable =
                renderRecipe.RenderRecipeContext != nullptr &&
                renderRecipe.RenderRecipeEditorState != nullptr &&
                renderRecipe.RenderRecipeCommandsAvailable,
            .RenderArtifactCommandsAvailable =
                renderRecipe.RenderArtifacts != nullptr &&
                renderRecipe.RenderRecipeCommandsAvailable,
            .LastAssetImportResult = frame.LastAssetImportResult,
            .LastSceneFileResult = frame.LastSceneFileResult,
            .LastUvRegenerationResult = frame.LastUvRegenerationResult,
        });
      });
}

const EditorWorkspaceSnapshot &
EditorWorkspaceSession::LastFrame() const noexcept {
  return m_Impl->Session.LastFrame();
}

bool EditorWorkspaceSession::IsAttached() const noexcept {
  return m_Impl->Session.IsAttached();
}
} // namespace Extrinsic::Runtime
