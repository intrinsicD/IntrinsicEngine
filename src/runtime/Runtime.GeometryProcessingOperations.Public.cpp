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
import Extrinsic.Runtime.Private.EditorWorkspaceAttachment;

namespace {
template <typename T>
[[nodiscard]] std::optional<T> CopyOptional(const T *value) {
  return value != nullptr ? std::optional<T>{*value} : std::nullopt;
}
} // namespace

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

const EditorGeometryProcessingContext *
EditorGeometryProcessingCommandsAccess::Resolve(
    const EditorGeometryProcessingCommands &commands) noexcept {
  return commands.IsBound() && commands.m_State != nullptr
             ? &commands.m_State->Context
             : nullptr;
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
        prepared.Results = EditorGeometryProcessingResultsSnapshot{
            .LastKMeansResult = CopyOptional(bindings.LastKMeansResult),
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

KMeansRunCompleted SubmitKMeansRun(
    const EditorGeometryProcessingCommands &commands,
    const RunKMeans &command) {
  return SubmitKMeansRun(ContextOrEmpty(commands), command);
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

std::optional<EditorProgressivePoissonConfig>
GetEditorProgressivePoissonConfig(
    const EditorGeometryProcessingCommands &commands) noexcept {
  return GetEditorProgressivePoissonConfig(ContextOrEmpty(commands));
}
} // namespace Extrinsic::Runtime
