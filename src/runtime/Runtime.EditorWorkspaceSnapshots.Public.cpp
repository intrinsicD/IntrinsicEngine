module;

#include <memory>
#include <optional>
#include <utility>

module Extrinsic.Runtime.EditorWorkspaceSnapshots;

import Extrinsic.Runtime.Private.EditorFeatures;
import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

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

const EditorWorkspaceSnapshotContext *
EditorWorkspaceSnapshotQueriesAccess::Resolve(
    const EditorWorkspaceSnapshotQueries &queries) noexcept {
  return queries.IsBound() && queries.m_State != nullptr
             ? &queries.m_State->Context
             : nullptr;
}

namespace {
const EditorWorkspaceSnapshotContext &ContextOrEmpty(
    const EditorWorkspaceSnapshotQueries &queries) noexcept {
  static const EditorWorkspaceSnapshotContext empty{};
  const EditorWorkspaceSnapshotContext *context =
      EditorWorkspaceSnapshotQueriesAccess::Resolve(queries);
  return context != nullptr ? *context : empty;
}
} // namespace

EditorWorkspaceSnapshotQueries
BindEditorWorkspaceSnapshotQueries(EditorWorkspaceSnapshotContext context) {
  return EditorWorkspaceSnapshotQueries{
      std::make_shared<EditorWorkspaceSnapshotQueries::State>(
          std::move(context))};
}

EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshot(
    const EditorWorkspaceSnapshotQueries &queries) {
  return BuildEditorWorkspaceSnapshot(ContextOrEmpty(queries));
}

EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshot(
    const EditorWorkspaceSnapshotQueries &queries,
    const EditorWorkspaceSnapshotRequest &request) {
  return BuildEditorWorkspaceSnapshot(ContextOrEmpty(queries), request);
}

EditorDomainWindowModel BuildEditorDomainWindowModel(
    const EditorWorkspaceSnapshotQueries &queries,
    EditorDomainWindowKind kind,
    EditorWorkspaceSnapshotStats *modelBuildStats) {
  EditorWorkspaceSnapshotContext context = ContextOrEmpty(queries);
  if (modelBuildStats != nullptr)
    context.Visualization.ModelBuildStats = modelBuildStats;
  return BuildEditorDomainWindowModel(context, kind);
}

std::optional<EditorWorkspaceSnapshotPreparedFrame>
PrepareEditorWorkspaceSnapshotFrame(
    const EditorWorkspaceAttachment &attachment,
    const EditorWorkspaceSnapshotRequest &request,
    std::string pendingAssetImportPath,
    EditorAssetPayloadKind pendingAssetImportPayloadKind,
    std::string pendingSceneFilePath) {
  const auto state =
      EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
  if (state == nullptr ||
      !state->Session.PrepareFrame(
          request, std::move(pendingAssetImportPath),
          pendingAssetImportPayloadKind, std::move(pendingSceneFilePath))) {
    return std::nullopt;
  }

  std::optional<EditorWorkspaceSnapshotPreparedFrame> prepared{};
  (void)state->Session.VisitPreparedFrame(
      [&prepared](EditorFeatureDetail::EditorWorkspacePreparedFrame frame) {
        const EditorFeatureDetail::EditorFeatureBindings &bindings =
            frame.Context;
        const EditorSceneEditingContext scene =
            EditorFeatureDetail::MakeEditorSceneEditingContext(bindings);
        const EditorGeometryProcessingContext geometry =
            EditorFeatureDetail::MakeEditorGeometryProcessingContext(bindings);
        const EditorVisualizationEditingContext visualization =
            EditorFeatureDetail::MakeEditorVisualizationEditingContext(
                bindings);
        const EditorRenderRecipeEditingContext renderRecipe =
            EditorFeatureDetail::MakeEditorRenderRecipeEditingContext(bindings);
        prepared = EditorWorkspaceSnapshotPreparedFrame{
            .Frame = frame.Frame,
            .SnapshotQueries = BindEditorWorkspaceSnapshotQueries(
                EditorWorkspaceSnapshotContext{
                    .Scene = scene,
                    .Geometry = geometry,
                    .Visualization = visualization,
                    .RenderRecipe = renderRecipe,
                    .SelectedModelCache = bindings.SelectedModelCache,
                }),
        };
      });
  return prepared;
}
} // namespace Extrinsic::Runtime
