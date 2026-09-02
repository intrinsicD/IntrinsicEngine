module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>

module Extrinsic.Sandbox.Editor.DomainPanels;

import Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorPropertyWidgets;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.VertexAttributeBinding;
import Extrinsic.Runtime.VertexChannelBindings;

namespace Extrinsic::Sandbox::Editor {
namespace {
using namespace Extrinsic::Runtime;

using SurfaceDomain =
    decltype(EditorRenderHintModel{}.SurfaceDomainValue);
using EdgeDomain = decltype(EditorRenderHintModel{}.EdgeDomainValue);
using PointRenderType =
    decltype(EditorRenderHintModel{}.PointRenderTypeValue);
using VisualizationColorSource =
    decltype(EditorVisualizationConfigModel{}.Source);
using ColormapType =
    decltype(EditorVisualizationConfigModel{}.ScalarColormap);

inline constexpr SurfaceDomain kSurfaceVertex = static_cast<SurfaceDomain>(0);
inline constexpr SurfaceDomain kSurfaceFace = static_cast<SurfaceDomain>(1);
inline constexpr EdgeDomain kEdgeVertex = static_cast<EdgeDomain>(0);
inline constexpr EdgeDomain kEdgeEdge = static_cast<EdgeDomain>(1);
inline constexpr PointRenderType kPointFlat = static_cast<PointRenderType>(0);
inline constexpr PointRenderType kPointSphere = static_cast<PointRenderType>(1);
inline constexpr PointRenderType kPointSurfel = static_cast<PointRenderType>(2);
inline constexpr VisualizationColorSource kUniformColorSource =
    static_cast<VisualizationColorSource>(1);
inline constexpr VisualizationColorSource kScalarFieldSource =
    static_cast<VisualizationColorSource>(2);

inline constexpr std::array<GeometryPresentationSlotSemantic, 5>
    kTextureBakeTargetSemantics{{
        GeometryPresentationSlotSemantic::Albedo,
        GeometryPresentationSlotSemantic::Normal,
        GeometryPresentationSlotSemantic::Roughness,
        GeometryPresentationSlotSemantic::Metallic,
        GeometryPresentationSlotSemantic::ScalarField,
    }};

inline constexpr std::array<PropertyTextureBakeEncoding, 8>
    kTextureBakeEncoders{{
        PropertyTextureBakeEncoding::Auto,
        PropertyTextureBakeEncoding::RgbaColor,
        PropertyTextureBakeEncoding::Normal,
        PropertyTextureBakeEncoding::ScalarColormap,
        PropertyTextureBakeEncoding::LinearScalar,
        PropertyTextureBakeEncoding::LabelPalette,
        PropertyTextureBakeEncoding::Vector2,
        PropertyTextureBakeEncoding::Vector3,
    }};

inline constexpr std::array<PropertyTextureBakeStorage, 3>
    kTextureBakeStorageModes{{
        PropertyTextureBakeStorage::Auto,
        PropertyTextureBakeStorage::RawFloat,
        PropertyTextureBakeStorage::EncodedRgba,
    }};

inline constexpr std::array<const char *, 3> kTextureBakeStorageNames{{
    "auto (raw except normals/labels)",
    "raw float texture",
    "encoded RGBA texture",
}};

inline constexpr std::array<const char *, 6> kColormapNames{{
    "Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"}};

inline constexpr std::array<const char *, 2> kNormalSpaceNames{{
    "object space", "world space"}};

[[nodiscard]] const char *DebugNameForTextureBakeEncoder(
    const PropertyTextureBakeEncoding encoder) noexcept {
  switch (encoder) {
  case PropertyTextureBakeEncoding::Auto:
    return "auto";
  case PropertyTextureBakeEncoding::LinearScalar:
    return "linear scalar";
  case PropertyTextureBakeEncoding::ScalarColormap:
    return "scalar colormap";
  case PropertyTextureBakeEncoding::LabelPalette:
    return "label palette";
  case PropertyTextureBakeEncoding::Vector2:
    return "vector2";
  case PropertyTextureBakeEncoding::Vector3:
    return "vector3";
  case PropertyTextureBakeEncoding::Normal:
    return "normal";
  case PropertyTextureBakeEncoding::RgbaColor:
    return "rgba color";
  }
  return "unknown";
}

struct PointCloudOutlierRemovalUiState {
  std::optional<EditorPointCloudOutlierRemovalResult> *LastResult{
      nullptr};
  std::int32_t *Method{nullptr};
  std::int32_t *KNeighbors{nullptr};
  float *StdDevMultiplier{nullptr};
  float *SearchRadius{nullptr};
  std::int32_t *MinNeighbors{nullptr};
};

struct TextureBakeUiState {
  std::optional<EditorUvRegenerationCommandResult>
      *LastUvRegenerationResult{nullptr};
  std::optional<EditorUvRegenerationCommandResult>
      *LastUvExtentAdoption{nullptr};
  std::int32_t *SourceIndex{nullptr};
  std::int32_t *TargetSemanticIndex{nullptr};
  std::int32_t *EncoderIndex{nullptr};
  std::int32_t *StorageIndex{nullptr};
  std::int32_t *ColormapIndex{nullptr};
  std::int32_t *NormalSpaceIndex{nullptr};
  std::uint32_t *AdditionalConsumerMask{nullptr};
  std::int32_t *Width{nullptr};
  std::int32_t *Height{nullptr};
  std::int32_t *Padding{nullptr};
  std::int32_t *UvResolution{nullptr};
  std::int32_t *UvPadding{nullptr};
  float *UvTexelsPerUnit{nullptr};
  bool *UvForceRegenerate{nullptr};
  bool *UvPreserveAuthored{nullptr};
};

[[nodiscard]] std::span<const EditorTextureBakeTarget>
TextureBakeTargetsFor(
    const EditorTextureBakeControlsModel &model,
    const std::string_view outputName) {
  const auto found = std::ranges::find(
      model.TextureBakeTargets, outputName,
      &EditorTextureBakeTargetSnapshot::OutputName);
  if (found == model.TextureBakeTargets.end())
    return {};
  return found->Targets;
}

void DrawDiagnostics(const std::vector<EditorDiagnostic> &diagnostics) {
  for (const EditorDiagnostic &diagnostic : diagnostics) {
    ImGui::TextDisabled(
        "%s: %s", DebugNameForEditorDiagnosticCode(diagnostic.Code),
        diagnostic.Message.c_str());
  }
}

void DrawVec3(const char *label, const glm::vec3 value) {
  ImGui::Text("%s: %.3f, %.3f, %.3f", label, value.x, value.y, value.z);
}

[[nodiscard]] EditorVisualizationConfigCommand
MakeUniformVisualizationConfigCommandFromModel(
    const std::uint32_t stableEntityId,
    const EditorVisualizationConfigModel &model,
    const EditorVisualizationTarget target, const glm::vec4 color) {
  return EditorVisualizationConfigCommand{
      .StableEntityId = stableEntityId,
      .Target = target,
      .EnableConfig = true,
      .Source = kUniformColorSource,
      .Color = color,
      .ScalarFieldName = model.ScalarFieldName,
      .ScalarDomain = model.ScalarDomain,
      .ColorBufferName = model.ColorBufferName,
      .ScalarAutoRange = model.ScalarAutoRange,
      .ScalarRangeMin = model.ScalarRangeMin,
      .ScalarRangeMax = model.ScalarRangeMax,
      .ScalarBinCount = model.ScalarBinCount,
      .IsolineCount = model.IsolineCount,
      .ScalarColormap = model.ScalarColormap,
      .IsolineWidth = model.IsolineWidth,
      .IsolineColor = model.IsolineColor,
      .IsolineValues = model.IsolineValues,
      .IsolineValueCount = model.IsolineValueCount,
  };
}

[[nodiscard]] EditorVisualizationConfigCommand
MakeScalarVisualizationConfigCommandFromModel(
    const std::uint32_t stableEntityId,
    const EditorVisualizationConfigModel &model,
    const EditorVisualizationTarget target) {
  return EditorVisualizationConfigCommand{
      .StableEntityId = stableEntityId,
      .Target = target,
      .EnableConfig = true,
      .Source = model.Source,
      .Color = model.Color,
      .ScalarFieldName = model.ScalarFieldName,
      .ScalarDomain = model.ScalarDomain,
      .ColorBufferName = model.ColorBufferName,
      .ScalarAutoRange = model.ScalarAutoRange,
      .ScalarRangeMin = model.ScalarRangeMin,
      .ScalarRangeMax = model.ScalarRangeMax,
      .ScalarBinCount = model.ScalarBinCount,
      .IsolineCount = model.IsolineCount,
      .ScalarColormap = model.ScalarColormap,
      .IsolineWidth = model.IsolineWidth,
      .IsolineColor = model.IsolineColor,
      .IsolineValues = model.IsolineValues,
      .IsolineValueCount = model.IsolineValueCount,
  };
}

[[nodiscard]] bool
DomainWindowReady(const EditorDomainWindowModel &model) noexcept {
  return model.HasSelectedEntity && model.DomainMatches;
}

[[nodiscard]] bool
DomainAppearanceReady(const EditorDomainWindowModel &model) noexcept {
  return model.HasSelectedEntity && model.VisualizationTargetAvailable;
}

void DrawDomainWindowHeader(const EditorDomainWindowModel &model) {
  ImGui::Text("Expected domain: %s",
              DebugNameForEditorGeometryDomain(model.ExpectedDomain));
  if (model.HasSelectedEntity) {
    ImGui::Text("Selected: %s (%u)", model.SelectedEntity.Name.c_str(),
                model.SelectedStableId);
    ImGui::Text("Selected domain: %s",
                DebugNameForEditorGeometryDomain(model.SelectedDomain));
  } else {
    ImGui::TextDisabled("Selected: none");
  }
  DrawDiagnostics(model.Diagnostics);
}

void DrawPropertyCatalogRows(const EditorPropertyCatalogModel &catalog) {
  ImGui::Text("Properties: %zu", catalog.Rows.size());
  if (catalog.Rows.empty()) {
    ImGui::TextDisabled("No geometry properties.");
    return;
  }

  constexpr ImGuiTableFlags tableFlags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("PropertyCatalog", 7, tableFlags)) {
    ImGui::TableSetupColumn("Domain");
    ImGui::TableSetupColumn("Name");
    ImGui::TableSetupColumn("Kind");
    ImGui::TableSetupColumn("Count");
    ImGui::TableSetupColumn("Tags");
    ImGui::TableSetupColumn("Preview");
    ImGui::TableSetupColumn("Reason");
    ImGui::TableHeadersRow();

    for (const EditorPropertyCatalogRow &row : catalog.Rows) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(
          DebugNameForEditorPropertyCatalogDomain(row.Domain));
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(row.Name.c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::Text(
          "%s/%u",
          DebugNameForGeometryPropertyValueKind(row.ValueKind),
          row.ComponentCount);
      ImGui::TableSetColumnIndex(3);
      ImGui::Text("%zu", row.ElementCount);
      ImGui::TableSetColumnIndex(4);
      std::string tags{};
      if (row.Bindable)
        tags += "bindable ";
      if (row.Internal)
        tags += "internal ";
      if (row.Connectivity)
        tags += "connectivity ";
      if (row.Generated)
        tags += "generated ";
      ImGui::TextUnformatted(tags.empty() ? "-" : tags.c_str());
      ImGui::TableSetColumnIndex(5);
      if (row.Preview.HasValue) {
        ImGui::Text("[%zu] %s", row.Preview.ElementIndex,
                    row.Preview.Text.c_str());
      } else {
        ImGui::TextDisabled("-");
      }
      ImGui::TableSetColumnIndex(6);
      ImGui::TextDisabled("%s", row.UnsupportedReason.empty()
                                    ? "-"
                                    : row.UnsupportedReason.c_str());
    }
    ImGui::EndTable();
  }
}

void DrawPropertyBindingTargets(
    const EditorPropertyCatalogModel &catalog) {
  if (catalog.BindingTargets.empty())
    return;

  ImGui::SeparatorText("Binding targets");
  for (std::size_t i = 0u; i < catalog.BindingTargets.size(); ++i) {
    const EditorPropertyBindingTargetModel &target =
        catalog.BindingTargets[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::Text("%s / %s / %s requires %s %zu",
                std::string(ToString(target.Lane)).c_str(),
                target.PresentationKey.c_str(),
                std::string(ToString(target.Semantic)).c_str(),
                DebugNameForGeometryPropertyValueKindFilter(
                    target.ExpectedValueKind),
                target.ExpectedElementCount);
    for (const EditorGeometryPresentationPropertyOptionModel &option :
         target.Options) {
      if (option.Compatible) {
        ImGui::BulletText("%s", option.Descriptor.Name.c_str());
      } else {
        ImGui::BulletText("%s", option.Descriptor.Name.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s", option.DisabledReason.c_str());
      }
    }
    ImGui::PopID();
  }
}

void DrawVertexChannelBindingTargets(
    const EditorPropertyCatalogModel &catalog,
    const SandboxEditorContext *context) {
  if (catalog.VertexChannelTargets.empty())
    return;

  ImGui::SeparatorText("Vertex channels");
  const bool commandsAvailable =
      context != nullptr && context->SceneAvailable;
  for (std::size_t i = 0u; i < catalog.VertexChannelTargets.size(); ++i) {
    const EditorVertexChannelBindingTargetModel &target =
        catalog.VertexChannelTargets[i];
    ImGui::PushID(static_cast<int>(i));

    const char *channelName = DebugNameForVertexChannel(target.Channel);
    const std::string currentLabel = target.HasBinding
                                         ? target.Binding.Property.Name
                                         : std::string{"Default"};
    ImGui::Text("%s", channelName);
    ImGui::SameLine();

    if (!commandsAvailable)
      ImGui::BeginDisabled();
    if (ImGui::BeginCombo("##VertexChannelBinding", currentLabel.c_str())) {
      if (ImGui::Selectable("Default", !target.HasBinding) &&
          commandsAvailable) {
        (void)ApplyEditorVertexChannelBindingCommand(
            context->VisualizationCommands, EditorVertexChannelBindingCommand{
                          .StableEntityId = catalog.SelectedStableId,
                          .Channel = target.Channel,
                          .EnableBinding = false,
                      });
      }

      for (const EditorVertexChannelBindingOptionModel &option :
           target.Options) {
        const bool selected =
            target.HasBinding &&
            target.Binding.Property.Name == option.PropertyName;
        if (!option.Compatible)
          ImGui::BeginDisabled();
        const std::string label =
            option.PropertyName + " (" +
            DebugNameForGeometryPropertyValueKind(
                option.ValueKind) +
            ", " + std::to_string(option.ElementCount) + ")";
        if (ImGui::Selectable(label.c_str(), selected) && option.Compatible &&
            commandsAvailable) {
          (void)ApplyEditorVertexChannelBindingCommand(
              context->VisualizationCommands, EditorVertexChannelBindingCommand{
                            .StableEntityId = catalog.SelectedStableId,
                            .Channel = target.Channel,
                            .EnableBinding = true,
                            .PropertyName = option.PropertyName,
                        });
        }
        if (!option.Compatible) {
          ImGui::EndDisabled();
          ImGui::SameLine();
          ImGui::TextDisabled("%s", option.DisabledReason.c_str());
        }
      }
      ImGui::EndCombo();
    }
    if (!commandsAvailable)
      ImGui::EndDisabled();

    if (target.HasBinding && !target.Diagnostic.empty())
      ImGui::TextDisabled("%s", target.Diagnostic.c_str());
    ImGui::PopID();
  }
}

void DrawBoundRenderStateRows(const EditorBoundRenderStateModel &bound) {
  ImGui::SeparatorText("Bound render state");
  ImGui::Text("Rows: %zu generation=%llu", bound.Rows.size(),
              static_cast<unsigned long long>(bound.RecipeGeneration));
  if (bound.Rows.empty()) {
    ImGui::TextDisabled("No bound render state rows.");
    DrawDiagnostics(bound.Diagnostics);
    return;
  }

  constexpr ImGuiTableFlags tableFlags =
      ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
  if (ImGui::BeginTable("BoundRenderState", 8, tableFlags)) {
    ImGui::TableSetupColumn("Kind");
    ImGui::TableSetupColumn("Lane");
    ImGui::TableSetupColumn("Label");
    ImGui::TableSetupColumn("Source");
    ImGui::TableSetupColumn("Readiness");
    ImGui::TableSetupColumn("Property");
    ImGui::TableSetupColumn("Job");
    ImGui::TableSetupColumn("Diagnostic");
    ImGui::TableHeadersRow();

    for (const EditorBoundRenderStateRow &row : bound.Rows) {
      const std::string laneText{ToString(row.Lane)};
      const std::string sourceText = row.SourceDescription.empty()
                                         ? std::string{ToString(row.SourceKind)}
                                         : row.SourceDescription;
      const std::string readinessText{ToString(row.Readiness)};
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(
          DebugNameForEditorBoundRenderStateRowKind(row.Kind));
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(laneText.c_str());
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(row.Label.c_str());
      ImGui::TableSetColumnIndex(3);
      ImGui::TextUnformatted(sourceText.c_str());
      ImGui::TableSetColumnIndex(4);
      ImGui::TextUnformatted(readinessText.c_str());
      ImGui::TableSetColumnIndex(5);
      if (!row.Property.Name.empty()) {
        ImGui::Text("%s%s", row.Property.Name.c_str(),
                    row.HasCatalogMatch ? " catalog" : "");
      } else {
        ImGui::TextDisabled("-");
      }
      ImGui::TableSetColumnIndex(6);
      if (row.Kind == EditorBoundRenderStateRowKind::DerivedJob) {
        ImGui::Text("%s %.2f", std::string(ToString(row.JobStatus)).c_str(),
                    row.JobProgress);
      } else if (row.TextureAsset.IsValid() || row.AuthoredTexture.IsValid() ||
                 row.GeneratedTexture.IsValid()) {
        ImGui::Text("texture");
      } else {
        ImGui::TextDisabled("-");
      }
      ImGui::TableSetColumnIndex(7);
      if (!row.Diagnostic.empty())
        ImGui::TextWrapped("%s", row.Diagnostic.c_str());
      else if (!row.DisabledReason.empty())
        ImGui::TextDisabled("%s", row.DisabledReason.c_str());
      else
        ImGui::TextDisabled("-");
    }
    ImGui::EndTable();
  }
  DrawDiagnostics(bound.Diagnostics);
}

void DrawUvRegenerationStatus(
    const EditorUvDiagnosticsModel &uv,
    const std::optional<EditorUvRegenerationCommandResult> &lastResult) {
  if (uv.UvRegenerationJob.has_value()) {
    const EditorJobModel &job = *uv.UvRegenerationJob;
    ImGui::Text("UV job: %s %.0f%%", std::string(ToString(job.Status)).c_str(),
                job.NormalizedProgress * 100.0f);
    if (!job.Diagnostic.empty())
      ImGui::TextWrapped("%s", job.Diagnostic.c_str());
  }

  if (!lastResult.has_value()) {
    ImGui::TextDisabled("Last UV regeneration: none");
    return;
  }

  const EditorUvRegenerationCommandResult &result = *lastResult;
  ImGui::Text("Last UV regeneration: %s",
              DebugNameForEditorCommandStatus(result.Status));
  ImGui::Text("Atlas: %s / %s  %ux%u  charts=%u  splits=%zu",
              DebugNameForEditorUvAtlasStatus(result.UvStatus),
              DebugNameForEditorUvAtlasProvenance(result.Provenance),
              result.AtlasWidth, result.AtlasHeight, result.ChartCount,
              result.SeamSplitVertexCount);
  if (!result.Diagnostic.empty())
    ImGui::TextWrapped("%s", result.Diagnostic.c_str());
}

void DrawTextureBakeControls(const EditorTextureBakeControlsModel &model,
                             const SandboxEditorContext *context,
                             TextureBakeUiState *state) {
  std::optional<EditorUvRegenerationCommandResult>
      fallbackUvRegenerationResult{};
  std::optional<EditorUvRegenerationCommandResult>
      fallbackUvExtentAdoption{};
  std::int32_t fallbackSourceIndex{0};
  std::int32_t fallbackSemanticIndex{0};
  std::int32_t fallbackEncoderIndex{0};
  std::int32_t fallbackStorageIndex{0};
  std::int32_t fallbackColormapIndex{0};
  std::int32_t fallbackNormalSpaceIndex{0};
  std::uint32_t fallbackAdditionalConsumerMask{0u};
  std::int32_t fallbackWidth{static_cast<std::int32_t>(model.DefaultWidth)};
  std::int32_t fallbackHeight{static_cast<std::int32_t>(model.DefaultHeight)};
  std::int32_t fallbackPadding{2};
  std::int32_t fallbackUvResolution{1024};
  std::int32_t fallbackUvPadding{2};
  float fallbackUvTexelsPerUnit{0.0f};
  bool fallbackUvForceRegenerate{true};
  bool fallbackUvPreserveAuthored{false};

  auto *lastUvRegenerationResult =
      state != nullptr && state->LastUvRegenerationResult != nullptr
          ? state->LastUvRegenerationResult
          : &fallbackUvRegenerationResult;
  auto *lastUvExtentAdoption =
      state != nullptr && state->LastUvExtentAdoption != nullptr
          ? state->LastUvExtentAdoption
          : &fallbackUvExtentAdoption;
  std::int32_t &sourceIndex = state != nullptr && state->SourceIndex != nullptr
                                  ? *state->SourceIndex
                                  : fallbackSourceIndex;
  std::int32_t &semanticIndex =
      state != nullptr && state->TargetSemanticIndex != nullptr
          ? *state->TargetSemanticIndex
          : fallbackSemanticIndex;
  std::int32_t &encoderIndex =
      state != nullptr && state->EncoderIndex != nullptr ? *state->EncoderIndex
                                                         : fallbackEncoderIndex;
  std::int32_t &storageIndex =
      state != nullptr && state->StorageIndex != nullptr ? *state->StorageIndex
                                                         : fallbackStorageIndex;
  std::int32_t &colormapIndex =
      state != nullptr && state->ColormapIndex != nullptr
          ? *state->ColormapIndex
          : fallbackColormapIndex;
  std::int32_t &normalSpaceIndex =
      state != nullptr && state->NormalSpaceIndex != nullptr
          ? *state->NormalSpaceIndex
          : fallbackNormalSpaceIndex;
  std::uint32_t &additionalConsumerMask =
      state != nullptr && state->AdditionalConsumerMask != nullptr
          ? *state->AdditionalConsumerMask
          : fallbackAdditionalConsumerMask;
  std::int32_t &bakeWidth = state != nullptr && state->Width != nullptr
                                ? *state->Width
                                : fallbackWidth;
  std::int32_t &bakeHeight = state != nullptr && state->Height != nullptr
                                 ? *state->Height
                                 : fallbackHeight;
  std::int32_t &bakePadding = state != nullptr && state->Padding != nullptr
                                  ? *state->Padding
                                  : fallbackPadding;
  std::int32_t &uvResolution =
      state != nullptr && state->UvResolution != nullptr ? *state->UvResolution
                                                         : fallbackUvResolution;
  std::int32_t &uvPadding = state != nullptr && state->UvPadding != nullptr
                                ? *state->UvPadding
                                : fallbackUvPadding;
  float &uvTexelsPerUnit = state != nullptr && state->UvTexelsPerUnit != nullptr
                               ? *state->UvTexelsPerUnit
                               : fallbackUvTexelsPerUnit;
  bool &uvForceRegenerate =
      state != nullptr && state->UvForceRegenerate != nullptr
          ? *state->UvForceRegenerate
          : fallbackUvForceRegenerate;
  bool &uvPreserveAuthored =
      state != nullptr && state->UvPreserveAuthored != nullptr
          ? *state->UvPreserveAuthored
          : fallbackUvPreserveAuthored;

  semanticIndex = std::clamp<std::int32_t>(
      semanticIndex, 0,
      static_cast<std::int32_t>(kTextureBakeTargetSemantics.size() - 1u));
  encoderIndex = std::clamp<std::int32_t>(
      encoderIndex, 0,
      static_cast<std::int32_t>(kTextureBakeEncoders.size() - 1u));
  storageIndex = std::clamp<std::int32_t>(
      storageIndex, 0,
      static_cast<std::int32_t>(kTextureBakeStorageModes.size() - 1u));
  colormapIndex = std::clamp<std::int32_t>(
      colormapIndex, 0,
      static_cast<std::int32_t>(kColormapNames.size() - 1u));
  normalSpaceIndex = std::clamp<std::int32_t>(
      normalSpaceIndex, 0,
      static_cast<std::int32_t>(kNormalSpaceNames.size() - 1u));
  bakeWidth = std::clamp<std::int32_t>(bakeWidth, 1, 8192);
  bakeHeight = std::clamp<std::int32_t>(bakeHeight, 1, 8192);
  uvResolution = std::clamp<std::int32_t>(uvResolution, 1, 16384);
  uvPadding = std::clamp<std::int32_t>(uvPadding, 0, uvResolution - 1);
  if (!std::isfinite(uvTexelsPerUnit) || uvTexelsPerUnit < 0.0f)
    uvTexelsPerUnit = 0.0f;

  ImGui::SeparatorText("UV / texture bake");
  ImGui::Text("UV: %s texcoords=%s count=%zu/%zu", model.Uv.Provenance.c_str(),
              model.Uv.HasTexcoords ? "yes" : "no", model.Uv.TexcoordCount,
              model.Uv.VertexCount);
  if (!model.Uv.LastFailure.empty())
    ImGui::TextDisabled("%s", model.Uv.LastFailure.c_str());
  if (!model.Uv.UvRegenerationAvailable)
    ImGui::TextDisabled("%s", model.Uv.UvRegenerationDisabledReason.c_str());

  ImGui::Checkbox("Force regenerate", &uvForceRegenerate);
  ImGui::SameLine();
  ImGui::Checkbox("Preserve valid authored", &uvPreserveAuthored);
  ImGui::InputInt("UV resolution", &uvResolution);
  ImGui::InputInt("UV padding", &uvPadding);
  ImGui::InputFloat("Texels per unit", &uvTexelsPerUnit, 0.0f, 0.0f, "%.3f");
  uvResolution = std::clamp<std::int32_t>(uvResolution, 1, 16384);
  uvPadding = std::clamp<std::int32_t>(uvPadding, 0, uvResolution - 1);
  if (!std::isfinite(uvTexelsPerUnit) || uvTexelsPerUnit < 0.0f)
    uvTexelsPerUnit = 0.0f;

  const bool canRegenerateUvs = model.Uv.UvRegenerationAvailable &&
                                context != nullptr &&
                                model.SelectedStableId != 0u;
  if (!canRegenerateUvs)
    ImGui::BeginDisabled();
  if (ImGui::Button("Regenerate UVs") && canRegenerateUvs) {
    *lastUvRegenerationResult = ApplyEditorUvRegenerationCommand(
        context->GeometryCommands, EditorUvRegenerationCommand{
                      .StableEntityId = model.SelectedStableId,
                      .PreserveValidAuthoredUvs = uvPreserveAuthored,
                      .ForceRegenerate = uvForceRegenerate,
                      .Resolution = static_cast<std::uint32_t>(uvResolution),
                      .Padding = static_cast<std::uint32_t>(uvPadding),
                      .TexelsPerUnit = uvTexelsPerUnit,
                  });
  }
  if (lastUvRegenerationResult->has_value()) {
    if (!lastUvRegenerationResult->value().Succeeded()) {
      lastUvExtentAdoption->reset();
    } else if (!lastUvExtentAdoption->has_value()) {
      bakeWidth = std::clamp<std::int32_t>(
          static_cast<std::int32_t>(lastUvRegenerationResult->value().AtlasWidth),
          1, 8192);
      bakeHeight = std::clamp<std::int32_t>(
          static_cast<std::int32_t>(lastUvRegenerationResult->value().AtlasHeight),
          1, 8192);
      bakePadding = std::clamp<std::int32_t>(uvPadding, 0, 32);
      *lastUvExtentAdoption = lastUvRegenerationResult->value();
    }
  }
  if (!canRegenerateUvs)
    ImGui::EndDisabled();
  DrawUvRegenerationStatus(model.Uv, *lastUvRegenerationResult);

  std::vector<std::size_t> bakeableIndices;
  bakeableIndices.reserve(model.Sources.size());
  for (std::size_t i = 0u; i < model.Sources.size(); ++i) {
    if (model.Sources[i].Bakeable)
      bakeableIndices.push_back(i);
  }
  if (bakeableIndices.empty())
    sourceIndex = 0;
  else
    sourceIndex = std::clamp<std::int32_t>(
        sourceIndex, 0, static_cast<std::int32_t>(bakeableIndices.size() - 1u));

  const EditorTextureBakeSourceRow *selectedSource =
      bakeableIndices.empty()
          ? nullptr
          : &model.Sources[bakeableIndices[static_cast<std::size_t>(
                sourceIndex)]];

  if (ImGui::BeginCombo("Bake source", selectedSource != nullptr
                                           ? selectedSource->Name.c_str()
                                           : "none")) {
    for (std::size_t i = 0u; i < bakeableIndices.size(); ++i) {
      const EditorTextureBakeSourceRow &row =
          model.Sources[bakeableIndices[i]];
      const bool selected = sourceIndex == static_cast<std::int32_t>(i);
      if (ImGui::Selectable(row.Name.c_str(), selected))
        sourceIndex = static_cast<std::int32_t>(i);
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  const auto targetCompatible =
      [selectedSource, storageIndex, encoderIndex, colormapIndex](
          const GeometryPresentationSlotSemantic semantic) {
        if (selectedSource == nullptr)
          return false;
        const std::array<EditorTextureBakeTarget, 1> target{{
        EditorTextureBakeTarget{
                .PresentationKey = "mesh.surface",
                .Semantic = semantic,
                .Colormap = static_cast<ColormapType>(colormapIndex),
            },
        }};
        const PropertyTextureBakeRepresentation representation =
        ResolveEditorTextureBakeTargetRepresentation(
                selectedSource->ResolvedExpectedValueKind(),
                kTextureBakeStorageModes[static_cast<std::size_t>(storageIndex)],
                kTextureBakeEncoders[static_cast<std::size_t>(encoderIndex)],
            target);
        return IsEditorTextureBakeTargetCompatible(
        target.front(), selectedSource->ResolvedExpectedValueKind(),
            representation.Storage, representation.Encoding);
      };

  if (ImGui::BeginCombo(
          "Target",
          std::string(
              ToString(kTextureBakeTargetSemantics[static_cast<std::size_t>(
                  semanticIndex)]))
              .c_str())) {
    for (std::size_t i = 0u; i < kTextureBakeTargetSemantics.size(); ++i) {
      const std::string label{ToString(kTextureBakeTargetSemantics[i])};
      const bool selected = semanticIndex == static_cast<std::int32_t>(i);
      const bool compatible = targetCompatible(kTextureBakeTargetSemantics[i]);
      if (!compatible)
        ImGui::BeginDisabled();
      if (ImGui::Selectable(label.c_str(), selected))
        semanticIndex = static_cast<std::int32_t>(i);
      if (!compatible)
        ImGui::EndDisabled();
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  if (ImGui::BeginCombo(
          "Encoder",
          DebugNameForTextureBakeEncoder(
              kTextureBakeEncoders[static_cast<std::size_t>(encoderIndex)]))) {
    for (std::size_t i = 0u; i < kTextureBakeEncoders.size(); ++i) {
      const bool selected = encoderIndex == static_cast<std::int32_t>(i);
      if (ImGui::Selectable(
              DebugNameForTextureBakeEncoder(kTextureBakeEncoders[i]),
              selected)) {
        encoderIndex = static_cast<std::int32_t>(i);
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  (void)ImGui::Combo("Storage", &storageIndex,
                     kTextureBakeStorageNames.data(),
                     static_cast<int>(kTextureBakeStorageNames.size()));
  (void)ImGui::Combo("Texture colormap", &colormapIndex,
                     kColormapNames.data(),
                     static_cast<int>(kColormapNames.size()));

  const auto makeTarget =
      [colormapIndex](const GeometryPresentationSlotSemantic semantic) {
        return EditorTextureBakeTarget{
            .PresentationKey = "mesh.surface",
            .Semantic = semantic,
            .Colormap = static_cast<ColormapType>(colormapIndex),
        };
      };
  const auto targetsCompatible =
      [selectedSource, storageIndex, encoderIndex](
          const std::vector<EditorTextureBakeTarget> &values) {
        if (selectedSource == nullptr)
          return false;
        const PropertyTextureBakeRepresentation representation =
            ResolveEditorTextureBakeTargetRepresentation(
                selectedSource->ResolvedExpectedValueKind(),
                kTextureBakeStorageModes[static_cast<std::size_t>(storageIndex)],
                kTextureBakeEncoders[static_cast<std::size_t>(encoderIndex)],
                values);
        return std::ranges::all_of(
            values, [&](const EditorTextureBakeTarget &target) {
              return IsEditorTextureBakeTargetCompatible(
                  target, selectedSource->ResolvedExpectedValueKind(),
                  representation.Storage, representation.Encoding);
            });
      };

  std::vector<EditorTextureBakeTarget> consumers{makeTarget(kTextureBakeTargetSemantics[
          static_cast<std::size_t>(semanticIndex)])};
  for (std::size_t i = 0u; i < kTextureBakeTargetSemantics.size(); ++i) {
    if (i == static_cast<std::size_t>(semanticIndex))
      continue;
    const std::uint32_t bit = 1u << static_cast<std::uint32_t>(i);
    if ((additionalConsumerMask & bit) != 0u)
      consumers.push_back(makeTarget(kTextureBakeTargetSemantics[i]));
  }

  ImGui::TextUnformatted("Additional consumers");
  for (std::size_t i = 0u; i < kTextureBakeTargetSemantics.size(); ++i) {
    if (i == static_cast<std::size_t>(semanticIndex))
      continue;
    const GeometryPresentationSlotSemantic semantic = kTextureBakeTargetSemantics[i];
    const std::uint32_t bit = 1u << static_cast<std::uint32_t>(i);
    bool selected = (additionalConsumerMask & bit) != 0u;
    std::vector<EditorTextureBakeTarget> candidate = consumers;
    if (!selected)
      candidate.push_back(makeTarget(semantic));
    const bool compatible = targetsCompatible(candidate);
    if (selected && !compatible) {
      additionalConsumerMask &= ~bit;
      std::erase_if(consumers,
                    [semantic](const EditorTextureBakeTarget &consumer) {
                      return consumer.Semantic == semantic;
                    });
      selected = false;
    }
    const std::string label{"Also bind to " + std::string(ToString(semantic))};
    if (!compatible)
      ImGui::BeginDisabled();
    if (ImGui::Checkbox(label.c_str(), &selected)) {
      if (selected) {
        additionalConsumerMask |= bit;
        consumers.push_back(makeTarget(semantic));
      } else {
        additionalConsumerMask &= ~bit;
        std::erase_if(
            consumers,
            [semantic](const EditorTextureBakeTarget &consumer) {
              return consumer.Semantic == semantic;
            });
      }
    }
    if (!compatible)
      ImGui::EndDisabled();
  }

  const bool hasNormalConsumer = std::ranges::any_of(
      consumers, [](const EditorTextureBakeTarget &consumer) {
        return consumer.Semantic == GeometryPresentationSlotSemantic::Normal;
      });
  if (hasNormalConsumer) {
    (void)ImGui::Combo("Normal texture space", &normalSpaceIndex,
                       kNormalSpaceNames.data(),
                       static_cast<int>(kNormalSpaceNames.size()));
  }
  const PropertyTextureBakeRepresentation resolvedRepresentation =
      selectedSource != nullptr
          ? ResolveEditorTextureBakeTargetRepresentation(
                selectedSource->ResolvedExpectedValueKind(),
                kTextureBakeStorageModes[static_cast<std::size_t>(storageIndex)],
                kTextureBakeEncoders[static_cast<std::size_t>(encoderIndex)],
                consumers)
          : PropertyTextureBakeRepresentation{};
  const bool paddingSupported =
      resolvedRepresentation.Storage == PropertyTextureBakeStorage::EncodedRgba;
  ImGui::InputInt("Bake width", &bakeWidth);
  ImGui::InputInt("Bake height", &bakeHeight);
  if (!paddingSupported)
    ImGui::BeginDisabled();
  ImGui::InputInt("Bake padding", &bakePadding);
  if (!paddingSupported)
    ImGui::EndDisabled();
  bakeWidth = std::clamp<std::int32_t>(bakeWidth, 1, 8192);
  bakeHeight = std::clamp<std::int32_t>(bakeHeight, 1, 8192);
  bakePadding = std::clamp<std::int32_t>(bakePadding, 0, 32);

  const bool canBake =
      model.CanBake && context != nullptr && selectedSource != nullptr &&
      targetsCompatible(consumers);
  if (!canBake)
    ImGui::BeginDisabled();
  if (ImGui::Button("Bake") && canBake) {
    (void)ApplyEditorTextureBakeCommand(
        context->VisualizationCommands,
        EditorTextureBakeCommand{
            .StableEntityId = model.SelectedStableId,
            .TargetSemantic =
                kTextureBakeTargetSemantics[static_cast<std::size_t>(
                    semanticIndex)],
            .SourceDomain = selectedSource->BakeDomain,
            .ExpectedValueKind = selectedSource->ResolvedExpectedValueKind(),
            .PropertyName = selectedSource->Name,
            .Encoder =
                kTextureBakeEncoders[static_cast<std::size_t>(encoderIndex)],
            .Width = static_cast<std::uint32_t>(bakeWidth),
            .Height = static_cast<std::uint32_t>(bakeHeight),
            .PaddingTexels = paddingSupported
                                 ? static_cast<std::uint32_t>(bakePadding)
                                 : 0u,
            .GeneratedKey = selectedSource->Name,
            .Storage = kTextureBakeStorageModes[
                static_cast<std::size_t>(storageIndex)],
            .EncodingColormap = static_cast<ColormapType>(colormapIndex),
            .NormalSpace = normalSpaceIndex == 1
                               ? GeometryPresentationNormalSpace::World
                               : GeometryPresentationNormalSpace::Object,
            .Targets = consumers,
            .BindGeneratedTexture = true,
        });
  }
  if (!canBake) {
    ImGui::EndDisabled();
    if (!model.DisabledReason.empty())
      ImGui::TextDisabled("%s", model.DisabledReason.c_str());
  }

  ImGui::SeparatorText("Baked textures");
  if (model.BakedTextures.empty())
    ImGui::TextDisabled("No baked property textures on this entity.");
  static std::string renameTarget{};
  static std::array<char, 128> renameBuffer{};
  static std::string mutationDiagnostic{};
  for (const PropertyTextureBakeRecord &record : model.BakedTextures) {
    ImGui::PushID(record.OutputName.c_str());
    if (ImGui::CollapsingHeader(record.OutputName.c_str(),
                                ImGuiTreeNodeFlags_DefaultOpen)) {
      const char *stateName = "pending";
      if (record.State == PropertyTextureBakeOutputState::Ready)
        stateName = "ready";
      else if (record.State == PropertyTextureBakeOutputState::Failed)
        stateName = "failed";
      const char *storageName =
          record.Storage == PropertyTextureBakeStorage::RawFloat
              ? "raw float"
              : record.Storage == PropertyTextureBakeStorage::EncodedRgba
                    ? "encoded RGBA"
                    : "auto";
      ImGui::Text("%s | %s | %ux%u", stateName, storageName, record.Width,
                  record.Height);
      ImGui::Text("Source: %s  range=[%.6g, %.6g]",
                  record.Source.Name.c_str(), record.RangeMin,
                  record.RangeMax);
      if (record.Encoding == PropertyTextureBakeEncoding::ScalarColormap) {
        const std::size_t mapIndex = std::min<std::size_t>(
            static_cast<std::size_t>(record.EncodingColormap),
            kColormapNames.size() - 1u);
        ImGui::Text("Baked colormap: %s", kColormapNames[mapIndex]);
      }
      const std::span<const EditorTextureBakeTarget> existingConsumers =
          TextureBakeTargetsFor(model, record.OutputName);
      if (record.Encoding == PropertyTextureBakeEncoding::Normal) {
        const auto normalConsumer = std::ranges::find(
            existingConsumers, GeometryPresentationSlotSemantic::Normal,
            &EditorTextureBakeTarget::Semantic);
        ImGui::Text("Normal space: %s",
                    normalConsumer != existingConsumers.end() &&
                            normalConsumer->NormalSpace ==
                                GeometryPresentationNormalSpace::World
                        ? "world"
                        : "object");
      }
      if (!record.Diagnostic.empty())
        ImGui::TextDisabled("%s", record.Diagnostic.c_str());

      if (ImGui::SmallButton("Rename")) {
        renameTarget = record.OutputName;
        renameBuffer.fill('\0');
        const std::size_t count = std::min(
            renameTarget.size(), renameBuffer.size() - 1u);
        std::copy_n(renameTarget.data(), count, renameBuffer.data());
        ImGui::OpenPopup("Rename baked texture");
      }
      if (context != nullptr && renameTarget == record.OutputName &&
          ImGui::BeginPopup("Rename baked texture")) {
        ImGui::InputText("Name", renameBuffer.data(), renameBuffer.size());
        if (ImGui::Button("Apply")) {
          const TextureBakeMutationResult result =
              RenameEditorBakedTexture(
                  context->VisualizationCommands, model.SelectedStableId, record.OutputName,
                  std::string_view{renameBuffer.data()});
          mutationDiagnostic = result.Diagnostic;
          if (result.Succeeded())
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
      ImGui::SameLine();
      if (context != nullptr && ImGui::SmallButton("Remove")) {
        const TextureBakeMutationResult result =
            RemoveEditorBakedTexture(
                context->VisualizationCommands, model.SelectedStableId, record.OutputName);
        mutationDiagnostic = result.Diagnostic;
      }

      std::vector<EditorTextureBakeTarget> nextConsumers{
          existingConsumers.begin(), existingConsumers.end()};
      bool consumersChanged = false;
      for (std::size_t i = 0u; i < kTextureBakeTargetSemantics.size(); ++i) {
        const GeometryPresentationSlotSemantic semantic =
            kTextureBakeTargetSemantics[i];
        const auto found = std::find_if(
            nextConsumers.begin(), nextConsumers.end(),
            [semantic](const EditorTextureBakeTarget &consumer) {
              return consumer.Semantic == semantic;
            });
        bool enabled = found != nextConsumers.end();
        const bool compatible = IsEditorTextureBakeTargetCompatible(
            EditorTextureBakeTarget{
                .PresentationKey = "mesh.surface",
                .Semantic = semantic,
            },
            record.Source.ValueKind, record.Storage, record.Encoding);
        const std::string label{
            "Consume as " + std::string(ToString(semantic))};
        const bool disableConsumer = !compatible && !enabled;
        if (disableConsumer)
          ImGui::BeginDisabled();
        if (ImGui::Checkbox(label.c_str(), &enabled)) {
          consumersChanged = true;
          if (enabled) {
            nextConsumers.push_back(EditorTextureBakeTarget{
                .PresentationKey = "mesh.surface",
                .Semantic = semantic,
                .Colormap = static_cast<ColormapType>(colormapIndex),
            });
          } else {
            std::erase_if(
                nextConsumers,
                [semantic](const EditorTextureBakeTarget &consumer) {
                  return consumer.Semantic == semantic;
                });
          }
        }
        if (disableConsumer)
          ImGui::EndDisabled();
      }

      const bool rawScalar =
          record.Storage == PropertyTextureBakeStorage::RawFloat &&
          (record.Source.ValueKind == Geometry::PropertyValueKind::Float ||
           record.Source.ValueKind == Geometry::PropertyValueKind::Double);
      if (rawScalar) {
        int recordColormap = 0;
        for (const EditorTextureBakeTarget &consumer : nextConsumers) {
          if (consumer.Semantic == GeometryPresentationSlotSemantic::Albedo ||
              consumer.Semantic == GeometryPresentationSlotSemantic::ScalarField) {
            recordColormap = static_cast<int>(consumer.Colormap);
            break;
          }
        }
        recordColormap = std::clamp(
            recordColormap, 0,
            static_cast<int>(kColormapNames.size() - 1u));
        if (ImGui::Combo("Render colormap", &recordColormap,
                         kColormapNames.data(),
                         static_cast<int>(kColormapNames.size()))) {
          consumersChanged = true;
          for (EditorTextureBakeTarget &consumer : nextConsumers) {
            if (consumer.Semantic == GeometryPresentationSlotSemantic::Albedo ||
                consumer.Semantic == GeometryPresentationSlotSemantic::ScalarField) {
              consumer.Colormap =
                  static_cast<ColormapType>(recordColormap);
            }
          }
        }
      }
      if (context != nullptr && consumersChanged) {
        const TextureBakeMutationResult result =
            SetEditorBakedTextureTargets(
                context->VisualizationCommands, EditorTextureBakeTargetUpdateRequest{
                              .StableEntityId = model.SelectedStableId,
                              .OutputName = record.OutputName,
                              .Targets = std::move(nextConsumers),
                          });
        mutationDiagnostic = result.Diagnostic;
      }
    }
    ImGui::PopID();
  }
  if (!mutationDiagnostic.empty())
    ImGui::TextDisabled("%s", mutationDiagnostic.c_str());

  if (ImGui::BeginTable("TextureBakeSources", 5,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Property");
    ImGui::TableSetupColumn("Domain");
    ImGui::TableSetupColumn("Kind");
    ImGui::TableSetupColumn("Bake");
    ImGui::TableSetupColumn("Reason");
    ImGui::TableHeadersRow();

    const std::size_t limit = std::min<std::size_t>(model.Sources.size(), 12u);
    for (std::size_t i = 0u; i < limit; ++i) {
      const EditorTextureBakeSourceRow &row = model.Sources[i];
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(row.Name.c_str());
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(
          DebugNameForEditorPropertyCatalogDomain(row.CatalogDomain));
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(
          DebugNameForGeometryPropertyValueKind(row.ValueKind));
      ImGui::TableSetColumnIndex(3);
      ImGui::TextUnformatted(row.Bakeable ? "yes" : "no");
      ImGui::TableSetColumnIndex(4);
      ImGui::TextDisabled(
          "%s", row.DisabledReason.empty() ? "-" : row.DisabledReason.c_str());
    }
    ImGui::EndTable();
  }
  DrawDiagnostics(model.Diagnostics);
}

// Properties is an exhaustive explorer: internal, connectivity, and generated
// rows remain visible, with unsupported actions diagnosed rather than hidden.
// Render, binding, and bake controls belong to Appearance.
void DrawDomainPropertyWindow(const EditorDomainWindowModel &model) {
  DrawDomainWindowHeader(model);
  if (!DomainWindowReady(model))
    return;
  DrawPropertyCatalogRows(model.PropertyCatalog);
  DrawDiagnostics(model.PropertyCatalog.Diagnostics);
}

void DrawRenderHintStatus(const EditorRenderHintModel &hints) {
  ImGui::Text("Surface: %s",
              hints.HasRenderSurface ? hints.SurfaceDomain.c_str() : "none");
  if (hints.HasRenderEdges) {
    ImGui::Text("Edges: %s", hints.EdgeDomain.c_str());
    if (hints.HasUniformEdgeWidth)
      ImGui::Text("Edge width: %.3f", hints.UniformEdgeWidth);
    if (hints.HasNamedEdgeWidth)
      ImGui::Text("Edge width source: %s", hints.EdgeWidthName.c_str());
  } else {
    ImGui::TextDisabled("Edges: none");
  }

  if (hints.HasRenderPoints) {
    ImGui::Text("Points: %s", hints.PointRenderType.c_str());
    if (hints.HasUniformPointSize)
      ImGui::Text("Point size: %.3f", hints.UniformPointSize);
    if (hints.HasNamedPointSize)
      ImGui::Text("Point size source: %s", hints.PointSizeName.c_str());
  } else {
    ImGui::TextDisabled("Points: none");
  }
}

[[nodiscard]] bool DrawSurfaceDomainCombo(SurfaceDomain *domain) {
  constexpr const char *kItems[]{"Vertex", "Face"};
  int current = *domain == kSurfaceFace ? 1 : 0;
  if (!ImGui::Combo("Surface domain", &current, kItems, 2))
    return false;
  *domain = current == 1 ? kSurfaceFace : kSurfaceVertex;
  return true;
}

[[nodiscard]] bool DrawEdgeDomainCombo(EdgeDomain *domain) {
  constexpr const char *kItems[]{"Vertex", "Edge"};
  int current = *domain == kEdgeEdge ? 1 : 0;
  if (!ImGui::Combo("Edge domain", &current, kItems, 2))
    return false;
  *domain = current == 1 ? kEdgeEdge : kEdgeVertex;
  return true;
}

[[nodiscard]] bool DrawPointTypeCombo(PointRenderType *type) {
  constexpr const char *kItems[]{"Flat", "Sphere", "Surfel"};
  int current = 1;
  switch (*type) {
  case kPointFlat:
    current = 0;
    break;
  case kPointSphere:
    current = 1;
    break;
  case kPointSurfel:
    current = 2;
    break;
  }
  if (!ImGui::Combo("Point type", &current, kItems, 3))
    return false;
  switch (current) {
  case 0:
    *type = kPointFlat;
    break;
  case 2:
    *type = kPointSurfel;
    break;
  case 1:
  default:
    *type = kPointSphere;
    break;
  }
  return true;
}

void DrawPointRenderHintControls(const EditorDomainWindowModel &model,
                                 const SandboxEditorContext &context,
                                 bool canEditRenderHints);

void DrawEdgeRenderHintControls(const EditorDomainWindowModel &model,
                                const SandboxEditorContext &context,
                                const bool canEditRenderHints) {
  bool edges = model.RenderHints.HasRenderEdges;
  if (ImGui::Checkbox("Edges", &edges) && canEditRenderHints) {
    (void)ApplyEditorRenderHintCommand(
        context.VisualizationCommands, EditorRenderHintCommand{
                     .StableEntityId = model.SelectedStableId,
                     .SetEdges = true,
                     .EnableEdges = edges,
                     .EdgeDomain = model.RenderHints.EdgeDomainValue,
                 });
  }

  if (!model.RenderHints.HasRenderEdges)
    return;

  EdgeDomain edgeDomain = model.RenderHints.EdgeDomainValue;
  if (DrawEdgeDomainCombo(&edgeDomain) && canEditRenderHints) {
    (void)ApplyEditorRenderHintCommand(
        context.VisualizationCommands, EditorRenderHintCommand{
                     .StableEntityId = model.SelectedStableId,
                     .SetEdges = true,
                     .EnableEdges = true,
                     .EdgeDomain = edgeDomain,
                 });
  }

  if (model.RenderHints.HasUniformEdgeWidth) {
    float edgeWidth = model.RenderHints.UniformEdgeWidth;
    if (ImGui::DragFloat("Edge width", &edgeWidth, 0.05f, 0.1f, 32.0f) &&
        canEditRenderHints) {
      (void)ApplyEditorRenderHintCommand(
          context.VisualizationCommands, EditorRenderHintCommand{
                       .StableEntityId = model.SelectedStableId,
                       .SetUniformEdgeWidth = true,
                       .UniformEdgeWidth = edgeWidth,
                   });
    }
  }
}

void DrawMeshRenderHintControls(const EditorDomainWindowModel &model,
                                const SandboxEditorContext &context,
                                const bool canEditRenderHints) {
  bool surface = model.RenderHints.HasRenderSurface;
  if (ImGui::Checkbox("Surface", &surface) && canEditRenderHints) {
    (void)ApplyEditorRenderHintCommand(
        context.VisualizationCommands, EditorRenderHintCommand{
                     .StableEntityId = model.SelectedStableId,
                     .SetSurface = true,
                     .EnableSurface = surface,
                     .SurfaceDomain = model.RenderHints.SurfaceDomainValue,
                 });
  }

  if (model.RenderHints.HasRenderSurface) {
    SurfaceDomain domain = model.RenderHints.SurfaceDomainValue;
    if (DrawSurfaceDomainCombo(&domain) && canEditRenderHints) {
      (void)ApplyEditorRenderHintCommand(
          context.VisualizationCommands, EditorRenderHintCommand{
                       .StableEntityId = model.SelectedStableId,
                       .SetSurface = true,
                       .EnableSurface = true,
                       .SurfaceDomain = domain,
                   });
    }
  }

  DrawEdgeRenderHintControls(model, context, canEditRenderHints);
  DrawPointRenderHintControls(model, context, canEditRenderHints);
}

void DrawPointRenderHintControls(const EditorDomainWindowModel &model,
                                 const SandboxEditorContext &context,
                                 const bool canEditRenderHints) {
  bool points = model.RenderHints.HasRenderPoints;
  if (ImGui::Checkbox("Points", &points) && canEditRenderHints) {
    (void)ApplyEditorRenderHintCommand(
        context.VisualizationCommands, EditorRenderHintCommand{
                     .StableEntityId = model.SelectedStableId,
                     .SetPoints = true,
                     .EnablePoints = points,
                     .PointType = model.RenderHints.PointRenderTypeValue,
                 });
  }

  if (!model.RenderHints.HasRenderPoints)
    return;

  PointRenderType pointType = model.RenderHints.PointRenderTypeValue;
  if (DrawPointTypeCombo(&pointType) && canEditRenderHints) {
    (void)ApplyEditorRenderHintCommand(
        context.VisualizationCommands, EditorRenderHintCommand{
                     .StableEntityId = model.SelectedStableId,
                     .PointType = pointType,
                     .SetPointRenderType = true,
                 });
  }

  if (model.RenderHints.HasUniformPointSize) {
    float pointSize = model.RenderHints.UniformPointSize;
    if (ImGui::DragFloat("Point size", &pointSize, 0.05f, 0.5f, 32.0f) &&
        canEditRenderHints) {
      (void)ApplyEditorRenderHintCommand(
          context.VisualizationCommands, EditorRenderHintCommand{
                       .StableEntityId = model.SelectedStableId,
                       .SetUniformPointSize = true,
                       .UniformPointSize = pointSize,
                   });
    }
  }
}

void DrawGraphRenderHintControls(const EditorDomainWindowModel &model,
                                 const SandboxEditorContext &context,
                                 const bool canEditRenderHints) {
  DrawEdgeRenderHintControls(model, context, canEditRenderHints);
  DrawPointRenderHintControls(model, context, canEditRenderHints);
}

void DrawVisualizationPropertyPresets(
    const std::vector<EditorVisualizationPropertyInfo> &properties,
    const EditorVisualizationConfigModel &visualization,
    const SandboxEditorContext &context, const std::uint32_t selectedStableId,
    const EditorVisualizationTarget target,
    const bool canEditVisualization) {
  ImGui::SeparatorText("Properties");
  if (properties.empty()) {
    ImGui::TextDisabled("No visualization-eligible properties.");
    return;
  }

  if (!canEditVisualization)
    ImGui::BeginDisabled();

  // Reapplying a preset preserves the target's tuned range and binning.
  const bool scalarAutoRange =
      visualization.HasConfig ? visualization.ScalarAutoRange : true;
  const float scalarRangeMin =
      visualization.HasConfig ? visualization.ScalarRangeMin : 0.0f;
  const float scalarRangeMax =
      visualization.HasConfig ? visualization.ScalarRangeMax : 1.0f;
  const std::uint32_t scalarBinCount =
      visualization.HasConfig ? visualization.ScalarBinCount : 0u;

  for (std::size_t i = 0u; i < properties.size(); ++i) {
    const EditorVisualizationPropertyInfo &property = properties[i];
    ImGui::PushID(static_cast<int>(i));
    ImGui::Text(
        "%s  [%s, %s, %llu]", property.Name.c_str(),
        DebugNameForEditorVisualizationPropertyDomain(property.Domain),
        DebugNameForGeometryPropertyValueKind(property.ValueKind),
        static_cast<unsigned long long>(property.ElementCount));

    bool wroteButton = false;
    if (property.ScalarPresetAvailable) {
      if (ImGui::SmallButton("Scalar") && canEditVisualization) {
        (void)ApplyEditorVisualizationPropertyCommand(
            context.VisualizationCommands,
            EditorVisualizationPropertyCommand{
                .StableEntityId = selectedStableId,
                .Target = target,
                .Domain = property.Domain,
                .Preset = EditorVisualizationPropertyPreset::Scalar,
                .PropertyName = property.Name,
                .ScalarAutoRange = scalarAutoRange,
                .ScalarRangeMin = scalarRangeMin,
                .ScalarRangeMax = scalarRangeMax,
                .ScalarBinCount = scalarBinCount,
            });
      }
      wroteButton = true;
    }
    if (property.IsolinePresetAvailable) {
      if (wroteButton)
        ImGui::SameLine();
      if (ImGui::SmallButton("Isolines") && canEditVisualization) {
        (void)ApplyEditorVisualizationPropertyCommand(
            context.VisualizationCommands,
            EditorVisualizationPropertyCommand{
                .StableEntityId = selectedStableId,
                .Target = target,
                .Domain = property.Domain,
                .Preset = EditorVisualizationPropertyPreset::Isoline,
                .PropertyName = property.Name,
                .ScalarAutoRange = scalarAutoRange,
                .ScalarRangeMin = scalarRangeMin,
                .ScalarRangeMax = scalarRangeMax,
                .ScalarBinCount = scalarBinCount,
                .IsolineCount = 12u,
            });
      }
      wroteButton = true;
    }
    if (property.ColorBufferPresetAvailable) {
      if (wroteButton)
        ImGui::SameLine();
      if (ImGui::SmallButton("Color buffer") && canEditVisualization) {
        (void)ApplyEditorVisualizationPropertyCommand(
            context.VisualizationCommands,
            EditorVisualizationPropertyCommand{
                .StableEntityId = selectedStableId,
                .Target = target,
                .Domain = property.Domain,
                .Preset = EditorVisualizationPropertyPreset::ColorBuffer,
                .PropertyName = property.Name,
            });
      }
      wroteButton = true;
    }
    if (property.VectorFieldCandidate && !wroteButton) {
      ImGui::TextDisabled("Vector-field candidate; adapter residency is not "
                          "owned by this UI slice.");
    }
    ImGui::PopID();
  }

  if (!canEditVisualization)
    ImGui::EndDisabled();
}

void DrawUniformVisualizationColorEdit(
    const EditorVisualizationConfigModel &visualization,
    const SandboxEditorContext &context, const std::uint32_t selectedStableId,
    const EditorVisualizationTarget target,
    const bool canEditVisualization) {
  if (!visualization.HasConfig || visualization.Source != kUniformColorSource) {
    return;
  }

  glm::vec4 color = visualization.Color;
  if (ImGui::ColorEdit4("Color##uniform-visualization-color", &color.x) &&
      canEditVisualization) {
    (void)ApplyEditorVisualizationConfigCommand(
        context.VisualizationCommands, MakeUniformVisualizationConfigCommandFromModel(
                     selectedStableId, visualization, target, color));
  }
}

// Each edit reapplies the complete model-backed configuration so fields not
// represented by that control retain their values.
void DrawScalarVisualizationControls(
    const EditorVisualizationConfigModel &visualization,
    const SandboxEditorContext &context, const std::uint32_t selectedStableId,
    const EditorVisualizationTarget target,
    const bool canEditVisualization) {
  if (!visualization.HasConfig || visualization.Source != kScalarFieldSource) {
    return;
  }

  ImGui::SeparatorText("Scalar field");
  ImGui::Text("Property: %s", visualization.ScalarFieldName.empty()
                                  ? "<none>"
                                  : visualization.ScalarFieldName.c_str());

  const auto submit = [&](const EditorVisualizationConfigModel &next) {
    if (canEditVisualization) {
      (void)ApplyEditorVisualizationConfigCommand(
          context.VisualizationCommands, MakeScalarVisualizationConfigCommandFromModel(
                       selectedStableId, next, target));
    }
  };

  static constexpr std::array<const char *, 6> kColormapNames{
      "Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"};
  int colormapIndex = static_cast<int>(visualization.ScalarColormap);
  if (colormapIndex < 0 ||
      colormapIndex >= static_cast<int>(kColormapNames.size())) {
    colormapIndex = 0;
  }
  if (ImGui::Combo("Colormap", &colormapIndex, kColormapNames.data(),
                   static_cast<int>(kColormapNames.size()))) {
    EditorVisualizationConfigModel next = visualization;
    next.ScalarColormap = static_cast<ColormapType>(colormapIndex);
    submit(next);
  }

  bool autoRange = visualization.ScalarAutoRange;
  if (ImGui::Checkbox("Auto range", &autoRange)) {
    EditorVisualizationConfigModel next = visualization;
    next.ScalarAutoRange = autoRange;
    submit(next);
  }
  if (!visualization.ScalarAutoRange) {
    float rangeMinMax[2]{visualization.ScalarRangeMin,
                         visualization.ScalarRangeMax};
    if (ImGui::DragFloat2("Clamp min/max", rangeMinMax, 0.01f, 0.0f, 0.0f,
                          "%.5f") &&
        rangeMinMax[0] < rangeMinMax[1]) {
      EditorVisualizationConfigModel next = visualization;
      next.ScalarRangeMin = rangeMinMax[0];
      next.ScalarRangeMax = rangeMinMax[1];
      submit(next);
    }
  }

  int binCount = static_cast<int>(visualization.ScalarBinCount);
  if (ImGui::DragInt("Bins (0 = continuous)", &binCount, 0.25f, 0, 64) &&
      binCount >= 0) {
    EditorVisualizationConfigModel next = visualization;
    next.ScalarBinCount = static_cast<std::uint32_t>(binCount);
    submit(next);
  }

  ImGui::SeparatorText("Isolines");
  int isolineCount = static_cast<int>(visualization.IsolineCount);
  if (ImGui::DragInt("Count##isolines", &isolineCount, 0.25f, 0, 256) &&
      isolineCount >= 0) {
    EditorVisualizationConfigModel next = visualization;
    next.IsolineCount = static_cast<std::uint32_t>(isolineCount);
    submit(next);
  }
  float isolineWidth = visualization.IsolineWidth;
  if (ImGui::DragFloat("Width##isolines", &isolineWidth, 0.05f, 0.1f, 16.0f) &&
      isolineWidth > 0.0f) {
    EditorVisualizationConfigModel next = visualization;
    next.IsolineWidth = isolineWidth;
    submit(next);
  }
  glm::vec4 isolineColor = visualization.IsolineColor;
  if (ImGui::ColorEdit4("Color##isolines", &isolineColor.x)) {
    EditorVisualizationConfigModel next = visualization;
    next.IsolineColor = isolineColor;
    submit(next);
  }

  ImGui::TextUnformatted("Highlight isovalues");
  for (std::uint32_t i = 0u; i < visualization.IsolineValueCount; ++i) {
    ImGui::PushID(static_cast<int>(i));
    float value = visualization.IsolineValues[i];
    if (ImGui::DragFloat("##isovalue", &value, 0.001f, 0.0f, 0.0f, "%.5f")) {
      EditorVisualizationConfigModel next = visualization;
      next.IsolineValues[i] = value;
      submit(next);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Remove")) {
      EditorVisualizationConfigModel next = visualization;
      for (std::uint32_t j = i; j + 1u < next.IsolineValueCount; ++j) {
        next.IsolineValues[j] = next.IsolineValues[j + 1u];
      }
      next.IsolineValueCount -= 1u;
      submit(next);
    }
    ImGui::PopID();
  }
  if (visualization.IsolineValueCount < visualization.IsolineValues.size()) {
    if (ImGui::SmallButton("Add isovalue")) {
      EditorVisualizationConfigModel next = visualization;
      const float seed = visualization.ScalarAutoRange
                             ? 0.0f
                             : 0.5f * (visualization.ScalarRangeMin +
                                       visualization.ScalarRangeMax);
      next.IsolineValues[next.IsolineValueCount] = seed;
      next.IsolineValueCount += 1u;
      submit(next);
    }
  }
}

void DrawDomainVisualizationControls(
    const EditorDomainWindowModel &model,
    const SandboxEditorContext &context);

// Appearance owns render hints and state, visualization controls, property and
// attribute bindings, and texture baking.
void DrawDomainRenderWindow(const EditorDomainWindowModel &model,
                            const SandboxEditorContext &context,
                            TextureBakeUiState *textureBakeState) {
  DrawDomainWindowHeader(model);
  ImGui::SeparatorText("Render hint status");
  DrawRenderHintStatus(model.RenderHints);

  ImGui::SeparatorText("Render controls");
  const bool canEditRenderHints = DomainAppearanceReady(model);
  if (!canEditRenderHints)
    ImGui::BeginDisabled();
  switch (model.Kind) {
  case EditorDomainWindowKind::Mesh:
    DrawMeshRenderHintControls(model, context, canEditRenderHints);
    break;
  case EditorDomainWindowKind::Graph:
    DrawGraphRenderHintControls(model, context, canEditRenderHints);
    break;
  case EditorDomainWindowKind::PointCloud:
    DrawPointRenderHintControls(model, context, canEditRenderHints);
    break;
  }
  if (!canEditRenderHints)
    ImGui::EndDisabled();

  if (DomainAppearanceReady(model)) {
    ImGui::SeparatorText("Visualization");
    DrawDomainVisualizationControls(model, context);
    ImGui::SeparatorText("Bound render state");
    DrawBoundRenderStateRows(model.BoundState);
    ImGui::SeparatorText("Property / attribute assignment");
    DrawPropertyBindingTargets(model.PropertyCatalog);
    DrawVertexChannelBindingTargets(model.PropertyCatalog, &context);
    ImGui::SeparatorText("Texture baking");
    DrawTextureBakeControls(model.TextureBake, &context, textureBakeState);
  }
}

void DrawDomainVisualizationControls(
    const EditorDomainWindowModel &model,
    const SandboxEditorContext &context) {
  const EditorVisualizationModel &visualization = model.Visualization;

  if (visualization.Visualization.HasConfig) {
    ImGui::Text("Visualization: %s",
                DebugNameForEditorVisualizationColorSource(
                    visualization.Visualization.Source));
  } else {
    ImGui::TextDisabled("Visualization: material/default");
  }

  const bool canEditVisualization = model.VisualizationTargetAvailable &&
                                    model.VisualizationControlsAvailable;
  if (!canEditVisualization)
    ImGui::BeginDisabled();

  if (ImGui::Button("Uniform color") && canEditVisualization) {
    (void)ApplyEditorVisualizationConfigCommand(
        context.VisualizationCommands,
        MakeUniformVisualizationConfigCommandFromModel(
            model.SelectedStableId, visualization.Visualization,
            model.VisualizationTarget, visualization.Visualization.Color));
  }
  ImGui::SameLine();
  if (ImGui::Button("Clear vis") && canEditVisualization) {
    (void)ApplyEditorVisualizationConfigCommand(
        context.VisualizationCommands, EditorVisualizationConfigCommand{
                     .StableEntityId = model.SelectedStableId,
                     .Target = model.VisualizationTarget,
                     .EnableConfig = false,
                 });
  }

  DrawUniformVisualizationColorEdit(
      visualization.Visualization, context, model.SelectedStableId,
      model.VisualizationTarget, canEditVisualization);

  DrawScalarVisualizationControls(
      visualization.Visualization, context, model.SelectedStableId,
      model.VisualizationTarget, canEditVisualization);

  if (!canEditVisualization)
    ImGui::EndDisabled();

  DrawVisualizationPropertyPresets(
      visualization.Properties, visualization.Visualization, context,
      model.SelectedStableId, model.VisualizationTarget, canEditVisualization);
}

void DrawPrimitiveDetails(const EditorPrimitiveDetailModel &primitive) {
  if (!primitive.HasPrimitive) {
    ImGui::TextDisabled("No refined primitive selection for this domain.");
    return;
  }

  const PrimitiveSelectionResult &result = primitive.Primitive;
  ImGui::Text("Primitive status: %s",
              DebugNameForPrimitiveRefineStatus(result.Status));
  ImGui::Text("Primitive domain/kind: %s / %s",
              DebugNameForEditorGeometryDomain(result.Domain),
              DebugNameForEditorPrimitiveKind(result.Kind));
  if (primitive.HasFaceId)
    ImGui::Text("Face id: %u", result.FaceId);
  if (primitive.HasEdgeId)
    ImGui::Text("Edge id: %u", result.EdgeId);
  if (primitive.HasVertexId)
    ImGui::Text("Vertex id: %u", result.VertexId);
  if (primitive.HasPointId)
    ImGui::Text("Point id: %u", result.PointId);
  if (result.HasHitPosition) {
    DrawVec3("Local hit", result.LocalHit);
    DrawVec3("World hit", result.WorldHit);
  }
}

void DrawDomainSelectionWindow(const EditorDomainWindowModel &model) {
  DrawDomainWindowHeader(model);
  ImGui::SeparatorText("Primitive selection");
  DrawPrimitiveDetails(model.Primitive);
}

void DrawPointCloudOutlierRemovalResultStatus(
    const std::optional<EditorPointCloudOutlierRemovalResult>
        &lastResult) {
  if (!lastResult.has_value()) {
    ImGui::TextDisabled("Last outlier removal: none");
    return;
  }

  const EditorPointCloudOutlierRemovalResult &result = *lastResult;
  ImGui::Text("Last outlier removal: %s",
              DebugNameForEditorCommandStatus(result.Status));
  ImGui::Text("Method: %s",
              result.Method == EditorPointCloudOutlierMethod::Statistical
                  ? "Statistical"
                  : "Radius");
  // NoChange still means the kernel executed, so its counters remain relevant.
  if (result.Succeeded() ||
      result.Status == EditorCommandStatus::NoChange) {
    ImGui::Text("Kept %zu / %zu  rejected %zu  non-finite %zu",
                result.KeptCount, result.OriginalCount, result.RejectedCount,
                result.NonFiniteCount);
    if (result.Method == EditorPointCloudOutlierMethod::Statistical) {
      ImGui::Text("Mean %.4f  stddev %.4f  threshold %.4f",
                  static_cast<double>(result.MeanDistance),
                  static_cast<double>(result.StdDevDistance),
                  static_cast<double>(result.DistanceThreshold));
    }
  }
  if (!result.Message.empty())
    ImGui::TextWrapped("%s", result.Message.c_str());
}

void DrawPointCloudOutlierRemovalControls(
    const EditorDomainWindowModel &model,
    const SandboxEditorContext &context,
    const EditorGeometryProcessingModel &processing,
    PointCloudOutlierRemovalUiState *outlierState) {
  ImGui::SeparatorText("Remove Outliers");
  if (!processing.PointCloudOutlierRemovalAvailable) {
    ImGui::TextDisabled(
        "Point-cloud outlier removal is unavailable for this selection.");
    return;
  }
  if (outlierState == nullptr || outlierState->LastResult == nullptr ||
      outlierState->Method == nullptr || outlierState->KNeighbors == nullptr ||
      outlierState->StdDevMultiplier == nullptr ||
      outlierState->SearchRadius == nullptr ||
      outlierState->MinNeighbors == nullptr) {
    ImGui::TextDisabled("Point-cloud outlier-removal controls are not bound.");
    return;
  }

  *outlierState->Method = std::clamp(*outlierState->Method, 0, 1);
  const bool statistical = *outlierState->Method == 0;
  if (ImGui::BeginCombo("Method##PointCloudOutlierRemoval",
                        statistical ? "Statistical" : "Radius")) {
    if (ImGui::Selectable("Statistical##PointCloudOutlierRemoval", statistical))
      *outlierState->Method = 0;
    if (statistical)
      ImGui::SetItemDefaultFocus();
    if (ImGui::Selectable("Radius##PointCloudOutlierRemoval", !statistical))
      *outlierState->Method = 1;
    if (!statistical)
      ImGui::SetItemDefaultFocus();
    ImGui::EndCombo();
  }

  if (statistical) {
    ImGui::TextDisabled(
        "Reject points beyond mean + k*stddev of mean-kNN distance.");
    *outlierState->KNeighbors = std::clamp(*outlierState->KNeighbors, 1, 512);
    *outlierState->StdDevMultiplier =
        std::clamp(*outlierState->StdDevMultiplier, 0.0f, 100.0f);
    ImGui::DragInt("K neighbors##PointCloudOutlierRemoval",
                   outlierState->KNeighbors, 1.0f, 1, 512);
    ImGui::DragFloat("Std-dev multiplier##PointCloudOutlierRemoval",
                     outlierState->StdDevMultiplier, 0.05f, 0.0f, 100.0f);
  } else {
    ImGui::TextDisabled(
        "Reject points with too few neighbors inside the search radius.");
    *outlierState->SearchRadius = std::max(*outlierState->SearchRadius, 0.0f);
    *outlierState->MinNeighbors =
        std::clamp(*outlierState->MinNeighbors, 0, 512);
    ImGui::DragFloat("Search radius##PointCloudOutlierRemoval",
                     outlierState->SearchRadius, 0.01f, 0.0f, 1000.0f);
    ImGui::DragInt("Min neighbors##PointCloudOutlierRemoval",
                   outlierState->MinNeighbors, 1.0f, 0, 512);
  }

  if (ImGui::Button("Remove Outliers##PointCloudOutlierRemoval")) {
    *outlierState
         ->LastResult = ApplyEditorPointCloudOutlierRemovalCommand(
        context.GeometryCommands,
        EditorPointCloudOutlierRemovalCommand{
            .StableEntityId = model.SelectedStableId,
            .Method = statistical
                          ? EditorPointCloudOutlierMethod::Statistical
                          : EditorPointCloudOutlierMethod::Radius,
            .KNeighbors = static_cast<std::uint32_t>(*outlierState->KNeighbors),
            .StdDevMultiplier = *outlierState->StdDevMultiplier,
            .SearchRadius = *outlierState->SearchRadius,
            .MinNeighbors =
                static_cast<std::uint32_t>(*outlierState->MinNeighbors),
        });
  }

  const std::optional<EditorPointCloudOutlierRemovalResult> &result =
      outlierState->LastResult->has_value()
          ? *outlierState->LastResult
          : processing.LastPointCloudOutlierRemovalResult;
  DrawPointCloudOutlierRemovalResultStatus(result);
}

void DrawDomainProcessingWindow(
    const EditorDomainWindowModel &model,
    const SandboxEditorContext &context,
    PointCloudOutlierRemovalUiState *pointCloudOutlierState) {
  // The header already includes processing diagnostics; render them only once.
  DrawDomainWindowHeader(model);

  const EditorGeometryProcessingModel &processing = model.Processing;
  if (!DomainWindowReady(model) || !processing.HasSelectedEntity) {
    ImGui::TextDisabled(
        "Select a matching domain entity to inspect processing affordances.");
    return;
  }

  DrawPointCloudOutlierRemovalControls(model, context, processing,
                                       pointCloudOutlierState);
}

} // namespace

struct DomainPanels::Impl {
  enum class Section : std::uint8_t {
    Appearance,
    Properties,
    Selection,
    PointCloudOutlierRemoval,
  };

  EditorShell *Shell{nullptr};
  std::vector<Runtime::EditorWindowHandle> Handles{};
  int CachedModelFrame{-1};
  std::array<std::optional<Runtime::EditorDomainWindowModel>, 3u>
      CachedDomainModels{};

  Runtime::EditorPropertyPlotWidgetState MeshPropertyPlotState{};
  std::optional<Runtime::EditorPointCloudOutlierRemovalResult>
      LastPointCloudOutlierRemovalResult{};
  std::optional<Runtime::EditorUvRegenerationCommandResult>
      LastUvRegenerationResult{};
  std::optional<Runtime::EditorUvRegenerationCommandResult>
      LastUvExtentAdoption{};
  std::int32_t PointCloudOutlierMethod{0};
  std::int32_t PointCloudOutlierKNeighbors{16};
  float PointCloudOutlierStdDevMultiplier{1.0f};
  float PointCloudOutlierSearchRadius{0.0f};
  std::int32_t PointCloudOutlierMinNeighbors{4};
  std::int32_t TextureBakeSourceIndex{0};
  std::int32_t TextureBakeTargetSemanticIndex{0};
  std::int32_t TextureBakeEncoderIndex{0};
  std::int32_t TextureBakeStorageIndex{0};
  std::int32_t TextureBakeColormapIndex{0};
  std::int32_t TextureBakeNormalSpaceIndex{0};
  std::uint32_t TextureBakeAdditionalConsumerMask{0u};
  std::int32_t TextureBakeWidth{1024};
  std::int32_t TextureBakeHeight{1024};
  std::int32_t TextureBakePadding{2};
  std::int32_t UvAtlasResolution{1024};
  std::int32_t UvAtlasPadding{2};
  float UvAtlasTexelsPerUnit{0.0f};
  bool UvAtlasForceRegenerate{true};
  bool UvAtlasPreserveAuthored{false};

  void Register(EditorShell &editorShell);
  void Unregister();
  void RegisterWindow(std::string id, std::vector<std::string> menuPath,
                      std::string title,
                      Runtime::EditorDomainWindowKind kind,
                      Section section);
  void ResetModelCache();
  [[nodiscard]] const Runtime::EditorDomainWindowModel &
  GetDomainWindowModel(const SandboxEditorContext &context,
                       Runtime::EditorDomainWindowKind kind);
  void DrawWindow(bool &open, const SandboxEditorContext &context,
                  Runtime::EditorDomainWindowKind kind, Section section,
                  const char *title);
};

void DomainPanels::Impl::Register(EditorShell &editorShell) {
  Unregister();
  Shell = &editorShell;

  RegisterWindow("pointcloud.appearance", {"PointCloud"}, "Appearance",
                 Runtime::EditorDomainWindowKind::PointCloud,
                 Section::Appearance);
  RegisterWindow("pointcloud.properties", {"PointCloud"}, "Properties",
                 Runtime::EditorDomainWindowKind::PointCloud,
                 Section::Properties);
  RegisterWindow("pointcloud.selection", {"PointCloud"}, "Selection details",
                 Runtime::EditorDomainWindowKind::PointCloud,
                 Section::Selection);
  RegisterWindow("pointcloud.processing.remove_outliers",
                 {"PointCloud", "Processing"}, "Remove Outliers",
                 Runtime::EditorDomainWindowKind::PointCloud,
                 Section::PointCloudOutlierRemoval);

  RegisterWindow("graph.appearance", {"Graph"}, "Appearance",
                 Runtime::EditorDomainWindowKind::Graph,
                 Section::Appearance);
  RegisterWindow("graph.properties", {"Graph"}, "Properties",
                 Runtime::EditorDomainWindowKind::Graph,
                 Section::Properties);
  RegisterWindow("graph.selection", {"Graph"}, "Selection details",
                 Runtime::EditorDomainWindowKind::Graph,
                 Section::Selection);

  RegisterWindow("mesh.appearance", {"Mesh"}, "Appearance",
                 Runtime::EditorDomainWindowKind::Mesh,
                 Section::Appearance);
  RegisterWindow("mesh.properties", {"Mesh"}, "Properties",
                 Runtime::EditorDomainWindowKind::Mesh,
                 Section::Properties);
  RegisterWindow("mesh.selection", {"Mesh"}, "Selection details",
                 Runtime::EditorDomainWindowKind::Mesh,
                 Section::Selection);
}

void DomainPanels::Impl::Unregister() {
  if (Shell != nullptr) {
    for (const Runtime::EditorWindowHandle handle : Handles)
      (void)Shell->UnregisterEditorWindow(handle);
  }
  Handles.clear();
  Shell = nullptr;
  ResetModelCache();
  LastPointCloudOutlierRemovalResult.reset();
  LastUvRegenerationResult.reset();
  LastUvExtentAdoption.reset();
  MeshPropertyPlotState.SelectedProperty.clear();
}

void DomainPanels::Impl::RegisterWindow(
    std::string id, std::vector<std::string> menuPath, std::string title,
    const Runtime::EditorDomainWindowKind kind, const Section section) {
  const std::string windowTitle =
      std::string(Runtime::DebugNameForEditorDomainWindowKind(kind)) +
      " / " +
      (section == Section::PointCloudOutlierRemoval
           ? "Processing / Remove Outliers"
       : section == Section::Selection ? "Selection"
                                       : title);

  Handles.push_back(
      Shell->RegisterEditorWindow(EditorWindowDescriptor{
          .Id = std::move(id),
          .MenuPath = std::move(menuPath),
          .Title = std::move(title),
          .OpenByDefault = false,
          .Draw =
              [this, kind, section, windowTitle](
                  bool &open, const SandboxEditorContext &context) {
                DrawWindow(open, context, kind, section, windowTitle.c_str());
              },
          .OpenStateChanged = [this](bool) { ResetModelCache(); },
      }));
}

void DomainPanels::Impl::ResetModelCache() {
  CachedModelFrame = -1;
  for (auto &model : CachedDomainModels)
    model.reset();
}

const Runtime::EditorDomainWindowModel &
DomainPanels::Impl::GetDomainWindowModel(
    const SandboxEditorContext &context,
    const Runtime::EditorDomainWindowKind kind) {
  const int frame = ImGui::GetFrameCount();
  if (CachedModelFrame != frame) {
    CachedModelFrame = frame;
    for (auto &model : CachedDomainModels)
      model.reset();
  }

  auto &model = CachedDomainModels[static_cast<std::size_t>(kind)];
  if (!model.has_value()) {
    model = Runtime::BuildEditorDomainWindowModel(
        context.SnapshotQueries, kind, context.ModelBuildStats);
  } else if (context.ModelBuildStats != nullptr) {
    ++context.ModelBuildStats->DomainWindowModelCacheHits;
  }
  return *model;
}

void DomainPanels::Impl::DrawWindow(
    bool &open, const SandboxEditorContext &context,
    const Runtime::EditorDomainWindowKind kind, const Section section,
    const char *title) {
  if (context.GeometryResults.LastPointCloudOutlierRemovalResult.has_value()) {
    LastPointCloudOutlierRemovalResult =
        *context.GeometryResults.LastPointCloudOutlierRemovalResult;
  }
  if (context.GeometryResults.LastUvRegenerationResult.has_value())
    LastUvRegenerationResult = *context.GeometryResults.LastUvRegenerationResult;

  PointCloudOutlierRemovalUiState outlierState{
      .LastResult = &LastPointCloudOutlierRemovalResult,
      .Method = &PointCloudOutlierMethod,
      .KNeighbors = &PointCloudOutlierKNeighbors,
      .StdDevMultiplier = &PointCloudOutlierStdDevMultiplier,
      .SearchRadius = &PointCloudOutlierSearchRadius,
      .MinNeighbors = &PointCloudOutlierMinNeighbors,
  };
  TextureBakeUiState textureBakeState{
      .LastUvRegenerationResult = &LastUvRegenerationResult,
      .LastUvExtentAdoption = &LastUvExtentAdoption,
      .SourceIndex = &TextureBakeSourceIndex,
      .TargetSemanticIndex = &TextureBakeTargetSemanticIndex,
      .EncoderIndex = &TextureBakeEncoderIndex,
      .StorageIndex = &TextureBakeStorageIndex,
      .ColormapIndex = &TextureBakeColormapIndex,
      .NormalSpaceIndex = &TextureBakeNormalSpaceIndex,
      .AdditionalConsumerMask = &TextureBakeAdditionalConsumerMask,
      .Width = &TextureBakeWidth,
      .Height = &TextureBakeHeight,
      .Padding = &TextureBakePadding,
      .UvResolution = &UvAtlasResolution,
      .UvPadding = &UvAtlasPadding,
      .UvTexelsPerUnit = &UvAtlasTexelsPerUnit,
      .UvForceRegenerate = &UvAtlasForceRegenerate,
      .UvPreserveAuthored = &UvAtlasPreserveAuthored,
  };

  ImGui::SetNextWindowSize(ImVec2(340.0f, 300.0f), ImGuiCond_FirstUseEver);
  if (ImGui::Begin(title, &open)) {
    const Runtime::EditorDomainWindowModel &model =
        GetDomainWindowModel(context, kind);
    switch (section) {
    case Section::Appearance:
      DrawDomainRenderWindow(model, context, &textureBakeState);
      if (kind == Runtime::EditorDomainWindowKind::Mesh &&
          model.DomainMatches) {
        const auto properties =
            Runtime::ResolveEditorSelectedMeshVertexProperties(context.GeometryCommands);
        if (properties) {
          ImGui::SeparatorText("Property distribution");
          (void)Runtime::DrawEditorScalarPropertyPlotWidget(
              "mesh.appearance.properties", properties, MeshPropertyPlotState);
        }
      }
      break;
    case Section::Properties:
      DrawDomainPropertyWindow(model);
      break;
    case Section::Selection:
      DrawDomainSelectionWindow(model);
      break;
    case Section::PointCloudOutlierRemoval:
      DrawDomainProcessingWindow(model, context, &outlierState);
      break;
    }
  }
  ImGui::End();
}

DomainPanels::DomainPanels() : m_Impl(std::make_unique<Impl>()) {}

DomainPanels::~DomainPanels() { m_Impl->Unregister(); }

void DomainPanels::Register(EditorShell &editorShell) {
  m_Impl->Register(editorShell);
}

void DomainPanels::Unregister() { m_Impl->Unregister(); }

} // namespace Extrinsic::Sandbox::Editor
