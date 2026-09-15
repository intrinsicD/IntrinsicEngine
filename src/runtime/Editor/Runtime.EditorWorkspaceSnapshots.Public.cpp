module;
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

module Extrinsic.Runtime.EditorWorkspaceSnapshots;

import Extrinsic.Asset.Registry;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;

namespace Extrinsic::Runtime {
// Compare standard containers beside their headers; the interface imports only
// the value-type declarations and need not publish standard operator overloads.
bool operator==(const EditorSelectedModelCacheKey&,
                const EditorSelectedModelCacheKey&) = default;

extern "C++" {
void EditorSelectedModelCache::Clear() noexcept
{
    for (EditorSelectedAnalysisCacheEntry& entry : SelectedAnalysis)
        entry.Valid = false;
    for (EditorVisualizationModelCacheEntry& entry : Visualization)
        entry.Valid = false;
    ++Counters.Invalidations;
}

EditorSelectedModelCacheStats EditorSelectedModelCache::Stats() const noexcept
{
    EditorSelectedModelCacheStats stats = Counters;
    for (const EditorSelectedAnalysisCacheEntry& entry : SelectedAnalysis)
    {
        if (entry.Valid)
            ++stats.Entries;
    }
    for (const EditorVisualizationModelCacheEntry& entry : Visualization)
    {
        if (entry.Valid)
            ++stats.Entries;
    }
    return stats;
}

} // extern "C++"

struct EditorWorkspaceSnapshotQueriesAccess final {
  // Expired handles expose default models without touching session borrows.
  [[nodiscard]] static const EditorWorkspaceSnapshotContext &
  ContextOrEmpty(const EditorWorkspaceSnapshotQueries &queries) noexcept {
    static const EditorWorkspaceSnapshotContext empty{};
    return queries.IsBound() && queries.m_Context ? *queries.m_Context : empty;
  }
};

EditorWorkspaceSnapshotQueries::EditorWorkspaceSnapshotQueries(
    std::shared_ptr<const EditorWorkspaceSnapshotContext> context)
    : m_Context(std::move(context)) {}

bool EditorWorkspaceSnapshotQueries::IsBound() const noexcept {
  return m_Context != nullptr && (!m_Context->Scene.AttachmentActive ||
                                  m_Context->Scene.AttachmentActive());
}

EditorWorkspaceSnapshotQueries
BindEditorWorkspaceSnapshotQueries(EditorWorkspaceSnapshotContext context) {
  return EditorWorkspaceSnapshotQueries{
      std::make_shared<const EditorWorkspaceSnapshotContext>(
          std::move(context))};
}

EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshot(
    const EditorWorkspaceSnapshotQueries &queries) {
  return BuildEditorWorkspaceSnapshot(
      EditorWorkspaceSnapshotQueriesAccess::ContextOrEmpty(queries));
}

EditorWorkspaceSnapshot BuildEditorWorkspaceSnapshot(
    const EditorWorkspaceSnapshotQueries &queries,
    const EditorWorkspaceSnapshotRequest &request) {
  return BuildEditorWorkspaceSnapshot(
      EditorWorkspaceSnapshotQueriesAccess::ContextOrEmpty(queries), request);
}

EditorInspectorModel BuildEditorInspectorModel(
    const EditorWorkspaceSnapshotQueries &queries,
    EditorWorkspaceSnapshotStats *modelBuildStats,
    std::optional<std::uint32_t> entity) {
  // Copied per query: the stats override is caller-scoped and must not mutate
  // the shared bound context.
  EditorWorkspaceSnapshotContext context =
      EditorWorkspaceSnapshotQueriesAccess::ContextOrEmpty(queries);
  if (modelBuildStats != nullptr)
    context.Visualization.ModelBuildStats = modelBuildStats;
  return BuildEditorInspectorModel(context, entity);
}

EditorDomainWindowModel BuildEditorDomainWindowModel(
    const EditorWorkspaceSnapshotQueries &queries,
    EditorDomainWindowKind kind,
    EditorWorkspaceSnapshotStats *modelBuildStats,
    std::optional<std::uint32_t> entity) {
  EditorWorkspaceSnapshotContext context =
      EditorWorkspaceSnapshotQueriesAccess::ContextOrEmpty(queries);
  if (modelBuildStats != nullptr)
    context.Visualization.ModelBuildStats = modelBuildStats;
  return BuildEditorDomainWindowModel(context, kind, entity);
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
        prepared = EditorWorkspaceSnapshotPreparedFrame{
            .Frame = frame.Frame,
            .SnapshotQueries = BindEditorWorkspaceSnapshotQueries(
                EditorFeatureDetail::MakeEditorWorkspaceSnapshotContext(
                    frame.Context, frame.Geometry)),
        };
      });
  return prepared;
}
} // namespace Extrinsic::Runtime
