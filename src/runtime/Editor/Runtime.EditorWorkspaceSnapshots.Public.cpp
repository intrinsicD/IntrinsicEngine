module;
#include <span>
#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <entt/entity/fwd.hpp>

#include <memory>
#include <optional>
#include <utility>

module Extrinsic.Runtime.EditorWorkspaceSnapshots;

import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;
import Extrinsic.Asset.ImportRouter;
import Extrinsic.Asset.Registry;
import Extrinsic.Asset.Service;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.Core.Geometry2D;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Graphics.Component.RenderGeometry;
import Extrinsic.Graphics.RenderRecipeConfig;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.Device;
import Extrinsic.Runtime.AssetWorkflowModule;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.PointCloudConsolidationModule;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.SceneDocumentModule;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ServiceRegistry;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.Properties;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;

#include "Editor/internal/Runtime.EditorFeatures.Internal.hpp"

namespace Extrinsic::Runtime {
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

EditorInspectorModel BuildEditorInspectorModel(
    const EditorWorkspaceSnapshotQueries &queries,
    EditorWorkspaceSnapshotStats *modelBuildStats,
    std::optional<std::uint32_t> entity) {
  EditorWorkspaceSnapshotContext context = ContextOrEmpty(queries);
  if (modelBuildStats != nullptr)
    context.Visualization.ModelBuildStats = modelBuildStats;
  return BuildEditorInspectorModel(context, entity);
}

EditorDomainWindowModel BuildEditorDomainWindowModel(
    const EditorWorkspaceSnapshotQueries &queries,
    EditorDomainWindowKind kind,
    EditorWorkspaceSnapshotStats *modelBuildStats,
    std::optional<std::uint32_t> entity) {
  EditorWorkspaceSnapshotContext context = ContextOrEmpty(queries);
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
        const EditorFeatureDetail::EditorFeatureBindings &bindings =
            frame.Context;
        const EditorSceneEditingContext scene =
            EditorFeatureDetail::MakeEditorSceneEditingContext(bindings);
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
                    .Geometry = frame.Geometry,
                    .Visualization = visualization,
                    .RenderRecipe = renderRecipe,
                    .SelectedModelCache = bindings.SelectedModelCache,
                }),
        };
      });
  return prepared;
}
} // namespace Extrinsic::Runtime
