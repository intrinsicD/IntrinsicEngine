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
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

module Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.DiagnosticsStream;


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
import Extrinsic.Runtime.AgentServer;
import Extrinsic.Runtime.ViewCapture;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.PointCloudConsolidationTypes;

#include "Sandbox.PanelSupport.hpp"
#include "Sandbox.DiagnosticsPanel.hpp"

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
                    const std::string overlay = FormatProgressOverlay(
                        row.ProgressDeterminate, row.NormalizedProgress,
                        row.StageText.empty() ? "active" : row.StageText);
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

        // File > Save Screenshot: requests go to the runtime capture queue; the menu only
        // reports its availability. Returns the ticket of a started capture (0 otherwise).
        std::uint64_t DrawFileMenu(Runtime::ViewCaptureModule* capture)
        {
            if (capture == nullptr || !ImGui::BeginMenu("File"))
                return 0u;
            std::uint64_t ticket = 0u;
            const auto unavailable = capture->UnavailableReason();
            ImGui::BeginDisabled(unavailable.has_value());
            if (ImGui::MenuItem("Save Screenshot", "F12"))
                ticket = capture->Request({.Region = Runtime::ViewCaptureRegion::Viewport});
            if (ImGui::MenuItem("Save Window Screenshot"))
                ticket = capture->Request({.Region = Runtime::ViewCaptureRegion::Window});
            ImGui::EndDisabled();
            if (unavailable.has_value())
                ImGui::TextDisabled("%s", unavailable->c_str());
            else
                ImGui::TextDisabled("Saved to %s", capture->ScreenshotDirectory().string().c_str());
            ImGui::EndMenu();
            return ticket;
        }

        std::uint64_t DrawMainMenuBar(EditorWindowRegistry* windowRegistry, Runtime::ViewCaptureModule* capture)
        {
            if (!ImGui::BeginMainMenuBar())
                return 0u;
            const std::uint64_t captureTicket = DrawFileMenu(capture);
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
            return captureTicket;
        }

        // Panel-own gating of the visualization preset buttons: the caller passes whether the selection can be edited.
        constexpr std::string_view kVisualizationEditUnavailable = "Visualization editing is unavailable for this selection.";

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
                    const bool scalarClicked = ImGui::SmallButton("Scalar");
                    if (!canEditVisualization)
                        DrawDisabledReasonTooltip(kVisualizationEditUnavailable);
                    if (scalarClicked && canEditVisualization)
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
                    const bool isolinesClicked = ImGui::SmallButton("Isolines");
                    if (!canEditVisualization)
                        DrawDisabledReasonTooltip(kVisualizationEditUnavailable);
                    if (isolinesClicked && canEditVisualization)
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
                    const bool colorbufferClicked = ImGui::SmallButton("Color buffer");
                    if (!canEditVisualization)
                        DrawDisabledReasonTooltip(kVisualizationEditUnavailable);
                    if (colorbufferClicked && canEditVisualization)
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
                if (property.VectorFieldCandidate)
                {
                    if (wroteButton)
                        ImGui::SameLine();
                    const bool vectorfieldClicked = ImGui::SmallButton("Vector field");
                    if (!canEditVisualization)
                        DrawDisabledReasonTooltip(kVisualizationEditUnavailable);
                    if (vectorfieldClicked && canEditVisualization)
                    {
                        (void)ApplyEditorGeometryVectorFieldCommand(
                            context.VisualizationCommands,
                            EditorGeometryVectorFieldCommand{
                                .StableEntityId = selectedStableId,
                                .Operation = EditorVectorFieldOperation::Add,
                                .Layer = GeometryVectorFieldLayerRecipe{
                                    .Vector = GeometryPropertyRef{
                                        .Domain = property.ElementDomain,
                                        .Name = property.Name,
                                        .ValueKind = Geometry::PropertyValueKind::Vec3,
                                    },
                                },
                            });
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Show as arrows; edit or remove it under Appearance > Vector fields.");
                    wroteButton = true;
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

            // Panel-own gating: the recipe model reports which draft actions are possible (`Can*`), not why not,
            // so the reason is derived per action; the state blocker applies only while the commands exist.
            const auto recipeActionReadiness = [&](const bool stateAllows, const char* const action)
            {
                const std::string stateReason = RenderRecipeDraftBlockedReason(
                    action, DebugNameForEditorRenderRecipeDraftState(model.DraftState));
                return ReadinessUnlessBlocked({
                    {!commandsAvailable, "Render recipe commands are unavailable.",
                     ReadinessCode::WorkspaceUnavailable},
                    {commandsAvailable && !stateAllows, stateReason, ReadinessCode::StaleInput}});
            };

            if (DrawProcessingActionButton("Update Draft", recipeActionReadiness(true, "Update Draft")))
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
            if (DrawProcessingActionButton("Debounce", recipeActionReadiness(true, "Debounce")))
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
            if (DrawProcessingActionButton("Validate", recipeActionReadiness(model.CanValidate, "Validate")))
            {
                (void)ApplyEditorRenderRecipeCommand(
                    context->RenderRecipeCommands,
                    EditorRenderRecipeCommand{
                        .Kind = EditorRenderRecipeCommandKind::ValidateDraft,
                        .Document = draftText(),
                        .SourceId = "sandbox-editor",
                    });
            }

            ImGui::SameLine();
            if (DrawProcessingActionButton("Preview", recipeActionReadiness(model.CanPreview, "Preview")))
            {
                (void)ApplyEditorRenderRecipeCommand(
                    context->RenderRecipeCommands,
                    EditorRenderRecipeCommand{
                        .Kind = EditorRenderRecipeCommandKind::PreviewDraft,
                        .Document = draftText(),
                        .SourceId = "sandbox-editor",
                    });
            }

            ImGui::SameLine();
            if (DrawProcessingActionButton("Activate Preview", recipeActionReadiness(model.CanActivate, "Activate Preview")))
            {
                (void)ApplyEditorRenderRecipeCommand(
                    context->RenderRecipeCommands,
                    EditorRenderRecipeCommand{
                        .Kind = EditorRenderRecipeCommandKind::ActivatePreview,
                    });
            }

            ImGui::SameLine();
            if (DrawProcessingActionButton("Cancel", recipeActionReadiness(model.CanCancel, "Cancel")))
            {
                (void)ApplyEditorRenderRecipeCommand(
                    context->RenderRecipeCommands,
                    EditorRenderRecipeCommand{
                        .Kind = EditorRenderRecipeCommandKind::CancelDraft,
                    });
            }

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
                        // The recipe runtime's row says why this artifact cannot be published; the
                        // panel adds only the command surfaces it is itself missing.
                        if (DrawProcessingActionButton(
                                "Publish",
                                ReadinessUnlessBlocked({
                                    {!commandsAvailable || !context->RenderArtifactCommandsAvailable,
                                     "Render artifact commands are unavailable.",
                                     ReadinessCode::WorkspaceUnavailable},
                                    {!artifact.CanPublish,
                                     artifact.DisabledReason.empty()
                                         ? std::string_view{"The artifact cannot be published in its current state."}
                                         : std::string_view{artifact.DisabledReason}}})))
                        {
                            (void)ApplyEditorRenderRecipeCommand(
                                context->RenderRecipeCommands,
                                EditorRenderRecipeCommand{
                                    .Kind = EditorRenderRecipeCommandKind::PublishArtifact,
                                    .ArtifactId = artifact.ArtifactId,
                                    .Provenance = "sandbox-editor",
                                });
                        }
                        ImGui::TableSetColumnIndex(6);
                        // The recipe runtime's row says why this artifact cannot be applied; the
                        // panel adds only the command surfaces it is itself missing.
                        if (DrawProcessingActionButton(
                                "Apply",
                                ReadinessUnlessBlocked({
                                    {!commandsAvailable || !context->RenderArtifactCommandsAvailable,
                                     "Render artifact commands are unavailable.",
                                     ReadinessCode::WorkspaceUnavailable},
                                    {!artifact.CanApply,
                                     artifact.DisabledReason.empty()
                                         ? std::string_view{"The artifact cannot be applied in its current state."}
                                         : std::string_view{artifact.DisabledReason}}})))
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
            CameraViewUiState* cameraViewState,
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
                                slot.Semantic == GeometryPresentationSlotSemantic::Albedo)
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
                // The runtime's history answers both: availability and whether a step exists.
                if (DrawProcessingActionButton(
                        "Undo",
                        ReadinessUnlessBlocked({
                            {!historyControlsAvailable, "Document history is unavailable.",
                             ReadinessCode::WorkspaceUnavailable},
                            {historyControlsAvailable && !frame.Document.CanUndo, "Nothing to undo."}})) &&
                    historyControlsAvailable)
                    (void)context->DocumentCommands.Undo();
                ImGui::SameLine();
                if (DrawProcessingActionButton(
                        "Redo",
                        ReadinessUnlessBlocked({
                            {!historyControlsAvailable, "Document history is unavailable.",
                             ReadinessCode::WorkspaceUnavailable},
                            {historyControlsAvailable && !frame.Document.CanRedo, "Nothing to redo."}})) &&
                    historyControlsAvailable)
                    (void)context->DocumentCommands.Redo();
                if (!frame.Document.UndoLabel.empty())
                    ImGui::Text("Undo next: %s", frame.Document.UndoLabel.c_str());
                if (!frame.Document.RedoLabel.empty())
                    ImGui::Text("Redo next: %s", frame.Document.RedoLabel.c_str());
                DrawDiagnostics(frame.Document.Diagnostics);
                ImGui::Separator();
                ImGui::TextWrapped("%s",
                                    frame.SceneFile.FileDialogBoundaryText.c_str());
                const bool sceneSurfaceBound = context != nullptr && lastSceneFileResult != nullptr;
                // Runtime: lifecycle commands wired; panel-own: the shell bound its context and result slot.
                const auto lifecycleReadiness = ReadinessUnlessBlocked({
                    {!frame.SceneFile.LifecycleEnabled, "Scene lifecycle commands are unavailable.",
                     ReadinessCode::WorkspaceUnavailable},
                    {!sceneSurfaceBound, "The scene command surface is not bound to this window.",
                     ReadinessCode::WorkspaceUnavailable}});
                if (DrawProcessingActionButton("New scene", lifecycleReadiness) &&
                    frame.SceneFile.LifecycleEnabled && sceneSurfaceBound)
                {
                    *lastSceneFileResult =
                        ApplyEditorNewSceneCommand(context->SceneCommands);
                }
                ImGui::SameLine();
                if (DrawProcessingActionButton("Close scene", lifecycleReadiness) &&
                    frame.SceneFile.LifecycleEnabled && sceneSurfaceBound)
                {
                    *lastSceneFileResult =
                        ApplyEditorCloseSceneCommand(context->SceneCommands);
                }

                const bool sceneControlsAvailable =
                    frame.SceneFile.CanSave &&
                    frame.SceneFile.CanOpen &&
                    context != nullptr &&
                    scenePathBuffer != nullptr &&
                    lastSceneFileResult != nullptr;
                const auto sceneFileReadiness = ReadinessUnlessBlocked({
                    {!frame.SceneFile.CanSave, "Saving scenes is unavailable.",
                     ReadinessCode::WorkspaceUnavailable},
                    {!frame.SceneFile.CanOpen, "Opening scenes is unavailable.",
                     ReadinessCode::WorkspaceUnavailable},
                    {context == nullptr || lastSceneFileResult == nullptr,
                     "The scene command surface is not bound to this window.",
                     ReadinessCode::WorkspaceUnavailable},
                    {scenePathBuffer == nullptr, "Scene path input is not bound.",
                     ReadinessCode::MissingProperty}});
                if (scenePathBuffer != nullptr)
                {
                    ImGui::BeginDisabled(!sceneControlsAvailable);
                    ImGui::InputText("Scene path",
                                     scenePathBuffer->data(),
                                     scenePathBuffer->size());
                    ImGui::EndDisabled();
                }
                else
                {
                    ImGui::TextDisabled("Scene path input is not bound.");
                }
                if (DrawProcessingActionButton("Save / Save As", sceneFileReadiness) && sceneControlsAvailable)
                {
                    *lastSceneFileResult = ApplyEditorSceneSaveCommand(
                        context->SceneCommands,
                        EditorSceneFileCommand{
                            .Path = std::string(scenePathBuffer->data()),
                        });
                }
                ImGui::SameLine();
                if (DrawProcessingActionButton("Open path", sceneFileReadiness) && sceneControlsAvailable)
                {
                    *lastSceneFileResult = ApplyEditorSceneLoadCommand(
                        context->SceneCommands,
                        EditorSceneFileCommand{
                            .Path = std::string(scenePathBuffer->data()),
                        });
                }
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

                    // View presets frame the selection (or the whole scene when nothing is
                    // selected) through the same command the agent uses.
                    DrawCameraViewControls(
                        *context,
                        frame.CameraRender.SelectedStableIds,
                        frame.CameraRender.MainCameraControllerKind,
                        *cameraViewState);
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
            Runtime::ViewCaptureModule* ViewCapture{nullptr};
            const Runtime::SelectionController* Selection{nullptr};
            Runtime::SceneInteractionModule* Interaction{nullptr};
            // The capture the user started last (menu, F12 or window) and when it finished,
            // for the short "Saved ..." notice; agent captures stay silent.
            std::uint64_t UserCaptureTicket{0u};
            double UserCaptureNoticeUntil{0.0};
            std::string UserCaptureNotice{};
            int ScreenshotRegionIndex{0};
            JobsWindowState JobsState{};
            int ScreenshotPresetIndex{0};
            bool ScreenshotLegend{false};
            Runtime::EditorUiFrameContributionHandle FrameContribution{};
            BuiltinWindowHandles BuiltinHandles{};
            std::vector<Runtime::EditorWindowHandle> RegisteredWindows{};
            std::vector<std::pair<std::uint64_t, std::function<void(const SandboxEditorContext&)>>>
                FrameObservers{};
            std::uint64_t NextFrameObserverId{1u};
            DiagnosticsPanelState DiagnosticsState{};
            Runtime::EditorDiagnosticsStream* Diagnostics{nullptr};
            std::array<char, 1024> ImportPathBuffer{};
            std::array<char, 1024> ScenePathBuffer{};
            Runtime::EditorAssetPayloadKind ImportPayloadKind{
                Runtime::EditorAssetPayloadKind::Unknown};
            std::array<char, 8192> RenderRecipeDraftBuffer{};
            CameraViewUiState CameraViewState{};
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
                    &CameraViewState,
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
                if (const std::uint64_t ticket = DrawMainMenuBar(&Host->Windows(), ViewCapture))
                    UserCaptureTicket = ticket;
                DrawScreenshotShortcutAndNotice();
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

            // Present only when the Sandbox runs with --agent-socket: shows who is
            // connected and lets the user drop the agent's connection.
            void RegisterAgentConnectionWindow(Runtime::AgentServerModule& agent)
            {
                (void)RegisterEditorWindow(EditorWindowDescriptor{
                    .Id = "view.agent_connection",
                    .MenuPath = {"View"},
                    .Title = "Agent Connection",
                    .Draw = [&agent](bool& open, const SandboxEditorContext&)
                    {
                        if (!ImGui::Begin("Agent Connection", &open)) { ImGui::End(); return; }
                        const auto status = agent.Status();
                        ImGui::Text("Socket: %s", status.SocketPath.c_str());
                        if (!status.Listening)
                            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Not listening: %s", status.LastError.c_str());
                        else if (status.ClientConnected)
                            ImGui::Text("Connected: %s", status.ClientName.empty() ? "(client)" : status.ClientName.c_str());
                        else
                            ImGui::TextDisabled("Waiting for a client (tools/agents/mcp_bridge.py).");
                        ImGui::Text("Mode: %s", status.ReadOnly ? "read-only" : "read and write (changes are undoable, labeled 'Agent:')");
                        ImGui::Text("Calls handled: %llu", static_cast<unsigned long long>(status.CallsHandled));
                        if (ImGui::TreeNode("Allowed file roots"))
                        {
                            for (const auto& root : status.AllowedRoots) ImGui::BulletText("%s", root.c_str());
                            ImGui::TreePop();
                        }
                        // Runtime: the agent server's status says whether a client is connected.
                        if (DrawProcessingActionButton("Disconnect agent", ReadinessUnlessBlocked({
                                {!status.ClientConnected, "No agent client is connected.", ReadinessCode::MissingEntity}})))
                            agent.DisconnectClient();
                        ImGui::End();
                    },
                });
            }

            void DrawScreenshotShortcutAndNotice()
            {
                if (ViewCapture == nullptr)
                    return;
                if (ImGui::IsKeyPressed(ImGuiKey_F12, false) && !ImGui::GetIO().WantTextInput &&
                    !ViewCapture->UnavailableReason().has_value())
                    UserCaptureTicket = ViewCapture->Request({.Region = Runtime::ViewCaptureRegion::Viewport});
                if (UserCaptureTicket != 0u)
                {
                    const Runtime::ViewCaptureStatus status = ViewCapture->Status(UserCaptureTicket);
                    if (status.State == Runtime::ViewCaptureState::Completed ||
                        status.State == Runtime::ViewCaptureState::Failed ||
                        status.State == Runtime::ViewCaptureState::Unknown)
                    {
                        UserCaptureNotice = status.State == Runtime::ViewCaptureState::Completed
                            ? "Saved " + status.Path
                            : "Screenshot failed: " + status.Diagnostic;
                        UserCaptureNoticeUntil = ImGui::GetTime() + 4.0;
                        UserCaptureTicket = 0u;
                    }
                }
                if (UserCaptureNotice.empty() || ImGui::GetTime() > UserCaptureNoticeUntil)
                    return;
                const ImGuiViewport* viewport = ImGui::GetMainViewport();
                ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 12.0f,
                                               viewport->WorkPos.y + viewport->WorkSize.y - 12.0f),
                                        ImGuiCond_Always, ImVec2(1.0f, 1.0f));
                ImGui::SetNextWindowBgAlpha(0.85f);
                if (ImGui::Begin("##ScreenshotNotice", nullptr,
                                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs))
                    ImGui::TextUnformatted(UserCaptureNotice.c_str());
                ImGui::End();
            }

            void RegisterDiagnosticsWindow()
            {
                (void)RegisterEditorWindow(EditorWindowDescriptor{
                    .Id = "view.diagnostics",
                    .MenuPath = {"View"},
                    .Title = "Diagnostics / Log",
                    .Draw = [this](bool& open, const SandboxEditorContext&)
                    {
                        ImGui::SetNextWindowSize(ImVec2(900, 650), ImGuiCond_FirstUseEver);
                        if (ImGui::Begin("Diagnostics / Log", &open))
                            DrawDiagnosticsPanel(Diagnostics, DiagnosticsState);
                        ImGui::End();
                    },
                });
            }

            void RegisterJobsWindow()
            {
                (void)RegisterEditorWindow(EditorWindowDescriptor{
                    .Id = "view.jobs",
                    .MenuPath = {"View"},
                    .Title = "Jobs",
                    .Draw = [this](bool& open, const SandboxEditorContext& context)
                    {
                        if (ImGui::Begin("Jobs", &open))
                            DrawJobsWindow(context.Processing, JobsState);
                        ImGui::End();
                    },
                });
            }

            void RegisterScreenshotWindow()
            {
                (void)RegisterEditorWindow(EditorWindowDescriptor{
                    .Id = "view.screenshot",
                    .MenuPath = {"View"},
                    .Title = "Screenshot",
                    .Draw = [this](bool& open, const SandboxEditorContext&)
                    {
                        if (!ImGui::Begin("Screenshot", &open)) { ImGui::End(); return; }
                        const auto unavailable = ViewCapture->UnavailableReason();
                        constexpr const char* kRegions[] = {"Viewport (3D scene)", "Whole window (with panels)"};
                        ImGui::Combo("Region", &ScreenshotRegionIndex, kRegions, 2);
                        constexpr const char* kPresets[] = {"Current view", "Front", "Back", "Left", "Right",
                                                            "Top", "Bottom", "Isometric"};
                        ImGui::Combo("Camera", &ScreenshotPresetIndex, kPresets, 8);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                            ImGui::SetTooltip("Presets frame the selected entity (or the whole scene) for the shot "
                                              "and restore your view afterwards.");
                        const auto selected = Selection != nullptr ? Selection->SelectedStableIds()
                                                                   : std::span<const std::uint32_t>{};
                        const std::uint32_t selectedId = selected.empty() ? 0u : selected.front();
                        ImGui::Checkbox("Colormap legend (selected entity)", &ScreenshotLegend);
                        if (ScreenshotLegend && selectedId == 0u)
                            ImGui::TextDisabled("Select the entity whose scalar coloring the legend shows.");
                        // Runtime: the capture module's own refusal; panel-own: a legend needs the entity it colors.
                        const std::string captureUnavailable = unavailable.value_or(std::string{});
                        if (DrawProcessingActionButton("Save PNG", ReadinessUnlessBlocked({
                                {unavailable.has_value(), captureUnavailable, ReadinessCode::DeviceUnavailable},
                                {ScreenshotLegend && selectedId == 0u,
                                 "Select the entity whose scalar coloring the legend shows.", ReadinessCode::MissingEntity}})))
                        {
                            const auto preset = static_cast<Runtime::ViewCapturePreset>(ScreenshotPresetIndex);
                            UserCaptureTicket = ViewCapture->Request({
                                .Region = ScreenshotRegionIndex == 1 ? Runtime::ViewCaptureRegion::Window
                                                                     : Runtime::ViewCaptureRegion::Viewport,
                                .Preset = preset,
                                .FitEntity = preset == Runtime::ViewCapturePreset::Current ? 0u : selectedId,
                                .LegendEntity = ScreenshotLegend ? selectedId : 0u});
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("F12 saves the viewport from anywhere.");
                        if (unavailable.has_value())
                            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "%s", unavailable->c_str());
                        ImGui::Text("Folder: %s", ViewCapture->ScreenshotDirectory().string().c_str());
                        if (UserCaptureTicket != 0u)
                            ImGui::TextDisabled("Capturing...");
                        else if (const auto last = ViewCapture->LastFinished())
                        {
                            if (last->State == Runtime::ViewCaptureState::Completed)
                            {
                                ImGui::TextWrapped("Last: %s (%ux%u)", last->Path.empty() ? "(agent, not saved)" : last->Path.c_str(),
                                                   last->Width, last->Height);
                                if (last->Legend)
                                    ImGui::TextWrapped("Legend: %s, %s, %g to %g%s", last->Legend->Property.c_str(),
                                                       last->Legend->Colormap.c_str(), last->Legend->Min, last->Legend->Max,
                                                       last->Legend->AutoRange ? " (auto)" : "");
                            }
                            else
                                ImGui::TextWrapped("Last capture failed: %s", last->Diagnostic.c_str());
                        }
                        ImGui::End();
                    },
                });
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
                if (auto* agent = services.Find<Runtime::AgentServerModule>())
                    RegisterAgentConnectionWindow(*agent);
                ViewCapture = services.Find<Runtime::ViewCaptureModule>();
                Selection = services.Find<Runtime::SelectionController>();
                Interaction = services.Find<Runtime::SceneInteractionModule>();
                if (ViewCapture != nullptr)
                    RegisterScreenshotWindow();
                RegisterJobsWindow();
                Diagnostics = services.Find<Runtime::EditorDiagnosticsStream>();
                RegisterDiagnosticsWindow();
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
                ViewCapture = nullptr;
                Diagnostics = nullptr;
                DiagnosticsState = {};
                Selection = nullptr;
                Interaction = nullptr;
                UserCaptureTicket = 0u;
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

        Runtime::SceneInteractionModule* EditorShell::SceneInteraction() const noexcept
        {
            return m_Impl->Interaction;
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
