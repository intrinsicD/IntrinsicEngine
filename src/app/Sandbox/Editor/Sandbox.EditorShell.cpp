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
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

module Extrinsic.Sandbox.Editor.Shell;


import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.Module;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.EditorWorkspaceAttachment;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.VisualizationRecipes;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.PointCloudConsolidationTypes;

#include "Sandbox.PanelSupport.hpp"

namespace Extrinsic::Sandbox::Editor
{
    using namespace Extrinsic::Runtime;

    namespace
    {
        struct BuiltinWindowSpec
        {
            std::string_view Id{};
            std::string_view Title{};
        };

        inline constexpr std::array<BuiltinWindowSpec, 10>
            kBuiltinWindows{{
                {"sandbox.shell", "Sandbox Editor"},
                {"scene.hierarchy", "Scene Hierarchy"},
                {"scene.inspector", "Inspector"},
                {"scene.selection", "Selection Details"},
                {"file.scene", "File / Scene"},
                {"file.import", "File / Import"},
                {"view.frame_graph", "Frame Graph"},
                {"view.render_recipes", "Render Recipes"},
                {"view.camera_render", "Camera / Render"},
                {"view.geometry_visualization", "Geometry Visualization"},
            }};

        [[nodiscard]] bool BeginFixedWindow(
            const char* title,
            bool& open,
            const ImVec2 firstUseSize)
        {
            if (firstUseSize.x > 0.0f && firstUseSize.y > 0.0f)
                ImGui::SetNextWindowSize(firstUseSize, ImGuiCond_FirstUseEver);

            if (ImGui::Begin(title, &open))
                return true;

            ImGui::End();
            return false;
        }

        using BuiltinWindowHandles =
            std::array<EditorWindowHandle, kBuiltinWindows.size()>;

        [[nodiscard]] EditorWorkspaceSnapshotRequest BuildModelRequest(
            const EditorWindowRegistry& registry,
            const BuiltinWindowHandles& builtinHandles)
        {
            EditorWorkspaceSnapshotRequest request{};
            request.Hierarchy = registry.IsOpen(builtinHandles[1u]);
            request.Inspector = registry.IsOpen(builtinHandles[2u]);
            request.Selection = registry.IsOpen(builtinHandles[3u]);
            request.Visualization = registry.IsOpen(builtinHandles[9u]);
            return request;
        }

        [[nodiscard]] std::string ProgressOverlayText(
            const EditorAssetImportQueueRow& row)
        {
            if (!row.ProgressDeterminate)
            {
                return row.StageText.empty() ? "active" : row.StageText;
            }
            const int percent = static_cast<int>(
                std::round(std::clamp(row.NormalizedProgress, 0.0f, 1.0f) * 100.0f));
            return std::to_string(percent) + "%";
        }

        void DrawAssetImportQueue(
            const EditorAssetImportQueueModel& model,
            const SandboxEditorContext* context)
        {
            ImGui::SeparatorText("AssetIO Queue");
            ImGui::TextWrapped("%s", model.StatusText.c_str());

            const bool clearAvailable =
                model.CanClearCompleted &&
                context != nullptr &&
                context->AssetImportQueueCommands.ClearAvailable();
            if (!clearAvailable)
            {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Clear completed") && clearAvailable)
            {
                (void)context->AssetImportQueueCommands.ClearCompleted();
            }
            if (!clearAvailable)
            {
                ImGui::EndDisabled();
                DrawDisabledReasonTooltip(
                    model.ClearCompletedDisabledReason);
                if (!model.ClearCompletedDisabledReason.empty())
                {
                    ImGui::TextDisabled(
                        "%s",
                        model.ClearCompletedDisabledReason.c_str());
                }
            }

            if (model.Rows.empty())
            {
                ImGui::TextDisabled("No asset import rows.");
                DrawDiagnostics(model.Diagnostics);
                return;
            }

            constexpr ImGuiTableFlags tableFlags =
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("AssetIOQueueTable", 8, tableFlags))
            {
                ImGui::TableSetupColumn("ID");
                ImGui::TableSetupColumn("Payload");
                ImGui::TableSetupColumn("Path");
                ImGui::TableSetupColumn("Stage");
                ImGui::TableSetupColumn("Progress");
                ImGui::TableSetupColumn("Elapsed");
                ImGui::TableSetupColumn("Diagnostic");
                ImGui::TableSetupColumn("Cancel");
                ImGui::TableHeadersRow();

                for (const EditorAssetImportQueueRow& row : model.Rows)
                {
                    ImGui::PushID(static_cast<int>(row.Sequence));
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%llu",
                                static_cast<unsigned long long>(row.Sequence));

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(
                        DebugNameForEditorAssetPayloadKind(row.PayloadKind));

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(row.PathBasename.c_str());

                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(row.StageText.c_str());

                    ImGui::TableSetColumnIndex(4);
                    const std::string overlay = ProgressOverlayText(row);
                    ImGui::ProgressBar(
                        row.ProgressDeterminate ? row.NormalizedProgress : 0.0f,
                        ImVec2(-1.0f, 0.0f),
                        overlay.c_str());

                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%.2fs", row.ElapsedSeconds);

                    ImGui::TableSetColumnIndex(6);
                    if (row.DiagnosticText.empty())
                    {
                        ImGui::TextDisabled("-");
                    }
                    else
                    {
                        ImGui::TextWrapped("%s", row.DiagnosticText.c_str());
                    }

                    ImGui::TableSetColumnIndex(7);
                    const bool cancelAvailable =
                        row.CanCancel &&
                        context != nullptr &&
                        context->AssetImportQueueCommands.CancelAvailable();
                    if (!cancelAvailable)
                    {
                        ImGui::BeginDisabled();
                    }
                    if (ImGui::Button("Cancel") && cancelAvailable)
                    {
                        (void)context->AssetImportQueueCommands.Cancel(row.Operation);
                    }
                    if (!cancelAvailable)
                    {
                        ImGui::EndDisabled();
                        DrawDisabledReasonTooltip(
                            row.CancelDisabledReason);
                        if (!row.CancelDisabledReason.empty())
                        {
                            ImGui::TextDisabled("%s",
                                                row.CancelDisabledReason.c_str());
                        }
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            DrawDiagnostics(model.Diagnostics);
        }

        void DrawQuat(const char* label, const glm::quat value)
        {
            ImGui::Text("%s: %.3f, %.3f, %.3f, %.3f",
                        label,
                        value.w,
                        value.x,
                        value.y,
                        value.z);
        }

        [[nodiscard]] bool RegisteredMenuPathStartsWith(
            const EditorWindowMenuEntry& entry,
            const std::vector<std::string>& path)
        {
            return entry.MenuPath.size() >= path.size() &&
                std::equal(path.begin(), path.end(), entry.MenuPath.begin());
        }

        void DrawRegisteredWindowMenuTree(
            EditorWindowRegistry& registry,
            const std::vector<EditorWindowMenuEntry>& entries,
            std::vector<std::string>& path);

        void DrawRegisteredWindowMenuLeaves(
            EditorWindowRegistry& registry,
            const std::vector<EditorWindowMenuEntry>& entries,
            const std::vector<std::string>& path)
        {
            for (const EditorWindowMenuEntry& entry : entries)
            {
                if (entry.MenuPath != path)
                    continue;

                bool open = entry.Open;
                if (ImGui::MenuItem(entry.Title.c_str(), nullptr, &open))
                    (void)registry.SetOpen(entry.Handle, open);
            }
        }

        void DrawRegisteredWindowMenuChildren(
            EditorWindowRegistry& registry,
            const std::vector<EditorWindowMenuEntry>& entries,
            std::vector<std::string>& path,
            const std::span<const std::string_view> excludedChildren = {})
        {
            std::vector<std::string> children{};
            for (const EditorWindowMenuEntry& entry : entries)
            {
                if (!RegisteredMenuPathStartsWith(entry, path) ||
                    entry.MenuPath.size() == path.size())
                {
                    continue;
                }

                const std::string& child = entry.MenuPath[path.size()];
                if (std::find(excludedChildren.begin(),
                              excludedChildren.end(),
                              child) != excludedChildren.end() ||
                    std::find(children.begin(), children.end(), child) !=
                        children.end())
                {
                    continue;
                }
                children.push_back(child);
            }

            for (const std::string& child : children)
            {
                if (!ImGui::BeginMenu(child.c_str()))
                    continue;
                path.push_back(child);
                DrawRegisteredWindowMenuTree(registry, entries, path);
                path.pop_back();
                ImGui::EndMenu();
            }
        }

        void DrawRegisteredWindowMenuTree(
            EditorWindowRegistry& registry,
            const std::vector<EditorWindowMenuEntry>& entries,
            std::vector<std::string>& path)
        {
            DrawRegisteredWindowMenuLeaves(registry, entries, path);
            DrawRegisteredWindowMenuChildren(registry, entries, path);
        }

        void DrawDomainMenu(
            const EditorDomainWindowKind kind,
            EditorWindowRegistry* windowRegistry,
            const std::vector<EditorWindowMenuEntry>* registeredEntries)
        {
            if (!ImGui::BeginMenu(DebugNameForEditorDomainWindowKind(kind)))
                return;

            if (windowRegistry != nullptr && registeredEntries != nullptr)
            {
                std::vector<std::string> registeredPath{
                    DebugNameForEditorDomainWindowKind(kind)};
                DrawRegisteredWindowMenuTree(
                    *windowRegistry,
                    *registeredEntries,
                    registeredPath);
            }
            else
            {
                ImGui::BeginDisabled();
                (void)ImGui::MenuItem(
                    "No registered windows", nullptr, false, false);
                ImGui::EndDisabled();
            }
            ImGui::EndMenu();
        }

        void DrawPanelWindowMenu(
            EditorWindowRegistry* windowRegistry,
            const std::vector<EditorWindowMenuEntry>* registeredEntries)
        {
            if (!ImGui::BeginMenu("View"))
                return;

            if (windowRegistry != nullptr && registeredEntries != nullptr)
            {
                std::vector<std::string> registeredPath{"View"};
                DrawRegisteredWindowMenuTree(
                    *windowRegistry,
                    *registeredEntries,
                    registeredPath);
            }
            ImGui::EndMenu();
        }

        void DrawMainMenuBar(EditorWindowRegistry* windowRegistry)
        {
            if (!ImGui::BeginMainMenuBar())
                return;
            std::vector<EditorWindowMenuEntry> registeredEntries{};
            if (windowRegistry != nullptr)
                registeredEntries = windowRegistry->BuildMenuModel();
            const std::vector<EditorWindowMenuEntry>* registeredEntriesPtr =
                windowRegistry != nullptr ? &registeredEntries : nullptr;
            DrawPanelWindowMenu(windowRegistry, registeredEntriesPtr);
            DrawDomainMenu(
                EditorDomainWindowKind::PointCloud,
                windowRegistry,
                registeredEntriesPtr);
            DrawDomainMenu(
                EditorDomainWindowKind::Graph,
                windowRegistry,
                registeredEntriesPtr);
            DrawDomainMenu(
                EditorDomainWindowKind::Mesh,
                windowRegistry,
                registeredEntriesPtr);
            if (windowRegistry != nullptr)
            {
                std::vector<std::string> rootPath{};
                constexpr std::array<std::string_view, 4> kFixedRootMenus{
                    "View",
                    "PointCloud",
                    "Graph",
                    "Mesh",
                };
                DrawRegisteredWindowMenuChildren(
                    *windowRegistry,
                    registeredEntries,
                    rootPath,
                    kFixedRootMenus);
            }
            ImGui::EndMainMenuBar();
        }

        void DrawVisualizationPropertyPresets(
            const std::vector<EditorVisualizationPropertyInfo>& properties,
            const EditorVisualizationConfigModel& visualization,
            const SandboxEditorContext& context,
            const std::uint32_t selectedStableId,
            const EditorVisualizationTarget target,
            const bool canEditVisualization)
        {
            ImGui::SeparatorText("Properties");
            if (properties.empty())
            {
                ImGui::TextDisabled("No visualization-eligible properties.");
                return;
            }

            if (!canEditVisualization)
                ImGui::BeginDisabled();

            // Reapplying a preset preserves the target's tuned range and
            // binning.
            const bool scalarAutoRange =
                visualization.HasConfig ? visualization.ScalarAutoRange : true;
            const float scalarRangeMin =
                visualization.HasConfig ? visualization.ScalarRangeMin : 0.0f;
            const float scalarRangeMax =
                visualization.HasConfig ? visualization.ScalarRangeMax : 1.0f;
            const std::uint32_t scalarBinCount =
                visualization.HasConfig ? visualization.ScalarBinCount : 0u;

            for (std::size_t i = 0u; i < properties.size(); ++i)
            {
                const EditorVisualizationPropertyInfo& property =
                    properties[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%s  [%s, %s, %llu]",
                            property.Name.c_str(),
                            DebugNameForEditorVisualizationPropertyDomain(
                                property.Domain),
                            DebugNameForGeometryPropertyValueKind(
                                property.ValueKind),
                            static_cast<unsigned long long>(
                                property.ElementCount));

                bool wroteButton = false;
                if (property.ScalarPresetAvailable)
                {
                    if (ImGui::SmallButton("Scalar") && canEditVisualization)
                    {
                        (void)ApplyEditorVisualizationPropertyCommand(
                            context.VisualizationCommands,
                            EditorVisualizationPropertyCommand{
                                .StableEntityId = selectedStableId,
                                .Target = target,
                                .Domain = property.Domain,
                                .Preset =
                                    EditorVisualizationPropertyPreset::Scalar,
                                .PropertyName = property.Name,
                                .ScalarAutoRange = scalarAutoRange,
                                .ScalarRangeMin = scalarRangeMin,
                                .ScalarRangeMax = scalarRangeMax,
                                .ScalarBinCount = scalarBinCount,
                            });
                    }
                    wroteButton = true;
                }
                if (property.IsolinePresetAvailable)
                {
                    if (wroteButton)
                        ImGui::SameLine();
                    if (ImGui::SmallButton("Isolines") && canEditVisualization)
                    {
                        (void)ApplyEditorVisualizationPropertyCommand(
                            context.VisualizationCommands,
                            EditorVisualizationPropertyCommand{
                                .StableEntityId = selectedStableId,
                                .Target = target,
                                .Domain = property.Domain,
                                .Preset =
                                    EditorVisualizationPropertyPreset::Isoline,
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
                if (property.ColorBufferPresetAvailable)
                {
                    if (wroteButton)
                        ImGui::SameLine();
                    if (ImGui::SmallButton("Color buffer") &&
                        canEditVisualization)
                    {
                        (void)ApplyEditorVisualizationPropertyCommand(
                            context.VisualizationCommands,
                            EditorVisualizationPropertyCommand{
                                .StableEntityId = selectedStableId,
                                .Target = target,
                                .Domain = property.Domain,
                                .Preset =
                                    EditorVisualizationPropertyPreset::ColorBuffer,
                                .PropertyName = property.Name,
                            });
                    }
                    wroteButton = true;
                }
                if (property.VectorFieldCandidate && !wroteButton)
                {
                    ImGui::TextDisabled("Vector-field candidate; adapter residency is not "
                                        "owned by this UI slice.");
                }
                ImGui::PopID();
            }

            if (!canEditVisualization)
                ImGui::EndDisabled();
        }

        void DrawScalarVisualizationControls(
            const EditorVisualizationConfigModel& visualization,
            const SandboxEditorContext& context,
            const std::uint32_t selectedStableId,
            const EditorVisualizationTarget target,
            const bool canEditVisualization)
        {
            if (!visualization.HasConfig || visualization.Source != kScalarFieldSource)
                return;
            DrawScalarFieldColorControls(visualization, context, selectedStableId, target,
                                         canEditVisualization);
            DrawScalarFieldBinAndIsolineControls(visualization, context, selectedStableId, target,
                                                canEditVisualization);
        }

        void DrawRenderRecipeEditor(
            const EditorRenderRecipeEditorModel& model,
            const SandboxEditorContext* context,
            std::array<char, 8192>* draftBuffer)
        {
            if (!model.Available)
            {
                ImGui::TextDisabled("Render recipe editor is unavailable.");
                DrawDiagnostics(model.Diagnostics);
                return;
            }

            ImGui::Text("Renderer: %s", model.RendererId.c_str());
            ImGui::Text("Active recipe: %s", model.ActiveRecipeId.c_str());
            ImGui::Text("View/output: %s / %s / %s",
                        model.ActiveViewOutputRecipeId.c_str(),
                        model.ViewKind.c_str(),
                        model.OutputTarget.c_str());
            ImGui::Text("Draft: %s revision=%llu active=%llu",
                        DebugNameForEditorRenderRecipeDraftState(
                            model.DraftState),
                        static_cast<unsigned long long>(model.DraftRevision),
                        static_cast<unsigned long long>(model.ActiveRevision));
            ImGui::Text("Validation: %s parsed slots=%u bindings=%u",
                        std::string(DebugNameForEditorRenderRecipeConfigState(model.ValidationState)).c_str(),
                        model.ParsedSlotCount,
                        model.ParsedBindingOverrideCount);

            const bool commandsAvailable =
                context != nullptr &&
                context->RenderRecipeCommandsAvailable;

            if (draftBuffer != nullptr)
            {
                const EditorRenderRecipeDraftSnapshot* state =
                    context != nullptr ? &context->RenderRecipeDraft : nullptr;
                if (state != nullptr &&
                    !state->DraftDocument.empty() &&
                    draftBuffer->front() == '\0')
                {
                    const std::size_t copyCount =
                        std::min(state->DraftDocument.size(),
                                 draftBuffer->size() - 1u);
                    std::copy_n(state->DraftDocument.data(),
                                copyCount,
                                draftBuffer->data());
                    (*draftBuffer)[copyCount] = '\0';
                }
                if (state != nullptr &&
                    state->DraftDocument.empty() &&
                    state->DraftState ==
                        EditorRenderRecipeDraftState::Canceled)
                {
                    draftBuffer->fill('\0');
                }

                ImGui::InputTextMultiline("Draft JSON",
                                          draftBuffer->data(),
                                          draftBuffer->size(),
                                          ImVec2(-1.0f, 180.0f));
            }
            else
            {
                ImGui::TextDisabled("Draft buffer unavailable.");
            }

            const auto draftText = [draftBuffer]() -> std::string
            {
                return draftBuffer != nullptr
                    ? std::string{draftBuffer->data()}
                    : std::string{};
            };

            if (!commandsAvailable)
                ImGui::BeginDisabled();

            if (ImGui::Button("Update Draft"))
            {
                (void)ApplyEditorRenderRecipeCommand(
                    context->RenderRecipeCommands,
                    EditorRenderRecipeCommand{
                        .Kind = EditorRenderRecipeCommandKind::UpdateDraft,
                        .Document = draftText(),
                        .SourceId = "sandbox-editor",
                    });
            }
            ImGui::SameLine();
            if (ImGui::Button("Debounce"))
            {
                (void)ApplyEditorRenderRecipeCommand(
                    context->RenderRecipeCommands,
                    EditorRenderRecipeCommand{
                        .Kind = EditorRenderRecipeCommandKind::UpdateDraft,
                        .Document = draftText(),
                        .SourceId = "sandbox-editor",
                        .Debounced = true,
                    });
            }
            ImGui::SameLine();
            if (!model.CanValidate)
                ImGui::BeginDisabled();
            if (ImGui::Button("Validate"))
            {
                (void)ApplyEditorRenderRecipeCommand(
                    context->RenderRecipeCommands,
                    EditorRenderRecipeCommand{
                        .Kind = EditorRenderRecipeCommandKind::ValidateDraft,
                        .Document = draftText(),
                        .SourceId = "sandbox-editor",
                    });
            }
            if (!model.CanValidate)
                ImGui::EndDisabled();

            ImGui::SameLine();
            if (!model.CanPreview)
                ImGui::BeginDisabled();
            if (ImGui::Button("Preview"))
            {
                (void)ApplyEditorRenderRecipeCommand(
                    context->RenderRecipeCommands,
                    EditorRenderRecipeCommand{
                        .Kind = EditorRenderRecipeCommandKind::PreviewDraft,
                        .Document = draftText(),
                        .SourceId = "sandbox-editor",
                    });
            }
            if (!model.CanPreview)
                ImGui::EndDisabled();

            ImGui::SameLine();
            if (!model.CanActivate)
                ImGui::BeginDisabled();
            if (ImGui::Button("Activate Preview"))
            {
                (void)ApplyEditorRenderRecipeCommand(
                    context->RenderRecipeCommands,
                    EditorRenderRecipeCommand{
                        .Kind = EditorRenderRecipeCommandKind::ActivatePreview,
                    });
            }
            if (!model.CanActivate)
                ImGui::EndDisabled();

            ImGui::SameLine();
            if (!model.CanCancel)
                ImGui::BeginDisabled();
            if (ImGui::Button("Cancel"))
            {
                (void)ApplyEditorRenderRecipeCommand(
                    context->RenderRecipeCommands,
                    EditorRenderRecipeCommand{
                        .Kind = EditorRenderRecipeCommandKind::CancelDraft,
                    });
            }
            if (!model.CanCancel)
                ImGui::EndDisabled();

            if (!commandsAvailable)
                ImGui::EndDisabled();

            constexpr ImGuiTableFlags tableFlags =
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp;

            if (ImGui::CollapsingHeader("Recipe Slots",
                                        ImGuiTreeNodeFlags_DefaultOpen))
            {
                const auto slotKindName = [](const EditorRecipeSlotKind kind)
                {
                    switch (kind)
                    {
                    case EditorRecipeSlotKind::FixedCore:
                        return "FixedCore";
                    case EditorRecipeSlotKind::Extension:
                        return "Extension";
                    }
                    return "Unknown";
                };
                if (ImGui::BeginTable("RenderRecipeSlots", 5, tableFlags))
                {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Kind");
                    ImGui::TableSetupColumn("Schema");
                    ImGui::TableSetupColumn("Editable");
                    ImGui::TableSetupColumn("Reason");
                    ImGui::TableHeadersRow();
                    for (const EditorRenderRecipeSlotModel& slot :
                         model.Slots)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(slot.StableName.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(
                            slotKindName(slot.Kind));
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(slot.SchemaId.c_str());
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(slot.Editable ? "yes" : "no");
                        ImGui::TableSetColumnIndex(4);
                        ImGui::TextWrapped("%s",
                                           slot.DisabledReason.c_str());
                    }
                    ImGui::EndTable();
                }
            }

            if (ImGui::CollapsingHeader("Binding Overrides",
                                        ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginTable("RenderRecipeBindings", 7, tableFlags))
                {
                    ImGui::TableSetupColumn("Semantic");
                    ImGui::TableSetupColumn("Slot");
                    ImGui::TableSetupColumn("Domain");
                    ImGui::TableSetupColumn("Source");
                    ImGui::TableSetupColumn("Type");
                    ImGui::TableSetupColumn("Editable");
                    ImGui::TableSetupColumn("Reason");
                    ImGui::TableHeadersRow();
                    for (const EditorRenderRecipeBindingOverrideModel& binding :
                         model.BindingOverrides)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(binding.SemanticName.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(binding.Slot.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(binding.SourceDomain.c_str());
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(binding.SourceIdentity.c_str());
                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text("%s / %s",
                                    binding.ValueType.c_str(),
                                    binding.ValueFormat.c_str());
                        ImGui::TableSetColumnIndex(5);
                        ImGui::TextUnformatted(binding.Editable ? "yes" : "no");
                        ImGui::TableSetColumnIndex(6);
                        ImGui::TextWrapped("%s",
                                           binding.DisabledReason.c_str());
                    }
                    ImGui::EndTable();
                }
            }

            if (ImGui::CollapsingHeader("Outputs"))
            {
                if (ImGui::BeginTable("RenderRecipeOutputs", 4, tableFlags))
                {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Kind");
                    ImGui::TableSetupColumn("Format");
                    ImGui::TableSetupColumn("Required");
                    ImGui::TableHeadersRow();
                    for (const EditorRenderRecipeOutputModel& output :
                         model.Outputs)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(output.Name.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(output.Kind.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(output.Format.c_str());
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(output.Required ? "yes" : "no");
                    }
                    ImGui::EndTable();
                }
            }

            if (ImGui::CollapsingHeader("Artifacts"))
            {
                if (model.Artifacts.empty())
                {
                    ImGui::TextDisabled("No render artifacts declared.");
                }
                if (ImGui::BeginTable("RenderRecipeArtifacts", 7, tableFlags))
                {
                    ImGui::TableSetupColumn("Artifact");
                    ImGui::TableSetupColumn("Purpose");
                    ImGui::TableSetupColumn("Kind");
                    ImGui::TableSetupColumn("Status");
                    ImGui::TableSetupColumn("Payload");
                    ImGui::TableSetupColumn("Publish");
                    ImGui::TableSetupColumn("Apply");
                    ImGui::TableHeadersRow();
                    for (const EditorRenderArtifactRow& artifact :
                         model.Artifacts)
                    {
                        ImGui::PushID(artifact.ArtifactId.c_str());
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(artifact.ArtifactId.c_str());
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(artifact.Purpose.c_str());
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(
                            std::string(ToString(artifact.Kind)).c_str());
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(
                            std::string(ToString(artifact.Status)).c_str());
                        ImGui::TableSetColumnIndex(4);
                        ImGui::TextUnformatted(artifact.PayloadUri.c_str());
                        ImGui::TableSetColumnIndex(5);
                        const bool publishAvailable =
                            commandsAvailable &&
                            context->RenderArtifactCommandsAvailable &&
                            artifact.CanPublish;
                        if (!publishAvailable)
                            ImGui::BeginDisabled();
                        if (ImGui::Button("Publish"))
                        {
                            (void)ApplyEditorRenderRecipeCommand(
                                context->RenderRecipeCommands,
                                EditorRenderRecipeCommand{
                                    .Kind = EditorRenderRecipeCommandKind::PublishArtifact,
                                    .ArtifactId = artifact.ArtifactId,
                                    .Provenance = "sandbox-editor",
                                });
                        }
                        if (!publishAvailable)
                            ImGui::EndDisabled();
                        ImGui::TableSetColumnIndex(6);
                        const bool applyAvailable =
                            commandsAvailable &&
                            context->RenderArtifactCommandsAvailable &&
                            artifact.CanApply;
                        if (!applyAvailable)
                            ImGui::BeginDisabled();
                        if (ImGui::Button("Apply"))
                        {
                            (void)ApplyEditorRenderRecipeCommand(
                                context->RenderRecipeCommands,
                                EditorRenderRecipeCommand{
                                    .Kind = EditorRenderRecipeCommandKind::ApplyArtifact,
                                    .ArtifactId = artifact.ArtifactId,
                                    .Provenance = "sandbox-editor",
                                    .ProjectTarget = "sandbox-render-recipe-artifact",
                                });
                        }
                        if (!applyAvailable)
                            ImGui::EndDisabled();
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }

            DrawDiagnostics(model.Diagnostics);
            for (const auto& diagnostic :
                 model.RecipeDiagnostics)
            {
                ImGui::TextDisabled("%s/%s: %s",
                                    std::string(DebugNameForEditorRenderRecipeConfigState(
                                        diagnostic.State)).c_str(),
                                    std::string(DebugNameForEditorRenderRecipeConfigDiagnosticCode(
                                        diagnostic.Code)).c_str(),
                                    diagnostic.Message.c_str());
            }
        }

        void DrawFixedWindow(
            const std::string_view windowId,
            bool& open,
            const EditorWorkspaceSnapshot& frame,
            const SandboxEditorContext* context,
            std::array<char, 1024>* importPathBuffer,
            std::array<char, 1024>* scenePathBuffer,
            std::array<char, 8192>* renderRecipeDraftBuffer,
            EditorAssetPayloadKind* importPayloadKind,
            std::optional<EditorFileImportResult>* lastImportResult,
            std::optional<EditorSceneFileResult>* lastSceneFileResult,
            TextureBakeUiState* textureBakeState)
        {
            if (windowId == "sandbox.shell" &&
                BeginFixedWindow("Sandbox Editor", open, ImVec2(360.0f, 520.0f)))
            {
                ImGui::TextUnformatted("Promoted runtime editor shell");
                DrawDiagnostics(frame.Diagnostics);
                ImGui::End();
            }

            if (windowId == "scene.hierarchy" &&
                BeginFixedWindow("Scene Hierarchy", open, ImVec2(280.0f, 420.0f)))
            {
                if (frame.Hierarchy.empty())
                {
                    ImGui::TextDisabled("No live scene entities.");
                }
                for (const EditorEntityRow& row : frame.Hierarchy)
                {
                    ImGui::PushID(static_cast<int>(row.StableEntityId));
                    const bool clicked =
                        ImGui::Selectable(row.Name.c_str(), row.Selected);
                    ImGui::PopID();
                    if (clicked && context != nullptr)
                        (void)SelectEditorEntity(context->SceneCommands, row.StableEntityId);
                    if (row.Hovered)
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("(hover)");
                    }
                }
                ImGui::End();
            }

            if (windowId == "scene.inspector" &&
                BeginFixedWindow("Inspector", open, ImVec2(360.0f, 420.0f)))
            {
                if (!frame.Inspector.HasEntity)
                {
                    DrawDiagnostics(frame.Inspector.Diagnostics);
                }
                else
                {
                    const EditorInspectorModel& inspector = frame.Inspector;
                    ImGui::Text("Entity: %s", inspector.Entity.Name.c_str());
                    ImGui::Text("Render id: %u", inspector.Entity.StableEntityId);
                    ImGui::Text("Durable StableId: %s",
                                inspector.Entity.HasDurableStableId ? "valid" : "none");
                    if (inspector.Transform.HasLocalTransform)
                    {
                        if (context != nullptr)
                        {
                            glm::vec3 localPosition = inspector.Transform.LocalPosition;
                            if (ImGui::DragFloat3("Local position",
                                                  &localPosition.x,
                                                  0.01f))
                            {
                                (void)ApplyEditorTransformEdit(
                                    context->SceneCommands,
                                    EditorTransformEditCommand{
                                        .StableEntityId = inspector.Entity.StableEntityId,
                                        .SetPosition = true,
                                        .Position = localPosition,
                                    });
                            }

                            glm::vec3 localScale = inspector.Transform.LocalScale;
                            if (ImGui::DragFloat3("Local scale",
                                                  &localScale.x,
                                                  0.01f))
                            {
                                (void)ApplyEditorTransformEdit(
                                    context->SceneCommands,
                                    EditorTransformEditCommand{
                                        .StableEntityId = inspector.Entity.StableEntityId,
                                        .SetScale = true,
                                        .Scale = localScale,
                                    });
                            }
                        }
                        else
                        {
                            DrawVec3("Local position", inspector.Transform.LocalPosition);
                            DrawVec3("Local scale", inspector.Transform.LocalScale);
                        }
                        DrawQuat("Local rotation (wxyz)", inspector.Transform.LocalRotation);
                    }
                    if (inspector.Transform.HasWorldTransform)
                        DrawVec3("World position", inspector.Transform.WorldPosition);
                    ImGui::Text("Render hints: surface=%s edges=%s points=%s",
                                inspector.RenderHints.HasRenderSurface ? "yes" : "no",
                                inspector.RenderHints.HasRenderEdges ? "yes" : "no",
                                inspector.RenderHints.HasRenderPoints ? "yes" : "no");
                    if (inspector.RenderHints.HasRenderSurface)
                        ImGui::Text("Surface domain: %s",
                                    inspector.RenderHints.SurfaceDomain.c_str());
                    if (inspector.RenderHints.HasRenderEdges)
                    {
                        ImGui::Text("Edge domain: %s",
                                    inspector.RenderHints.EdgeDomain.c_str());
                        if (inspector.RenderHints.HasUniformEdgeWidth)
                            ImGui::Text("Edge width: %.3f",
                                        inspector.RenderHints.UniformEdgeWidth);
                        if (inspector.RenderHints.HasNamedEdgeWidth)
                            ImGui::Text("Edge width source: %s",
                                        inspector.RenderHints.EdgeWidthName.c_str());
                    }
                    if (inspector.RenderHints.HasRenderPoints)
                    {
                        ImGui::Text("Point type: %s",
                                    inspector.RenderHints.PointRenderType.c_str());
                        if (inspector.RenderHints.HasUniformPointSize)
                            ImGui::Text("Point size: %.3f",
                                        inspector.RenderHints.UniformPointSize);
                        if (inspector.RenderHints.HasNamedPointSize)
                            ImGui::Text("Point size source: %s",
                                        inspector.RenderHints.PointSizeName.c_str());
                    }
                    ImGui::Text("Geometry domain: %s",
                                DebugNameForEditorGeometryDomain(
                                    inspector.Geometry.Domain));
                    ImGui::Text("Counts: v=%zu e=%zu h=%zu f=%zu n=%zu",
                                inspector.Geometry.VertexCount,
                                inspector.Geometry.EdgeCount,
                                inspector.Geometry.HalfedgeCount,
                                inspector.Geometry.FaceCount,
                                inspector.Geometry.NodeCount);
                    ImGui::SeparatorText("Property catalog");
                    ImGui::Text("Rows: %zu binding targets: %zu",
                                inspector.PropertyCatalog.Rows.size(),
                                inspector.PropertyCatalog.BindingTargets.size());
                    for (std::size_t propertyIndex = 0u;
                         propertyIndex < inspector.PropertyCatalog.Rows.size() &&
                         propertyIndex < 8u;
                         ++propertyIndex)
                    {
                        const EditorPropertyCatalogRow& row =
                            inspector.PropertyCatalog.Rows[propertyIndex];
                        ImGui::BulletText("%s / %s / %s",
                                          DebugNameForEditorPropertyCatalogDomain(
                                              row.Domain),
                                          row.Name.c_str(),
                                          DebugNameForGeometryPropertyValueKind(
                                              row.ValueKind));
                    }
                    const EditorGeometryPresentationModel& presentation =
                        inspector.GeometryPresentation;
                    DrawBoundRenderStateRows(inspector.BoundState);
                    static TextureBakeMutationUiState mutationState{};
                    DrawTextureBakeControls(inspector.TextureBake, context, textureBakeState, mutationState);
                    ImGui::SeparatorText("Geometry presentation");
                    ImGui::Text("Shape: %s",
                                std::string(ToString(presentation.Shape)).c_str());
                    ImGui::Text("Recipe: %s generation=%llu",
                                presentation.HasRecipe ? "yes" : "no",
                                static_cast<unsigned long long>(
                                    presentation.RecipeGeneration));
                    if (presentation.Composition.HasChildren)
                    {
                        ImGui::Text("Composition: children=%u bindings=%u slots=%u pending=%u "
                                    "failed=%u jobs=%u active=%u job failures=%u",
                                    presentation.Composition.ChildCount,
                                    presentation.Composition.ChildRecipeCount,
                                    presentation.Composition.ChildSlotCount,
                                    presentation.Composition.ChildPendingSlotCount,
                                    presentation.Composition.ChildFailedSlotCount,
                                    presentation.Composition.ChildJobCount,
                                    presentation.Composition.ChildActiveJobCount,
                                    presentation.Composition.ChildFailedJobCount);
                    }
                    if (!presentation.Slots.empty())
                    {
                        ImGui::Text("Slots: %zu", presentation.Slots.size());
                        for (std::size_t slotIndex = 0u;
                             slotIndex < presentation.Slots.size();
                             ++slotIndex)
                        {
                            const EditorGeometryPresentationSlotModel& slot =
                                presentation.Slots[slotIndex];
                            ImGui::PushID(static_cast<int>(slotIndex));
                            ImGui::Text("%s / %s / %s / %s",
                                        std::string(ToString(slot.Lane)).c_str(),
                                        slot.PresentationKey.c_str(),
                                        std::string(ToString(slot.Semantic)).c_str(),
                                        std::string(ToString(slot.Readiness)).c_str());
                            ImGui::Text("Source: %s property=%s",
                                        std::string(ToString(slot.SourceKind)).c_str(),
                                        slot.Property.Name.empty()
                                            ? "(none)"
                                            : slot.Property.Name.c_str());
                            if (!slot.Diagnostic.empty())
                                ImGui::TextWrapped("%s", slot.Diagnostic.c_str());

                            if (context != nullptr &&
                                (slot.Semantic == GeometryPresentationSlotSemantic::Albedo ||
                                 slot.Semantic == GeometryPresentationSlotSemantic::PointColor ||
                                 slot.Semantic == GeometryPresentationSlotSemantic::LineColor))
                            {
                                glm::vec4 color = slot.UniformDefault.Vector;
                                if (ImGui::ColorEdit4("Default color", &color.x))
                                {
                                    GeometryPresentationDefaultValue value =
                                        slot.UniformDefault;
                                    value.Kind = Geometry::PropertyValueKind::Vec4;
                                    value.Vector = color;
                                    (void)ApplyEditorGeometryPresentationSlotDefaultCommand(
                                        context->VisualizationCommands,
                                        EditorGeometryPresentationSlotDefaultCommand{
                                            .StableEntityId =
                                                inspector.Entity.StableEntityId,
                                            .PresentationKey =
                                                slot.PresentationKey,
                                            .Semantic = slot.Semantic,
                                            .Value = value,
                                            .Enabled = slot.Enabled,
                                        });
                                }
                            }

                            if (context != nullptr &&
                                !slot.PropertyOptions.empty())
                            {
                                const char* currentProperty =
                                    slot.Property.Name.empty()
                                        ? "(uniform/default)"
                                        : slot.Property.Name.c_str();
                                if (ImGui::BeginCombo("Source property",
                                                      currentProperty))
                                {
                                    for (const GeometryPresentationPropertyOption&
                                             option : slot.PropertyOptions)
                                    {
                                        if (!option.Compatible)
                                            ImGui::BeginDisabled();
                                        const bool selected =
                                            option.Property.Name ==
                                            slot.Property.Name;
                                        if (ImGui::Selectable(
                                                option.Property.Name.c_str(),
                                                selected) &&
                                            option.Compatible)
                                        {
                                            (void)ApplyEditorGeometryPresentationSlotPropertyCommand(
                                                context->VisualizationCommands,
                                                EditorGeometryPresentationSlotPropertyCommand{
                                                    .StableEntityId =
                                                        inspector.Entity.StableEntityId,
                                                    .PresentationKey =
                                                        slot.PresentationKey,
                                                    .Semantic = slot.Semantic,
                                                    .SourceKind =
                                                        IsSurfaceTextureSemantic(
                                                            slot.Semantic)
                                                            ? GeometryPresentationSourceKind::PropertyBake
                                                            : GeometryPresentationSourceKind::PropertyBuffer,
                                                    .Domain =
                                                        option.Property.Domain,
                                                    .ExpectedValueKind =
                                                        option.Property.ValueKind,
                                                    .PropertyName =
                                                        option.Property.Name,
                                                });
                                        }
                                        if (!option.Compatible)
                                        {
                                            ImGui::SameLine();
                                            ImGui::TextDisabled("%s",
                                                option.DisabledReason.c_str());
                                            ImGui::EndDisabled();
                                        }
                                    }
                                    ImGui::EndCombo();
                                }
                            }
                            ImGui::PopID();
                        }
                    }
                    if (!presentation.Jobs.empty())
                    {
                        ImGui::Text("Derived jobs: %zu", presentation.Jobs.size());
                        for (const EditorJobRecord& job :
                             presentation.Jobs)
                        {
                            ImGui::BulletText("%s %s %.0f%% deps=%zu %s",
                                              job.Name.c_str(),
                                              std::string(ToString(job.State)).c_str(),
                                              job.NormalizedProgress * 100.0f,
                                              job.Dependencies.size(),
                                              job.Diagnostic.c_str());
                        }
                    }
                    DrawDiagnostics(presentation.Diagnostics);
                    DrawDiagnostics(inspector.Diagnostics);
                }
                ImGui::End();
            }

            if (windowId == "scene.selection" &&
                BeginFixedWindow("Selection Details", open, ImVec2(0.0f, 0.0f)))
            {
                ImGui::Text("Selected entities: %zu", frame.Selection.SelectedStableIds.size());
                for (const EditorEntityRow& row : frame.Selection.SelectedEntities)
                    ImGui::BulletText("%s (%u)", row.Name.c_str(), row.StableEntityId);
                if (frame.Selection.HasHovered)
                {
                    ImGui::Text("Hovered render id: %u", frame.Selection.HoveredStableId);
                    if (frame.Selection.HasHoveredEntity)
                        ImGui::Text("Hovered entity: %s",
                                    frame.Selection.HoveredEntity.Name.c_str());
                }
                if (frame.Selection.Primitive.HasPrimitive)
                {
                    const PrimitiveSelectionResult& primitive =
                        frame.Selection.Primitive.Primitive;
                    ImGui::Text("Primitive status: %s",
                                DebugNameForPrimitiveRefineStatus(primitive.Status));
                    ImGui::Text("Primitive domain/kind: %s / %s",
                                DebugNameForEditorGeometryDomain(primitive.Domain),
                                DebugNameForEditorPrimitiveKind(primitive.Kind));
                    if (frame.Selection.Primitive.HasFaceId)
                        ImGui::Text("Face id: %u", primitive.FaceId);
                    if (frame.Selection.Primitive.HasEdgeId)
                        ImGui::Text("Edge id: %u", primitive.EdgeId);
                    if (frame.Selection.Primitive.HasVertexId)
                        ImGui::Text("Vertex id: %u", primitive.VertexId);
                    if (frame.Selection.Primitive.HasPointId)
                        ImGui::Text("Point id: %u", primitive.PointId);
                    if (primitive.HasHitPosition)
                    {
                        DrawVec3("Local hit", primitive.LocalHit);
                        DrawVec3("World hit", primitive.WorldHit);
                    }
                }
                DrawDiagnostics(frame.Selection.Diagnostics);
                ImGui::End();
            }

            if (windowId == "file.scene" &&
                BeginFixedWindow("File / Scene", open, ImVec2(0.0f, 0.0f)))
            {
                ImGui::TextWrapped("%s", frame.Document.StatusText.c_str());
                ImGui::Text("Active path: %s",
                            frame.Document.HasActivePath
                                ? frame.Document.ActivePath.c_str()
                                : "(none)");
                ImGui::Text("Dirty: %s", frame.Document.Dirty ? "yes" : "no");
                ImGui::Text("Revision: %llu saved: %llu",
                            static_cast<unsigned long long>(frame.Document.Revision),
                            static_cast<unsigned long long>(frame.Document.SavedRevision));
                const bool historyControlsAvailable =
                    context != nullptr &&
                    context->DocumentCommands.Available();
                if (!historyControlsAvailable || !frame.Document.CanUndo)
                    ImGui::BeginDisabled();
                if (ImGui::Button("Undo") && historyControlsAvailable)
                    (void)context->DocumentCommands.Undo();
                if (!historyControlsAvailable || !frame.Document.CanUndo)
                    ImGui::EndDisabled();
                ImGui::SameLine();
                if (!historyControlsAvailable || !frame.Document.CanRedo)
                    ImGui::BeginDisabled();
                if (ImGui::Button("Redo") && historyControlsAvailable)
                    (void)context->DocumentCommands.Redo();
                if (!historyControlsAvailable || !frame.Document.CanRedo)
                    ImGui::EndDisabled();
                if (!frame.Document.UndoLabel.empty())
                    ImGui::Text("Undo next: %s", frame.Document.UndoLabel.c_str());
                if (!frame.Document.RedoLabel.empty())
                    ImGui::Text("Redo next: %s", frame.Document.RedoLabel.c_str());
                DrawDiagnostics(frame.Document.Diagnostics);
                ImGui::Separator();
                ImGui::TextWrapped("%s",
                                    frame.SceneFile.FileDialogBoundaryText.c_str());
                if (!frame.SceneFile.LifecycleEnabled ||
                    context == nullptr ||
                    lastSceneFileResult == nullptr)
                {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("New scene") &&
                    frame.SceneFile.LifecycleEnabled &&
                    context != nullptr &&
                    lastSceneFileResult != nullptr)
                {
                    *lastSceneFileResult =
                        ApplyEditorNewSceneCommand(context->SceneCommands);
                }
                ImGui::SameLine();
                if (ImGui::Button("Close scene") &&
                    frame.SceneFile.LifecycleEnabled &&
                    context != nullptr &&
                    lastSceneFileResult != nullptr)
                {
                    *lastSceneFileResult =
                        ApplyEditorCloseSceneCommand(context->SceneCommands);
                }
                if (!frame.SceneFile.LifecycleEnabled ||
                    context == nullptr ||
                    lastSceneFileResult == nullptr)
                {
                    ImGui::EndDisabled();
                }

                const bool sceneControlsAvailable =
                    frame.SceneFile.CanSave &&
                    frame.SceneFile.CanOpen &&
                    context != nullptr &&
                    scenePathBuffer != nullptr &&
                    lastSceneFileResult != nullptr;
                if (!sceneControlsAvailable)
                    ImGui::BeginDisabled();
                if (scenePathBuffer != nullptr)
                {
                    ImGui::InputText("Scene path",
                                     scenePathBuffer->data(),
                                     scenePathBuffer->size());
                }
                else
                {
                    ImGui::TextDisabled("Scene path input is not bound.");
                }
                if (ImGui::Button("Save / Save As") && sceneControlsAvailable)
                {
                    *lastSceneFileResult = ApplyEditorSceneSaveCommand(
                        context->SceneCommands,
                        EditorSceneFileCommand{
                            .Path = std::string(scenePathBuffer->data()),
                        });
                }
                ImGui::SameLine();
                if (ImGui::Button("Open path") && sceneControlsAvailable)
                {
                    *lastSceneFileResult = ApplyEditorSceneLoadCommand(
                        context->SceneCommands,
                        EditorSceneFileCommand{
                            .Path = std::string(scenePathBuffer->data()),
                        });
                }
                if (!sceneControlsAvailable)
                    ImGui::EndDisabled();
                ImGui::TextWrapped("%s", frame.SceneFile.StatusText.c_str());
                const EditorSceneFileResult* result =
                    lastSceneFileResult != nullptr && lastSceneFileResult->has_value()
                        ? &**lastSceneFileResult
                        : frame.SceneFile.LastResult.has_value()
                            ? &*frame.SceneFile.LastResult
                            : nullptr;
                if (result != nullptr)
                {
                    ImGui::Text("Last scene command: %s",
                                DebugNameForEditorCommandStatus(
                                    result->Status));
                    ImGui::Text("Stats: entities=%u mesh=%u graph=%u pointCloud=%u",
                                result->Stats.Entities,
                                result->Stats.MeshEntities,
                                result->Stats.GraphEntities,
                                result->Stats.PointCloudEntities);
                }
                DrawDiagnostics(frame.SceneFile.Diagnostics);
                ImGui::End();
            }

            if (windowId == "file.import" &&
                BeginFixedWindow("File / Import", open, ImVec2(0.0f, 0.0f)))
            {
                if (importPathBuffer != nullptr)
                {
                    ImGui::InputText("Path",
                                     importPathBuffer->data(),
                                     importPathBuffer->size());
                }
                else
                {
                    ImGui::TextDisabled("Path input is not bound.");
                }
                if (importPayloadKind != nullptr)
                {
                    const bool payloadHintAvailable =
                        frame.FileImport.CanChoosePayloadHint;
                    if (!payloadHintAvailable)
                        ImGui::BeginDisabled();
                    if (ImGui::BeginCombo(
                            "Payload hint",
                            DebugNameForEditorAssetPayloadKind(*importPayloadKind)))
                    {
                        for (const EditorFileImportPayloadOption& option :
                             frame.FileImport.PayloadOptions)
                        {
                            const bool selected =
                                *importPayloadKind == option.Kind;
                            if (!option.Enabled)
                                ImGui::BeginDisabled();
                            if (ImGui::Selectable(
                                    DebugNameForEditorAssetPayloadKind(
                                        option.Kind),
                                    selected))
                            {
                                *importPayloadKind = option.Kind;
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                            if (!option.Enabled)
                            {
                                ImGui::EndDisabled();
                                DrawDisabledReasonTooltip(
                                    option.DisabledReason);
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (!payloadHintAvailable)
                    {
                        ImGui::EndDisabled();
                        DrawDisabledReasonTooltip(
                            frame.FileImport.PayloadHintDisabledReason);
                    }
                }
                else
                {
                    ImGui::TextDisabled("Payload hint is not bound.");
                }

                const bool importAvailable =
                    frame.FileImport.CanImport &&
                    context != nullptr &&
                    importPathBuffer != nullptr &&
                    importPayloadKind != nullptr &&
                    lastImportResult != nullptr;
                if (!importAvailable)
                    ImGui::BeginDisabled();
                if (ImGui::Button("Import asset") && importAvailable)
                {
                    *lastImportResult = ApplyEditorFileImportCommand(
                        context->SceneCommands,
                        EditorFileImportCommand{
                            .Path = std::string(importPathBuffer->data()),
                            .PayloadKind = *importPayloadKind,
                        });
                }
                if (!importAvailable)
                {
                    ImGui::EndDisabled();
                    DrawDisabledReasonTooltip(
                        frame.FileImport.ImportDisabledReason);
                }
                ImGui::TextWrapped("%s", frame.FileImport.StatusText.c_str());
                const EditorFileImportResult* result =
                    lastImportResult != nullptr && lastImportResult->has_value()
                        ? &**lastImportResult
                        : frame.FileImport.LastResult.has_value()
                            ? &*frame.FileImport.LastResult
                            : nullptr;
                if (result != nullptr)
                {
                    ImGui::Text("Last import: %s",
                                DebugNameForEditorCommandStatus(
                                    result->Status));
                    ImGui::Text("Payload: %s",
                                DebugNameForEditorAssetPayloadKind(
                                    result->PayloadKind));
                    if (result->Asset.IsValid())
                    {
                        ImGui::Text("Asset: %u:%u",
                                    result->Asset.Index,
                                    result->Asset.Generation);
                    }
                    if (result->PrimitiveEntitiesCreated > 0u)
                    {
                        ImGui::Text("Primitive entities: %llu",
                                    static_cast<unsigned long long>(
                                        result->PrimitiveEntitiesCreated));
                    }
                    if (result->EmbeddedTextureAssetsCreated > 0u)
                    {
                        ImGui::Text("Embedded textures: %llu",
                                    static_cast<unsigned long long>(
                                        result->EmbeddedTextureAssetsCreated));
                    }
                    if (result->TextureUploadRequests > 0u)
                    {
                        ImGui::Text("Texture upload requests: %llu",
                                    static_cast<unsigned long long>(
                                        result->TextureUploadRequests));
                    }
                }
                DrawAssetImportQueue(frame.AssetImportQueue, context);
                DrawDiagnostics(frame.FileImport.Diagnostics);
                ImGui::End();
            }

            if (windowId == "view.frame_graph" &&
                BeginFixedWindow("Frame Graph", open, ImVec2(0.0f, 0.0f)))
            {
                bool gpuProfilingEnabled =
                    frame.RenderGraph.GpuProfilingEnabled;
                const bool gpuProfilingToggleAvailable =
                    context != nullptr &&
                    frame.RenderGraph.GpuProfilingToggleAvailable;
                std::optional<EditorGpuProfilingConfigResult>
                    gpuProfilingConfigResult{};
                if (!gpuProfilingToggleAvailable)
                    ImGui::BeginDisabled();
                if (ImGui::Checkbox("Enable GPU profiling",
                                    &gpuProfilingEnabled) &&
                    gpuProfilingToggleAvailable)
                {
                    gpuProfilingConfigResult =
                        ApplyEditorGpuProfilingConfigCommand(
                            context->RenderRecipeCommands, gpuProfilingEnabled);
                }
                if (!gpuProfilingToggleAvailable)
                {
                    ImGui::EndDisabled();
                    DrawDisabledReasonTooltip(
                        frame.RenderGraph.GpuProfilingToggleDisabledReason);
                }
                if (gpuProfilingConfigResult.has_value() &&
                    !gpuProfilingConfigResult->Message.empty())
                {
                    ImGui::TextWrapped(
                        "%s", gpuProfilingConfigResult->Message.c_str());
                    for (const auto& diagnostic :
                         gpuProfilingConfigResult->Preview.Diagnostics)
                    {
                        const std::string text = diagnostic.Subject.empty()
                                                     ? diagnostic.Message
                                                     : diagnostic.Subject +
                                                           ": " +
                                                           diagnostic.Message;
                        ImGui::BulletText("%s", text.c_str());
                    }
                    for (const std::string& field :
                         gpuProfilingConfigResult->Apply.RejectedBootOnlyFields)
                    {
                        ImGui::BulletText("Boot-only field rejected: %s",
                                          field.c_str());
                    }
                }
                if (!frame.RenderGraph.GpuProfilingControlStatusText.empty())
                {
                    ImGui::TextWrapped(
                        "%s",
                        frame.RenderGraph.GpuProfilingControlStatusText
                            .c_str());
                }
                for (const std::string& diagnostic :
                     frame.RenderGraph.GpuProfilingControlDiagnostics)
                {
                    ImGui::BulletText("%s", diagnostic.c_str());
                }
                ImGui::Separator();

                if (!frame.RenderGraph.Enabled)
                {
                    ImGui::TextDisabled(
                        "Renderer frame graph diagnostics are unavailable.");
                    DrawDiagnostics(frame.RenderGraph.Diagnostics);
                }
                else
                {
                    ImGui::TextWrapped("%s",
                                       frame.RenderGraph.StatusText.c_str());
                    ImGui::Text("Compile: %s (%llu us)",
                                frame.RenderGraph.CompileSucceeded ? "yes"
                                                                   : "no",
                                    static_cast<unsigned long long>(
                                    frame.RenderGraph.CompileTimeMicros));
                    ImGui::Text("Execute: %s (%llu us), device=%s",
                                frame.RenderGraph.ExecuteSucceeded ? "yes"
                                                                   : "no",
                                    static_cast<unsigned long long>(
                                    frame.RenderGraph.ExecuteTimeMicros),
                                frame.RenderGraph.DeviceOperational
                                    ? "operational"
                                    : "not operational");
                    ImGui::Text("Passes: %u live, %u culled",
                                frame.RenderGraph.PassCount,
                                frame.RenderGraph.CulledPassCount);
                    ImGui::Text(
                        "Resources: %u, barriers=%u, transient=%llu bytes",
                        frame.RenderGraph
                            .ResourceCount,
                        frame.RenderGraph
                         .BarrierCount,
                                static_cast<unsigned long long>(
                                    frame.RenderGraph.TransientMemoryEstimateBytes));
                    ImGui::Text(
                        "Queue handoffs: %u, timeline edges=%u signals=%u "
                        "waits=%u "
                        "ownership=%u",
                                frame.RenderGraph.QueueHandoffEdgeCount,
                        frame.RenderGraph.CrossQueueTimelineEdgeCount,
                        frame.RenderGraph.CrossQueueTimelineSignalCount,
                        frame.RenderGraph.CrossQueueTimelineWaitCount,
                        frame.RenderGraph.CrossQueueOwnershipTransferCount);
                    ImGui::Text(
                        "Command passes: recorded=%u skipped=%u "
                        "nonOperational=%u "
                        "unavailable=%u",
                        frame.RenderGraph.CommandPassesRecorded,
                        frame.RenderGraph.CommandPassesSkipped,
                        frame.RenderGraph.CommandPassesSkippedNonOperational,
                        frame.RenderGraph.CommandPassesSkippedUnavailable);
                    ImGui::Text("Async compute frames: %u",
                                frame.RenderGraph.AsyncComputeUtilizedFrames);
                    if (ImGui::CollapsingHeader("GPU Profile",
                                                ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        const EditorGpuProfileModel& profile =
                            frame.RenderGraph.GpuProfile;
                        ImGui::Text("Status: %s, source=%s, fresh=%s, stale=%s",
                                    profile.Status.c_str(),
                                    profile.Source.c_str(),
                                    profile.Fresh ? "yes" : "no",
                                    profile.Stale ? "yes" : "no");
                        if (profile.HasResolvedFrame)
                        {
                            ImGui::Text(
                                "Resolved submission: frame=%llu slot=%u "
                                "age=%llu frame(s)",
                                static_cast<unsigned long long>(
                                    profile.ResolvedSubmittedFrameNumber),
                                profile.ResolvedFrameSlot,
                                static_cast<unsigned long long>(
                                    profile.SampleAgeFrames));
                        }
                        else
                        {
                            ImGui::TextDisabled("No resolved submission key.");
                        }
                        if (!profile.Diagnostic.empty())
                        {
                            ImGui::TextWrapped("Profile diagnostic: %s",
                                               profile.Diagnostic.c_str());
                        }

                        ImGui::Text("Queue envelopes:");
                        if (profile.QueueEnvelopes.empty())
                        {
                            ImGui::TextDisabled("No queue envelope samples.");
                        }
                        for (const EditorGpuProfileQueueModel& queue :
                             profile.QueueEnvelopes)
                        {
                            if (queue.DurationNs.has_value())
                            {
                                ImGui::BulletText(
                                    "%s - %llu ns (%s)",
                                    queue.Queue.c_str(),
                                static_cast<unsigned long long>(
                                        *queue.DurationNs),
                                    queue.Source.c_str());
                            }
                            else
                            {
                                ImGui::BulletText("%s - unavailable (%s)",
                                                  queue.Queue.c_str(),
                                                  queue.Source.c_str());
                            }
                        }

                        ImGui::Text("Pass samples:");
                        if (profile.Passes.empty())
                        {
                            ImGui::TextDisabled("No pass samples.");
                        }
                        for (const EditorGpuProfilePassModel& pass :
                             profile.Passes)
                        {
                            const std::string typedId =
                                pass.HasTypedId
                                    ? " [" + std::to_string(pass.TypedId) + "]"
                                    : std::string{};
                            if (pass.DurationNs.has_value())
                            {
                                ImGui::BulletText(
                                    "%s%s - %llu ns, queue=%s, command=%s, "
                                    "source=%s",
                                    pass.Name.c_str(),
                                    typedId.c_str(),
                                static_cast<unsigned long long>(
                                        *pass.DurationNs),
                                    pass.Queue.c_str(),
                                    pass.CommandStatus.c_str(),
                                    pass.Source.c_str());
                            }
                            else
                            {
                                ImGui::BulletText(
                                    "%s%s - unavailable, queue=%s, command=%s, source=%s",
                                    pass.Name.c_str(),
                                    typedId.c_str(),
                                    pass.Queue.c_str(),
                                    pass.CommandStatus.c_str(),
                                    pass.Source.c_str());
                            }
                        }
                    }
                    if (!frame.RenderGraph.LifecycleDiagnostic.empty())
                    {
                        ImGui::TextWrapped("Lifecycle: %s",
                                           frame.RenderGraph.LifecycleDiagnostic.c_str());
                    }
                    if (!frame.RenderGraph.Diagnostic.empty() &&
                        frame.RenderGraph.Diagnostic != frame.RenderGraph.StatusText)
                    {
                        ImGui::TextWrapped("Diagnostic: %s",
                                           frame.RenderGraph.Diagnostic.c_str());
                    }

                    if (ImGui::CollapsingHeader("Command Passes",
                                                ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        if (frame.RenderGraph.CommandPasses.empty())
                        {
                            ImGui::TextDisabled("No command pass records.");
                        }
                        for (const EditorRenderGraphPassModel& pass :
                             frame.RenderGraph.CommandPasses)
                        {
                            if (pass.HasTypedId)
                            {
                                ImGui::BulletText(
                                    "%s [%u] - %s",
                                    pass.Name.c_str(),
                                    pass.TypedId,
                                    pass.Status.c_str());
                            }
                            else
                            {
                                ImGui::BulletText("%s - %s",
                                                  pass.Name.c_str(),
                                                  pass.Status.c_str());
                            }
                        }
                    }

                    if (ImGui::CollapsingHeader("Compiler Debug Dump"))
                    {
                        if (frame.RenderGraph.DebugDump.empty())
                        {
                            ImGui::TextDisabled("No debug dump available.");
                        }
                        else
                        {
                            ImGui::BeginChild("##FrameGraphDebugDump",
                                              ImVec2(0.0f, 240.0f),
                                              true,
                                              ImGuiWindowFlags_HorizontalScrollbar);
                            ImGui::TextUnformatted(
                                frame.RenderGraph.DebugDump.c_str());
                            ImGui::EndChild();
                        }
                    }
                }
                ImGui::End();
            }

            if (windowId == "view.render_recipes" &&
                BeginFixedWindow("Render Recipes", open, ImVec2(0.0f, 0.0f)))
            {
                DrawRenderRecipeEditor(frame.RenderRecipe,
                                       context,
                                       renderRecipeDraftBuffer);
                ImGui::End();
            }

            if (windowId == "view.camera_render" &&
                BeginFixedWindow("Camera / Render", open, ImVec2(0.0f, 0.0f)))
            {
                if (frame.CameraRender.HasMainCameraController)
                {
                    ImGui::Text("Main camera: %s",
                                DebugNameForEditorCameraControllerKind(
                                    frame.CameraRender.MainCameraControllerKind));
                }
                else
                {
                    ImGui::TextDisabled("Main camera: not registered");
                }

                if (context != nullptr &&
                    frame.CameraRender.CameraControlsAvailable)
                {
                    ImGui::TextDisabled("Viewport controls: RMB/MMB drag rotates; WASD "
                                        "pans/moves; Shift accelerates; scroll zooms.");
                    if (ImGui::Button("Orbit"))
                    {
                        (void)ApplyEditorCameraControllerCommand(
                            context->SceneCommands,
                            EditorCameraControllerCommand{
                                .Kind = EditorCameraControllerKind::Orbit,
                            });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Fly"))
                    {
                        (void)ApplyEditorCameraControllerCommand(
                            context->SceneCommands,
                            EditorCameraControllerCommand{
                                .Kind = EditorCameraControllerKind::Fly,
                            });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Free look"))
                    {
                        (void)ApplyEditorCameraControllerCommand(
                            context->SceneCommands,
                            EditorCameraControllerCommand{
                                .Kind = EditorCameraControllerKind::FreeLook,
                            });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Top down"))
                    {
                        (void)ApplyEditorCameraControllerCommand(
                            context->SceneCommands,
                            EditorCameraControllerCommand{
                                .Kind = EditorCameraControllerKind::TopDown,
                            });
                    }
                }

                DrawDiagnostics(frame.CameraRender.Diagnostics);
                ImGui::End();
            }

            if (windowId == "view.geometry_visualization" &&
                BeginFixedWindow("Geometry Visualization", open, ImVec2(0.0f, 0.0f)))
            {
                if (frame.Visualization.HasSelectedEntity)
                {
                    ImGui::Text("Selected render id: %u",
                                frame.Visualization.SelectedStableId);
                    ImGui::Text("Geometry domain: %s",
                                DebugNameForEditorGeometryDomain(
                                    frame.Visualization.SelectedDomain));

                    if (context != nullptr &&
                        frame.Visualization.GeometryDomainControlsAvailable)
                    {
                        if (frame.Visualization.Visualization.HasConfig)
                        {
                            ImGui::Text("Visualization: %s",
                                        DebugNameForEditorVisualizationColorSource(
                                            frame.Visualization.Visualization.Source));
                        }
                        else
                        {
                            ImGui::TextDisabled("Visualization: material/default");
                        }
                        if (frame.Visualization.RecipeControlsAvailable)
                        {
                            if (frame.Visualization.Recipe.HasRecipe)
                            {
                                ImGui::Text(
                                    "Recipe: %s",
                                    DebugNameForEditorVisualizationRecipeKind(
                                        frame.Visualization.Recipe.Kind));
                            }
                            else
                            {
                                ImGui::TextDisabled("Recipe: config/presentation-derived");
                            }
                        }

                        if (ImGui::Button("Uniform color"))
                        {
                            (void)ApplyEditorVisualizationConfigCommand(
                                context->VisualizationCommands,
                                MakeUniformVisualizationConfigCommandFromModel(
                                    frame.Visualization.SelectedStableId,
                                    frame.Visualization.Visualization,
                                    EditorVisualizationTarget::Entity,
                                    frame.Visualization.Visualization.Color));
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Clear vis"))
                        {
                            for (const auto target : {
                                     EditorVisualizationTarget::Surface,
                                     EditorVisualizationTarget::Edges,
                                     EditorVisualizationTarget::Points,
                                     EditorVisualizationTarget::Entity})
                            {
                                (void)ApplyEditorVisualizationConfigCommand(
                                    context->VisualizationCommands,
                                    EditorVisualizationConfigCommand{
                                        .StableEntityId = frame.Visualization.SelectedStableId,
                                        .Target = target,
                                        .EnableConfig = false,
                                    });
                            }
                            (void)ApplyEditorVisualizationRecipeCommand(
                                context->VisualizationCommands,
                                EditorVisualizationRecipeCommand{
                                    .StableEntityId = frame.Visualization.SelectedStableId,
                                    .EnableRecipe = false,
                                });
                        }
                        DrawUniformVisualizationColorEdit(
                            frame.Visualization.Visualization,
                            *context,
                            frame.Visualization.SelectedStableId,
                            EditorVisualizationTarget::Entity,
                            true);
                        DrawScalarVisualizationControls(
                            frame.Visualization.Visualization,
                            *context,
                            frame.Visualization.SelectedStableId,
                            EditorVisualizationTarget::Entity,
                            true);
                        DrawVisualizationPropertyPresets(
                            frame.Visualization.Properties,
                            frame.Visualization.Visualization,
                            *context,
                            frame.Visualization.SelectedStableId,
                            EditorVisualizationTarget::Entity,
                            true);
                    }
                }
                DrawDiagnostics(frame.Visualization.Diagnostics);
                ImGui::End();
            }
        }
    }

    extern "C++"
    {
        struct EditorShell::Impl
        {
            struct SandboxPreparedFrame final
            {
                Runtime::EditorWorkspaceSnapshotPreparedFrame Workspace{};
                Runtime::EditorSceneEditingPreparedFrame Scene{};
                Runtime::EditorPointCloudServicePreparedFrame PointCloudService{};
                Runtime::EditorVisualizationEditingPreparedFrame Visualization{};
                Runtime::EditorRenderRecipeEditingPreparedFrame RenderRecipe{};
            };

            Runtime::EditorWorkspaceAttachment Attachment{};
            Runtime::EditorUiHost* Host{nullptr};
            Runtime::EditorUiFrameContributionHandle FrameContribution{};
            BuiltinWindowHandles BuiltinHandles{};
            std::vector<Runtime::EditorWindowHandle> RegisteredWindows{};
            std::vector<std::pair<std::uint64_t, std::function<void(const SandboxEditorContext&)>>>
                FrameObservers{};
            std::uint64_t NextFrameObserverId{1u};
            std::array<char, 1024> ImportPathBuffer{};
            std::array<char, 1024> ScenePathBuffer{};
            Runtime::EditorAssetPayloadKind ImportPayloadKind{
                Runtime::EditorAssetPayloadKind::Unknown};
            std::array<char, 8192> RenderRecipeDraftBuffer{};
            std::int32_t TextureBakeSourceIndex{0};
            std::int32_t TextureBakeTargetSemanticIndex{0};
            std::int32_t TextureBakeEncoderIndex{0};
            std::int32_t TextureBakeStorageIndex{0};
            std::int32_t TextureBakeColormapIndex{0};
            std::int32_t TextureBakeNormalSpaceIndex{0};
            std::uint32_t TextureBakeAdditionalConsumerMask{0u};
            // Panel-lifetime, not frame-lifetime: `ActiveContext` is rebuilt
            // and reset every frame, so the submit-time result and the
            // once-per-result extent latch must outlive it. The session slot
            // refreshes the copy below while it holds a terminal result.
            std::optional<EditorUvRegenerationCommandResult>
                LastUvRegenerationResult{};
            std::optional<EditorUvRegenerationCommandResult>
                LastUvExtentAdoption{};
            std::int32_t TextureBakeWidth{1024};
            std::int32_t TextureBakeHeight{1024};
            std::int32_t TextureBakePadding{2};
            bool UvAtlasForceRegenerate{true};
            bool UvAtlasPreserveAuthored{false};
            SandboxEditorFrame LastFrame{};
            std::optional<SandboxEditorContext> ActiveContext{};
            std::optional<SandboxPreparedFrame> ActivePreparedFrame{};

            void RegisterBuiltinWindows()
            {
                if (Host == nullptr)
                    return;
                for (std::size_t index = 0u; index < kBuiltinWindows.size(); ++index)
                {
                    const BuiltinWindowSpec& spec = kBuiltinWindows[index];
                    BuiltinHandles[index] = Host->RegisterWindow(
                        Runtime::EditorWindowDescriptor{
                            .Id = std::string{spec.Id},
                            .MenuPath = {"View"},
                            .Title = std::string{spec.Title},
                            .OpenByDefault = false,
                            .Draw =
                                [this, id = std::string{spec.Id}](bool& open)
                                {
                                    DrawBuiltinWindow(id, open);
                                },
                        });
                }
            }

            void UnregisterAllWindows()
            {
                if (Host != nullptr)
                {
                    for (const Runtime::EditorWindowHandle handle :
                         RegisteredWindows)
                    {
                        (void)Host->UnregisterWindow(handle);
                    }
                    for (const Runtime::EditorWindowHandle handle :
                         BuiltinHandles)
                    {
                        if (handle.IsValid())
                            (void)Host->UnregisterWindow(handle);
                    }
                }
                RegisteredWindows.clear();
                BuiltinHandles = {};
            }

            void DrawBuiltinWindow(
                const std::string_view id,
                bool& open)
            {
                if (!ActivePreparedFrame.has_value() || !ActiveContext.has_value())
                    return;

                if (ActiveContext->Parameterization.Results
                        .LastUvRegenerationResult.has_value())
                {
                    LastUvRegenerationResult =
                        *ActiveContext->Parameterization.Results
                             .LastUvRegenerationResult;
                }

                TextureBakeUiState textureBakeState{
                    .LastUvRegenerationResult = &LastUvRegenerationResult,
                    .LastUvExtentAdoption = &LastUvExtentAdoption,
                    .SourceIndex = &TextureBakeSourceIndex,
                    .TargetSemanticIndex = &TextureBakeTargetSemanticIndex,
                    .EncoderIndex = &TextureBakeEncoderIndex,
                    .StorageIndex = &TextureBakeStorageIndex,
                    .ColormapIndex = &TextureBakeColormapIndex,
                    .NormalSpaceIndex = &TextureBakeNormalSpaceIndex,
                    .AdditionalConsumerMask =
                        &TextureBakeAdditionalConsumerMask,
                    .Width = &TextureBakeWidth,
                    .Height = &TextureBakeHeight,
                    .Padding = &TextureBakePadding,
                    .UvForceRegenerate = &UvAtlasForceRegenerate,
                    .UvPreserveAuthored = &UvAtlasPreserveAuthored,
                };
                DrawFixedWindow(
                    id,
                    open,
                    LastFrame,
                    &*ActiveContext,
                    &ImportPathBuffer,
                    &ScenePathBuffer,
                    &RenderRecipeDraftBuffer,
                    &ImportPayloadKind,
                    &ActivePreparedFrame->Scene.LastAssetImportResult,
                    &ActivePreparedFrame->Scene.LastSceneFileResult,
                    &textureBakeState);
            }

            void DrawFrame()
            {
                if (!Attachment.IsAttached() || Host == nullptr)
                    return;

                std::optional<Runtime::EditorWorkspaceSnapshotPreparedFrame>
                    workspace = Runtime::PrepareEditorWorkspaceSnapshotFrame(
                        Attachment,
                        BuildModelRequest(Host->Windows(), BuiltinHandles),
                        std::string{ImportPathBuffer.data()},
                        ImportPayloadKind,
                        std::string{ScenePathBuffer.data()});
                if (!workspace.has_value())
                {
                    return;
                }

                ActivePreparedFrame.emplace(SandboxPreparedFrame{
                    .Workspace = std::move(*workspace),
                    .Scene = Runtime::PrepareEditorSceneEditingFrame(Attachment),
                    .PointCloudService = Runtime::PrepareEditorPointCloudServiceFrame(Attachment),
                    .Visualization =
                        Runtime::PrepareEditorVisualizationEditingFrame(Attachment),
                    .RenderRecipe =
                        Runtime::PrepareEditorRenderRecipeEditingFrame(Attachment),
                });
                SandboxPreparedFrame& prepared = *ActivePreparedFrame;
                LastFrame = SandboxEditorFrame{prepared.Workspace.Frame};
                ActiveContext.emplace(
                    prepared.Workspace,
                    prepared.Scene,
                    Runtime::PrepareEditorProcessingCommands(Attachment),
                    Runtime::PrepareEditorPointFieldFrame(Attachment),
                    Runtime::PrepareEditorPointAnalysisFrame(Attachment),
                    Runtime::PrepareEditorPointSetFrame(Attachment),
                    Runtime::PrepareEditorPointConstructionFrame(Attachment),
                    prepared.PointCloudService,
                    Runtime::PrepareEditorNormalFrame(Attachment),
                    Runtime::PrepareEditorRegistrationFrame(Attachment),
                    Runtime::PrepareEditorMeshFieldFrame(Attachment),
                    Runtime::PrepareEditorMeshTopologyFrame(Attachment),
                    Runtime::PrepareEditorParameterizationFrame(Attachment),
                    prepared.Visualization,
                    prepared.RenderRecipe,
                    LastFrame);
                ActiveContext->ClaimSceneViewport =
                    [host = Host](const float x, const float y, const float width, const float height)
                    {
                        host->SetSceneViewport(Runtime::EditorSceneViewportRect{
                            .X = x, .Y = y, .Width = width, .Height = height});
                    };
                DrawMainMenuBar(&Host->Windows());
                // Copy: an observer may add or remove observers.
                const auto observers = FrameObservers;
                for (const auto& [id, observer] : observers)
                {
                    if (observer)
                        observer(*ActiveContext);
                }
                (void)Host->Windows().DrawOpenWindows();
                // Drop the context before the frame storage it borrows.
                ActiveContext.reset();
                ActivePreparedFrame.reset();
            }

            Runtime::EditorWindowHandle RegisterEditorWindow(
                EditorWindowDescriptor descriptor)
            {
                if (Host == nullptr)
                    return {};
                auto draw = std::move(descriptor.Draw);
                const Runtime::EditorWindowHandle handle =
                    Host->RegisterWindow(
                    Runtime::EditorWindowDescriptor{
                        .Id = std::move(descriptor.Id),
                        .MenuPath = std::move(descriptor.MenuPath),
                        .Title = std::move(descriptor.Title),
                        .OpenByDefault = descriptor.OpenByDefault,
                        .Draw =
                            [this, draw = std::move(draw)](bool& open)
                            {
                                if (draw && ActiveContext.has_value())
                                {
                                    draw(open, *ActiveContext);
                                }
                            },
                        .OpenStateChanged =
                            std::move(descriptor.OpenStateChanged),
                    });
                if (handle.IsValid())
                    RegisteredWindows.push_back(handle);
                return handle;
            }

            void Attach(Runtime::WorldRegistry& worlds, Runtime::ServiceRegistry& services)
            {
                Detach();
                Host = services.Find<Runtime::EditorUiHost>();
                if (Host == nullptr || !Host->IsOperational())
                {
                    Host = nullptr;
                    return;
                }

                RegisterBuiltinWindows();
                Attachment.Attach(worlds, services);
                if (!Attachment.IsAttached())
                {
                    Detach();
                    return;
                }
                FrameContribution = Host->RegisterFrameContribution(
                    [this]
                    {
                        DrawFrame();
                    });
                if (!FrameContribution.IsValid())
                    Detach();
            }

            void Detach()
            {
                // Drop the context before the frame storage it borrows.
                ActiveContext.reset();
                ActivePreparedFrame.reset();
                LastFrame = {};
                LastUvRegenerationResult.reset();
                LastUvExtentAdoption.reset();
                if (Host != nullptr && FrameContribution.IsValid())
                    (void)Host->UnregisterFrameContribution(FrameContribution);
                FrameContribution = {};
                UnregisterAllWindows();
                Host = nullptr;
                Attachment.Detach();
            }
        };

        EditorShell::EditorShell()
            : m_Impl(std::make_unique<Impl>())
        {
        }

        EditorShell::~EditorShell()
        {
            Detach();
        }

        void EditorShell::Attach(Runtime::WorldRegistry& worlds, Runtime::ServiceRegistry& services)
        {
            m_Impl->Attach(worlds, services);
        }

        void EditorShell::Detach()
        {
            m_Impl->Detach();
        }

        Runtime::EditorWindowHandle EditorShell::RegisterEditorWindow(
            EditorWindowDescriptor descriptor)
        {
            return m_Impl->RegisterEditorWindow(std::move(descriptor));
        }

        std::uint64_t EditorShell::AddFrameObserver(
            std::function<void(const SandboxEditorContext&)> observer)
        {
            const std::uint64_t id = m_Impl->NextFrameObserverId++;
            m_Impl->FrameObservers.emplace_back(id, std::move(observer));
            return id;
        }

        void EditorShell::RemoveFrameObserver(const std::uint64_t id) noexcept
        {
            std::erase_if(m_Impl->FrameObservers,
                          [id](const auto& entry) { return entry.first == id; });
        }

        bool EditorShell::UnregisterEditorWindow(
            const Runtime::EditorWindowHandle handle)
        {
            if (m_Impl->Host == nullptr)
                return false;
            const bool removed = m_Impl->Host->UnregisterWindow(handle);
            if (removed)
            {
                std::erase(m_Impl->RegisteredWindows, handle);
            }
            return removed;
        }

        Runtime::EditorUiVisibilityCommandResult
        EditorShell::ApplyEditorUiVisibilityCommand(
            const Runtime::EditorUiVisibilityCommand command) noexcept
        {
            if (m_Impl->Host == nullptr)
                return {};
            return m_Impl->Host->ApplyVisibilityCommand(command);
        }

        bool EditorShell::IsEditorVisible() const noexcept
        {
            return m_Impl->Host != nullptr && m_Impl->Host->IsVisible();
        }

        std::vector<Runtime::EditorWindowMenuEntry>
        EditorShell::BuildEditorWindowMenuModel() const
        {
            if (m_Impl->Host == nullptr)
                return {};
            return m_Impl->Host->BuildWindowMenuModel();
        }

        bool EditorShell::SetEditorWindowOpen(
            const std::string_view id,
            const bool open)
        {
            return m_Impl->Host != nullptr &&
                   m_Impl->Host->SetWindowOpen(id, open);
        }

        bool EditorShell::IsAttached() const noexcept
        {
            return m_Impl->Host != nullptr &&
                   m_Impl->FrameContribution.IsValid() &&
                   m_Impl->Host->IsOperational() &&
                   m_Impl->Attachment.IsAttached();
        }

        const SandboxEditorFrame&
        EditorShell::GetLastFrame() const noexcept
        {
            return m_Impl->LastFrame;
        }
    }

}
