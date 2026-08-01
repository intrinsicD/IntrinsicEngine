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
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

module Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.EditorUiHost;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.TextureBakeModule;

namespace Extrinsic::Sandbox::Editor
{
    void DrawDisabledReasonTooltip(const std::string_view disabledReason)
    {
        constexpr ImGuiHoveredFlags hoverFlags =
            ImGuiHoveredFlags_ForTooltip |
            ImGuiHoveredFlags_AllowWhenDisabled;
        if (disabledReason.empty() || !ImGui::IsItemHovered(hoverFlags))
            return;

        ImGui::BeginTooltip();
        ImGui::TextUnformatted(
            disabledReason.data(),
            disabledReason.data() + disabledReason.size());
        ImGui::EndTooltip();
    }

    namespace
    {
        using namespace Extrinsic::Runtime;

        using VisualizationColorSource =
            decltype(EditorVisualizationConfigModel{}.Source);
        using ColormapType =
            decltype(EditorVisualizationConfigModel{}.ScalarColormap);
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

        inline constexpr std::array<const char*, 3>
            kTextureBakeStorageNames{{
                "auto (raw except normals/labels)",
                "raw float texture",
                "encoded RGBA texture",
            }};

        inline constexpr std::array<const char*, 6> kColormapNames{{
            "Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"}};

        inline constexpr std::array<const char*, 2> kNormalSpaceNames{{
            "object space", "world space"}};

        [[nodiscard]] const char* DebugNameForTextureBakeEncoder(
            const PropertyTextureBakeEncoding encoder) noexcept
        {
            switch (encoder)
            {
            case PropertyTextureBakeEncoding::Auto: return "auto";
            case PropertyTextureBakeEncoding::LinearScalar: return "linear scalar";
            case PropertyTextureBakeEncoding::ScalarColormap: return "scalar colormap";
            case PropertyTextureBakeEncoding::LabelPalette: return "label palette";
            case PropertyTextureBakeEncoding::Vector2: return "vector2";
            case PropertyTextureBakeEncoding::Vector3: return "vector3";
            case PropertyTextureBakeEncoding::Normal: return "normal";
            case PropertyTextureBakeEncoding::RgbaColor: return "rgba color";
            }
            return "unknown";
        }

        struct TextureBakeUiState
        {
            std::optional<EditorUvRegenerationCommandResult>*
                LastUvRegenerationResult{nullptr};
            std::int32_t* SourceIndex{nullptr};
            std::int32_t* TargetSemanticIndex{nullptr};
            std::int32_t* EncoderIndex{nullptr};
            std::int32_t* StorageIndex{nullptr};
            std::int32_t* ColormapIndex{nullptr};
            std::int32_t* NormalSpaceIndex{nullptr};
            std::uint32_t* AdditionalConsumerMask{nullptr};
            std::int32_t* Width{nullptr};
            std::int32_t* Height{nullptr};
            std::int32_t* UvResolution{nullptr};
            std::int32_t* UvPadding{nullptr};
            float* UvTexelsPerUnit{nullptr};
            bool* UvForceRegenerate{nullptr};
            bool* UvPreserveAuthored{nullptr};
        };

        [[nodiscard]] std::span<const EditorTextureBakeTarget>
        TextureBakeTargetsFor(
                const EditorTextureBakeControlsModel& model,
                const std::string_view outputName)
        {
            const auto found = std::ranges::find(
                model.TextureBakeTargets,
                outputName,
                &EditorTextureBakeTargetSnapshot::OutputName);
            if (found == model.TextureBakeTargets.end())
                return {};
            return found->Targets;
        }

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

        [[nodiscard]] EditorVisualizationConfigCommand
        MakeUniformVisualizationConfigCommandFromModel(
            const std::uint32_t stableEntityId,
            const EditorVisualizationConfigModel& model,
            const EditorVisualizationTarget target,
            const glm::vec4 color)
        {
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
            const EditorVisualizationConfigModel& model,
            const EditorVisualizationTarget target)
        {
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

        void DrawDiagnostics(const std::vector<EditorDiagnostic>& diagnostics)
        {
            for (const EditorDiagnostic& diagnostic : diagnostics)
            {
                ImGui::TextDisabled("%s: %s",
                                    DebugNameForEditorDiagnosticCode(diagnostic.Code),
                                    diagnostic.Message.c_str());
            }
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

        void DrawVec3(const char* label, const glm::vec3 value)
        {
            ImGui::Text("%s: %.3f, %.3f, %.3f", label, value.x, value.y, value.z);
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

        void DrawBoundRenderStateRows(
            const EditorBoundRenderStateModel& bound)
        {
            ImGui::SeparatorText("Bound render state");
            ImGui::Text("Rows: %zu generation=%llu",
                        bound.Rows.size(),
                        static_cast<unsigned long long>(
                            bound.RecipeGeneration));
            if (bound.Rows.empty())
            {
                ImGui::TextDisabled("No bound render state rows.");
                DrawDiagnostics(bound.Diagnostics);
                return;
            }

            constexpr ImGuiTableFlags tableFlags =
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("BoundRenderState", 8, tableFlags))
            {
                ImGui::TableSetupColumn("Kind");
                ImGui::TableSetupColumn("Lane");
                ImGui::TableSetupColumn("Label");
                ImGui::TableSetupColumn("Source");
                ImGui::TableSetupColumn("Readiness");
                ImGui::TableSetupColumn("Property");
                ImGui::TableSetupColumn("Job");
                ImGui::TableSetupColumn("Diagnostic");
                ImGui::TableHeadersRow();

                for (const EditorBoundRenderStateRow& row :
                     bound.Rows)
                {
                    const std::string laneText{ToString(row.Lane)};
                    const std::string sourceText =
                        row.SourceDescription.empty()
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
                    if (!row.Property.Name.empty())
                    {
                        ImGui::Text("%s%s",
                                    row.Property.Name.c_str(),
                                    row.HasCatalogMatch ? " catalog" : "");
                    }
                    else
                    {
                        ImGui::TextDisabled("-");
                    }
                    ImGui::TableSetColumnIndex(6);
                    if (row.Kind == EditorBoundRenderStateRowKind::DerivedJob)
                    {
                        ImGui::Text("%s %.2f",
                                    std::string(ToString(row.JobStatus)).c_str(),
                                    row.JobProgress);
                    }
                    else if (row.TextureAsset.IsValid() ||
                             row.AuthoredTexture.IsValid() ||
                             row.GeneratedTexture.IsValid())
                    {
                        ImGui::Text("texture");
                    }
                    else
                    {
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
            const EditorUvDiagnosticsModel& uv,
            const std::optional<EditorUvRegenerationCommandResult>&
                lastResult)
        {
            if (uv.UvRegenerationJob.has_value())
            {
                const EditorJobModel& job =
                    *uv.UvRegenerationJob;
                ImGui::Text("UV job: %s %.0f%%",
                            std::string(ToString(job.Status)).c_str(),
                            job.NormalizedProgress * 100.0f);
                if (!job.Diagnostic.empty())
                    ImGui::TextWrapped("%s", job.Diagnostic.c_str());
            }

            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last UV regeneration: none");
                return;
            }

            const EditorUvRegenerationCommandResult& result =
                *lastResult;
            ImGui::Text("Last UV regeneration: %s",
                        DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Atlas: %s / %s  %ux%u  charts=%u  splits=%zu",
                        DebugNameForEditorUvAtlasStatus(result.UvStatus),
                        DebugNameForEditorUvAtlasProvenance(result.Provenance),
                        result.AtlasWidth,
                        result.AtlasHeight,
                        result.ChartCount,
                        result.SeamSplitVertexCount);
            if (!result.Diagnostic.empty())
                ImGui::TextWrapped("%s", result.Diagnostic.c_str());
        }

        void DrawTextureBakeControls(
            const EditorTextureBakeControlsModel& model,
            const SandboxEditorContext* context,
            TextureBakeUiState* state)
        {
            std::optional<EditorUvRegenerationCommandResult>
                fallbackUvRegenerationResult{};
            std::int32_t fallbackSourceIndex{0};
            std::int32_t fallbackSemanticIndex{0};
            std::int32_t fallbackEncoderIndex{0};
            std::int32_t fallbackStorageIndex{0};
            std::int32_t fallbackColormapIndex{0};
            std::int32_t fallbackNormalSpaceIndex{0};
            std::uint32_t fallbackAdditionalConsumerMask{0u};
            std::int32_t fallbackWidth{
                static_cast<std::int32_t>(model.DefaultWidth)};
            std::int32_t fallbackHeight{
                static_cast<std::int32_t>(model.DefaultHeight)};
            std::int32_t fallbackUvResolution{1024};
            std::int32_t fallbackUvPadding{2};
            float fallbackUvTexelsPerUnit{0.0f};
            bool fallbackUvForceRegenerate{true};
            bool fallbackUvPreserveAuthored{false};

            auto* lastUvRegenerationResult =
                state != nullptr &&
                        state->LastUvRegenerationResult != nullptr
                    ? state->LastUvRegenerationResult
                    : &fallbackUvRegenerationResult;
            std::int32_t& sourceIndex =
                state != nullptr && state->SourceIndex != nullptr
                    ? *state->SourceIndex
                    : fallbackSourceIndex;
            std::int32_t& semanticIndex =
                state != nullptr && state->TargetSemanticIndex != nullptr
                    ? *state->TargetSemanticIndex
                    : fallbackSemanticIndex;
            std::int32_t& encoderIndex =
                state != nullptr && state->EncoderIndex != nullptr
                    ? *state->EncoderIndex
                    : fallbackEncoderIndex;
            std::int32_t& storageIndex =
                state != nullptr && state->StorageIndex != nullptr
                    ? *state->StorageIndex
                    : fallbackStorageIndex;
            std::int32_t& colormapIndex =
                state != nullptr && state->ColormapIndex != nullptr
                    ? *state->ColormapIndex
                    : fallbackColormapIndex;
            std::int32_t& normalSpaceIndex =
                state != nullptr && state->NormalSpaceIndex != nullptr
                    ? *state->NormalSpaceIndex
                    : fallbackNormalSpaceIndex;
            std::uint32_t& additionalConsumerMask =
                state != nullptr && state->AdditionalConsumerMask != nullptr
                    ? *state->AdditionalConsumerMask
                    : fallbackAdditionalConsumerMask;
            std::int32_t& bakeWidth =
                state != nullptr && state->Width != nullptr
                    ? *state->Width
                    : fallbackWidth;
            std::int32_t& bakeHeight =
                state != nullptr && state->Height != nullptr
                    ? *state->Height
                    : fallbackHeight;
            std::int32_t& uvResolution =
                state != nullptr && state->UvResolution != nullptr
                    ? *state->UvResolution
                    : fallbackUvResolution;
            std::int32_t& uvPadding =
                state != nullptr && state->UvPadding != nullptr
                    ? *state->UvPadding
                    : fallbackUvPadding;
            float& uvTexelsPerUnit =
                state != nullptr && state->UvTexelsPerUnit != nullptr
                    ? *state->UvTexelsPerUnit
                    : fallbackUvTexelsPerUnit;
            bool& uvForceRegenerate =
                state != nullptr && state->UvForceRegenerate != nullptr
                    ? *state->UvForceRegenerate
                    : fallbackUvForceRegenerate;
            bool& uvPreserveAuthored =
                state != nullptr && state->UvPreserveAuthored != nullptr
                    ? *state->UvPreserveAuthored
                    : fallbackUvPreserveAuthored;

            semanticIndex = std::clamp<std::int32_t>(
                semanticIndex,
                0,
                static_cast<std::int32_t>(kTextureBakeTargetSemantics.size() - 1u));
            encoderIndex = std::clamp<std::int32_t>(
                encoderIndex,
                0,
                static_cast<std::int32_t>(kTextureBakeEncoders.size() - 1u));
            storageIndex = std::clamp<std::int32_t>(
                storageIndex,
                0,
                static_cast<std::int32_t>(
                    kTextureBakeStorageModes.size() - 1u));
            colormapIndex = std::clamp<std::int32_t>(
                colormapIndex,
                0,
                static_cast<std::int32_t>(kColormapNames.size() - 1u));
            normalSpaceIndex = std::clamp<std::int32_t>(
                normalSpaceIndex,
                0,
                static_cast<std::int32_t>(kNormalSpaceNames.size() - 1u));
            bakeWidth = std::clamp<std::int32_t>(bakeWidth, 1, 8192);
            bakeHeight = std::clamp<std::int32_t>(bakeHeight, 1, 8192);
            uvResolution = std::clamp<std::int32_t>(uvResolution, 1, 16384);
            uvPadding = std::clamp<std::int32_t>(uvPadding, 0, uvResolution - 1);
            if (!std::isfinite(uvTexelsPerUnit) || uvTexelsPerUnit < 0.0f)
                uvTexelsPerUnit = 0.0f;

            ImGui::SeparatorText("UV / texture bake");
            ImGui::Text("UV: %s texcoords=%s count=%zu/%zu",
                        model.Uv.Provenance.c_str(),
                        model.Uv.HasTexcoords ? "yes" : "no",
                        model.Uv.TexcoordCount,
                        model.Uv.VertexCount);
            if (!model.Uv.LastFailure.empty())
                ImGui::TextDisabled("%s", model.Uv.LastFailure.c_str());
            if (!model.Uv.UvRegenerationAvailable)
                ImGui::TextDisabled("%s",
                                    model.Uv.UvRegenerationDisabledReason.c_str());

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

            const bool canRegenerateUvs =
                model.Uv.UvRegenerationAvailable &&
                context != nullptr &&
                model.SelectedStableId != 0u;
            if (!canRegenerateUvs)
                ImGui::BeginDisabled();
            if (ImGui::Button("Regenerate UVs") && canRegenerateUvs)
            {
                *lastUvRegenerationResult =
                    ApplyEditorUvRegenerationCommand(
                    context->GeometryCommands,
                    EditorUvRegenerationCommand{
                        .StableEntityId = model.SelectedStableId,
                        .PreserveValidAuthoredUvs = uvPreserveAuthored,
                        .ForceRegenerate = uvForceRegenerate,
                        .Resolution = static_cast<std::uint32_t>(uvResolution),
                        .Padding = static_cast<std::uint32_t>(uvPadding),
                        .TexelsPerUnit = uvTexelsPerUnit,
                    });
            }
            if (!canRegenerateUvs)
                ImGui::EndDisabled();
            DrawUvRegenerationStatus(model.Uv, *lastUvRegenerationResult);

            std::vector<std::size_t> bakeableIndices;
            bakeableIndices.reserve(model.Sources.size());
            for (std::size_t i = 0u; i < model.Sources.size(); ++i)
            {
                if (model.Sources[i].Bakeable)
                    bakeableIndices.push_back(i);
            }
            if (bakeableIndices.empty())
                sourceIndex = 0;
            else
                sourceIndex = std::clamp<std::int32_t>(
                    sourceIndex,
                    0,
                    static_cast<std::int32_t>(bakeableIndices.size() - 1u));

            const EditorTextureBakeSourceRow* selectedSource =
                bakeableIndices.empty()
                    ? nullptr
                    : &model.Sources[bakeableIndices[static_cast<std::size_t>(sourceIndex)]];

            if (ImGui::BeginCombo("Bake source",
                                  selectedSource != nullptr
                                      ? selectedSource->Name.c_str()
                                      : "none"))
            {
                for (std::size_t i = 0u; i < bakeableIndices.size(); ++i)
                {
                    const EditorTextureBakeSourceRow& row =
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
                    const GeometryPresentationSlotSemantic semantic)
                {
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
                            kTextureBakeStorageModes[
                                static_cast<std::size_t>(storageIndex)],
                            kTextureBakeEncoders[
                                static_cast<std::size_t>(encoderIndex)],
                        target);
                    return IsEditorTextureBakeTargetCompatible(
                    target.front(),
                        selectedSource->ResolvedExpectedValueKind(),
                        representation.Storage,
                        representation.Encoding);
                };

            if (ImGui::BeginCombo(
                    "Target",
                    std::string(ToString(kTextureBakeTargetSemantics[
                        static_cast<std::size_t>(semanticIndex)])).c_str()))
            {
                for (std::size_t i = 0u; i < kTextureBakeTargetSemantics.size(); ++i)
                {
                    const std::string label{
                        ToString(kTextureBakeTargetSemantics[i])};
                    const bool selected = semanticIndex == static_cast<std::int32_t>(i);
                    const bool compatible =
                        targetCompatible(kTextureBakeTargetSemantics[i]);
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
                        kTextureBakeEncoders[static_cast<std::size_t>(encoderIndex)])))
            {
                for (std::size_t i = 0u; i < kTextureBakeEncoders.size(); ++i)
                {
                    const bool selected = encoderIndex == static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(
                            DebugNameForTextureBakeEncoder(kTextureBakeEncoders[i]),
                            selected))
                    {
                        encoderIndex = static_cast<std::int32_t>(i);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            (void)ImGui::Combo(
                "Storage",
                &storageIndex,
                kTextureBakeStorageNames.data(),
                static_cast<int>(kTextureBakeStorageNames.size()));
            (void)ImGui::Combo(
                "Texture colormap",
                &colormapIndex,
                kColormapNames.data(),
                static_cast<int>(kColormapNames.size()));

            const auto makeTarget =
                [colormapIndex](const GeometryPresentationSlotSemantic semantic)
                {
                    return EditorTextureBakeTarget{
                        .PresentationKey = "mesh.surface",
                        .Semantic = semantic,
                        .Colormap = static_cast<ColormapType>(colormapIndex),
                    };
                };
            const auto targetsCompatible =
                [selectedSource, storageIndex, encoderIndex](
                    const std::vector<EditorTextureBakeTarget>& values)
                {
                    if (selectedSource == nullptr)
                        return false;
                    const PropertyTextureBakeRepresentation representation =
                    ResolveEditorTextureBakeTargetRepresentation(
                            selectedSource->ResolvedExpectedValueKind(),
                            kTextureBakeStorageModes[
                                static_cast<std::size_t>(storageIndex)],
                            kTextureBakeEncoders[
                                static_cast<std::size_t>(encoderIndex)],
                            values);
                    return std::ranges::all_of(
                        values,
                        [&](const EditorTextureBakeTarget& target)
                        {
                            return IsEditorTextureBakeTargetCompatible(
                            target,
                                selectedSource->ResolvedExpectedValueKind(),
                                representation.Storage,
                                representation.Encoding);
                        });
                };

            std::vector<EditorTextureBakeTarget> consumers{
                makeTarget(kTextureBakeTargetSemantics[
                    static_cast<std::size_t>(semanticIndex)])};
            for (std::size_t i = 0u;
                 i < kTextureBakeTargetSemantics.size();
                 ++i)
            {
                if (i == static_cast<std::size_t>(semanticIndex))
                    continue;
                const std::uint32_t bit =
                    1u << static_cast<std::uint32_t>(i);
                if ((additionalConsumerMask & bit) != 0u)
                    consumers.push_back(
                        makeTarget(kTextureBakeTargetSemantics[i]));
            }

            ImGui::TextUnformatted("Additional consumers");
            for (std::size_t i = 0u;
                 i < kTextureBakeTargetSemantics.size();
                 ++i)
            {
                if (i == static_cast<std::size_t>(semanticIndex))
                    continue;
                const GeometryPresentationSlotSemantic semantic =
                    kTextureBakeTargetSemantics[i];
                const std::uint32_t bit =
                    1u << static_cast<std::uint32_t>(i);
                bool selected = (additionalConsumerMask & bit) != 0u;
                std::vector<EditorTextureBakeTarget> candidate = consumers;
                if (!selected)
                    candidate.push_back(makeTarget(semantic));
                const bool compatible = targetsCompatible(candidate);
                if (selected && !compatible)
                {
                    additionalConsumerMask &= ~bit;
                    std::erase_if(
                        consumers,
                        [semantic](const EditorTextureBakeTarget& consumer)
                        {
                            return consumer.Semantic == semantic;
                        });
                    selected = false;
                }
                const std::string label{
                    "Also bind to " + std::string(ToString(semantic))};
                if (!compatible)
                    ImGui::BeginDisabled();
                if (ImGui::Checkbox(label.c_str(), &selected))
                {
                    if (selected)
                    {
                        additionalConsumerMask |= bit;
                        consumers.push_back(makeTarget(semantic));
                    }
                    else
                    {
                        additionalConsumerMask &= ~bit;
                        std::erase_if(
                            consumers,
                            [semantic](
                                const EditorTextureBakeTarget& consumer)
                            {
                                return consumer.Semantic == semantic;
                            });
                    }
                }
                if (!compatible)
                    ImGui::EndDisabled();
            }

            const bool hasNormalConsumer = std::ranges::any_of(
                consumers,
                [](const EditorTextureBakeTarget& consumer)
                {
                    return consumer.Semantic == GeometryPresentationSlotSemantic::Normal;
                });
            if (hasNormalConsumer)
            {
                (void)ImGui::Combo(
                    "Normal texture space",
                    &normalSpaceIndex,
                    kNormalSpaceNames.data(),
                    static_cast<int>(kNormalSpaceNames.size()));
            }
            ImGui::InputInt("Bake width", &bakeWidth);
            ImGui::InputInt("Bake height", &bakeHeight);
            bakeWidth = std::clamp<std::int32_t>(bakeWidth, 1, 8192);
            bakeHeight = std::clamp<std::int32_t>(bakeHeight, 1, 8192);

            const bool canBake =
                model.CanBake &&
                context != nullptr &&
                selectedSource != nullptr &&
                targetsCompatible(consumers);
            if (!canBake)
                ImGui::BeginDisabled();
            if (ImGui::Button("Bake") && canBake)
            {
                (void)ApplyEditorTextureBakeCommand(
                    context->VisualizationCommands,
                    EditorTextureBakeCommand{
                        .StableEntityId = model.SelectedStableId,
                        .TargetSemantic =
                            kTextureBakeTargetSemantics[
                                static_cast<std::size_t>(semanticIndex)],
                        .SourceDomain = selectedSource->BakeDomain,
                        .ExpectedValueKind = selectedSource->ResolvedExpectedValueKind(),
                        .PropertyName = selectedSource->Name,
                        .Encoder =
                            kTextureBakeEncoders[
                                static_cast<std::size_t>(encoderIndex)],
                        .Width = static_cast<std::uint32_t>(bakeWidth),
                        .Height = static_cast<std::uint32_t>(bakeHeight),
                        .GeneratedKey = selectedSource->Name,
                        .Storage = kTextureBakeStorageModes[
                            static_cast<std::size_t>(storageIndex)],
                        .EncodingColormap =
                            static_cast<ColormapType>(colormapIndex),
                        .NormalSpace = normalSpaceIndex == 1
                            ? GeometryPresentationNormalSpace::World
                            : GeometryPresentationNormalSpace::Object,
                        .Targets = consumers,
                        .BindGeneratedTexture = true,
                    });
            }
            if (!canBake)
            {
                ImGui::EndDisabled();
                if (!model.DisabledReason.empty())
                    ImGui::TextDisabled("%s", model.DisabledReason.c_str());
            }

            ImGui::SeparatorText("Baked textures");
            if (model.BakedTextures.empty())
            {
                ImGui::TextDisabled(
                    "No baked property textures on this entity.");
            }
            static std::string renameTarget{};
            static std::array<char, 128> renameBuffer{};
            static std::string mutationDiagnostic{};
            for (const PropertyTextureBakeRecord& record :
                 model.BakedTextures)
            {
                ImGui::PushID(record.OutputName.c_str());
                if (ImGui::CollapsingHeader(
                        record.OutputName.c_str(),
                        ImGuiTreeNodeFlags_DefaultOpen))
                {
                    const char* stateName = "pending";
                    if (record.State == PropertyTextureBakeOutputState::Ready)
                        stateName = "ready";
                    else if (record.State == PropertyTextureBakeOutputState::Failed)
                        stateName = "failed";
                    const char* storageName =
                        record.Storage ==
                                PropertyTextureBakeStorage::RawFloat
                            ? "raw float"
                            : record.Storage ==
                                      PropertyTextureBakeStorage::EncodedRgba
                                  ? "encoded RGBA"
                                  : "auto";
                    ImGui::Text(
                        "%s | %s | %ux%u",
                        stateName,
                        storageName,
                        record.Width,
                        record.Height);
                    ImGui::Text(
                        "Source: %s  range=[%.6g, %.6g]",
                        record.Source.Name.c_str(),
                        record.RangeMin,
                        record.RangeMax);
                    if (record.Encoding ==
                        PropertyTextureBakeEncoding::ScalarColormap)
                    {
                        const std::size_t mapIndex = std::min<std::size_t>(
                            static_cast<std::size_t>(record.EncodingColormap),
                            kColormapNames.size() - 1u);
                        ImGui::Text("Baked colormap: %s",
                                    kColormapNames[mapIndex]);
                    }
                    const std::span<const EditorTextureBakeTarget>
                        existingConsumers =
                            TextureBakeTargetsFor(
                                model,
                                record.OutputName);
                    if (record.Encoding ==
                        PropertyTextureBakeEncoding::Normal)
                    {
                        const auto normalConsumer = std::ranges::find(
                            existingConsumers,
                            GeometryPresentationSlotSemantic::Normal,
                            &EditorTextureBakeTarget::Semantic);
                        ImGui::Text(
                            "Normal space: %s",
                            normalConsumer != existingConsumers.end() &&
                                    normalConsumer->NormalSpace ==
                                        GeometryPresentationNormalSpace::World
                                ? "world"
                                : "object");
                    }
                    if (!record.Diagnostic.empty())
                        ImGui::TextDisabled("%s", record.Diagnostic.c_str());

                    if (ImGui::SmallButton("Rename"))
                    {
                        renameTarget = record.OutputName;
                        renameBuffer.fill('\0');
                        const std::size_t count = std::min(
                            renameTarget.size(), renameBuffer.size() - 1u);
                        std::copy_n(
                            renameTarget.data(), count, renameBuffer.data());
                        ImGui::OpenPopup("Rename baked texture");
                    }
                    if (context != nullptr &&
                        renameTarget == record.OutputName &&
                        ImGui::BeginPopup("Rename baked texture"))
                    {
                        ImGui::InputText(
                            "Name", renameBuffer.data(), renameBuffer.size());
                        if (ImGui::Button("Apply"))
                        {
                            const TextureBakeMutationResult result =
                                RenameEditorBakedTexture(
                                    context->VisualizationCommands,
                                    model.SelectedStableId,
                                    record.OutputName,
                                    std::string_view{renameBuffer.data()});
                            mutationDiagnostic = result.Diagnostic;
                            if (result.Succeeded())
                                ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndPopup();
                    }
                    ImGui::SameLine();
                    if (context != nullptr && ImGui::SmallButton("Remove"))
                    {
                        const TextureBakeMutationResult result =
                            RemoveEditorBakedTexture(
                                context->VisualizationCommands,
                                model.SelectedStableId,
                                record.OutputName);
                        mutationDiagnostic = result.Diagnostic;
                    }

                    std::vector<EditorTextureBakeTarget> nextConsumers{
                        existingConsumers.begin(),
                        existingConsumers.end()};
                    bool consumersChanged = false;
                    for (std::size_t i = 0u;
                         i < kTextureBakeTargetSemantics.size();
                         ++i)
                    {
                        const GeometryPresentationSlotSemantic semantic =
                            kTextureBakeTargetSemantics[i];
                        const auto found = std::find_if(
                            nextConsumers.begin(),
                            nextConsumers.end(),
                            [semantic](
                                const EditorTextureBakeTarget& consumer)
                            {
                                return consumer.Semantic == semantic;
                            });
                        bool enabled = found != nextConsumers.end();
                        const bool compatible =
                            IsEditorTextureBakeTargetCompatible(
                                EditorTextureBakeTarget{
                                    .PresentationKey = "mesh.surface",
                                    .Semantic = semantic,
                                },
                                record.Source.ValueKind,
                                record.Storage,
                                record.Encoding);
                        const std::string label{
                            "Consume as " + std::string(ToString(semantic))};
                        const bool disableConsumer = !compatible && !enabled;
                        if (disableConsumer)
                            ImGui::BeginDisabled();
                        if (ImGui::Checkbox(label.c_str(), &enabled))
                        {
                            consumersChanged = true;
                            if (enabled)
                            {
                                nextConsumers.push_back(
                                    EditorTextureBakeTarget{
                                        .PresentationKey = "mesh.surface",
                                        .Semantic = semantic,
                                        .Colormap = static_cast<ColormapType>(
                                            colormapIndex),
                                    });
                            }
                            else
                            {
                                std::erase_if(
                                    nextConsumers,
                                    [semantic](
                                        const EditorTextureBakeTarget&
                                            consumer)
                                    {
                                        return consumer.Semantic == semantic;
                                });
                            }
                        }
                        if (disableConsumer)
                            ImGui::EndDisabled();
                    }

                    const bool rawScalar =
                        record.Storage ==
                            PropertyTextureBakeStorage::RawFloat &&
                        (record.Source.ValueKind ==
                             Geometry::PropertyValueKind::Float ||
                         record.Source.ValueKind ==
                             Geometry::PropertyValueKind::Double);
                    if (rawScalar)
                    {
                        int recordColormap = 0;
                        for (const EditorTextureBakeTarget& consumer :
                             nextConsumers)
                        {
                            if (consumer.Semantic ==
                                    GeometryPresentationSlotSemantic::Albedo ||
                                consumer.Semantic ==
                                    GeometryPresentationSlotSemantic::ScalarField)
                            {
                                recordColormap =
                                    static_cast<int>(consumer.Colormap);
                                break;
                            }
                        }
                        recordColormap = std::clamp(
                            recordColormap,
                            0,
                            static_cast<int>(kColormapNames.size() - 1u));
                        if (ImGui::Combo(
                                "Render colormap",
                                &recordColormap,
                                kColormapNames.data(),
                                static_cast<int>(kColormapNames.size())))
                        {
                            consumersChanged = true;
                            for (EditorTextureBakeTarget& consumer :
                                 nextConsumers)
                            {
                                if (consumer.Semantic ==
                                        GeometryPresentationSlotSemantic::Albedo ||
                                    consumer.Semantic ==
                                        GeometryPresentationSlotSemantic::ScalarField)
                                {
                                    consumer.Colormap =
                                        static_cast<ColormapType>(
                                            recordColormap);
                                }
                            }
                        }
                    }
                    if (context != nullptr && consumersChanged)
                    {
                        const TextureBakeMutationResult result =
                            SetEditorBakedTextureTargets(
                                context->VisualizationCommands,
                                EditorTextureBakeTargetUpdateRequest{
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
                                  ImGuiTableFlags_Borders |
                                      ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Property");
                ImGui::TableSetupColumn("Domain");
                ImGui::TableSetupColumn("Kind");
                ImGui::TableSetupColumn("Bake");
                ImGui::TableSetupColumn("Reason");
                ImGui::TableHeadersRow();

                const std::size_t limit =
                    std::min<std::size_t>(model.Sources.size(), 12u);
                for (std::size_t i = 0u; i < limit; ++i)
                {
                    const EditorTextureBakeSourceRow& row =
                        model.Sources[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(row.Name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(
                        DebugNameForEditorPropertyCatalogDomain(
                            row.CatalogDomain));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(
                        DebugNameForGeometryPropertyValueKind(
                            row.ValueKind));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(row.Bakeable ? "yes" : "no");
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextDisabled("%s",
                                        row.DisabledReason.empty()
                                            ? "-"
                                            : row.DisabledReason.c_str());
                }
                ImGui::EndTable();
            }
            DrawDiagnostics(model.Diagnostics);
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

            // UI-032 — preset re-clicks keep the target's tuned range/binning
            // instead of resetting to defaults.
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

        void DrawUniformVisualizationColorEdit(
            const EditorVisualizationConfigModel& visualization,
            const SandboxEditorContext& context,
            const std::uint32_t selectedStableId,
            const EditorVisualizationTarget target,
            const bool canEditVisualization)
        {
            if (!visualization.HasConfig ||
                visualization.Source != kUniformColorSource)
            {
                return;
            }

            glm::vec4 color = visualization.Color;
            if (ImGui::ColorEdit4("Color##uniform-visualization-color",
                                  &color.x) &&
                canEditVisualization)
            {
                (void)ApplyEditorVisualizationConfigCommand(
                    context.VisualizationCommands,
                    MakeUniformVisualizationConfigCommandFromModel(
                        selectedStableId,
                        visualization,
                        target,
                        color));
            }
        }

        // UI-032 — scalar-field styling controls: colormap selection, range
        // clamping, binning, isoline styling, and explicit highlight
        // isovalues. Every edit reissues the full config command built from
        // the current model so unrelated fields never reset.
        void DrawScalarVisualizationControls(
            const EditorVisualizationConfigModel& visualization,
            const SandboxEditorContext& context,
            const std::uint32_t selectedStableId,
            const EditorVisualizationTarget target,
            const bool canEditVisualization)
        {
            if (!visualization.HasConfig ||
                visualization.Source !=
                    kScalarFieldSource)
            {
                return;
            }

            ImGui::SeparatorText("Scalar field");
            ImGui::Text("Property: %s",
                        visualization.ScalarFieldName.empty()
                            ? "<none>"
                            : visualization.ScalarFieldName.c_str());

            const auto submit =
                [&](const EditorVisualizationConfigModel& next)
            {
                if (canEditVisualization)
                {
                    (void)ApplyEditorVisualizationConfigCommand(
                        context.VisualizationCommands,
                        MakeScalarVisualizationConfigCommandFromModel(
                            selectedStableId,
                            next,
                            target));
                }
            };

            static constexpr std::array<const char*, 6> kColormapNames{
                "Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"};
            int colormapIndex = static_cast<int>(visualization.ScalarColormap);
            if (colormapIndex < 0 ||
                colormapIndex >= static_cast<int>(kColormapNames.size()))
            {
                colormapIndex = 0;
            }
            if (ImGui::Combo("Colormap",
                             &colormapIndex,
                             kColormapNames.data(),
                             static_cast<int>(kColormapNames.size())))
            {
                EditorVisualizationConfigModel next = visualization;
                next.ScalarColormap =
                    static_cast<ColormapType>(colormapIndex);
                submit(next);
            }

            bool autoRange = visualization.ScalarAutoRange;
            if (ImGui::Checkbox("Auto range", &autoRange))
            {
                EditorVisualizationConfigModel next = visualization;
                next.ScalarAutoRange = autoRange;
                submit(next);
            }
            if (!visualization.ScalarAutoRange)
            {
                float rangeMinMax[2]{visualization.ScalarRangeMin,
                                     visualization.ScalarRangeMax};
                if (ImGui::DragFloat2("Clamp min/max",
                                      rangeMinMax,
                                      0.01f,
                                      0.0f,
                                      0.0f,
                                      "%.5f") &&
                    rangeMinMax[0] < rangeMinMax[1])
                {
                    EditorVisualizationConfigModel next = visualization;
                    next.ScalarRangeMin = rangeMinMax[0];
                    next.ScalarRangeMax = rangeMinMax[1];
                    submit(next);
                }
            }

            int binCount = static_cast<int>(visualization.ScalarBinCount);
            if (ImGui::DragInt("Bins (0 = continuous)", &binCount, 0.25f, 0, 64) &&
                binCount >= 0)
            {
                EditorVisualizationConfigModel next = visualization;
                next.ScalarBinCount = static_cast<std::uint32_t>(binCount);
                submit(next);
            }

            ImGui::SeparatorText("Isolines");
            int isolineCount = static_cast<int>(visualization.IsolineCount);
            if (ImGui::DragInt("Count##isolines", &isolineCount, 0.25f, 0, 256) &&
                isolineCount >= 0)
            {
                EditorVisualizationConfigModel next = visualization;
                next.IsolineCount = static_cast<std::uint32_t>(isolineCount);
                submit(next);
            }
            float isolineWidth = visualization.IsolineWidth;
            if (ImGui::DragFloat("Width##isolines", &isolineWidth, 0.05f, 0.1f, 16.0f) &&
                isolineWidth > 0.0f)
            {
                EditorVisualizationConfigModel next = visualization;
                next.IsolineWidth = isolineWidth;
                submit(next);
            }
            glm::vec4 isolineColor = visualization.IsolineColor;
            if (ImGui::ColorEdit4("Color##isolines", &isolineColor.x))
            {
                EditorVisualizationConfigModel next = visualization;
                next.IsolineColor = isolineColor;
                submit(next);
            }

            ImGui::TextUnformatted("Highlight isovalues");
            for (std::uint32_t i = 0u; i < visualization.IsolineValueCount; ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                float value = visualization.IsolineValues[i];
                if (ImGui::DragFloat("##isovalue", &value, 0.001f, 0.0f, 0.0f, "%.5f"))
                {
                    EditorVisualizationConfigModel next = visualization;
                    next.IsolineValues[i] = value;
                    submit(next);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    EditorVisualizationConfigModel next = visualization;
                    for (std::uint32_t j = i; j + 1u < next.IsolineValueCount; ++j)
                    {
                        next.IsolineValues[j] = next.IsolineValues[j + 1u];
                    }
                    next.IsolineValueCount -= 1u;
                    submit(next);
                }
                ImGui::PopID();
            }
            if (visualization.IsolineValueCount <
                visualization.IsolineValues.size())
            {
                if (ImGui::SmallButton("Add isovalue"))
                {
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
                    DrawTextureBakeControls(inspector.TextureBake, context, textureBakeState);
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
                                    for (const EditorGeometryPresentationPropertyOptionModel&
                                             option : slot.PropertyOptions)
                                    {
                                        if (!option.Compatible)
                                            ImGui::BeginDisabled();
                                        const bool selected =
                                            option.Descriptor.Name ==
                                            slot.Property.Name;
                                        if (ImGui::Selectable(
                                                option.Descriptor.Name.c_str(),
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
                                                        option.Descriptor.Domain,
                                                    .ExpectedValueKind =
                                                        option.Descriptor.ValueKind,
                                                    .PropertyName =
                                                        option.Descriptor.Name,
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
                        for (const EditorJobModel& job :
                             presentation.Jobs)
                        {
                            ImGui::BulletText("%s %s %.0f%% deps=%zu %s",
                                              job.Name.c_str(),
                                              std::string(ToString(job.Status)).c_str(),
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
                            (void)ApplyEditorVisualizationConfigCommand(
                                context->VisualizationCommands,
                                EditorVisualizationConfigCommand{
                                    .StableEntityId =
                                        frame.Visualization.SelectedStableId,
                                    .Target =
                                        EditorVisualizationTarget::Entity,
                                    .EnableConfig = false,
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

    struct EditorShell::Impl
    {
        Runtime::EditorWorkspaceSession Session{};
        Runtime::EditorUiHost* Host{nullptr};
        Runtime::EditorUiFrameContributionHandle FrameContribution{};
        BuiltinWindowHandles BuiltinHandles{};
        std::vector<Runtime::EditorWindowHandle> RegisteredWindows{};
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
        std::int32_t TextureBakeWidth{64};
        std::int32_t TextureBakeHeight{64};
        std::int32_t UvAtlasResolution{1024};
        std::int32_t UvAtlasPadding{2};
        float UvAtlasTexelsPerUnit{0.0f};
        bool UvAtlasForceRegenerate{true};
        bool UvAtlasPreserveAuthored{false};
        SandboxEditorFrame LastFrame{};
        std::optional<SandboxEditorContext> ActiveContext{};
        const Runtime::EditorWorkspacePreparedFrame* ActivePreparedFrame{
            nullptr};

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
            if (ActivePreparedFrame == nullptr || !ActiveContext.has_value())
                return;

            TextureBakeUiState textureBakeState{
                .LastUvRegenerationResult =
                    &ActivePreparedFrame->LastUvRegenerationResult,
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
                .UvResolution = &UvAtlasResolution,
                .UvPadding = &UvAtlasPadding,
                .UvTexelsPerUnit = &UvAtlasTexelsPerUnit,
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
                &ActivePreparedFrame->LastAssetImportResult,
                &ActivePreparedFrame->LastSceneFileResult,
                &textureBakeState);
        }

        void DrawFrame()
        {
            if (!Session.IsAttached() || Host == nullptr)
                return;

            if (!Session.PrepareFrame(
                    BuildModelRequest(Host->Windows(), BuiltinHandles),
                    std::string{ImportPathBuffer.data()},
                    ImportPayloadKind,
                    std::string{ScenePathBuffer.data()}))
            {
                return;
            }

            (void)Session.VisitPreparedFrame(
                [this](Runtime::EditorWorkspacePreparedFrame frame)
                {
                    LastFrame = SandboxEditorFrame{frame.Frame};
                    ActiveContext.emplace(frame, LastFrame);
                    ActivePreparedFrame = &frame;
                    DrawMainMenuBar(&Host->Windows());
                    (void)Host->Windows().DrawOpenWindows();
                    const std::uint32_t domainWindowModelCacheHits =
                        LastFrame.ModelBuildStats.DomainWindowModelCacheHits;
                    LastFrame.ModelBuildStats = frame.Frame.ModelBuildStats;
                    LastFrame.ModelBuildStats.DomainWindowModelCacheHits =
                        domainWindowModelCacheHits;
                    ActivePreparedFrame = nullptr;
                    ActiveContext.reset();
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
            Session.Attach(worlds, services);
            if (!Session.IsAttached())
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
            ActivePreparedFrame = nullptr;
            ActiveContext.reset();
            LastFrame = {};
            if (Host != nullptr && FrameContribution.IsValid())
                (void)Host->UnregisterFrameContribution(FrameContribution);
            FrameContribution = {};
            UnregisterAllWindows();
            Host = nullptr;
            Session.Detach();
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
               m_Impl->Session.IsAttached();
    }

    const SandboxEditorFrame&
    EditorShell::GetLastFrame() const noexcept
    {
        return m_Impl->LastFrame;
    }
}
