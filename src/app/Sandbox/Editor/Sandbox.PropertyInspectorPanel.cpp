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
#include <variant>
#include <cstdio>
#include <type_traits>
#include <implot.h>

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

import Extrinsic.Runtime.PropertyInspectionOperations;
#include "Sandbox.PanelSupport.hpp"
#include "Sandbox.PropertyInspectorPanel.hpp"

namespace Extrinsic::Sandbox::Editor
{
    namespace
    {
        using namespace Runtime;
        std::string PropertyTitle(const GeometryPropertyRef& ref)
        { return std::string(ToString(ref.Domain)) + ": " + ref.Name; }
        bool ChooseInspectionProperty(const char* label, const GeometryPropertyCatalogSnapshot& catalog,
                                      GeometryPropertyRef& selected)
        {
            bool changed = false;
            if (ImGui::BeginCombo(label, selected.HasName() ? PropertyTitle(selected).c_str() : "Choose property"))
            {
                for (const auto& entry : catalog.Entries)
                {
                    const auto title = PropertyTitle(entry.Ref);
                    if (ImGui::Selectable(title.c_str(), selected == entry.Ref))
                    { selected = entry.Ref; changed = true; }
                }
                ImGui::EndCombo();
            }
            return changed;
        }
        bool SameInspectionSource(const GeometryPropertyCatalogSnapshot& a, const GeometryPropertyCatalogSnapshot& b)
        {
            if (a.SourceStableId != b.SourceStableId || a.SourceGeneration != b.SourceGeneration || a.Entries.size() != b.Entries.size())
                return false;
            for (std::size_t i = 0; i < a.Entries.size(); ++i)
                if (a.Entries[i].Ref != b.Entries[i].Ref || a.Entries[i].ElementCount != b.Entries[i].ElementCount ||
                    a.Entries[i].PropertyGeneration != b.Entries[i].PropertyGeneration) return false;
            return true;
        }
        void DrawStatisticsRow(const char* label, const EditorPropertyNumericStatistics& s)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
            for (const auto n : {s.FiniteCount, s.NaNCount, s.InfCount, s.ZeroCount})
            { ImGui::TableNextColumn(); ImGui::Text("%zu", n); }
            for (const auto v : {s.Min, s.Max, s.Mean, s.RMS, s.StdDev})
            {
                ImGui::TableNextColumn();
                if (s.FiniteCount) ImGui::Text("%.9g", v); else ImGui::TextDisabled("—");
            }
        }
        std::string ValueText(const EditorPropertyValue& value)
        {
            return std::visit([](const auto& v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::string>) return v;
                else if constexpr (std::is_same_v<T, bool>) return v ? "true" : "false";
                else if constexpr (std::is_same_v<T, double>)
                { char text[64]; std::snprintf(text, sizeof(text), "%.17g", v); return text; }
                else return std::to_string(v);
            }, value);
        }
    }
    extern "C++" void DrawPropertyInspectorContents(const SandboxEditorContext& context, PropertyInspectorUiState& state)
    {
        const auto workspace = BuildProcessingInputWorkspace(context);
        bool changed = SynchronizeProcessingEntity(workspace.Selection, state.PreviousSelection, state.Entity);
        std::string entityLabel = state.Entity ? std::to_string(state.Entity) : "Choose entity";
        for (const auto& row : workspace.Hierarchy)
            if (row.StableEntityId == state.Entity) entityLabel = row.Name + " (" + std::to_string(state.Entity) + ")";
        if (ImGui::BeginCombo("Entity", entityLabel.c_str()))
        {
            for (const auto& row : workspace.Hierarchy)
            {
                const auto properties = GetEditorPropertyCatalog(context.Processing, row.StableEntityId);
                if (!properties.Success || properties.Catalog.Entries.empty()) continue;
                const auto label = row.Name + " (" + std::to_string(row.StableEntityId) + ")";
                if (ImGui::Selectable(label.c_str(), row.StableEntityId == state.Entity))
                { state.Entity = row.StableEntityId; changed = true; }
            }
            ImGui::EndCombo();
        }
        if (state.Entity == 0)
        {
            state.Valid = false; state.DisplayDiagnostic.clear();
            state.Statistics = {}; state.Comparison = {}; state.Values = {};
            ImGui::TextDisabled("Choose an entity to inspect its properties."); return;
        }
        const auto catalog = GetEditorPropertyCatalog(context.Processing, state.Entity);
        if (!catalog.Success)
        {
            state.Valid = false; state.DisplayDiagnostic.clear();
            state.Statistics = {}; state.Comparison = {}; state.Values = {};
            DrawDiagnostics(catalog.Diagnostics); return;
        }
        if (changed) { state.Property = {}; state.CompareWith = {}; state.Offset = 0; }
        const auto exists = [&](const GeometryPropertyRef& ref) {
            return std::ranges::any_of(catalog.Catalog.Entries, [&](const auto& e) { return e.Ref == ref; });
        };
        if (!exists(state.Property))
        {
            const auto supported = std::ranges::find_if(catalog.Catalog.Entries,
                [](const auto& entry) { return entry.Ref.ValueKind != Geometry::PropertyValueKind::Unknown; });
            state.Property = supported != catalog.Catalog.Entries.end() ? supported->Ref :
                catalog.Catalog.Entries.empty() ? GeometryPropertyRef{} : catalog.Catalog.Entries.front().Ref;
            state.Offset = 0; changed = true;
        }
        if (ChooseInspectionProperty("Property", catalog.Catalog, state.Property))
        { state.Offset = 0; state.HistogramComponent = 0; changed = true; }
        if (!state.Property.HasName())
        {
            state.Valid = false; state.DisplayDiagnostic.clear();
            state.Statistics = {}; state.Comparison = {}; state.Values = {};
            ImGui::TextDisabled("No geometry properties."); return;
        }
        if (state.Catalog.SourceStableId != state.Entity || state.QueriedProperty != state.Property ||
            state.Epoch != GetEditorSceneEpoch(context.Processing)) state.DisplayDiagnostic.clear();
        if (ImGui::Button("Refresh")) changed = true;
        ImGui::SameLine();
        (void)DrawProcessingPropertyShowButton(context, state.Entity, state.Property, state.DisplayDiagnostic, "Show");
        if (!state.DisplayDiagnostic.empty()) ImGui::TextWrapped("%s", state.DisplayDiagnostic.c_str());
        ImGui::SliderInt("Bins", &state.Bins, 1, static_cast<int>(MaxEditorPropertyHistogramBins));
        ImGui::Checkbox("Compare", &state.Compare);
        if (state.Compare) ChooseInspectionProperty("Compare with", catalog.Catalog, state.CompareWith);
        ImGui::Checkbox("Values", &state.ShowValues);
        const auto epoch = GetEditorSceneEpoch(context.Processing);
        changed = changed || !state.Valid || epoch != state.Epoch || !SameInspectionSource(state.Catalog, catalog.Catalog) ||
            state.QueriedProperty != state.Property || state.QueriedComparison != state.CompareWith ||
            state.QueriedBins != state.Bins || state.QueriedOffset != state.Offset ||
            state.QueriedCompare != state.Compare || state.QueriedValues != state.ShowValues;
        if (changed)
        {
            state.Statistics = GetEditorPropertyStatistics(context.Processing, state.Entity, state.Property, state.Bins);
            state.Comparison = state.Compare ? CompareEditorProperties(context.Processing, state.Entity, state.Property, state.CompareWith)
                                             : EditorPropertyComparisonResult{};
            state.Values = state.ShowValues ? ReadEditorPropertyValues(context.Processing, state.Entity, state.Property, state.Offset, 64)
                                            : EditorPropertyValuesResult{};
            state.Epoch = epoch; state.Catalog = catalog.Catalog;
            state.QueriedProperty = state.Property; state.QueriedComparison = state.CompareWith;
            state.QueriedBins = state.Bins; state.QueriedOffset = state.Offset;
            state.QueriedCompare = state.Compare; state.QueriedValues = state.ShowValues; state.Valid = true;
        }
        DrawDiagnostics(state.Statistics.Diagnostics);
        if (state.Statistics.Success)
        {
            const auto& s = state.Statistics.Statistics;
            ImGui::Text("Rows: %zu   Included: %zu   Deleted: %zu", s.RowCount, s.Count, s.DeletedCount);
            ImGui::Text("Finite: %zu   NaN: %zu   Inf: %zu   Zero: %zu", s.FiniteCount, s.NaNCount, s.InfCount, s.ZeroCount);
            if (ImGui::BeginTable("PropertyStatistics", 10, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX | ImGuiTableFlags_RowBg))
            {
                for (const auto* title : {"Component", "Finite", "NaN", "Inf", "Zero", "Min", "Max", "Mean", "RMS", "StdDev"})
                    ImGui::TableSetupColumn(title);
                ImGui::TableHeadersRow();
                for (std::size_t i = 0; i < s.Components.size(); ++i)
                    DrawStatisticsRow(s.Components.size() == 1 ? "Scalar" : std::to_string(i).c_str(), s.Components[i]);
                if (s.Magnitude) DrawStatisticsRow("Magnitude", *s.Magnitude);
                ImGui::EndTable();
            }
            if (!s.Components.empty())
            {
                const int componentCount = static_cast<int>(s.Components.size());
                state.HistogramComponent = std::clamp(state.HistogramComponent, 0, componentCount - 1 + (s.Magnitude ? 1 : 0));
                if (s.Components.size() > 1)
                    ImGui::SliderInt("Histogram component (last = magnitude)", &state.HistogramComponent, 0, componentCount);
                const auto& values = state.HistogramComponent == componentCount && s.Magnitude ? *s.Magnitude
                    : s.Components[static_cast<std::size_t>(state.HistogramComponent)];
                if (ImPlot::GetCurrentContext()) DrawEditorPropertyHistogramWidget("InspectorHistogram", values);
            }
        }
        if (state.Compare)
        {
            DrawDiagnostics(state.Comparison.Diagnostics);
            if (state.Comparison.Success)
            {
                const auto& c = state.Comparison.Comparison;
                ImGui::Text("Comparable: %zu   Identical: %zu   Nonfinite: %zu", c.ComparableRows, c.IdenticalRows, c.NonFiniteRows);
                ImGui::Text("Max error: %.9g   Mean error: %.9g   RMS error: %.9g", c.MaxAbsError, c.MeanAbsError, c.RMSError);
                if (c.MaxErrorRow) ImGui::Text("Max error row: %zu", *c.MaxErrorRow);
            }
        }
        if (state.ShowValues)
        {
            DrawDiagnostics(state.Values.Diagnostics);
            if (state.Values.Success)
            {
                ImGui::BeginDisabled(state.Offset == 0);
                if (ImGui::Button("Previous")) state.Offset = state.Offset > 64 ? state.Offset - 64 : 0;
                ImGui::EndDisabled(); ImGui::SameLine();
                ImGui::BeginDisabled(!state.Values.HasMore);
                if (ImGui::Button("Next")) state.Offset += state.Values.Rows.size();
                ImGui::EndDisabled(); ImGui::SameLine();
                ImGui::Text("Offset %zu / %zu", state.Values.Offset, state.Values.TotalCount);
                if (ImGui::BeginTable("PropertyValues", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
                {
                    ImGui::TableSetupColumn("Row"); ImGui::TableSetupColumn("Deleted"); ImGui::TableSetupColumn("Values");
                    ImGui::TableHeadersRow();
                    for (const auto& row : state.Values.Rows)
                    {
                        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%zu", row.Index);
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(row.Deleted ? "yes" : "no");
                        ImGui::TableNextColumn();
                        std::string text;
                        for (const auto& cell : row.Components) { if (!text.empty()) text += ", "; text += ValueText(cell); }
                        ImGui::TextUnformatted(text.c_str());
                    }
                    ImGui::EndTable();
                }
            }
        }
    }
    extern "C++" void DrawPropertyInspectorWindow(bool& open, const SandboxEditorContext& context, PropertyInspectorUiState& state)
    {
        ImGui::SetNextWindowSize({850, 650}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Property Inspector", &open)) DrawPropertyInspectorContents(context, state);
        ImGui::End();
    }
}
