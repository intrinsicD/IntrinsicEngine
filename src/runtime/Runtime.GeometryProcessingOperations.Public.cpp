module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>

module Extrinsic.Runtime.GeometryProcessingOperations;

import Extrinsic.Runtime.Private.EditorFeatures;
import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.SelectionController;

namespace {
template <typename T>
[[nodiscard]] std::optional<T> CopyOptional(const T *value) {
  return value != nullptr ? std::optional<T>{*value} : std::nullopt;
}
} // namespace

namespace Extrinsic::Runtime {
namespace {
EditorGeometryProcessingContext MakeExpiredGeometryProcessingContext(
    EditorGeometryProcessingContext context) {
  context.AttachmentActive = [] { return false; };
  return EditorFeatureDetail::MakeEditorGeometryProcessingContext(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
}
} // namespace

struct EditorGeometryProcessingCommands::State {
  explicit State(EditorGeometryProcessingContext context)
      : Context(std::move(context)),
        ExpiredContext(MakeExpiredGeometryProcessingContext(Context)) {}

  EditorGeometryProcessingContext Context{};
  EditorGeometryProcessingContext ExpiredContext{};
};

EditorGeometryProcessingCommands::EditorGeometryProcessingCommands(
    std::shared_ptr<const State> state)
    : m_State(std::move(state)) {}

bool EditorGeometryProcessingCommands::IsBound() const noexcept {
  return m_State != nullptr && (!m_State->Context.AttachmentActive ||
                                m_State->Context.AttachmentActive());
}

const EditorGeometryProcessingContext *
EditorGeometryProcessingCommandsAccess::Resolve(
    const EditorGeometryProcessingCommands &commands) noexcept {
  if (commands.m_State == nullptr)
    return nullptr;
  return commands.IsBound() ? &commands.m_State->Context
                            : &commands.m_State->ExpiredContext;
}

namespace {
const EditorGeometryProcessingContext &ContextOrEmpty(
    const EditorGeometryProcessingCommands &commands) noexcept {
  static const EditorGeometryProcessingContext empty{};
  const EditorGeometryProcessingContext *context =
      EditorGeometryProcessingCommandsAccess::Resolve(commands);
  return context != nullptr ? *context : empty;
}
} // namespace

EditorGeometryProcessingCommands
BindEditorGeometryProcessingCommands(EditorGeometryProcessingContext context) {
  return EditorGeometryProcessingCommands{
      std::make_shared<EditorGeometryProcessingCommands::State>(
          std::move(context))};
}

EditorGeometryProcessingPreparedFrame PrepareEditorGeometryProcessingFrame(
    const EditorWorkspaceAttachment &attachment) {
  EditorGeometryProcessingPreparedFrame prepared{};
  const auto state =
      EditorFeatureDetail::ResolveEditorWorkspaceAttachmentState(attachment);
  if (state == nullptr)
    return prepared;

  (void)state->Session.VisitPreparedFrame(
      [&prepared](EditorFeatureDetail::EditorWorkspacePreparedFrame frame) {
        const EditorFeatureDetail::EditorFeatureBindings &bindings =
            frame.Context;
        const EditorGeometryProcessingContext context =
            EditorFeatureDetail::MakeEditorGeometryProcessingContext(bindings);
        prepared.Commands = BindEditorGeometryProcessingCommands(context);
        prepared.ResultSinks = bindings.MethodResultSinks;
        prepared.ConfigCommandsAvailable =
            AreEditorGeometryConfigCommandsAvailable(context);
        prepared.ClusteringAvailable = IsEditorClusteringAvailable(context);
        prepared.PointCloudConsolidationAvailable =
            IsEditorPointCloudConsolidationAvailable(context);
        prepared.Results = EditorGeometryProcessingResultsSnapshot{
            .LastKMeansResult = CopyOptional(bindings.LastKMeansResult),
            .LastPointCloudConsolidationResult =
                CopyOptional(bindings.LastPointCloudConsolidationResult),
            .LastMeshDenoiseResult =
                CopyOptional(bindings.LastMeshDenoiseResult),
            .LastMeshCurvatureResult =
                CopyOptional(bindings.LastMeshCurvatureResult),
            .LastMeshRemeshResult = CopyOptional(bindings.LastMeshRemeshResult),
            .LastMeshSubdivideResult =
                CopyOptional(bindings.LastMeshSubdivideResult),
            .LastMeshSimplifyResult =
                CopyOptional(bindings.LastMeshSimplifyResult),
            .LastMeshVertexNormalsResult =
                CopyOptional(bindings.LastMeshVertexNormalsResult),
            .LastGraphVertexNormalsResult =
                CopyOptional(bindings.LastGraphVertexNormalsResult),
            .LastPointCloudVertexNormalsResult =
                CopyOptional(bindings.LastPointCloudVertexNormalsResult),
            .LastPointCloudOutlierRemovalResult =
                CopyOptional(bindings.LastPointCloudOutlierRemovalResult),
            .LastUvRegenerationResult =
                CopyOptional(bindings.LastUvRegenerationResult),
            .LastParameterizationResult =
                CopyOptional(bindings.LastParameterizationResult),
            .LastProgressivePoissonResult =
                CopyOptional(bindings.LastProgressivePoissonResult),
            .LastRegistrationResult =
                CopyOptional(bindings.LastRegistrationResult),
        };
      });
  return prepared;
}

bool AreEditorGeometryConfigCommandsAvailable(
    const EditorGeometryProcessingContext &context) noexcept {
  return (!context.AttachmentActive || context.AttachmentActive()) &&
         context.EngineConfigControlState != nullptr &&
         context.EngineConfigCommandsAvailable &&
         static_cast<bool>(context.PreviewEngineConfigDocument) &&
         static_cast<bool>(context.ApplyEngineConfigHotSubset);
}

bool IsEditorClusteringAvailable(
    const EditorGeometryProcessingContext &context) noexcept {
  return (!context.AttachmentActive || context.AttachmentActive()) &&
         context.Clustering != nullptr && context.Clustering->Available();
}

bool IsEditorPointCloudConsolidationAvailable(
    const EditorGeometryProcessingContext &context) noexcept {
  return (!context.AttachmentActive || context.AttachmentActive()) &&
         context.PointCloudConsolidation != nullptr &&
         context.PointCloudConsolidation->Available();
}

KMeansRunCompleted SubmitKMeansRun(
    const EditorGeometryProcessingContext &context, const RunKMeans &command) {
  KMeansRunCompleted result{
      .World = context.World,
      .Status = KMeansRunStatus::ModuleUnavailable,
      .StableEntityId = command.StableEntityId,
      .Properties = command.Properties,
      .Parameters = command.Parameters,
      .RequestedBackend = command.Backend,
      .ActualBackend = ClusteringBackend::None,
      .Message = "ClusteringService is unavailable.",
  };
  if (!IsEditorClusteringAvailable(context))
    return result;

  result.Correlation = context.Clustering->RunKMeans(command);
  result.Status = KMeansRunStatus::Queued;
  result.Message = "K-Means runtime job queued.";
  return result;
}

PointCloudConsolidationResult SubmitEditorPointCloudConsolidation(
    const EditorGeometryProcessingContext &context,
    PointCloudConsolidationRequest request) {
  PointCloudConsolidationResult result{
      .World = context.World,
      .Status = PointCloudConsolidationRunStatus::ModuleUnavailable,
      .StableEntityId = request.StableEntityId,
      .Properties = request.Properties,
      .Config = request.Config,
      .StrategyToken = std::string{StableToken(request.Config.Strategy)},
      .Error = Core::ErrorCode::Unknown,
      .Message = "Point-cloud consolidation service is unavailable.",
  };
  if (!IsEditorPointCloudConsolidationAvailable(context))
    return result;

  result.Correlation =
      context.PointCloudConsolidation->Run(std::move(request));
  if (!result.Correlation.IsValid()) {
    result.Message = "Point-cloud consolidation command could not be queued.";
    return result;
  }
  result.Status = PointCloudConsolidationRunStatus::Queued;
  result.Error = Core::ErrorCode::Success;
  result.Message = "Point-cloud consolidation runtime job queued.";
  return result;
}

Geometry::ConstPropertySet ResolveEditorSelectedMeshVertexProperties(
    const EditorGeometryProcessingContext &context) {
  if (context.Scene == nullptr || context.Selection == nullptr)
    return {};

  const entt::registry &raw = context.Scene->Raw();
  for (const std::uint32_t stableId : context.Selection->SelectedStableIds()) {
    const ECS::EntityHandle entity =
        SelectionController::ToEntityHandle(stableId);
    if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
      continue;

    const GeometryEntityAvailability availability =
        BuildGeometryAvailability(raw, entity);
    const Geometry::PropertySet *properties = ResolveGeometryPropertySet(
        availability, GeometryElementDomain::MeshVertex);
    return properties != nullptr ? Geometry::ConstPropertySet{*properties}
                                 : Geometry::ConstPropertySet{};
  }
  return {};
}

const char *DebugNameForEditorUvAtlasStatus(
    Geometry::UvAtlas::UvAtlasStatus status) noexcept {
  return Geometry::UvAtlas::ToString(status);
}

const char *DebugNameForEditorUvAtlasProvenance(
    Geometry::UvAtlas::UvAtlasProvenance provenance) noexcept {
  return Geometry::UvAtlas::ToString(provenance);
}

KMeansRunCompleted SubmitKMeansRun(
    const EditorGeometryProcessingCommands &commands,
    const RunKMeans &command) {
  return SubmitKMeansRun(ContextOrEmpty(commands), command);
}

PointCloudConsolidationResult SubmitEditorPointCloudConsolidation(
    const EditorGeometryProcessingCommands &commands,
    PointCloudConsolidationRequest request) {
  return SubmitEditorPointCloudConsolidation(ContextOrEmpty(commands),
                                             std::move(request));
}

Geometry::ConstPropertySet ResolveEditorSelectedMeshVertexProperties(
    const EditorGeometryProcessingCommands &commands) {
  return ResolveEditorSelectedMeshVertexProperties(ContextOrEmpty(commands));
}

EditorUvRegenerationCommandResult ApplyEditorUvRegenerationCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorUvRegenerationCommand &command) {
  return ApplyEditorUvRegenerationCommand(ContextOrEmpty(commands), command);
}

EditorParameterizationResult ApplyEditorParameterizationCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorParameterizationCommand &command) {
  return ApplyEditorParameterizationCommand(ContextOrEmpty(commands), command);
}

EditorParameterizationResult ApplyEditorConfiguredParameterizationCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorConfiguredParameterizationCommand &command) {
  return ApplyEditorConfiguredParameterizationCommand(ContextOrEmpty(commands),
                                                       command);
}

EditorParameterizationConfigResult ApplyEditorParameterizationConfigCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorParameterizationConfigCommand &command) {
  return ApplyEditorParameterizationConfigCommand(ContextOrEmpty(commands),
                                                  command);
}

std::optional<ParameterizationConfig> GetEditorParameterizationConfig(
    const EditorGeometryProcessingCommands &commands) noexcept {
  return GetEditorParameterizationConfig(ContextOrEmpty(commands));
}

EditorParameterizationViewModel BuildEditorParameterizationViewModel(
    const EditorGeometryProcessingCommands &commands) {
  return BuildEditorParameterizationViewModel(ContextOrEmpty(commands));
}

EditorParameterizationUvViewState SubmitEditorParameterizationUvView(
    const EditorGeometryProcessingCommands &commands,
    const EditorParameterizationViewModel &model,
    std::uint32_t width,
    std::uint32_t height) {
  return SubmitEditorParameterizationUvView(ContextOrEmpty(commands), model,
                                            width, height);
}

void DisableEditorParameterizationUvView(
    const EditorGeometryProcessingCommands &commands) {
  DisableEditorParameterizationUvView(ContextOrEmpty(commands));
}

EditorMeshDenoiseResult ApplyEditorMeshDenoiseCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorMeshDenoiseCommand &command) {
  return ApplyEditorMeshDenoiseCommand(ContextOrEmpty(commands), command);
}

EditorMeshCurvatureResult ApplyEditorMeshCurvatureCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorMeshCurvatureCommand &command) {
  return ApplyEditorMeshCurvatureCommand(ContextOrEmpty(commands), command);
}

EditorMeshRemeshResult ApplyEditorMeshRemeshCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorMeshRemeshCommand &command) {
  return ApplyEditorMeshRemeshCommand(ContextOrEmpty(commands), command);
}

EditorMeshSubdivideResult ApplyEditorMeshSubdivideCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorMeshSubdivideCommand &command) {
  return ApplyEditorMeshSubdivideCommand(ContextOrEmpty(commands), command);
}

EditorMeshSimplifyResult ApplyEditorMeshSimplifyCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorMeshSimplifyCommand &command) {
  return ApplyEditorMeshSimplifyCommand(ContextOrEmpty(commands), command);
}

EditorMeshVertexNormalsResult ApplyEditorMeshVertexNormalsCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorMeshVertexNormalsCommand &command) {
  return ApplyEditorMeshVertexNormalsCommand(ContextOrEmpty(commands), command);
}

EditorGraphVertexNormalsResult ApplyEditorGraphVertexNormalsCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorGraphVertexNormalsCommand &command) {
  return ApplyEditorGraphVertexNormalsCommand(ContextOrEmpty(commands), command);
}

EditorPointCloudVertexNormalsResult ApplyEditorPointCloudVertexNormalsCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorPointCloudVertexNormalsCommand &command) {
  return ApplyEditorPointCloudVertexNormalsCommand(ContextOrEmpty(commands),
                                                   command);
}

EditorPointCloudOutlierRemovalResult ApplyEditorPointCloudOutlierRemovalCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorPointCloudOutlierRemovalCommand &command) {
  return ApplyEditorPointCloudOutlierRemovalCommand(ContextOrEmpty(commands),
                                                    command);
}

EditorRegistrationResult ApplyEditorRegistrationCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorRegistrationCommand &command) {
  return ApplyEditorRegistrationCommand(ContextOrEmpty(commands), command);
}

EditorProgressivePoissonResult ApplyEditorProgressivePoissonCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorProgressivePoissonCommand &command) {
  return ApplyEditorProgressivePoissonCommand(ContextOrEmpty(commands), command);
}

EditorProgressivePoissonConfigResult ApplyEditorProgressivePoissonConfigCommand(
    const EditorGeometryProcessingCommands &commands,
    const EditorProgressivePoissonConfigCommand &command) {
  return ApplyEditorProgressivePoissonConfigCommand(ContextOrEmpty(commands),
                                                    command);
}

RuntimeEngineConfigApplyResult ApplyEditorClusteringConfig(
    const EditorGeometryProcessingCommands &commands,
    const ClusteringConfig &config,
    std::string sourceId) {
  return ApplyEditorClusteringConfig(ContextOrEmpty(commands), config,
                                     std::move(sourceId));
}

std::optional<ClusteringConfig> GetEditorClusteringConfig(
    const EditorGeometryProcessingCommands &commands) noexcept {
  return GetEditorClusteringConfig(ContextOrEmpty(commands));
}

RuntimeEngineConfigApplyResult ApplyEditorPointCloudConsolidationConfig(
    const EditorGeometryProcessingCommands &commands,
    const PointCloudConsolidationConfig &config,
    std::string sourceId) {
  return ApplyEditorPointCloudConsolidationConfig(
      ContextOrEmpty(commands), config, std::move(sourceId));
}

std::optional<PointCloudConsolidationConfig>
GetEditorPointCloudConsolidationConfig(
    const EditorGeometryProcessingCommands &commands) noexcept {
  return GetEditorPointCloudConsolidationConfig(ContextOrEmpty(commands));
}

std::optional<EditorProgressivePoissonConfig>
GetEditorProgressivePoissonConfig(
    const EditorGeometryProcessingCommands &commands) noexcept {
  return GetEditorProgressivePoissonConfig(ContextOrEmpty(commands));
}
} // namespace Extrinsic::Runtime
