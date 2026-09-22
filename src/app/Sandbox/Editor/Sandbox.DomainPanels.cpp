module;
#include <functional>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>

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

import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.EditorCommon;
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
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.ParameterizationOperations;

#include "Sandbox.PanelSupport.hpp"

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

inline constexpr SurfaceDomain kSurfaceVertex = static_cast<SurfaceDomain>(0);
inline constexpr SurfaceDomain kSurfaceFace = static_cast<SurfaceDomain>(1);
inline constexpr EdgeDomain kEdgeVertex = static_cast<EdgeDomain>(0);
inline constexpr EdgeDomain kEdgeEdge = static_cast<EdgeDomain>(1);
inline constexpr PointRenderType kPointFlat = static_cast<PointRenderType>(0);
inline constexpr PointRenderType kPointSphere = static_cast<PointRenderType>(1);
inline constexpr PointRenderType kPointSurfel = static_cast<PointRenderType>(2);
inline constexpr VisualizationColorSource kMaterialSource =
    static_cast<VisualizationColorSource>(0);
[[nodiscard]] bool
DomainAppearanceReady(const EditorDomainWindowModel &model) noexcept {
  return model.HasSelectedEntity && model.VisualizationTargetAvailable;
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
    for (const GeometryPresentationPropertyOption &option :
         target.Options) {
      if (option.Compatible) {
        ImGui::BulletText("%s", option.Property.Name.c_str());
      } else {
        ImGui::BulletText("%s", option.Property.Name.c_str());
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
    if (ImGui::DragFloat("Edge width", &edgeWidth, 0.05f, 0.01f, 32.0f,
                         "%.3f", ImGuiSliderFlags_AlwaysClamp) &&
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

}

void DrawPointRenderHintControls(const EditorDomainWindowModel &model,
                                 const SandboxEditorContext &context,
                                 const bool canEditRenderHints) {
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
    if (ImGui::DragFloat("Point size", &pointSize, 0.05f, 0.01f, 32.0f,
                         "%.3f", ImGuiSliderFlags_AlwaysClamp) &&
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
}

void DrawVisualizationPropertyDropdown(const EditorDomainWindowModel &model,
                                       const SandboxEditorContext &context,
                                       EditorCommandStatus &lastStatus) {
  const auto &visualization = model.Visualization.Visualization;
  const auto &properties = model.Visualization.Properties;
  const bool scalar = visualization.Source == kScalarFieldSource;
  const bool color = static_cast<int>(visualization.Source) >= 3;
  const std::string &propertyName =
      scalar ? visualization.ScalarFieldName : visualization.ColorBufferName;
  const char *preview =
      !visualization.HasConfig || (!scalar && !color)
          ? (visualization.Source == kUniformColorSource ? "Uniform color"
                                                         : "Material / default")
          : propertyName.c_str();
  if (!ImGui::BeginCombo("Property", preview))
    return;
  if (ImGui::Selectable("Material / default", !visualization.HasConfig ||
                        visualization.Source == kMaterialSource)) {
    lastStatus = ApplyEditorVisualizationConfigCommand(
        context.VisualizationCommands,
        EditorVisualizationConfigCommand{.StableEntityId =
                                             model.SelectedStableId,
                                         .Target = model.VisualizationTarget,
                                          .EnableConfig = true,
                                          .Source = kMaterialSource});
  }
  if (ImGui::Selectable("Uniform color",
                        visualization.Source == kUniformColorSource)) {
    lastStatus = ApplyEditorVisualizationConfigCommand(
        context.VisualizationCommands,
        MakeUniformVisualizationConfigCommandFromModel(
            model.SelectedStableId, visualization, model.VisualizationTarget,
            visualization.Color));
  }
  for (const auto &property : properties) {
    if (!property.ScalarPresetAvailable && !property.ColorBufferPresetAvailable)
      continue;
    const std::string label =
        property.Name + "  (" +
        DebugNameForEditorVisualizationPropertyDomain(property.Domain) + ")";
    const int propertyDomain =
        property.Domain == EditorVisualizationPropertyDomain::MeshFaces ? 2
        : (property.Domain == EditorVisualizationPropertyDomain::MeshEdges ||
           property.Domain == EditorVisualizationPropertyDomain::GraphEdges) ? 1
                                                                             : 0;
    const int activeDomain = scalar ? static_cast<int>(visualization.ScalarDomain)
                                   : static_cast<int>(visualization.Source) - 3;
    const bool selected =
        property.Name == propertyName && (scalar || color) &&
        propertyDomain == activeDomain;
    if (ImGui::Selectable(label.c_str(), selected)) {
      lastStatus = ApplyEditorVisualizationPropertyCommand(
          context.VisualizationCommands,
          EditorVisualizationPropertyCommand{
              .StableEntityId = model.SelectedStableId,
              .Target = model.VisualizationTarget,
              .Domain = property.Domain,
              .Preset = property.ScalarPresetAvailable
                            ? (visualization.IsolineCount > 0u
                                   ? EditorVisualizationPropertyPreset::Isoline
                                   : EditorVisualizationPropertyPreset::Scalar)
                            : EditorVisualizationPropertyPreset::ColorBuffer,
              .PropertyName = property.Name,
              .ScalarAutoRange = visualization.ScalarAutoRange,
              .ScalarRangeMin = visualization.ScalarRangeMin,
              .ScalarRangeMax = visualization.ScalarRangeMax,
              .ScalarBinCount = visualization.ScalarBinCount,
              .IsolineCount = visualization.IsolineCount,
          });
    }
    if (selected)
      ImGui::SetItemDefaultFocus();
  }
  ImGui::EndCombo();
}

void DrawScalarVisualizationControls(
    const EditorVisualizationConfigModel &visualization,
    const SandboxEditorContext &context, const std::uint32_t selectedStableId,
    const EditorVisualizationTarget target,
    const bool canEditVisualization) {
  if (!visualization.HasConfig || visualization.Source != kScalarFieldSource) {
    return;
  }
  DrawScalarFieldColorControls(visualization, context, selectedStableId, target,
                              canEditVisualization);
  if (visualization.UseBakedTexture) {
    ImGui::TextDisabled(
        "Binning and isolines are available with attribute rendering.");
    return;
  }
  DrawScalarFieldBinAndIsolineControls(visualization, context, selectedStableId,
                                     target, canEditVisualization);
}

void DrawDomainVisualizationControls(const EditorDomainWindowModel &model,
                                     const SandboxEditorContext &context,
                                     EditorCommandStatus &lastStatus);

// Appearance owns render hints and state, visualization controls, property and
// attribute bindings, and texture baking.
void DrawDomainRenderWindow(
    const std::span<const EditorDomainWindowModel *const> models,
    const SandboxEditorContext &context, TextureBakeUiState *textureBakeState,
    std::array<EditorCommandStatus, 3> &statuses) {
  const auto &selected = *models.front();
  if (!selected.HasSelectedEntity) {
    ImGui::TextDisabled("Select a mesh, graph, or point cloud.");
    return;
  }
  ImGui::TextUnformatted(selected.SelectedEntity.Name.c_str());
  for (const auto *current : models) {
    const auto &model = *current;
    const bool available = DomainAppearanceReady(model);
    if (!available)
      continue;
    ImGui::PushID(static_cast<int>(model.Kind));
    auto &status = statuses[static_cast<std::size_t>(model.Kind)];
    const bool mesh = model.Kind == EditorDomainWindowKind::Mesh;
    const bool graph = model.Kind == EditorDomainWindowKind::Graph;
    const char *label = mesh ? "Surface" : graph ? "Edges" : "Points";
    bool visible = mesh ? model.RenderHints.HasRenderSurface
                        : graph ? model.RenderHints.HasRenderEdges
                                : model.RenderHints.HasRenderPoints;
    if (ImGui::Checkbox(label, &visible)) {
      status = ApplyEditorRenderHintCommand(
          context.VisualizationCommands,
          EditorRenderHintCommand{
              .StableEntityId = model.SelectedStableId,
              .SetSurface = mesh,
              .EnableSurface = visible,
              .SurfaceDomain = model.RenderHints.SurfaceDomainValue,
              .SetEdges = graph,
              .EnableEdges = visible,
              .EdgeDomain = model.RenderHints.EdgeDomainValue,
              .SetPoints = !mesh && !graph,
              .EnablePoints = visible,
              .PointType = model.RenderHints.PointRenderTypeValue,
          });
    }
    if (status != EditorCommandStatus::Applied &&
        status != EditorCommandStatus::NoChange)
      ImGui::TextWrapped("Appearance change failed: %s",
                         DebugNameForEditorCommandStatus(status));
    if (!visible || !ImGui::TreeNode("Settings")) {
      ImGui::PopID();
      continue;
    }
    switch (model.Kind) {
    case EditorDomainWindowKind::Mesh:
      DrawMeshRenderHintControls(model, context, available);
      break;
    case EditorDomainWindowKind::Graph:
      DrawGraphRenderHintControls(model, context, available);
      break;
    case EditorDomainWindowKind::PointCloud:
      DrawPointRenderHintControls(model, context, available);
      break;
    }
    DrawDomainVisualizationControls(model, context, status);
    if (ImGui::CollapsingHeader("Advanced")) {
      DrawRenderHintStatus(model.RenderHints);
      DrawBoundRenderStateRows(model.BoundState);
      DrawPropertyBindingTargets(model.PropertyCatalog);
      DrawVertexChannelBindingTargets(model.PropertyCatalog, &context);
      if (model.Kind == EditorDomainWindowKind::Mesh) {
        static TextureBakeMutationUiState mutationState{};
        DrawTextureBakeControls(model.TextureBake, &context, textureBakeState, mutationState);
      }
    }
    ImGui::TreePop();
    ImGui::PopID();
  }
}

void DrawDomainVisualizationControls(const EditorDomainWindowModel &model,
                                     const SandboxEditorContext &context,
                                     EditorCommandStatus &lastStatus) {
  const auto &visualization = model.Visualization.Visualization;
  const bool available = model.VisualizationTargetAvailable &&
                         model.VisualizationControlsAvailable;
  ImGui::BeginDisabled(!available);
  DrawVisualizationPropertyDropdown(model, context, lastStatus);
  if (visualization.HasConfig && static_cast<int>(visualization.Source) >= 3) {
    int interpretation = static_cast<int>(visualization.Interpretation);
    if (ImGui::Combo("Color interpretation", &interpretation, "Components\0Normal direction\0")) {
      auto command = MakeVisualizationConfigCommandFromModel(
          model.SelectedStableId, visualization, model.VisualizationTarget);
      command.Interpretation = static_cast<decltype(command.Interpretation)>(interpretation);
      lastStatus = ApplyEditorVisualizationConfigCommand(context.VisualizationCommands, command);
    }
  }
  DrawUniformVisualizationColorEdit(visualization, context,
                                    model.SelectedStableId,
                                    model.VisualizationTarget, available);
  if (model.Kind == EditorDomainWindowKind::Mesh &&
      ImGui::CollapsingHeader("Texture baking")) {
    bool baked = visualization.UseBakedTexture;
    const bool hasProperty = visualization.HasConfig &&
                             (visualization.Source == kScalarFieldSource ||
                              static_cast<int>(visualization.Source) >= 3);
    const bool canEnable = hasProperty && model.TextureBake.CanBake;
    ImGui::BeginDisabled(!baked && !canEnable);
    if (ImGui::Checkbox("Use baked texture", &baked)) {
      auto command = MakeVisualizationConfigCommandFromModel(
          model.SelectedStableId, visualization, model.VisualizationTarget);
      command.UseBakedTexture = baked;
      lastStatus = ApplyEditorVisualizationConfigCommand(
          context.VisualizationCommands, command);
    }
    ImGui::EndDisabled();
    if (!canEnable && !baked) {
      ImGui::TextDisabled("%s", !hasProperty
                                    ? "Select a property to bake."
                                    : model.TextureBake.DisabledReason.c_str());
    }
    if (baked) {
      const auto output = std::ranges::find(
          model.TextureBake.BakedTextures, kSurfaceAppearanceTextureOutput,
          &PropertyTextureBakeRecord::OutputName);
      if (output != model.TextureBake.BakedTextures.end())
        ImGui::TextWrapped("%s", output->Diagnostic.c_str());
    }
  }
  if (visualization.Source == kScalarFieldSource &&
      ImGui::CollapsingHeader("Color mapping"))
    DrawScalarVisualizationControls(visualization, context,
                                    model.SelectedStableId,
                                    model.VisualizationTarget, available);
  ImGui::EndDisabled();
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

void DrawDomainSelectionWindow(const EditorDomainWindowModel& model,
                               const SandboxEditorContext& context, int& index, int& domainIndex,
                               std::string& message)
{
    DrawDomainWindowHeader(model);
    ImGui::SeparatorText("Selection tool");
    auto config = GetEditorSelectionInteractionConfig(context.Processing);
    int target = static_cast<int>(config.Target);
    bool changed = ImGui::Combo("Pick", &target, "Entities\0Vertices / points\0Edges\0Faces\0");
    config.Target = static_cast<SelectionTarget>(target);
    changed |= ImGui::Checkbox("Highlight selected primitives", &config.Highlight);
    changed |=
        ImGui::InputFloat("Point radius (world units)", &config.PointRadius, 0.001f, 0.01f, "%.4f");
    if (changed)
    {
        const auto applied =
            ApplyEditorSelectionInteractionConfig(context.Processing, config);
        message = applied.Succeeded() ? "Selection settings applied."
                                      : "Selection settings could not be applied.";
    }
    ImGui::TextWrapped("Click to replace; Shift-click to add; Ctrl-click to toggle. A background "
                       "click clears primitive selections. Vertex and edge picks on a surface use "
                       "the nearest corner or edge of the hit face.");
    if (!model.HasSelectedEntity)
        return;
    ImGui::SeparatorText("Selected elements");
    constexpr GeometryElementDomain domains[] = {
        GeometryElementDomain::MeshVertex,    GeometryElementDomain::MeshEdge,
        GeometryElementDomain::MeshHalfedge,  GeometryElementDomain::MeshFace,
        GeometryElementDomain::GraphNode,     GeometryElementDomain::GraphEdge,
        GeometryElementDomain::GraphHalfedge, GeometryElementDomain::PointCloudPoint};
    std::vector<GeometryElementDomain> available;
    for (auto domain : domains)
    {
        const auto selection =
            ReadEditorPrimitiveSelection(context.Processing, model.SelectedStableId, domain);
        if (selection.Status != PrimitiveSelectionStatus::UnsupportedDomain &&
            selection.Status != PrimitiveSelectionStatus::Unavailable)
            available.push_back(domain);
    }
    if (available.empty())
    {
        ImGui::TextDisabled("No selectable element domain.");
        return;
    }
    domainIndex = std::clamp(domainIndex, 0, static_cast<int>(available.size()) - 1);
    if (ImGui::BeginCombo("Element domain", ToString(available[domainIndex]).data()))
    {
        for (std::size_t i = 0; i < available.size(); ++i)
            if (ImGui::Selectable(ToString(available[i]).data(),
                                  domainIndex == static_cast<int>(i)))
                domainIndex = static_cast<int>(i);
        ImGui::EndCombo();
    }
    const auto domain = available[domainIndex];
    const auto edit = [&](PrimitiveSelectionEdit operation,
                          std::span<const std::uint32_t> indices = {}) {
        auto result = ApplyEditorPrimitiveSelection(
            context.Processing, model.SelectedStableId, domain, operation, indices);
        message = result.Usable() ? "Selection updated." : result.Message;
    };
    ImGui::InputInt("Element index", &index);
    if (index >= 0)
    {
        const auto value = static_cast<std::uint32_t>(index);
        if (ImGui::Button("Add"))
            edit(PrimitiveSelectionEdit::Add, std::span{&value, 1u});
        ImGui::SameLine();
        if (ImGui::Button("Remove"))
            edit(PrimitiveSelectionEdit::Remove, std::span{&value, 1u});
    }
    if (ImGui::Button("Select all"))
        edit(PrimitiveSelectionEdit::All);
    ImGui::SameLine();
    if (ImGui::Button("Invert"))
        edit(PrimitiveSelectionEdit::Invert);
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        edit(PrimitiveSelectionEdit::Clear);
    const auto selected =
        ReadEditorPrimitiveSelection(context.Processing, model.SelectedStableId, domain);
    ImGui::Text("%zu selected / %zu elements", selected.Indices.size(), selected.ElementCount);
    std::string indices = "In selection order:";
    const auto displayed = std::min<std::size_t>(selected.Indices.size(), 64);
    for (std::size_t i = 0; i < displayed; ++i)
        indices += " " + std::to_string(selected.Indices[i]);
    if (displayed < selected.Indices.size())
        indices += " ...";
    ImGui::TextWrapped("%s", indices.c_str());
    if (!selected.Message.empty())
        ImGui::TextWrapped("%s", selected.Message.c_str());
    if (!message.empty())
        ImGui::TextWrapped("%s", message.c_str());
    if (ImGui::CollapsingHeader("Last pick details"))
        DrawPrimitiveDetails(model.Primitive);
}

} // namespace

struct DomainPanels::Impl {
  std::array<EditorCommandStatus, 3> AppearanceStatuses{
      EditorCommandStatus::NoChange, EditorCommandStatus::NoChange,
      EditorCommandStatus::NoChange};
  std::uint32_t AppearanceEntity{0u};
  int SelectionElementIndex{0};
  int SelectionDomainIndex{0};
  std::string SelectionMessage{};

  enum class Section : std::uint8_t {
    Appearance,
    Properties,
    Selection,
  };

  EditorShell *Shell{nullptr};
  std::vector<Runtime::EditorWindowHandle> Handles{};
  int CachedModelFrame{-1};
  std::array<std::optional<Runtime::EditorDomainWindowModel>, 3u>
      CachedDomainModels{};

  Runtime::EditorPropertyPlotWidgetState MeshPropertyPlotState{};
  std::optional<Runtime::EditorUvRegenerationCommandResult>
      LastUvRegenerationResult{};
  std::optional<Runtime::EditorUvRegenerationCommandResult>
      LastUvExtentAdoption{};
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
  RegisterWindow("pointcloud.selection", {"PointCloud"}, "Selection",
                 Runtime::EditorDomainWindowKind::PointCloud,
                 Section::Selection);


  RegisterWindow("graph.appearance", {"Graph"}, "Appearance",
                 Runtime::EditorDomainWindowKind::Graph,
                 Section::Appearance);
  RegisterWindow("graph.properties", {"Graph"}, "Properties",
                 Runtime::EditorDomainWindowKind::Graph,
                 Section::Properties);
  RegisterWindow("graph.selection", {"Graph"}, "Selection",
                 Runtime::EditorDomainWindowKind::Graph,
                 Section::Selection);

  RegisterWindow("mesh.appearance", {"Mesh"}, "Appearance",
                 Runtime::EditorDomainWindowKind::Mesh,
                 Section::Appearance);
  RegisterWindow("mesh.properties", {"Mesh"}, "Properties",
                 Runtime::EditorDomainWindowKind::Mesh,
                 Section::Properties);
  RegisterWindow("mesh.selection", {"Mesh"}, "Selection",
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
      title;

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

  if (context.Parameterization.Results.LastUvRegenerationResult.has_value())
    LastUvRegenerationResult = *context.Parameterization.Results.LastUvRegenerationResult;


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
    case Section::Appearance: {
      if (AppearanceEntity != model.SelectedStableId) {
        AppearanceStatuses.fill(EditorCommandStatus::NoChange);
        AppearanceEntity = model.SelectedStableId;
      }
      std::array<const EditorDomainWindowModel *, 3> appearanceModels{&model};
      std::size_t count = 1;
      if (kind == EditorDomainWindowKind::Mesh && model.HasSelectedEntity)
        appearanceModels[count++] =
            &GetDomainWindowModel(context, EditorDomainWindowKind::Graph);
      if (kind != EditorDomainWindowKind::PointCloud && model.HasSelectedEntity)
        appearanceModels[count++] =
            &GetDomainWindowModel(context, EditorDomainWindowKind::PointCloud);
      DrawDomainRenderWindow({appearanceModels.data(), count}, context,
                             &textureBakeState, AppearanceStatuses);
    }
      if (kind == Runtime::EditorDomainWindowKind::Mesh &&
          model.DomainMatches && ImGui::CollapsingHeader("Property distribution")) {
        const auto properties =
            Runtime::ResolveEditorSelectedMeshVertexProperties(context.Processing);
        if (properties) {
          (void)Runtime::DrawEditorScalarPropertyPlotWidget(
              "mesh.appearance.properties", properties, MeshPropertyPlotState);
        }
      }
      break;
    case Section::Properties:
      DrawDomainPropertyWindow(model);
      break;
    case Section::Selection:
      DrawDomainSelectionWindow(model, context, SelectionElementIndex, SelectionDomainIndex, SelectionMessage);
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
