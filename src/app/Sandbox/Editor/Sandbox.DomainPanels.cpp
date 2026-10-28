module;
#include <functional>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
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
import Extrinsic.Runtime.PointSamplingOperations;
import Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorPropertyWidgets;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.ConfigFieldSpec;
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

import Extrinsic.Runtime.PropertyInspectionOperations;
#include "Sandbox.PanelSupport.hpp"
#include "Sandbox.PropertyInspectorPanel.hpp"

namespace Extrinsic::Sandbox::Editor
{
    namespace
    {
        using namespace Extrinsic::Runtime;

        using SurfaceDomain = decltype(EditorRenderHintModel{}.SurfaceDomainValue);
        using EdgeDomain = decltype(EditorRenderHintModel{}.EdgeDomainValue);
        using PointRenderType = decltype(EditorRenderHintModel{}.PointRenderTypeValue);
        using VisualizationColorSource = decltype(EditorVisualizationConfigModel{}.Config.Source);

        inline constexpr SurfaceDomain kSurfaceVertex = static_cast<SurfaceDomain>(0);
        inline constexpr SurfaceDomain kSurfaceFace = static_cast<SurfaceDomain>(1);
        inline constexpr EdgeDomain kEdgeVertex = static_cast<EdgeDomain>(0);
        inline constexpr EdgeDomain kEdgeEdge = static_cast<EdgeDomain>(1);
        inline constexpr PointRenderType kPointFlat = static_cast<PointRenderType>(0);
        inline constexpr PointRenderType kPointSphere = static_cast<PointRenderType>(1);
        inline constexpr PointRenderType kPointSurfel = static_cast<PointRenderType>(2);
        inline constexpr VisualizationColorSource kMaterialSource = static_cast<VisualizationColorSource>(0);
        [[nodiscard]] bool DomainAppearanceReady(const EditorDomainWindowModel& model) noexcept
        {
            return model.HasSelectedEntity && model.VisualizationTargetAvailable;
        }

        // A scalar field or a color buffer colors the lane (not its default or a uniform color).
        [[nodiscard]] bool ColoredByProperty(const EditorVisualizationConfigModel& visualization) noexcept
        {
            return visualization.HasConfig &&
                (visualization.Config.Source == kScalarFieldSource || static_cast<int>(visualization.Config.Source) >= 3);
        }

        // One element index per element domain, shared by every property of that
        // domain and by the three Properties windows (framework24's picker ids).
        struct PropertyBrowserState
        {
            static constexpr std::size_t kDomains =
                static_cast<std::size_t>(GeometryElementDomain::PointCloudPoint) + 1u;
            std::uint32_t Entity{0u};
            std::array<std::string, kDomains> Property{};
            std::array<std::size_t, kDomains> Element{};
            std::array<bool, kDomains> SelectInViewport{};
            std::array<std::optional<std::size_t>, kDomains> SeenPick{};
            std::array<std::string, kDomains> Message{};
        };

        [[nodiscard]] std::string PropertyValueLabel(const EditorPropertyValueRow& row)
        {
            std::string label = "[" + std::to_string(row.Index) + "] " + PropertyValueRowText(row);
            if (row.Deleted)
                label += " (deleted)";
            return label;
        }

        void DrawPropertyRowInfo(const EditorPropertyCatalogRow& row)
        {
            std::string tags{};
            if (row.Bindable)
                tags += " bindable";
            if (row.Internal)
                tags += " internal";
            if (row.Connectivity)
                tags += " connectivity";
            if (row.Generated)
                tags += " generated";
            ImGui::TextDisabled("%s/%u, %zu elements%s",
                DebugNameForGeometryPropertyValueKind(row.ValueKind),
                row.ComponentCount,
                row.ElementCount,
                tags.c_str());
            if (!row.UnsupportedReason.empty())
                ImGui::TextDisabled("%s", row.UnsupportedReason.c_str());
        }

        // The preview is the value at `element`; the popup lists every element and
        // reads only the rows the clipper shows; a click sets `element`.
        void DrawPropertyValuesCombo(const SandboxEditorContext& context,
            const std::uint32_t entity,
            const EditorPropertyCatalogRow& row,
            std::size_t& element)
        {
            if (!row.Supported)
                return; // the info line states the reason
            const EditorPropertyValuesResult current =
                ReadEditorPropertyValues(context.Processing, entity, row.Descriptor, element, 1u);
            DrawDiagnostics(current.Diagnostics);
            if (!current.Success || current.Rows.empty())
                return;
            if (ImGui::BeginCombo("Values", PropertyValueLabel(current.Rows.front()).c_str()))
            {
                ImGuiListClipper clipper;
                clipper.Begin(
                    static_cast<int>(std::min<std::size_t>(current.TotalCount, std::numeric_limits<int>::max())));
                clipper.IncludeItemByIndex(static_cast<int>(element));
                while (clipper.Step())
                {
                    const auto first = static_cast<std::size_t>(clipper.DisplayStart);
                    const EditorPropertyValuesResult page = ReadEditorPropertyValues(context.Processing,
                        entity,
                        row.Descriptor,
                        first,
                        static_cast<std::size_t>(clipper.DisplayEnd - clipper.DisplayStart));
                    for (std::size_t index = first; index < static_cast<std::size_t>(clipper.DisplayEnd); ++index)
                    {
                        const std::string id = "###" + std::to_string(index);
                        if (index - first >= page.Rows.size())
                        {
                            ImGui::TextDisabled("[%zu] -", index);
                            continue;
                        }
                        const bool selected = index == element;
                        if (ImGui::Selectable((PropertyValueLabel(page.Rows[index - first]) + id).c_str(), selected))
                            element = index;
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
        }

        void DrawPropertyDomainSection(std::span<const EditorPropertyCatalogRow> rows,
            const SandboxEditorContext& context,
            const std::uint32_t entity,
            PropertyBrowserState& state)
        {
            const EditorPropertyCatalogRow& first = rows.front();
            const char* name = DebugNameForEditorPropertyCatalogDomain(first.Domain);
            const std::string header = std::string(name) + " (" + std::to_string(first.ElementCount) + ")###" + name;
            if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                return;
            ImGui::PushID(name);
            const GeometryElementDomain domain = first.Descriptor.Domain;
            const auto slot = static_cast<std::size_t>(domain);
            auto selected = std::ranges::find(rows, state.Property[slot], &EditorPropertyCatalogRow::Name);
            if (selected == rows.end())
                selected = rows.begin();
            if (ImGui::BeginCombo("Property", selected->Name.c_str()))
            {
                for (auto row = rows.begin(); row != rows.end(); ++row)
                {
                    if (ImGui::Selectable(row->Name.c_str(), row == selected))
                        selected = row;
                    if (row == selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            state.Property[slot] = selected->Name;
            DrawPropertyRowInfo(*selected);
            if (selected->ElementCount == 0u)
            {
                ImGui::PopID();
                return;
            }

            std::size_t& element = state.Element[slot];
            const auto previous = static_cast<std::uint32_t>(element);
            // A new viewport pick moves the domain's element, as framework24's picker does.
            const auto pick = std::ranges::find_if(rows,
                [](const EditorPropertyCatalogRow& row)
                {
                    return row.Preview.HasValue;
                });
            const auto viewportPick = pick == rows.end() ? std::nullopt : std::optional{pick->Preview.ElementIndex};
            if (viewportPick != state.SeenPick[slot])
            {
                state.SeenPick[slot] = viewportPick;
                if (viewportPick)
                    element = *viewportPick;
            }
            if (element >= selected->ElementCount)
                element = 0u;
            DrawPropertyValuesCombo(context, entity, *selected, element);
            bool& selectInViewport = state.SelectInViewport[slot];
            const bool toggled = ImGui::Checkbox("Select in viewport", &selectInViewport);
            // Only this element is added or removed, so the domain's other selected
            // elements (method pins, sources) stay unless the box's element is one of them.
            const auto edit = [&](const PrimitiveSelectionEdit operation, const std::uint32_t index)
            {
                const PrimitiveSelectionSnapshot result =
                    ApplyEditorPrimitiveSelection(context.Processing, entity, domain, operation, std::span{&index, 1u});
                state.Message[slot] = result.Usable() ? std::string{} : result.Message;
                if (!result.Usable())
                    selectInViewport = false;
            };
            const auto current = static_cast<std::uint32_t>(element);
            if (toggled)
                edit(selectInViewport ? PrimitiveSelectionEdit::Add : PrimitiveSelectionEdit::Remove, current);
            else if (selectInViewport && current != previous) // a combo or viewport pick
            {
                edit(PrimitiveSelectionEdit::Remove, previous);
                if (selectInViewport) // a failed Remove unticks the box
                    edit(PrimitiveSelectionEdit::Add, current);
            }
            if (!state.Message[slot].empty())
                ImGui::TextWrapped("%s", state.Message[slot].c_str());
            ImGui::PopID();
        }

// Properties is an exhaustive explorer: internal, connectivity, and generated
// rows remain listed, with unsupported actions diagnosed rather than hidden.
// Render, binding, and bake controls belong to Appearance.
        void DrawDomainPropertyWindow(
            const EditorDomainWindowModel& model, const SandboxEditorContext& context, PropertyBrowserState& state)
        {
            DrawDomainWindowHeader(model);
            if (!DomainWindowReady(model))
                return;
            if (state.Entity != model.SelectedStableId)
                state = PropertyBrowserState{.Entity = model.SelectedStableId};
            const std::vector<EditorPropertyCatalogRow>& rows = model.PropertyCatalog.Rows;
            if (rows.empty())
                ImGui::TextDisabled("No geometry properties.");
            // Rows arrive grouped by domain.
            for (std::size_t begin = 0u; begin < rows.size();)
            {
                std::size_t end = begin + 1u;
                while (end < rows.size() && rows[end].Domain == rows[begin].Domain)
                    ++end;
                DrawPropertyDomainSection(
                    std::span{rows}.subspan(begin, end - begin), context, model.SelectedStableId, state);
                begin = end;
            }
            DrawDiagnostics(model.PropertyCatalog.Diagnostics);
        }

        void DrawRenderHintStatus(const EditorRenderHintModel& hints)
        {
            ImGui::Text("Surface: %s", hints.HasRenderSurface ? hints.SurfaceDomain.c_str() : "none");
            if (hints.HasRenderEdges)
            {
                ImGui::Text("Edges: %s", hints.EdgeDomain.c_str());
                if (hints.HasUniformEdgeWidth)
                    ImGui::Text("Edge width: %.3f", hints.UniformEdgeWidth);
                if (hints.HasNamedEdgeWidth)
                    ImGui::Text("Edge width source: %s", hints.EdgeWidthName.c_str());
            }
            else
            {
                ImGui::TextDisabled("Edges: none");
            }

            if (hints.HasRenderPoints)
            {
                ImGui::Text("Points: %s", hints.PointRenderType.c_str());
                if (hints.HasUniformPointSize)
                    ImGui::Text("Point size: %.3f", hints.UniformPointSize);
                if (hints.HasNamedPointSize)
                    ImGui::Text("Point size source: %s", hints.PointSizeName.c_str());
            }
            else
            {
                ImGui::TextDisabled("Points: none");
            }
        }

        [[nodiscard]] bool DrawSurfaceDomainCombo(SurfaceDomain* domain)
        {
            constexpr const char* kItems[]{"Vertex", "Face"};
            int current = *domain == kSurfaceFace ? 1 : 0;
            if (!ImGui::Combo("Surface domain", &current, kItems, 2))
                return false;
            *domain = current == 1 ? kSurfaceFace : kSurfaceVertex;
            return true;
        }

        [[nodiscard]] bool DrawEdgeDomainCombo(EdgeDomain* domain)
        {
            constexpr const char* kItems[]{"Vertex", "Edge"};
            int current = *domain == kEdgeEdge ? 1 : 0;
            if (!ImGui::Combo("Edge domain", &current, kItems, 2))
                return false;
            *domain = current == 1 ? kEdgeEdge : kEdgeVertex;
            return true;
        }

        [[nodiscard]] bool DrawPointTypeCombo(PointRenderType* type)
        {
            constexpr const char* kItems[]{"Flat", "Sphere", "Surfel"};
            int current = 1;
            switch (*type)
            {
            case kPointFlat:   current = 0; break;
            case kPointSphere: current = 1; break;
            case kPointSurfel: current = 2; break;
            }
            if (!ImGui::Combo("Point type", &current, kItems, 3))
                return false;
            switch (current)
            {
            case 0:  *type = kPointFlat; break;
            case 2:  *type = kPointSurfel; break;
            case 1:
            default: *type = kPointSphere; break;
            }
            return true;
        }

        void DrawPointRenderHintControls(
            const EditorDomainWindowModel& model, const SandboxEditorContext& context, bool canEditRenderHints);

        void DrawEdgeRenderHintControls(
            const EditorDomainWindowModel& model, const SandboxEditorContext& context, const bool canEditRenderHints)
        {
            EdgeDomain edgeDomain = model.RenderHints.EdgeDomainValue;
            if (DrawEdgeDomainCombo(&edgeDomain) && canEditRenderHints)
            {
                (void)ApplyEditorRenderHintCommand(context.VisualizationCommands,
                    EditorRenderHintCommand{
                        .StableEntityId = model.SelectedStableId,
                        .SetEdges = true,
                        .EnableEdges = true,
                        .EdgeDomain = edgeDomain,
                    });
            }
        }

// The uniform edge width, shown on the Line width row while no property is
// bound there (a bound width is drawn from its source instead).
        void DrawUniformEdgeWidthField(
            const EditorDomainWindowModel& model, const SandboxEditorContext& context, const bool bound)
        {
            if (!model.RenderHints.HasUniformEdgeWidth)
                return;
            float edgeWidth = model.RenderHints.UniformEdgeWidth;
            ImGui::BeginDisabled(bound || !context.SceneAvailable || !model.VisualizationControlsAvailable);
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool changed = ImGui::DragFloat(
                "##Edge width", &edgeWidth, 0.05f, 0.01f, 32.0f, "%.3f px", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
            if (changed && !bound)
            {
                (void)ApplyEditorRenderHintCommand(context.VisualizationCommands,
                    EditorRenderHintCommand{
                        .StableEntityId = model.SelectedStableId,
                        .SetUniformEdgeWidth = true,
                        .UniformEdgeWidth = edgeWidth,
                    });
            }
        }

        void DrawMeshRenderHintControls(
            const EditorDomainWindowModel& model, const SandboxEditorContext& context, const bool canEditRenderHints)
        {
            if (model.RenderHints.HasRenderSurface)
            {
                SurfaceDomain domain = model.RenderHints.SurfaceDomainValue;
                if (DrawSurfaceDomainCombo(&domain) && canEditRenderHints)
                {
                    (void)ApplyEditorRenderHintCommand(context.VisualizationCommands,
                        EditorRenderHintCommand{
                            .StableEntityId = model.SelectedStableId,
                            .SetSurface = true,
                            .EnableSurface = true,
                            .SurfaceDomain = domain,
                        });
                }
            }
        }

        void DrawPointRenderHintControls(
            const EditorDomainWindowModel& model, const SandboxEditorContext& context, const bool canEditRenderHints)
        {
            PointRenderType pointType = model.RenderHints.PointRenderTypeValue;
            if (DrawPointTypeCombo(&pointType) && canEditRenderHints)
            {
                (void)ApplyEditorRenderHintCommand(context.VisualizationCommands,
                    EditorRenderHintCommand{
                        .StableEntityId = model.SelectedStableId,
                        .PointType = pointType,
                        .SetPointRenderType = true,
                    });
            }
        }

// The uniform point size, shown on the Point size row while no property is bound.
        void DrawUniformPointSizeField(
            const EditorDomainWindowModel& model, const SandboxEditorContext& context, const bool bound)
        {
            if (!model.RenderHints.HasUniformPointSize)
                return;
            float pointSize = model.RenderHints.UniformPointSize;
            ImGui::BeginDisabled(bound || !context.SceneAvailable || !model.VisualizationControlsAvailable);
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool changed = ImGui::DragFloat(
                "##Point size", &pointSize, 0.05f, 0.01f, 32.0f, "%.3f px", ImGuiSliderFlags_AlwaysClamp);
            ImGui::EndDisabled();
            if (changed && !bound)
            {
                (void)ApplyEditorRenderHintCommand(context.VisualizationCommands,
                    EditorRenderHintCommand{
                        .StableEntityId = model.SelectedStableId,
                        .SetUniformPointSize = true,
                        .UniformPointSize = pointSize,
                    });
            }
        }

// The uniform color of a Color row. Editing it makes that color the overlay
// lane's Default, which unbinding a property later restores; while a property
// is bound the field is disabled and shows that remembered color.
        void DrawUniformColorField(const std::string& label,
            const EditorDomainWindowModel& lane,
            const SandboxEditorContext& context,
            EditorCommandStatus& status,
            const bool bound)
        {
            const auto& visualization = lane.Visualization.Visualization;
            ImGui::SeparatorText(label.c_str());
            glm::vec4 color = visualization.Config.Color;
            ImGui::BeginDisabled(bound || !context.SceneAvailable || !lane.VisualizationControlsAvailable);
            ImGui::SetNextItemWidth(-FLT_MIN);
            const bool changed = ImGui::ColorEdit4("##Uniform color", &color.x);
            if (!bound && visualization.Config.Source != kUniformColorSource)
            {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("Drawn with its default color until you pick one.");
                ImGui::PopTextWrapPos();
            }
            ImGui::EndDisabled();
            // A disabled widget can still be activated programmatically; a bound row never writes.
            if (changed && !bound)
            {
                status = ApplyEditorVisualizationConfigCommand(context.VisualizationCommands,
                    MakeUniformVisualizationConfigCommandFromModel(
                        lane.SelectedStableId, visualization, lane.VisualizationTarget, color));
            }
        }

        void DrawGraphRenderHintControls(
            const EditorDomainWindowModel& model, const SandboxEditorContext& context, const bool canEditRenderHints)
        {
            DrawEdgeRenderHintControls(model, context, canEditRenderHints);
        }

        void DrawVisualizationPropertyDropdown(
            const EditorDomainWindowModel& model, const SandboxEditorContext& context, EditorCommandStatus& lastStatus)
        {
            const auto& visualization = model.Visualization.Visualization;
            const auto& properties = model.Visualization.Properties;
            const bool scalar = visualization.Config.Source == kScalarFieldSource;
            const bool color = static_cast<int>(visualization.Config.Source) >= 3;
            const std::string& propertyName = scalar ? visualization.Config.ScalarFieldName : visualization.Config.ColorBufferName;
            const char* preview = !visualization.HasConfig || (!scalar && !color)
                ? (visualization.Config.Source == kUniformColorSource ? "Uniform color" : "Material / default")
                : propertyName.c_str();
            if (!ImGui::BeginCombo("Property", preview))
                return;
            if (ImGui::Selectable(
                    "Material / default", !visualization.HasConfig || visualization.Config.Source == kMaterialSource))
            {
                // Keeps the lane's chosen color for a later "Uniform color".
                lastStatus = ApplyEditorVisualizationConfigCommand(context.VisualizationCommands,
                    EditorVisualizationConfigCommand{.StableEntityId = model.SelectedStableId,
                        .Target = model.VisualizationTarget,
                        .EnableConfig = true,
                        .Config = {.Source = kMaterialSource, .Color = visualization.Config.Color}});
            }
            if (ImGui::Selectable("Uniform color", visualization.Config.Source == kUniformColorSource))
            {
                lastStatus = ApplyEditorVisualizationConfigCommand(context.VisualizationCommands,
                    MakeUniformVisualizationConfigCommandFromModel(
                        model.SelectedStableId, visualization, model.VisualizationTarget, visualization.Config.Color));
            }
            for (const auto& property : properties)
            {
                if (!property.ScalarPresetAvailable && !property.ColorBufferPresetAvailable)
                    continue;
                const std::string label =
                    property.Name + "  (" + DebugNameForEditorVisualizationPropertyDomain(property.Domain) + ")";
                const int propertyDomain = property.Domain == EditorVisualizationPropertyDomain::MeshFaces ? 2
                    : (property.Domain == EditorVisualizationPropertyDomain::MeshEdges ||
                          property.Domain == EditorVisualizationPropertyDomain::GraphEdges)
                    ? 1
                    : 0;
                const int activeDomain =
                    scalar ? static_cast<int>(visualization.Config.ScalarDomain) : static_cast<int>(visualization.Config.Source) - 3;
                const bool selected =
                    property.Name == propertyName && (scalar || color) && propertyDomain == activeDomain;
                if (ImGui::Selectable(label.c_str(), selected))
                {
                    lastStatus = ApplyEditorVisualizationPropertyCommand(context.VisualizationCommands,
                        EditorVisualizationPropertyCommand{
                            .StableEntityId = model.SelectedStableId,
                            .Target = model.VisualizationTarget,
                            .Domain = property.Domain,
                            .Preset = property.ScalarPresetAvailable
                                ? (visualization.Config.Scalar.Isolines.Num > 0u ? EditorVisualizationPropertyPreset::Isoline
                                                                   : EditorVisualizationPropertyPreset::Scalar)
                                : EditorVisualizationPropertyPreset::ColorBuffer,
                            .PropertyName = property.Name,
                            .ScalarAutoRange = visualization.Config.Scalar.AutoRange,
                            .ScalarRangeMin = visualization.Config.Scalar.RangeMin,
                            .ScalarRangeMax = visualization.Config.Scalar.RangeMax,
                            .ScalarBinCount = visualization.Config.Scalar.BinCount,
                            .IsolineCount = visualization.Config.Scalar.Isolines.Num,
                        });
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        void DrawScalarVisualizationControls(const EditorVisualizationConfigModel& visualization,
            const SandboxEditorContext& context,
            const std::uint32_t selectedStableId,
            const EditorVisualizationTarget target,
            const bool canEditVisualization)
        {
            if (!visualization.HasConfig || visualization.Config.Source != kScalarFieldSource)
            {
                return;
            }
            DrawScalarFieldColorControls(visualization, context, selectedStableId, target, canEditVisualization);
            if (visualization.Config.UseBakedTexture)
            {
                ImGui::TextDisabled("Binning and isolines are available with attribute rendering.");
                return;
            }
            DrawScalarFieldBinAndIsolineControls(
                visualization, context, selectedStableId, target, canEditVisualization);
        }

        void DrawDomainVisualizationControls(
            const EditorDomainWindowModel& model, const SandboxEditorContext& context, EditorCommandStatus& lastStatus);

        struct VectorFieldUiState
        {
            std::size_t DomainIndex{0u};
            EditorCommandStatus LastStatus{EditorCommandStatus::NoChange};
            PointSamplingMethod OrderMethod{PointSamplingMethod::FarthestPoint};
            std::string OrderMessage{};
            // The entity and domain the message is about; it is hidden after a switch.
            std::uint32_t OrderMessageEntity{0u};
            GeometryElementDomain OrderMessageDomain{GeometryElementDomain::Unknown};
        };

        [[nodiscard]] EditorCommandStatus ApplyVectorFieldLayer(const SandboxEditorContext& context,
            const std::uint32_t stableId,
            const EditorVectorFieldOperation operation,
            const GeometryVectorFieldLayerRecipe& layer)
        {
            return ApplyEditorGeometryVectorFieldCommand(context.VisualizationCommands,
                EditorGeometryVectorFieldCommand{
                    .StableEntityId = stableId, .Operation = operation, .Layer = layer, .UseLayerStyle = true});
        }

// Edits one active field; every change goes through the validated command.
        void DrawVectorFieldLayer(const EditorVectorFieldLayerRow& row,
            const EditorVectorFieldDomainOption* domain,
            const SandboxEditorContext& context,
            const std::uint32_t stableId,
            VectorFieldUiState& state)
        {
            bool keep = true;
            const std::string header =
                row.DomainLabel + ": " + row.Layer.Vector.Name + "###" + row.DomainLabel + row.Layer.Vector.Name;
            const bool open = ImGui::CollapsingHeader(header.c_str(), &keep, ImGuiTreeNodeFlags_DefaultOpen);
            if (!keep)
            {
                state.LastStatus =
                    ApplyVectorFieldLayer(context, stableId, EditorVectorFieldOperation::Remove, row.Layer);
                return;
            }
            if (!open)
                return;
            if (!row.Status.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", row.Status.c_str());

            GeometryVectorFieldLayerRecipe layer = row.Layer;
            // Names and bounds come from the declared style fields (RUNTIME-327).
            const auto spec = [](const std::string_view name)
            { return *FindConfigFieldSpec(GeometryVectorFieldStyleFields(), name); };
            bool changed = ImGui::Checkbox("Visible", &layer.Enabled);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Depth test", &layer.DepthTested);

            changed |= DrawSpecEnumCombo("Length mode", GeometryVectorFieldStyleFields(), "length_mode", layer.LengthMode,
                GeometryVectorFieldLengthMode::Normalized);
            // UI-083: per-arrow inputs from properties on the field's domain; Default is none.
            static const std::vector<EditorVectorFieldStyleOption> kNoOptions{};
            const std::vector<EditorVectorFieldStyleOption>& styleOptions =
                domain != nullptr ? domain->StyleProperties : kNoOptions;
            const auto styleCombo = [&](const char* label, std::string& property, const bool scalarOnly)
            {
                if (!ImGui::BeginCombo(label, property.empty() ? "Default" : property.c_str()))
                    return false;
                bool picked = false;
                if (ImGui::Selectable("Default", property.empty()))
                {
                    property.clear();
                    picked = true;
                }
                for (const EditorVectorFieldStyleOption& option : styleOptions)
                {
                    if (scalarOnly && !option.Scalar)
                        continue;
                    if (ImGui::Selectable(option.Name.c_str(), option.Name == property))
                    {
                        property = option.Name;
                        picked = true;
                    }
                }
                ImGui::EndCombo();
                return picked;
            };
            changed |= styleCombo("Length property", layer.LengthProperty, true);
            // The uniform length multiplies a bound property, so it stays editable. The declared rule is
            // only "positive and finite"; 1e-6..1e9 is this drag's own range (a drag needs finite ends).
            const char* lengthLabel =
                layer.LengthMode == GeometryVectorFieldLengthMode::Normalized && layer.LengthProperty.empty()
                ? "Length###length"
                : "Scale###length";
            changed |= ImGui::DragFloat(lengthLabel,
                &layer.Length,
                std::max(layer.Length * 0.01f, 1.0e-6f),
                1.0e-6f,
                1.0e9f,
                "%.4g",
                ImGuiSliderFlags_AlwaysClamp);
            // RUNTIME-331: one slider per pass-through style float, bounded by its spec.
            for (const GeometryVectorFieldStyleSlider& slider : GeometryVectorFieldStyleSliders())
            {
                const ConfigFieldSpec& field = spec(slider.Field);
                changed |= ImGui::SliderFloat(slider.Label.data(),
                    &(layer.*slider.Member),
                    static_cast<float>(*field.Min),
                    static_cast<float>(*field.Max),
                    "%.1f",
                    ImGuiSliderFlags_AlwaysClamp);
            }
            changed |= styleCombo("Color property", layer.ColorProperty, false);
            // Question 7: the uniform color is active only on Default and keeps its value meanwhile;
            // a bound layer's color is not written.
            const bool colorBound = !layer.ColorProperty.empty();
            glm::vec4 uniformColor = layer.Color;
            ImGui::BeginDisabled(colorBound);
            const bool colorEdited = ImGui::ColorEdit4("Color", &uniformColor.x);
            ImGui::EndDisabled();
            if (colorEdited && !colorBound)
            {
                layer.Color = uniformColor;
                changed = true;
            }
            // A scalar (or a name without an option: a GPU-only scalar) goes through the colormap.
            const auto bound =
                std::ranges::find(styleOptions, layer.ColorProperty, &EditorVectorFieldStyleOption::Name);
            if (!layer.ColorProperty.empty() && (bound == styleOptions.end() || bound->Scalar))
            {
                changed |= DrawSpecEnumCombo("Colormap", GeometryVectorFieldStyleFields(), "colormap", layer.ColorMap,
                    decltype(layer.ColorMap)::Viridis);
                changed |= ImGui::Checkbox("Auto range", &layer.ColorAutoRange);
                if (!layer.ColorAutoRange)
                {
                    float range[2]{layer.ColorRangeMin, layer.ColorRangeMax};
                    if (ImGui::DragFloat2("Range min/max", range, 0.01f, 0.0f, 0.0f, "%.4g"))
                    {
                        layer.ColorRangeMin = range[0];
                        layer.ColorRangeMax = range[1];
                        changed = true;
                    }
                }
            }

            // With a valid arrow order the slider draws its first N live elements (MaxGlyphs, 0 = all;
            // an older layer's stride shows as the count it draws); without one the layer keeps every
            // n-th element.
            if (domain != nullptr && domain->OrderRows > 0u)
            {
                const int all = static_cast<int>(std::min<std::uint32_t>(
                    domain->OrderRows, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
                const std::uint32_t strided = (domain->OrderRows + layer.Stride - 1u) / std::max(layer.Stride, 1u);
                const std::uint32_t drawn = layer.MaxGlyphs > 0u ? std::min(strided, layer.MaxGlyphs) : strided;
                int arrows = static_cast<int>(std::min<std::uint32_t>(drawn, static_cast<std::uint32_t>(all)));
                if (ImGui::SliderInt(
                        "Arrows", &arrows, 1, all, "%d", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp))
                {
                    layer.Stride = 1u;
                    layer.MaxGlyphs = arrows >= all ? 0u : static_cast<std::uint32_t>(arrows);
                    changed = true;
                }
            }
            else
            {
                int stride = static_cast<int>(
                    std::min<std::uint32_t>(layer.Stride, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
                if (ImGui::InputInt("Every Nth element", &stride))
                {
                    const ConfigFieldSpec& strides = spec("stride");
                    layer.Stride = static_cast<std::uint32_t>(
                        std::clamp(stride, static_cast<int>(*strides.Min), static_cast<int>(*strides.Max)));
                    changed = true;
                }
                int maxGlyphs = static_cast<int>(std::min<std::uint32_t>(
                    layer.MaxGlyphs, static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
                if (ImGui::InputInt("Max arrows (0 = all)", &maxGlyphs))
                {
                    layer.MaxGlyphs = static_cast<std::uint32_t>(std::max(maxGlyphs, 0));
                    changed = true;
                }
            }
            if (changed)
                state.LastStatus = ApplyVectorFieldLayer(context, stableId, EditorVectorFieldOperation::Update, layer);
        }

// Framework24-style vector fields: choose an element domain, then one of its
// vec3 properties to add. Fields are drawn regardless of lane visibility.
        void DrawVectorFieldSection(
            const EditorDomainWindowModel& model, const SandboxEditorContext& context, VectorFieldUiState& state)
        {
            if (!ImGui::CollapsingHeader("Vector fields"))
                return;
            const EditorVectorFieldModel& fields = model.VectorFields;
            if (!fields.Available)
            {
                ImGui::TextDisabled("No vertex, edge, face or point elements to attach arrows to.");
                return;
            }
            ImGui::PushID("vector-fields");
            ImGui::BeginDisabled(!model.VisualizationControlsAvailable);

            state.DomainIndex = std::min(state.DomainIndex, fields.Domains.size() - 1u);
            const EditorVectorFieldDomainOption& domain = fields.Domains[state.DomainIndex];
            if (ImGui::BeginCombo("Domain", domain.Label.c_str()))
            {
                for (std::size_t i = 0u; i < fields.Domains.size(); ++i)
                {
                    const std::string label =
                        fields.Domains[i].Label + " (" + std::to_string(fields.Domains[i].ElementCount) + ")";
                    if (ImGui::Selectable(label.c_str(), i == state.DomainIndex))
                        state.DomainIndex = i;
                }
                ImGui::EndCombo();
            }

            // One arrow order per domain, shared by its layers until recomputed (UI-081).
            if (ImGui::BeginCombo("Arrow order method", std::string(DisplayName(state.OrderMethod)).c_str()))
            {
                for (int m = 0; m <= static_cast<int>(PointSamplingMethod::Tournament); ++m)
                {
                    const auto method = static_cast<PointSamplingMethod>(m);
                    if (ImGui::Selectable(std::string(DisplayName(method)).c_str(), method == state.OrderMethod))
                        state.OrderMethod = method;
                }
                ImGui::EndCombo();
            }
            if (ImGui::Button("Compute arrow order"))
            {
                const EditorGlyphOrderResult result = ApplyEditorGlyphOrderCommand(context.Processing,
                    EditorGlyphOrderCommand{.StableEntityId = model.SelectedStableId,
                        .Domain = domain.Domain,
                        .Method = state.OrderMethod});
                state.OrderMessage = result.Message;
                state.OrderMessageEntity = model.SelectedStableId;
                state.OrderMessageDomain = domain.Domain;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s",
                domain.OrderRows > 0u     ? "even spacing ready"
                    : domain.OrderPresent ? "stale: elements changed, recompute"
                                          : "every n-th element");
            if (!state.OrderMessage.empty() && state.OrderMessageEntity == model.SelectedStableId &&
                state.OrderMessageDomain == domain.Domain)
            {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextDisabled("%s", state.OrderMessage.c_str());
                ImGui::PopTextWrapPos();
            }

            if (domain.Properties.empty())
            {
                ImGui::TextDisabled("%s have no vec3 properties.", domain.Label.c_str());
            }
            else if (ImGui::BeginCombo("Property", "Add vec3 property..."))
            {
                for (const EditorVectorFieldPropertyOption& property : domain.Properties)
                {
                    if (!property.Compatible)
                    {
                        ImGui::TextDisabled("%s (%s)", property.Name.c_str(), property.DisabledReason.c_str());
                        continue;
                    }
                    const ImGuiSelectableFlags flags = property.Active ? ImGuiSelectableFlags_Disabled : 0;
                    const std::string label = property.Active ? property.Name + " (shown)" : property.Name;
                    if (ImGui::Selectable(label.c_str(), false, flags))
                    {
                        state.LastStatus = ApplyEditorGeometryVectorFieldCommand(context.VisualizationCommands,
                            EditorGeometryVectorFieldCommand{
                                .StableEntityId = model.SelectedStableId,
                                .Operation = EditorVectorFieldOperation::Add,
                                .Layer =
                                    GeometryVectorFieldLayerRecipe{
                                        .Vector =
                                            GeometryPropertyRef{
                                                .Domain = domain.Domain,
                                                .Name = property.Name,
                                                .ValueKind = Geometry::PropertyValueKind::Vec3,
                                            },
                                    },
                            });
                    }
                }
                ImGui::EndCombo();
            }

            for (const EditorVectorFieldLayerRow& row : fields.Layers)
            {
                const auto option =
                    std::ranges::find(fields.Domains, row.Layer.Vector.Domain, &EditorVectorFieldDomainOption::Domain);
                ImGui::PushID((row.DomainLabel + row.Layer.Vector.Name).c_str());
                DrawVectorFieldLayer(
                    row, option == fields.Domains.end() ? nullptr : &*option, context, model.SelectedStableId, state);
                ImGui::PopID();
            }
            if (state.LastStatus != EditorCommandStatus::Applied && state.LastStatus != EditorCommandStatus::NoChange)
                ImGui::TextWrapped("Vector field change failed: %s", DebugNameForEditorCommandStatus(state.LastStatus));

            ImGui::EndDisabled();
            ImGui::PopID();
        }

// What the Color row of a bound overlay adds: how a color-buffer property is
// read, and the colormap/range of a scalar one. Both belong to the lane overlay
// that the row binds, so they sit with the row instead of in lane settings.
        void DrawColorRowControls(
            const EditorDomainWindowModel& model, const SandboxEditorContext& context, EditorCommandStatus& lastStatus)
        {
            const auto& visualization = model.Visualization.Visualization;
            const bool available = model.VisualizationTargetAvailable && model.VisualizationControlsAvailable;
            ImGui::BeginDisabled(!available);
  // Leave room for the right-hand labels so nothing clips at the default width.
            ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x * 0.5f);
            if (visualization.HasConfig && static_cast<int>(visualization.Config.Source) >= 3)
            {
                int interpretation = static_cast<int>(visualization.Config.Interpretation);
                if (DrawColorInterpretationCombo(interpretation))
                {
                    auto command = MakeVisualizationConfigCommandFromModel(
                        model.SelectedStableId, visualization, model.VisualizationTarget);
                    command.Config.Interpretation = static_cast<decltype(command.Config.Interpretation)>(interpretation);
                    lastStatus = ApplyEditorVisualizationConfigCommand(context.VisualizationCommands, command);
                }
            }
            if (visualization.Config.Source == kScalarFieldSource && ImGui::CollapsingHeader("Color mapping"))
                DrawScalarVisualizationControls(
                    visualization, context, model.SelectedStableId, model.VisualizationTarget, available);
            ImGui::PopItemWidth();
            ImGui::EndDisabled();
        }

// One element-domain section of the Appearance window. `Kind` selects the
// per-lane window model (its render hints and visualization target);
// `ElementProbe` is the element domain the UI-051 reading predicate is asked
// about, so a section shows exactly where the entity carries that data.
        struct AppearanceSection
        {
            const char* Title;
            const char* LaneLabel;
            EditorDomainWindowKind Kind;
            GeometryElementDomain ElementProbe;
            GeometryRenderLane Lane;
        };

        inline constexpr std::array<AppearanceSection, 3> kAppearanceSections{{
            {"Vertices",
                "Points",
                EditorDomainWindowKind::PointCloud,
                GeometryElementDomain::PointCloudPoint,
                GeometryRenderLane::Points},
            {"Edges",
                "Edges",
                EditorDomainWindowKind::Graph,
                GeometryElementDomain::GraphEdge,
                GeometryRenderLane::Edges},
            {"Faces",
                "Surface",
                EditorDomainWindowKind::Mesh,
                GeometryElementDomain::MeshFace,
                GeometryRenderLane::Surface},
        }};

        [[nodiscard]] bool AppearanceSectionApplies(
            const AppearanceSection& section, const decltype(EditorDomainWindowModel{}.SelectedDomain) entity)
        {
            return GeometryDomainReadingIncludes(entity, section.ElementProbe);
        }

// Everything one Appearance draw borrows; `Lanes[i]` is the model of
// `kAppearanceSections[i]` (null when the reading predicate excludes it).
        struct AppearanceDraw
        {
            std::array<const EditorDomainWindowModel*, 3> Lanes;
            const SandboxEditorContext& Context;
            TextureBakeUiState* TextureBake;
            std::array<EditorCommandStatus, 3>& Statuses;
            AttributeSourceUiState& AttributeState;
        };

        [[nodiscard]] const EditorDomainWindowModel* FindLaneForTarget(
            const AppearanceDraw& draw, const EditorVisualizationTarget target)
        {
            for (const EditorDomainWindowModel* lane : draw.Lanes)
            {
                if (lane != nullptr && lane->VisualizationTarget == target)
                    return lane;
            }
            return nullptr;
        }

// One lane's visibility toggle, attribute table (with the controls that belong
// to each attribute row) and settings (render hints, property, advanced state).
        void DrawAppearanceLane(
            const AppearanceSection& section, const EditorDomainWindowModel& model, const AppearanceDraw& draw)
        {
            const SandboxEditorContext& context = draw.Context;
            EditorCommandStatus& status = draw.Statuses[static_cast<std::size_t>(model.Kind)];
            const bool mesh = model.Kind == EditorDomainWindowKind::Mesh;
            const bool graph = model.Kind == EditorDomainWindowKind::Graph;
            bool visible = mesh ? model.RenderHints.HasRenderSurface
                : graph         ? model.RenderHints.HasRenderEdges
                                : model.RenderHints.HasRenderPoints;
            if (ImGui::Checkbox(section.LaneLabel, &visible))
            {
                status = ApplyEditorRenderHintCommand(context.VisualizationCommands,
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
            if (status != EditorCommandStatus::Applied && status != EditorCommandStatus::NoChange)
                ImGui::TextWrapped("Appearance change failed: %s", DebugNameForEditorCommandStatus(status));
  // A bound color, size or width is drawn from its source (the row's selector);
  // the uniform field stays visible but is disabled, keeping the value Default restores.
            const auto rowDetails = [&](const EditorAttributeBindingRow& row, const std::string& label)
            {
                switch (row.Attribute)
                {
                case RenderAttribute::Color:
                    if (row.OverlayTarget.has_value())
                    {
                        if (const EditorDomainWindowModel* lane = FindLaneForTarget(draw, *row.OverlayTarget))
                        {
                            EditorCommandStatus& laneStatus = draw.Statuses[static_cast<std::size_t>(lane->Kind)];
                            DrawUniformColorField(label, *lane, context, laneStatus, row.Bound);
                            if (row.Bound)
                                DrawColorRowControls(*lane, context, laneStatus);
                        }
                    }
                    break;
                case RenderAttribute::PointSize:
                    if (model.RenderHints.HasUniformPointSize)
                    {
                        ImGui::SeparatorText(label.c_str());
                        DrawUniformPointSizeField(model, context, row.Bound);
                    }
                    break;
                case RenderAttribute::LineWidth:
                    if (model.RenderHints.HasUniformEdgeWidth)
                    {
                        ImGui::SeparatorText(label.c_str());
                        DrawUniformEdgeWidthField(model, context, row.Bound);
                    }
                    break;
                default: break;
                }
            };
            DrawAttributeSourceTable(
                model.PropertyCatalog.AttributeBindings, section.Lane, &context, draw.AttributeState, rowDetails);
            if (!visible || !ImGui::TreeNode("Settings"))
                return;
            switch (model.Kind)
            {
            case EditorDomainWindowKind::Mesh:       DrawMeshRenderHintControls(model, context, true); break;
            case EditorDomainWindowKind::Graph:      DrawGraphRenderHintControls(model, context, true); break;
            case EditorDomainWindowKind::PointCloud: DrawPointRenderHintControls(model, context, true); break;
            }
            DrawDomainVisualizationControls(model, context, status);
            if (ImGui::CollapsingHeader("Advanced"))
            {
                DrawRenderHintStatus(model.RenderHints);
                DrawBoundRenderStateRows(model.BoundState);
                if (mesh)
                {
                    static TextureBakeMutationUiState mutationState{};
                    DrawTextureBakeControls(model.TextureBake, &context, draw.TextureBake, mutationState);
                }
            }
            ImGui::TreePop();
        }

// The one Appearance window: a section per element domain the selected entity
// carries (UI-051 reading predicate), each with its attribute source table,
// then the entity-level vector fields. `lanes[i]` is the model of `kAppearanceSections[i]`, or
// null when the reading predicate excludes it; a section the predicate reaches
// but the model refuses draws disabled with the runtime's reason.
// `selected` names the entity (any lane's model carries it).
        void DrawAppearanceContent(
            const EditorDomainWindowModel& selected, const AppearanceDraw& draw, VectorFieldUiState& vectorFieldState)
        {
            const SandboxEditorContext& context = draw.Context;
            if (!selected.HasSelectedEntity)
            {
                ImGui::TextDisabled("Select a mesh, graph, or point cloud.");
                return;
            }
            ImGui::TextUnformatted(selected.SelectedEntity.Name.c_str());
            bool anySection = false;
            for (std::size_t i = 0u; i < kAppearanceSections.size(); ++i)
            {
                const EditorDomainWindowModel* model = draw.Lanes[i];
                if (model == nullptr)
                    continue;
                anySection = true;
                if (!model->DomainUsable || !DomainAppearanceReady(*model))
                {
                    DrawUnavailableAppearanceSection(kAppearanceSections[i].Title, *model);
                    continue;
                }
                if (!ImGui::CollapsingHeader(kAppearanceSections[i].Title, ImGuiTreeNodeFlags_DefaultOpen))
                    continue;
                ImGui::PushID(static_cast<int>(model->Kind));
                DrawAppearanceLane(kAppearanceSections[i], *model, draw);
                ImGui::PopID();
            }
            if (!anySection)
            {
                ImGui::TextDisabled("No element domain to show for this entity.");
                DrawDiagnostics(selected.Diagnostics);
            }
            DrawVectorFieldSection(selected, context, vectorFieldState);
        }

        void DrawDomainVisualizationControls(
            const EditorDomainWindowModel& model, const SandboxEditorContext& context, EditorCommandStatus& lastStatus)
        {
            const auto& visualization = model.Visualization.Visualization;
            const bool available = model.VisualizationTargetAvailable && model.VisualizationControlsAvailable;
            ImGui::BeginDisabled(!available);
            DrawVisualizationPropertyDropdown(model, context, lastStatus);
            DrawUniformVisualizationColorEdit(
                visualization, context, model.SelectedStableId, model.VisualizationTarget, available);
            if (model.Kind == EditorDomainWindowKind::Mesh && ImGui::CollapsingHeader("Texture baking"))
            {
                bool baked = visualization.Config.UseBakedTexture;
                const bool hasProperty = ColoredByProperty(visualization);
                const bool canEnable = hasProperty && model.TextureBake.CanBake;
                ImGui::BeginDisabled(!baked && !canEnable);
                if (ImGui::Checkbox("Use baked texture", &baked))
                {
                    auto command = MakeVisualizationConfigCommandFromModel(
                        model.SelectedStableId, visualization, model.VisualizationTarget);
                    command.Config.UseBakedTexture = baked;
                    lastStatus = ApplyEditorVisualizationConfigCommand(context.VisualizationCommands, command);
                }
                ImGui::EndDisabled();
                if (!canEnable && !baked)
                {
                    ImGui::TextDisabled(
                        "%s", !hasProperty ? "Select a property to bake." : model.TextureBake.DisabledReason.c_str());
                }
                if (baked)
                {
                    const auto output = std::ranges::find(model.TextureBake.BakedTextures,
                        kSurfaceAppearanceTextureOutput,
                        &PropertyTextureBakeRecord::OutputName);
                    if (output != model.TextureBake.BakedTextures.end())
                        ImGui::TextWrapped("%s", output->Diagnostic.c_str());
                }
            }
            ImGui::EndDisabled();
        }

        void DrawPrimitiveDetails(const EditorPrimitiveDetailModel& primitive)
        {
            if (!primitive.HasPrimitive)
            {
                ImGui::TextDisabled("No refined primitive selection for this domain.");
                return;
            }

            const PrimitiveSelectionResult& result = primitive.Primitive;
            ImGui::Text("Primitive status: %s", DebugNameForPrimitiveRefineStatus(result.Status));
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
            if (result.HasHitPosition)
            {
                DrawVec3("Local hit", result.LocalHit);
                DrawVec3("World hit", result.WorldHit);
            }
        }

        void DrawDomainSelectionWindow(const EditorDomainWindowModel& model,
            const SandboxEditorContext& context,
            int& index,
            int& domainIndex,
            std::string& message)
        {
            DrawDomainWindowHeader(model);
            ImGui::SeparatorText("Selection tool");
            auto config = GetEditorSelectionInteractionConfig(context.Processing);
            int target = static_cast<int>(config.Target);
            bool changed = ImGui::Combo("Pick", &target, "Entities\0Vertices / points\0Edges\0Faces\0");
            config.Target = static_cast<SelectionTarget>(target);
            changed |= ImGui::Checkbox("Highlight selected primitives", &config.Highlight);
            changed |= ImGui::InputFloat("Point radius (world units)", &config.PointRadius, 0.001f, 0.01f, "%.4f");
            if (changed)
            {
                const auto applied = ApplyEditorSelectionInteractionConfig(context.Processing, config);
                message =
                    applied.Succeeded() ? "Selection settings applied." : "Selection settings could not be applied.";
            }
            ImGui::TextWrapped("Click to replace; Shift-click to add; Ctrl-click to toggle. A background "
                               "click clears primitive selections. Vertex and edge picks on a surface use "
                               "the nearest corner or edge of the hit face.");
            if (!DomainWindowReady(model))
                return; // the header already states the runtime's reason
            ImGui::SeparatorText("Selected elements");
            constexpr GeometryElementDomain domains[] = {GeometryElementDomain::MeshVertex,
                GeometryElementDomain::MeshEdge,
                GeometryElementDomain::MeshHalfedge,
                GeometryElementDomain::MeshFace,
                GeometryElementDomain::GraphNode,
                GeometryElementDomain::GraphEdge,
                GeometryElementDomain::GraphHalfedge,
                GeometryElementDomain::PointCloudPoint};
            std::vector<GeometryElementDomain> available;
            for (auto domain : domains)
            {
                if (!GeometryDomainReadingIncludes(model.ExpectedDomain, domain))
                    continue;
                const auto selection = ReadEditorPrimitiveSelection(context.Processing, model.SelectedStableId, domain);
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
                    if (ImGui::Selectable(ToString(available[i]).data(), domainIndex == static_cast<int>(i)))
                        domainIndex = static_cast<int>(i);
                ImGui::EndCombo();
            }
            const auto domain = available[domainIndex];
            const auto edit = [&](PrimitiveSelectionEdit operation, std::span<const std::uint32_t> indices = {})
            {
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
            const auto selected = ReadEditorPrimitiveSelection(context.Processing, model.SelectedStableId, domain);
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

    struct DomainPanels::Impl
    {
        PropertyInspectorUiState PropertyInspector{};
        PropertyBrowserState PropertyBrowser{};
        VectorFieldUiState VectorFieldState{};
        AttributeSourceUiState AttributeSourceState{};
        std::array<EditorCommandStatus, 3> AppearanceStatuses{
            EditorCommandStatus::NoChange, EditorCommandStatus::NoChange, EditorCommandStatus::NoChange};
        std::uint32_t AppearanceEntity{0u};
        int SelectionElementIndex{0};
        int SelectionDomainIndex{0};
        std::string SelectionMessage{};

        enum class Section : std::uint8_t
        {
            Properties,
            Selection,
        };

        EditorShell* Shell{nullptr};
        std::vector<Runtime::EditorWindowHandle> Handles{};
        int CachedModelFrame{-1};
        std::array<std::optional<Runtime::EditorDomainWindowModel>, 3u> CachedDomainModels{};

        Runtime::EditorPropertyPlotWidgetState MeshPropertyPlotState{};
        std::optional<Runtime::EditorUvRegenerationCommandResult> LastUvRegenerationResult{};
        std::optional<Runtime::EditorUvRegenerationCommandResult> LastUvExtentAdoption{};
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
        bool UvAtlasForceRegenerate{true};
        bool UvAtlasPreserveAuthored{false};

        void Register(EditorShell& editorShell);
        void Unregister();
        void RegisterWindow(std::string id,
            std::vector<std::string> menuPath,
            std::string title,
            Runtime::EditorDomainWindowKind kind,
            Section section);
        void ResetModelCache();
        [[nodiscard]] const Runtime::EditorDomainWindowModel& GetDomainWindowModel(
            const SandboxEditorContext& context, Runtime::EditorDomainWindowKind kind);
        void DrawWindow(bool& open,
            const SandboxEditorContext& context,
            Runtime::EditorDomainWindowKind kind,
            Section section,
            const char* title);
        void RegisterAppearanceWindow();
        void DrawAppearanceWindow(bool& open, const SandboxEditorContext& context);
    };

    void DomainPanels::Impl::Register(EditorShell& editorShell)
    {
        Unregister();
        Shell = &editorShell;

        RegisterAppearanceWindow();
        Handles.push_back(Shell->RegisterEditorWindow(EditorWindowDescriptor{.Id = "view.property_inspector",
            .MenuPath = {"View"},
            .Title = "Property Inspector",
            .OpenByDefault = false,
            .Draw = [this](bool& open, const SandboxEditorContext& context)
            {
                DrawPropertyInspectorWindow(open, context, PropertyInspector);
            }}));
        RegisterWindow("pointcloud.properties",
            {"PointCloud"},
            "Properties",
            Runtime::EditorDomainWindowKind::PointCloud,
            Section::Properties);
        RegisterWindow("pointcloud.selection",
            {"PointCloud"},
            "Selection",
            Runtime::EditorDomainWindowKind::PointCloud,
            Section::Selection);

        RegisterWindow(
            "graph.properties", {"Graph"}, "Properties", Runtime::EditorDomainWindowKind::Graph, Section::Properties);
        RegisterWindow(
            "graph.selection", {"Graph"}, "Selection", Runtime::EditorDomainWindowKind::Graph, Section::Selection);

        RegisterWindow(
            "mesh.properties", {"Mesh"}, "Properties", Runtime::EditorDomainWindowKind::Mesh, Section::Properties);
        RegisterWindow(
            "mesh.selection", {"Mesh"}, "Selection", Runtime::EditorDomainWindowKind::Mesh, Section::Selection);
    }

    void DomainPanels::Impl::Unregister()
    {
        if (Shell != nullptr)
        {
            for (const Runtime::EditorWindowHandle handle : Handles)
                (void)Shell->UnregisterEditorWindow(handle);
        }
        Handles.clear();
        Shell = nullptr;
        ResetModelCache();
        LastUvRegenerationResult.reset();
        LastUvExtentAdoption.reset();
        MeshPropertyPlotState.SelectedProperty.clear();
        PropertyInspector = {};
        PropertyBrowser = {};
    }

    void DomainPanels::Impl::RegisterWindow(std::string id,
        std::vector<std::string> menuPath,
        std::string title,
        const Runtime::EditorDomainWindowKind kind,
        const Section section)
    {
        const std::string windowTitle = std::string(Runtime::DebugNameForEditorDomainWindowKind(kind)) + " / " + title;

        Handles.push_back(Shell->RegisterEditorWindow(EditorWindowDescriptor{
            .Id = std::move(id),
            .MenuPath = std::move(menuPath),
            .Title = std::move(title),
            .OpenByDefault = false,
            .Draw =
                [this, kind, section, windowTitle](bool& open, const SandboxEditorContext& context)
            {
                DrawWindow(open, context, kind, section, windowTitle.c_str());
            },
            .OpenStateChanged =
                [this](bool)
            {
                ResetModelCache();
            },
        }));
    }

    void DomainPanels::Impl::ResetModelCache()
    {
        CachedModelFrame = -1;
        for (auto& model : CachedDomainModels)
            model.reset();
    }

    const Runtime::EditorDomainWindowModel& DomainPanels::Impl::GetDomainWindowModel(
        const SandboxEditorContext& context, const Runtime::EditorDomainWindowKind kind)
    {
        const int frame = ImGui::GetFrameCount();
        if (CachedModelFrame != frame)
        {
            CachedModelFrame = frame;
            for (auto& model : CachedDomainModels)
                model.reset();
        }

        auto& model = CachedDomainModels[static_cast<std::size_t>(kind)];
        if (!model.has_value())
        {
            model = Runtime::BuildEditorDomainWindowModel(context.SnapshotQueries, kind, context.ModelBuildStats);
        }
        else if (context.ModelBuildStats != nullptr)
        {
            ++context.ModelBuildStats->DomainWindowModelCacheHits;
        }
        return *model;
    }

    void DomainPanels::Impl::RegisterAppearanceWindow()
    {
        Handles.push_back(Shell->RegisterEditorWindow(EditorWindowDescriptor{
            .Id = "scene.appearance",
            .MenuPath = {"View"},
            .Title = "Appearance",
            .OpenByDefault = false,
            .Draw =
                [this](bool& open, const SandboxEditorContext& context)
            {
                DrawAppearanceWindow(open, context);
            },
            .OpenStateChanged =
                [this](bool)
            {
                ResetModelCache();
            },
        }));
    }

    void DomainPanels::Impl::DrawAppearanceWindow(bool& open, const SandboxEditorContext& context)
    {
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
            .UvForceRegenerate = &UvAtlasForceRegenerate,
            .UvPreserveAuthored = &UvAtlasPreserveAuthored,
        };

        ImGui::SetNextWindowSize(ImVec2(340.0f, 300.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Appearance", &open))
        {
    // The first lane's model names the selection; each section's model is
    // built only where the entity's reading reaches it.
            const EditorDomainWindowModel& first = GetDomainWindowModel(context, kAppearanceSections[0].Kind);
            if (AppearanceEntity != first.SelectedStableId)
            {
                AppearanceStatuses.fill(EditorCommandStatus::NoChange);
                VectorFieldState.LastStatus = EditorCommandStatus::NoChange;
                AppearanceEntity = first.SelectedStableId;
            }
            std::array<const EditorDomainWindowModel*, 3> lanes{};
            for (std::size_t i = 0u; i < kAppearanceSections.size(); ++i)
            {
                if (first.HasSelectedEntity && AppearanceSectionApplies(kAppearanceSections[i], first.SelectedDomain))
                    lanes[i] = &GetDomainWindowModel(context, kAppearanceSections[i].Kind);
            }
            const EditorDomainWindowModel* mesh = lanes[2];
            DrawAppearanceContent(first,
                AppearanceDraw{lanes, context, &textureBakeState, AppearanceStatuses, AttributeSourceState},
                VectorFieldState);
            if (mesh != nullptr && mesh->DomainMatches && ImGui::CollapsingHeader("Property distribution"))
            {
                const auto properties = Runtime::ResolveEditorSelectedMeshVertexProperties(context.Processing);
                if (properties)
                {
                    (void)Runtime::DrawEditorScalarPropertyPlotWidget(
                        "mesh.appearance.properties", properties, MeshPropertyPlotState);
                }
            }
        }
        ImGui::End();
    }

    void DomainPanels::Impl::DrawWindow(bool& open,
        const SandboxEditorContext& context,
        const Runtime::EditorDomainWindowKind kind,
        const Section section,
        const char* title)
    {
        ImGui::SetNextWindowSize(ImVec2(340.0f, 300.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title, &open))
        {
            const Runtime::EditorDomainWindowModel& model = GetDomainWindowModel(context, kind);
            switch (section)
            {
            case Section::Properties: DrawDomainPropertyWindow(model, context, PropertyBrowser); break;
            case Section::Selection:
                DrawDomainSelectionWindow(
                    model, context, SelectionElementIndex, SelectionDomainIndex, SelectionMessage);
                break;
            }
        }
        ImGui::End();
    }

    DomainPanels::DomainPanels() : m_Impl(std::make_unique<Impl>()) {}

    DomainPanels::~DomainPanels()
    {
        m_Impl->Unregister();
    }

    void DomainPanels::Register(EditorShell& editorShell)
    {
        m_Impl->Register(editorShell);
    }

    void DomainPanels::Unregister()
    {
        m_Impl->Unregister();
    }

} // namespace Extrinsic::Sandbox::Editor
