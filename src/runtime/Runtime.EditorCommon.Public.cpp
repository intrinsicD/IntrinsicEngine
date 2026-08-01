module;

module Extrinsic.Runtime.EditorCommon;

namespace Extrinsic::Runtime {
const char *
DebugNameForEditorDiagnosticCode(EditorDiagnosticCode code) noexcept {
  switch (code) {
  case EditorDiagnosticCode::MissingScene: return "MissingScene";
  case EditorDiagnosticCode::MissingSelectionController: return "MissingSelectionController";
  case EditorDiagnosticCode::MissingImGuiAdapter: return "MissingImGuiAdapter";
  case EditorDiagnosticCode::AssetImportUnavailable: return "AssetImportUnavailable";
  case EditorDiagnosticCode::AssetImportFailed: return "AssetImportFailed";
  case EditorDiagnosticCode::SceneFileUnavailable: return "SceneFileUnavailable";
  case EditorDiagnosticCode::SceneFileFailed: return "SceneFileFailed";
  case EditorDiagnosticCode::NoSelectedEntity: return "NoSelectedEntity";
  case EditorDiagnosticCode::UnsupportedGeometryDomain: return "UnsupportedGeometryDomain";
  case EditorDiagnosticCode::CameraRenderCommandsUnavailable: return "CameraRenderCommandsUnavailable";
  case EditorDiagnosticCode::VisualizationCommandsUnavailable: return "VisualizationCommandsUnavailable";
  case EditorDiagnosticCode::RenderRecipeCommandsUnavailable: return "RenderRecipeCommandsUnavailable";
  case EditorDiagnosticCode::InvalidVisualizationProperty: return "InvalidVisualizationProperty";
  case EditorDiagnosticCode::InvalidVertexChannelBinding: return "InvalidVertexChannelBinding";
  case EditorDiagnosticCode::GeometryProcessingFailed: return "GeometryProcessingFailed";
  case EditorDiagnosticCode::RenderGraphStatsUnavailable: return "RenderGraphStatsUnavailable";
  case EditorDiagnosticCode::EditorCommandHistoryUnavailable: return "EditorCommandHistoryUnavailable";
  case EditorDiagnosticCode::CorruptHierarchy: return "CorruptHierarchy";
  }
  return "Unknown";
}

const char *
DebugNameForEditorCommandStatus(EditorCommandStatus status) noexcept {
  switch (status) {
  case EditorCommandStatus::Applied: return "Applied";
  case EditorCommandStatus::Pending: return "Pending";
  case EditorCommandStatus::NoChange: return "NoChange";
  case EditorCommandStatus::MissingScene: return "MissingScene";
  case EditorCommandStatus::MissingSelectionController: return "MissingSelectionController";
  case EditorCommandStatus::MissingCameraControllerRegistry: return "MissingCameraControllerRegistry";
  case EditorCommandStatus::MissingAssetImportCommands: return "MissingAssetImportCommands";
  case EditorCommandStatus::MissingSceneFileCommands: return "MissingSceneFileCommands";
  case EditorCommandStatus::MissingPrimitiveViewCommands: return "MissingPrimitiveViewCommands";
  case EditorCommandStatus::MissingVisualizationCommands: return "MissingVisualizationCommands";
  case EditorCommandStatus::AssetImportFailed: return "AssetImportFailed";
  case EditorCommandStatus::SceneNewFailed: return "SceneNewFailed";
  case EditorCommandStatus::SceneSaveFailed: return "SceneSaveFailed";
  case EditorCommandStatus::SceneLoadFailed: return "SceneLoadFailed";
  case EditorCommandStatus::SceneCloseFailed: return "SceneCloseFailed";
  case EditorCommandStatus::StaleEntity: return "StaleEntity";
  case EditorCommandStatus::MissingTransform: return "MissingTransform";
  case EditorCommandStatus::UnsupportedGeometryDomain: return "UnsupportedGeometryDomain";
  case EditorCommandStatus::InvalidVisualizationProperty: return "InvalidVisualizationProperty";
  case EditorCommandStatus::InvalidVertexChannelBinding: return "InvalidVertexChannelBinding";
  case EditorCommandStatus::InvalidProcessingParameters: return "InvalidProcessingParameters";
  case EditorCommandStatus::GeometryProcessingFailed: return "GeometryProcessingFailed";
  }
  return "Unknown";
}

const char *DebugNameForEditorGeometryDomain(
    ECS::Components::GeometrySources::Domain domain) noexcept {
  using Domain = ECS::Components::GeometrySources::Domain;
  switch (domain) {
  case Domain::None: return "None";
  case Domain::Mesh: return "Mesh";
  case Domain::Graph: return "Graph";
  case Domain::PointCloud: return "PointCloud";
  case Domain::Unknown: return "Unknown";
  }
  return "Unknown";
}

const char *
DebugNameForEditorDomainWindowKind(const EditorDomainWindowKind kind) noexcept {
  switch (kind) {
  case EditorDomainWindowKind::Mesh: return "Mesh";
  case EditorDomainWindowKind::Graph: return "Graph";
  case EditorDomainWindowKind::PointCloud: return "PointCloud";
  }
  return "Unknown";
}
} // namespace Extrinsic::Runtime
