module;

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

module Extrinsic.Runtime.GeometryProcessingOperations;

import Extrinsic.Runtime.Private.EditorFeatures;

namespace Extrinsic::Runtime {
struct EditorGeometryProcessingCommands::State {
  explicit State(EditorGeometryProcessingContext context)
      : Context(std::move(context)) {}

  EditorGeometryProcessingContext Context{};
};

EditorGeometryProcessingCommands::EditorGeometryProcessingCommands(
    std::shared_ptr<const State> state)
    : m_State(std::move(state)) {}

bool EditorGeometryProcessingCommands::IsBound() const noexcept {
  return m_State != nullptr && (!m_State->Context.AttachmentActive ||
                                m_State->Context.AttachmentActive());
}

EditorGeometryProcessingCommands::
operator const EditorGeometryProcessingContext &() const noexcept {
  static const EditorGeometryProcessingContext empty{};
  return m_State != nullptr ? m_State->Context : empty;
}

EditorGeometryProcessingCommands
BindEditorGeometryProcessingCommands(EditorGeometryProcessingContext context) {
  return EditorGeometryProcessingCommands{
      std::make_shared<EditorGeometryProcessingCommands::State>(
          std::move(context))};
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

std::vector<EditorGeometryProcessingMenuItem>
GetEditorGeometryProcessingMenuItems(EditorDomainWindowKind kind) {
  return EditorFeatureDetail::GetEditorGeometryProcessingMenuItemsImpl(kind);
}

EditorGeometryProcessingDomain GetEditorSupportedGeometryProcessingDomains(
    EditorGeometryProcessingAlgorithm algorithm) noexcept {
  return EditorFeatureDetail::GetEditorSupportedGeometryProcessingDomainsImpl(
      algorithm);
}

bool SupportsEditorGeometryProcessingDomain(
    EditorGeometryProcessingAlgorithm algorithm,
    EditorGeometryProcessingDomain domain) noexcept {
  return EditorFeatureDetail::SupportsEditorGeometryProcessingDomainImpl(
      algorithm, domain);
}

EditorGeometryProcessingCapabilities
GetEditorGeometryProcessingCapabilities(const ECS::Scene::Registry &registry,
                                        ECS::EntityHandle entity) {
  return EditorFeatureDetail::GetEditorGeometryProcessingCapabilitiesImpl(
      registry, entity);
}

std::vector<EditorGeometryProcessingEntry>
ResolveEditorGeometryProcessingEntries(
    EditorGeometryProcessingCapabilities capabilities) {
  return EditorFeatureDetail::ResolveEditorGeometryProcessingEntriesImpl(
      capabilities);
}

std::vector<EditorGeometryProcessingEntry>
ResolveEditorGeometryProcessingEntries(const ECS::Scene::Registry &registry,
                                       ECS::EntityHandle entity) {
  return EditorFeatureDetail::ResolveEditorGeometryProcessingEntriesImpl(
      registry, entity);
}

std::vector<EditorGeometryProcessingDomain>
GetAvailableEditorKMeansDomains(const ECS::Scene::Registry &registry,
                                ECS::EntityHandle entity) {
  return EditorFeatureDetail::GetAvailableEditorKMeansDomainsImpl(registry,
                                                                  entity);
}

const char *DebugNameForEditorGeometryProcessingDomain(
    EditorGeometryProcessingDomain domain) noexcept {
  return EditorFeatureDetail::DebugNameForEditorGeometryProcessingDomainImpl(
      domain);
}

const char *DebugNameForEditorGeometryProcessingAlgorithm(
    EditorGeometryProcessingAlgorithm algorithm) noexcept {
  return EditorFeatureDetail::DebugNameForEditorGeometryProcessingAlgorithmImpl(
      algorithm);
}

const char *DebugNameForEditorProgressivePoissonChannel(
    EditorProgressivePoissonChannel channel) noexcept {
  return EditorFeatureDetail::DebugNameForEditorProgressivePoissonChannelImpl(
      channel);
}

const char *DebugNameForEditorProgressivePoissonBackend(
    EditorProgressivePoissonBackend backend) noexcept {
  return EditorFeatureDetail::DebugNameForEditorProgressivePoissonBackendImpl(
      backend);
}

EditorProgressivePoissonChannel MakeEditorProgressivePoissonChannel(
    ProgressivePoissonPlaygroundChannel channel) noexcept {
  return EditorFeatureDetail::MakeEditorProgressivePoissonChannelImpl(channel);
}

EditorProgressivePoissonBackend MakeEditorProgressivePoissonBackend(
    ProgressivePoissonPlaygroundBackend backend) noexcept {
  return EditorFeatureDetail::MakeEditorProgressivePoissonBackendImpl(backend);
}

EditorProgressivePoissonConfig MakeEditorProgressivePoissonConfig(
    const ProgressivePoissonPlaygroundConfig &config) noexcept {
  return EditorFeatureDetail::MakeEditorProgressivePoissonConfigImpl(config);
}

const char *DebugNameForEditorMeshCurvatureOutput(
    EditorMeshCurvatureOutput output) noexcept {
  return EditorFeatureDetail::DebugNameForEditorMeshCurvatureOutputImpl(output);
}

const char *
DebugNameForEditorMeshRemeshMode(EditorMeshRemeshMode mode) noexcept {
  return EditorFeatureDetail::DebugNameForEditorMeshRemeshModeImpl(mode);
}

const char *DebugNameForEditorMeshRemeshSizingLaw(
    EditorMeshRemeshSizingLaw sizingLaw) noexcept {
  return EditorFeatureDetail::DebugNameForEditorMeshRemeshSizingLawImpl(
      sizingLaw);
}

const char *DebugNameForEditorMeshSubdivideOperator(
    EditorMeshSubdivideOperator op) noexcept {
  return EditorFeatureDetail::DebugNameForEditorMeshSubdivideOperatorImpl(op);
}

const char *
DebugNameForEditorMeshSimplifyMetric(EditorMeshSimplifyMetric metric) noexcept {
  return EditorFeatureDetail::DebugNameForEditorMeshSimplifyMetricImpl(metric);
}

const char *DebugNameForEditorICPVariant(EditorICPVariant variant) noexcept {
  return EditorFeatureDetail::DebugNameForEditorICPVariantImpl(variant);
}

Geometry::ConstPropertySet ResolveEditorSelectedMeshVertexProperties(
    const EditorGeometryProcessingContext &context) {
  return EditorFeatureDetail::ResolveEditorSelectedMeshVertexPropertiesImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
}

std::string_view StableTokenForEditorParameterizationStrategy(
    EditorParameterizationStrategy strategy) noexcept {
  return EditorFeatureDetail::StableTokenForEditorParameterizationStrategyImpl(
      strategy);
}

const char *DebugNameForEditorUvAtlasStatus(
    Geometry::UvAtlas::UvAtlasStatus status) noexcept {
  return EditorFeatureDetail::DebugNameForEditorUvAtlasStatusImpl(status);
}

const char *DebugNameForEditorUvAtlasProvenance(
    Geometry::UvAtlas::UvAtlasProvenance provenance) noexcept {
  return EditorFeatureDetail::DebugNameForEditorUvAtlasProvenanceImpl(
      provenance);
}

EditorUvRegenerationCommandResult
ApplyEditorUvRegenerationCommand(const EditorGeometryProcessingContext &context,
                                 const EditorUvRegenerationCommand &command) {
  return EditorFeatureDetail::ApplyEditorUvRegenerationCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorParameterizationResult ApplyEditorParameterizationCommand(
    const EditorGeometryProcessingContext &context,
    const EditorParameterizationCommand &command) {
  return EditorFeatureDetail::ApplyEditorParameterizationCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorParameterizationResult ApplyEditorConfiguredParameterizationCommand(
    const EditorGeometryProcessingContext &context,
    const EditorConfiguredParameterizationCommand &command) {
  return EditorFeatureDetail::ApplyEditorConfiguredParameterizationCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorParameterizationConfigResult ApplyEditorParameterizationConfigCommand(
    const EditorGeometryProcessingContext &context,
    const EditorParameterizationConfigCommand &command) {
  return EditorFeatureDetail::ApplyEditorParameterizationConfigCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

std::optional<ParameterizationConfig> GetEditorParameterizationConfig(
    const EditorGeometryProcessingContext &context) noexcept {
  return EditorFeatureDetail::GetEditorParameterizationConfigImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
}

EditorParameterizationViewModel BuildEditorParameterizationViewModel(
    const EditorGeometryProcessingContext &context) {
  return EditorFeatureDetail::BuildEditorParameterizationViewModelImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
}

EditorParameterizationUvViewState SubmitEditorParameterizationUvView(
    const EditorGeometryProcessingContext &context,
    const EditorParameterizationViewModel &model, std::uint32_t width,
    std::uint32_t height) {
  return EditorFeatureDetail::SubmitEditorParameterizationUvViewImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), model, width,
      height);
}

void DisableEditorParameterizationUvView(
    const EditorGeometryProcessingContext &context) {
  EditorFeatureDetail::DisableEditorParameterizationUvViewImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
}

const char *DebugNameForEditorParameterizationUvViewStatus(
    EditorParameterizationUvViewStatus status) noexcept {
  return EditorFeatureDetail::
      DebugNameForEditorParameterizationUvViewStatusImpl(status);
}

EditorMeshDenoiseResult
ApplyEditorMeshDenoiseCommand(const EditorGeometryProcessingContext &context,
                              const EditorMeshDenoiseCommand &command) {
  return EditorFeatureDetail::ApplyEditorMeshDenoiseCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorMeshCurvatureResult
ApplyEditorMeshCurvatureCommand(const EditorGeometryProcessingContext &context,
                                const EditorMeshCurvatureCommand &command) {
  return EditorFeatureDetail::ApplyEditorMeshCurvatureCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorMeshRemeshResult
ApplyEditorMeshRemeshCommand(const EditorGeometryProcessingContext &context,
                             const EditorMeshRemeshCommand &command) {
  return EditorFeatureDetail::ApplyEditorMeshRemeshCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorMeshSubdivideResult
ApplyEditorMeshSubdivideCommand(const EditorGeometryProcessingContext &context,
                                const EditorMeshSubdivideCommand &command) {
  return EditorFeatureDetail::ApplyEditorMeshSubdivideCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorMeshSimplifyResult
ApplyEditorMeshSimplifyCommand(const EditorGeometryProcessingContext &context,
                               const EditorMeshSimplifyCommand &command) {
  return EditorFeatureDetail::ApplyEditorMeshSimplifyCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorMeshVertexNormalsResult ApplyEditorMeshVertexNormalsCommand(
    const EditorGeometryProcessingContext &context,
    const EditorMeshVertexNormalsCommand &command) {
  return EditorFeatureDetail::ApplyEditorMeshVertexNormalsCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorGraphVertexNormalsResult ApplyEditorGraphVertexNormalsCommand(
    const EditorGeometryProcessingContext &context,
    const EditorGraphVertexNormalsCommand &command) {
  return EditorFeatureDetail::ApplyEditorGraphVertexNormalsCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorPointCloudVertexNormalsResult ApplyEditorPointCloudVertexNormalsCommand(
    const EditorGeometryProcessingContext &context,
    const EditorPointCloudVertexNormalsCommand &command) {
  return EditorFeatureDetail::ApplyEditorPointCloudVertexNormalsCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorPointCloudOutlierRemovalResult ApplyEditorPointCloudOutlierRemovalCommand(
    const EditorGeometryProcessingContext &context,
    const EditorPointCloudOutlierRemovalCommand &command) {
  return EditorFeatureDetail::ApplyEditorPointCloudOutlierRemovalCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorRegistrationResult
ApplyEditorRegistrationCommand(const EditorGeometryProcessingContext &context,
                               const EditorRegistrationCommand &command) {
  return EditorFeatureDetail::ApplyEditorRegistrationCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorProgressivePoissonResult ApplyEditorProgressivePoissonCommand(
    const EditorGeometryProcessingContext &context,
    const EditorProgressivePoissonCommand &command) {
  return EditorFeatureDetail::ApplyEditorProgressivePoissonCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

EditorProgressivePoissonConfigResult ApplyEditorProgressivePoissonConfigCommand(
    const EditorGeometryProcessingContext &context,
    const EditorProgressivePoissonConfigCommand &command) {
  return EditorFeatureDetail::ApplyEditorProgressivePoissonConfigCommandImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), command);
}

RuntimeEngineConfigApplyResult
ApplyEditorClusteringConfig(const EditorGeometryProcessingContext &context,
                            const ClusteringConfig &config,

                            std::string sourceId) {
  return EditorFeatureDetail::ApplyEditorClusteringConfigImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context), config,
      sourceId);
}

std::optional<ClusteringConfig> GetEditorClusteringConfig(
    const EditorGeometryProcessingContext &context) noexcept {
  return EditorFeatureDetail::GetEditorClusteringConfigImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
}

std::optional<EditorProgressivePoissonConfig> GetEditorProgressivePoissonConfig(
    const EditorGeometryProcessingContext &context) noexcept {
  return EditorFeatureDetail::GetEditorProgressivePoissonConfigImpl(
      EditorFeatureDetail::ToEditorFeatureBindingsImpl(context));
}
} // namespace Extrinsic::Runtime
