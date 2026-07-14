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
import Extrinsic.Runtime.DerivedJobGraph;
import Extrinsic.Runtime.MeshAttributeTextureBake;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Extrinsic.Runtime.ProgressivePresentationExtraction;
import Extrinsic.Runtime.ProgressiveRenderData;
import Extrinsic.Runtime.RenderArtifactPublication;
import Extrinsic.Runtime.SandboxEditorFacades;

namespace Extrinsic::Sandbox::Editor
{
    namespace
    {
        using namespace Extrinsic::Runtime;

        using VisualizationColorSource =
            decltype(SandboxEditorVisualizationConfigModel{}.Source);
        using ColormapType =
            decltype(SandboxEditorVisualizationConfigModel{}.ScalarColormap);
        inline constexpr VisualizationColorSource kUniformColorSource =
            static_cast<VisualizationColorSource>(1);
        inline constexpr VisualizationColorSource kScalarFieldSource =
            static_cast<VisualizationColorSource>(2);

        inline constexpr std::array<SandboxEditorAssetPayloadKind, 6>
            kImportPayloadKinds{{
                SandboxEditorAssetPayloadKind::Unknown,
                SandboxEditorAssetPayloadKind::Mesh,
                SandboxEditorAssetPayloadKind::PointCloud,
                SandboxEditorAssetPayloadKind::Graph,
                SandboxEditorAssetPayloadKind::ModelScene,
                SandboxEditorAssetPayloadKind::Texture2D,
            }};

        inline constexpr std::array<ProgressiveSlotSemantic, 6>
            kTextureBakeTargetSemantics{{
                ProgressiveSlotSemantic::Albedo,
                ProgressiveSlotSemantic::Normal,
                ProgressiveSlotSemantic::Roughness,
                ProgressiveSlotSemantic::Metallic,
                ProgressiveSlotSemantic::ScalarField,
                ProgressiveSlotSemantic::Displacement,
            }};

        inline constexpr std::array<MeshAttributeTextureBakeEncoder, 8>
            kTextureBakeEncoders{{
                MeshAttributeTextureBakeEncoder::Auto,
                MeshAttributeTextureBakeEncoder::RgbaColor,
                MeshAttributeTextureBakeEncoder::Normal,
                MeshAttributeTextureBakeEncoder::ScalarColormap,
                MeshAttributeTextureBakeEncoder::LinearScalar,
                MeshAttributeTextureBakeEncoder::LabelPalette,
                MeshAttributeTextureBakeEncoder::Vector2,
                MeshAttributeTextureBakeEncoder::Vector3,
            }};

        [[nodiscard]] const char* DebugNameForTextureBakeEncoder(
            const MeshAttributeTextureBakeEncoder encoder) noexcept
        {
            switch (encoder)
            {
            case MeshAttributeTextureBakeEncoder::Auto: return "auto";
            case MeshAttributeTextureBakeEncoder::LinearScalar: return "linear scalar";
            case MeshAttributeTextureBakeEncoder::ScalarColormap: return "scalar colormap";
            case MeshAttributeTextureBakeEncoder::LabelPalette: return "label palette";
            case MeshAttributeTextureBakeEncoder::Vector2: return "vector2";
            case MeshAttributeTextureBakeEncoder::Vector3: return "vector3";
            case MeshAttributeTextureBakeEncoder::Normal: return "normal";
            case MeshAttributeTextureBakeEncoder::RgbaColor: return "rgba color";
            }
            return "unknown";
        }

        struct TextureBakeUiState
        {
            std::optional<SandboxEditorUvRegenerationCommandResult>*
                LastUvRegenerationResult{nullptr};
            std::int32_t* SourceIndex{nullptr};
            std::int32_t* TargetSemanticIndex{nullptr};
            std::int32_t* EncoderIndex{nullptr};
            std::int32_t* Width{nullptr};
            std::int32_t* Height{nullptr};
            std::int32_t* UvResolution{nullptr};
            std::int32_t* UvPadding{nullptr};
            float* UvTexelsPerUnit{nullptr};
            bool* UvForceRegenerate{nullptr};
            bool* UvPreserveAuthored{nullptr};
        };

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

        [[nodiscard]] SandboxEditorModelBuildRequest BuildModelRequest(
            const EditorWindowRegistry& registry,
            const BuiltinWindowHandles& builtinHandles)
        {
            SandboxEditorModelBuildRequest request{};
            request.Hierarchy = registry.IsOpen(builtinHandles[1u]);
            request.Inspector = registry.IsOpen(builtinHandles[2u]);
            request.Selection = registry.IsOpen(builtinHandles[3u]);
            request.Visualization = registry.IsOpen(builtinHandles[9u]);
            return request;
        }

        [[nodiscard]] SandboxEditorVisualizationConfigCommand
        MakeUniformVisualizationConfigCommandFromModel(
            const std::uint32_t stableEntityId,
            const SandboxEditorVisualizationConfigModel& model,
            const SandboxEditorVisualizationTarget target,
            const glm::vec4 color)
        {
            return SandboxEditorVisualizationConfigCommand{
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

        [[nodiscard]] SandboxEditorVisualizationConfigCommand
        MakeScalarVisualizationConfigCommandFromModel(
            const std::uint32_t stableEntityId,
            const SandboxEditorVisualizationConfigModel& model,
            const SandboxEditorVisualizationTarget target)
        {
            return SandboxEditorVisualizationConfigCommand{
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

        void DrawDiagnostics(const std::vector<SandboxEditorDiagnostic>& diagnostics)
        {
            for (const SandboxEditorDiagnostic& diagnostic : diagnostics)
            {
                ImGui::TextDisabled("%s: %s",
                                    DebugNameForSandboxEditorDiagnosticCode(diagnostic.Code),
                                    diagnostic.Message.c_str());
            }
        }

        [[nodiscard]] std::string ProgressOverlayText(
            const SandboxEditorAssetImportQueueRow& row)
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
            const SandboxEditorAssetImportQueueModel& model,
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

                for (const SandboxEditorAssetImportQueueRow& row : model.Rows)
                {
                    ImGui::PushID(static_cast<int>(row.Sequence));
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%llu",
                                static_cast<unsigned long long>(row.Sequence));

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(
                        DebugNameForSandboxEditorAssetPayloadKind(row.PayloadKind));

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
            const SandboxEditorDomainWindowKind kind,
            EditorWindowRegistry* windowRegistry,
            const std::vector<EditorWindowMenuEntry>* registeredEntries)
        {
            if (!ImGui::BeginMenu(DebugNameForSandboxEditorDomainWindowKind(kind)))
                return;

            if (windowRegistry != nullptr && registeredEntries != nullptr)
            {
                std::vector<std::string> registeredPath{
                    DebugNameForSandboxEditorDomainWindowKind(kind)};
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
                SandboxEditorDomainWindowKind::PointCloud,
                windowRegistry,
                registeredEntriesPtr);
            DrawDomainMenu(
                SandboxEditorDomainWindowKind::Graph,
                windowRegistry,
                registeredEntriesPtr);
            DrawDomainMenu(
                SandboxEditorDomainWindowKind::Mesh,
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
            const SandboxEditorBoundRenderStateModel& bound)
        {
            ImGui::SeparatorText("Bound render state");
            ImGui::Text("Rows: %zu generation=%llu",
                        bound.Rows.size(),
                        static_cast<unsigned long long>(
                            bound.BindingGeneration));
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

                for (const SandboxEditorBoundRenderStateRow& row :
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
                        DebugNameForSandboxEditorBoundRenderStateRowKind(row.Kind));
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(laneText.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(row.Label.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(sourceText.c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(readinessText.c_str());
                    ImGui::TableSetColumnIndex(5);
                    if (!row.Property.PropertyName.empty())
                    {
                        ImGui::Text("%s%s",
                                    row.Property.PropertyName.c_str(),
                                    row.HasCatalogMatch ? " catalog" : "");
                    }
                    else
                    {
                        ImGui::TextDisabled("-");
                    }
                    ImGui::TableSetColumnIndex(6);
                    if (row.Kind == SandboxEditorBoundRenderStateRowKind::DerivedJob)
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
            const SandboxEditorUvDiagnosticsModel& uv,
            const std::optional<SandboxEditorUvRegenerationCommandResult>&
                lastResult)
        {
            if (uv.UvRegenerationJob.has_value())
            {
                const SandboxEditorProgressiveJobModel& job =
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

            const SandboxEditorUvRegenerationCommandResult& result =
                *lastResult;
            ImGui::Text("Last UV regeneration: %s",
                        DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text("Atlas: %s / %s  %ux%u  charts=%u  splits=%zu",
                        DebugNameForSandboxEditorUvAtlasStatus(result.UvStatus),
                        DebugNameForSandboxEditorUvAtlasProvenance(result.Provenance),
                        result.AtlasWidth,
                        result.AtlasHeight,
                        result.ChartCount,
                        result.SeamSplitVertexCount);
            if (!result.Diagnostic.empty())
                ImGui::TextWrapped("%s", result.Diagnostic.c_str());
        }

        void DrawTextureBakeControls(
            const SandboxEditorTextureBakeControlsModel& model,
            const SandboxEditorContext* context,
            TextureBakeUiState* state)
        {
            std::optional<SandboxEditorUvRegenerationCommandResult>
                fallbackUvRegenerationResult{};
            std::int32_t fallbackSourceIndex{0};
            std::int32_t fallbackSemanticIndex{0};
            std::int32_t fallbackEncoderIndex{0};
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
                    ApplySandboxEditorUvRegenerationCommand(
                    *context,
                    SandboxEditorUvRegenerationCommand{
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

            const SandboxEditorTextureBakeSourceRow* selectedSource =
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
                    const SandboxEditorTextureBakeSourceRow& row =
                        model.Sources[bakeableIndices[i]];
                    const bool selected = sourceIndex == static_cast<std::int32_t>(i);
                    if (ImGui::Selectable(row.Name.c_str(), selected))
                        sourceIndex = static_cast<std::int32_t>(i);
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

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
                    if (ImGui::Selectable(label.c_str(), selected))
                        semanticIndex = static_cast<std::int32_t>(i);
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
            ImGui::InputInt("Bake width", &bakeWidth);
            ImGui::InputInt("Bake height", &bakeHeight);
            bakeWidth = std::clamp<std::int32_t>(bakeWidth, 1, 8192);
            bakeHeight = std::clamp<std::int32_t>(bakeHeight, 1, 8192);

            const bool canBake =
                model.CanBake &&
                context != nullptr &&
                selectedSource != nullptr;
            if (!canBake)
                ImGui::BeginDisabled();
            if (ImGui::Button("Bake") && canBake)
            {
                (void)ApplySandboxEditorTextureBakeCommand(
                    *context,
                    SandboxEditorTextureBakeCommand{
                        .StableEntityId = model.SelectedStableId,
                        .TargetSemantic =
                            kTextureBakeTargetSemantics[
                                static_cast<std::size_t>(semanticIndex)],
                        .SourceDomain = selectedSource->BakeDomain,
                        .ExpectedValueKind = selectedSource->ExpectedValueKind,
                        .PropertyName = selectedSource->Name,
                        .Encoder =
                            kTextureBakeEncoders[
                                static_cast<std::size_t>(encoderIndex)],
                        .Width = static_cast<std::uint32_t>(bakeWidth),
                        .Height = static_cast<std::uint32_t>(bakeHeight),
                        .GeneratedKey = selectedSource->Name,
                        .BindGeneratedTexture = true,
                    });
            }
            if (!canBake)
            {
                ImGui::EndDisabled();
                if (!model.DisabledReason.empty())
                    ImGui::TextDisabled("%s", model.DisabledReason.c_str());
            }

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
                    const SandboxEditorTextureBakeSourceRow& row =
                        model.Sources[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(row.Name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(
                        DebugNameForSandboxEditorPropertyCatalogDomain(
                            row.CatalogDomain));
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(
                        DebugNameForSandboxEditorPropertyCatalogValueKind(
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
            const std::vector<SandboxEditorVisualizationPropertyInfo>& properties,
            const SandboxEditorVisualizationConfigModel& visualization,
            const SandboxEditorContext& context,
            const std::uint32_t selectedStableId,
            const SandboxEditorVisualizationTarget target,
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
                const SandboxEditorVisualizationPropertyInfo& property =
                    properties[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("%s  [%s, %s, %llu]",
                            property.Name.c_str(),
                            DebugNameForSandboxEditorVisualizationPropertyDomain(
                                property.Domain),
                            DebugNameForSandboxEditorVisualizationPropertyValueKind(
                                property.ValueKind),
                            static_cast<unsigned long long>(
                                property.ElementCount));

                bool wroteButton = false;
                if (property.ScalarPresetAvailable)
                {
                    if (ImGui::SmallButton("Scalar") && canEditVisualization)
                    {
                        (void)ApplySandboxEditorVisualizationPropertyCommand(
                            context,
                            SandboxEditorVisualizationPropertyCommand{
                                .StableEntityId = selectedStableId,
                                .Target = target,
                                .Domain = property.Domain,
                                .Preset =
                                    SandboxEditorVisualizationPropertyPreset::Scalar,
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
                        (void)ApplySandboxEditorVisualizationPropertyCommand(
                            context,
                            SandboxEditorVisualizationPropertyCommand{
                                .StableEntityId = selectedStableId,
                                .Target = target,
                                .Domain = property.Domain,
                                .Preset =
                                    SandboxEditorVisualizationPropertyPreset::Isoline,
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
                        (void)ApplySandboxEditorVisualizationPropertyCommand(
                            context,
                            SandboxEditorVisualizationPropertyCommand{
                                .StableEntityId = selectedStableId,
                                .Target = target,
                                .Domain = property.Domain,
                                .Preset =
                                    SandboxEditorVisualizationPropertyPreset::ColorBuffer,
                                .PropertyName = property.Name,
                            });
                    }
                    wroteButton = true;
                }
                if (property.VectorFieldCandidate && !wroteButton)
                {
                    ImGui::TextDisabled("Vector-field candidate; adapter residency is not owned by this UI slice.");
                }
                ImGui::PopID();
            }

            if (!canEditVisualization)
                ImGui::EndDisabled();
        }

        void DrawUniformVisualizationColorEdit(
            const SandboxEditorVisualizationConfigModel& visualization,
            const SandboxEditorContext& context,
            const std::uint32_t selectedStableId,
            const SandboxEditorVisualizationTarget target,
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
                (void)ApplySandboxEditorVisualizationConfigCommand(
                    context,
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
            const SandboxEditorVisualizationConfigModel& visualization,
            const SandboxEditorContext& context,
            const std::uint32_t selectedStableId,
            const SandboxEditorVisualizationTarget target,
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
                [&](const SandboxEditorVisualizationConfigModel& next)
            {
                if (canEditVisualization)
                {
                    (void)ApplySandboxEditorVisualizationConfigCommand(
                        context,
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
                SandboxEditorVisualizationConfigModel next = visualization;
                next.ScalarColormap =
                    static_cast<ColormapType>(colormapIndex);
                submit(next);
            }

            bool autoRange = visualization.ScalarAutoRange;
            if (ImGui::Checkbox("Auto range", &autoRange))
            {
                SandboxEditorVisualizationConfigModel next = visualization;
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
                    SandboxEditorVisualizationConfigModel next = visualization;
                    next.ScalarRangeMin = rangeMinMax[0];
                    next.ScalarRangeMax = rangeMinMax[1];
                    submit(next);
                }
            }

            int binCount = static_cast<int>(visualization.ScalarBinCount);
            if (ImGui::DragInt("Bins (0 = continuous)", &binCount, 0.25f, 0, 64) &&
                binCount >= 0)
            {
                SandboxEditorVisualizationConfigModel next = visualization;
                next.ScalarBinCount = static_cast<std::uint32_t>(binCount);
                submit(next);
            }

            ImGui::SeparatorText("Isolines");
            int isolineCount = static_cast<int>(visualization.IsolineCount);
            if (ImGui::DragInt("Count##isolines", &isolineCount, 0.25f, 0, 256) &&
                isolineCount >= 0)
            {
                SandboxEditorVisualizationConfigModel next = visualization;
                next.IsolineCount = static_cast<std::uint32_t>(isolineCount);
                submit(next);
            }
            float isolineWidth = visualization.IsolineWidth;
            if (ImGui::DragFloat("Width##isolines", &isolineWidth, 0.05f, 0.1f, 16.0f) &&
                isolineWidth > 0.0f)
            {
                SandboxEditorVisualizationConfigModel next = visualization;
                next.IsolineWidth = isolineWidth;
                submit(next);
            }
            glm::vec4 isolineColor = visualization.IsolineColor;
            if (ImGui::ColorEdit4("Color##isolines", &isolineColor.x))
            {
                SandboxEditorVisualizationConfigModel next = visualization;
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
                    SandboxEditorVisualizationConfigModel next = visualization;
                    next.IsolineValues[i] = value;
                    submit(next);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove"))
                {
                    SandboxEditorVisualizationConfigModel next = visualization;
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
                    SandboxEditorVisualizationConfigModel next = visualization;
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
            const SandboxEditorRenderRecipeEditorModel& model,
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
                        DebugNameForSandboxEditorRenderRecipeDraftState(
                            model.DraftState),
                        static_cast<unsigned long long>(model.DraftRevision),
                        static_cast<unsigned long long>(model.ActiveRevision));
            ImGui::Text("Validation: %s parsed slots=%u bindings=%u",
                        std::string(DebugNameForSandboxEditorRenderRecipeConfigState(model.ValidationState)).c_str(),
                        model.ParsedSlotCount,
                        model.ParsedBindingOverrideCount);

            const bool commandsAvailable =
                context != nullptr &&
                context->RenderRecipeContext != nullptr &&
                context->RenderRecipeEditorState != nullptr &&
                context->RenderRecipeCommandsAvailable;

            if (draftBuffer != nullptr)
            {
                SandboxEditorRenderRecipeEditorState* state =
                    context != nullptr ? context->RenderRecipeEditorState : nullptr;
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
                        SandboxEditorRenderRecipeDraftState::Canceled)
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
                (void)ApplySandboxEditorRenderRecipeCommand(
                    *context,
                    SandboxEditorRenderRecipeCommand{
                        .Kind = SandboxEditorRenderRecipeCommandKind::UpdateDraft,
                        .Document = draftText(),
                        .SourceId = "sandbox-editor",
                    });
            }
            ImGui::SameLine();
            if (ImGui::Button("Debounce"))
            {
                (void)ApplySandboxEditorRenderRecipeCommand(
                    *context,
                    SandboxEditorRenderRecipeCommand{
                        .Kind = SandboxEditorRenderRecipeCommandKind::UpdateDraft,
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
                (void)ApplySandboxEditorRenderRecipeCommand(
                    *context,
                    SandboxEditorRenderRecipeCommand{
                        .Kind = SandboxEditorRenderRecipeCommandKind::ValidateDraft,
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
                (void)ApplySandboxEditorRenderRecipeCommand(
                    *context,
                    SandboxEditorRenderRecipeCommand{
                        .Kind = SandboxEditorRenderRecipeCommandKind::PreviewDraft,
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
                (void)ApplySandboxEditorRenderRecipeCommand(
                    *context,
                    SandboxEditorRenderRecipeCommand{
                        .Kind = SandboxEditorRenderRecipeCommandKind::ActivatePreview,
                    });
            }
            if (!model.CanActivate)
                ImGui::EndDisabled();

            ImGui::SameLine();
            if (!model.CanCancel)
                ImGui::BeginDisabled();
            if (ImGui::Button("Cancel"))
            {
                (void)ApplySandboxEditorRenderRecipeCommand(
                    *context,
                    SandboxEditorRenderRecipeCommand{
                        .Kind = SandboxEditorRenderRecipeCommandKind::CancelDraft,
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
                const auto slotKindName = [](const SandboxEditorRecipeSlotKind kind)
                {
                    switch (kind)
                    {
                    case SandboxEditorRecipeSlotKind::FixedCore:
                        return "FixedCore";
                    case SandboxEditorRecipeSlotKind::Extension:
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
                    for (const SandboxEditorRenderRecipeSlotModel& slot :
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
                    for (const SandboxEditorRenderRecipeBindingOverrideModel& binding :
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
                    for (const SandboxEditorRenderRecipeOutputModel& output :
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
                    for (const SandboxEditorRenderArtifactRow& artifact :
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
                            context->RenderArtifacts != nullptr &&
                            artifact.CanPublish;
                        if (!publishAvailable)
                            ImGui::BeginDisabled();
                        if (ImGui::Button("Publish"))
                        {
                            (void)ApplySandboxEditorRenderRecipeCommand(
                                *context,
                                SandboxEditorRenderRecipeCommand{
                                    .Kind = SandboxEditorRenderRecipeCommandKind::PublishArtifact,
                                    .ArtifactId = artifact.ArtifactId,
                                    .Provenance = "sandbox-editor",
                                });
                        }
                        if (!publishAvailable)
                            ImGui::EndDisabled();
                        ImGui::TableSetColumnIndex(6);
                        const bool applyAvailable =
                            commandsAvailable &&
                            context->RenderArtifacts != nullptr &&
                            artifact.CanApply;
                        if (!applyAvailable)
                            ImGui::BeginDisabled();
                        if (ImGui::Button("Apply"))
                        {
                            (void)ApplySandboxEditorRenderRecipeCommand(
                                *context,
                                SandboxEditorRenderRecipeCommand{
                                    .Kind = SandboxEditorRenderRecipeCommandKind::ApplyArtifact,
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
                                    std::string(DebugNameForSandboxEditorRenderRecipeConfigState(
                                        diagnostic.State)).c_str(),
                                    std::string(DebugNameForSandboxEditorRenderRecipeConfigDiagnosticCode(
                                        diagnostic.Code)).c_str(),
                                    diagnostic.Message.c_str());
            }
        }

        void DrawFixedWindow(
            const std::string_view windowId,
            bool& open,
            const SandboxEditorPanelFrame& frame,
            const SandboxEditorContext* context,
            std::array<char, 1024>* importPathBuffer,
            std::array<char, 1024>* scenePathBuffer,
            std::array<char, 8192>* renderRecipeDraftBuffer,
            SandboxEditorAssetPayloadKind* importPayloadKind,
            std::optional<SandboxEditorFileImportResult>* lastImportResult,
            std::optional<SandboxEditorSceneFileResult>* lastSceneFileResult,
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
                for (const SandboxEditorEntityRow& row : frame.Hierarchy)
                {
                    ImGui::PushID(static_cast<int>(row.StableEntityId));
                    const bool clicked =
                        ImGui::Selectable(row.Name.c_str(), row.Selected);
                    ImGui::PopID();
                    if (clicked && context != nullptr)
                        (void)SelectSandboxEditorEntity(*context, row.StableEntityId);
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
                    const SandboxEditorInspectorModel& inspector = frame.Inspector;
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
                                (void)ApplySandboxEditorTransformEdit(
                                    *context,
                                    SandboxEditorTransformEditCommand{
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
                                (void)ApplySandboxEditorTransformEdit(
                                    *context,
                                    SandboxEditorTransformEditCommand{
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
                                DebugNameForSandboxEditorGeometryDomain(
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
                        const SandboxEditorPropertyCatalogRow& row =
                            inspector.PropertyCatalog.Rows[propertyIndex];
                        ImGui::BulletText("%s / %s / %s",
                                          DebugNameForSandboxEditorPropertyCatalogDomain(
                                              row.Domain),
                                          row.Name.c_str(),
                                          DebugNameForSandboxEditorPropertyCatalogValueKind(
                                              row.ValueKind));
                    }
                    const SandboxEditorProgressiveRenderDataModel& progressive =
                        inspector.Progressive;
                    DrawBoundRenderStateRows(inspector.BoundState);
                    DrawTextureBakeControls(inspector.TextureBake, context, textureBakeState);
                    ImGui::SeparatorText("Progressive render data");
                    ImGui::Text("Shape: %s",
                                std::string(ToString(progressive.Shape)).c_str());
                    ImGui::Text("Bindings: %s generation=%llu",
                                progressive.HasBindings ? "yes" : "no",
                                static_cast<unsigned long long>(
                                    progressive.BindingGeneration));
                    if (progressive.Composition.HasChildren)
                    {
                        ImGui::Text("Composition: children=%u bindings=%u slots=%u pending=%u failed=%u jobs=%u active=%u job failures=%u",
                                    progressive.Composition.ChildCount,
                                    progressive.Composition.ChildBindingsCount,
                                    progressive.Composition.ChildSlotCount,
                                    progressive.Composition.ChildPendingSlotCount,
                                    progressive.Composition.ChildFailedSlotCount,
                                    progressive.Composition.ChildJobCount,
                                    progressive.Composition.ChildActiveJobCount,
                                    progressive.Composition.ChildFailedJobCount);
                    }
                    if (!progressive.Slots.empty())
                    {
                        ImGui::Text("Slots: %zu", progressive.Slots.size());
                        for (std::size_t slotIndex = 0u;
                             slotIndex < progressive.Slots.size();
                             ++slotIndex)
                        {
                            const SandboxEditorProgressiveSlotModel& slot =
                                progressive.Slots[slotIndex];
                            ImGui::PushID(static_cast<int>(slotIndex));
                            ImGui::Text("%s / %s / %s / %s",
                                        std::string(ToString(slot.Lane)).c_str(),
                                        slot.PresentationKey.c_str(),
                                        std::string(ToString(slot.Semantic)).c_str(),
                                        std::string(ToString(slot.Readiness)).c_str());
                            ImGui::Text("Source: %s property=%s",
                                        std::string(ToString(slot.SourceKind)).c_str(),
                                        slot.Property.PropertyName.empty()
                                            ? "(none)"
                                            : slot.Property.PropertyName.c_str());
                            if (!slot.Diagnostic.empty())
                                ImGui::TextWrapped("%s", slot.Diagnostic.c_str());

                            if (context != nullptr &&
                                (slot.Semantic == ProgressiveSlotSemantic::Albedo ||
                                 slot.Semantic == ProgressiveSlotSemantic::PointColor ||
                                 slot.Semantic == ProgressiveSlotSemantic::LineColor))
                            {
                                glm::vec4 color = slot.UniformDefault.Vector;
                                if (ImGui::ColorEdit4("Default color", &color.x))
                                {
                                    ProgressiveDefaultValue value =
                                        slot.UniformDefault;
                                    value.Kind = ProgressivePropertyValueKind::Vec4;
                                    value.Vector = color;
                                    (void)ApplySandboxEditorProgressiveSlotDefaultCommand(
                                        *context,
                                        SandboxEditorProgressiveSlotDefaultCommand{
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
                                    slot.Property.PropertyName.empty()
                                        ? "(uniform/default)"
                                        : slot.Property.PropertyName.c_str();
                                if (ImGui::BeginCombo("Source property",
                                                      currentProperty))
                                {
                                    for (const SandboxEditorProgressivePropertyOptionModel&
                                             option : slot.PropertyOptions)
                                    {
                                        if (!option.Compatible)
                                            ImGui::BeginDisabled();
                                        const bool selected =
                                            option.Descriptor.PropertyName ==
                                            slot.Property.PropertyName;
                                        if (ImGui::Selectable(
                                                option.Descriptor.PropertyName.c_str(),
                                                selected) &&
                                            option.Compatible)
                                        {
                                            (void)ApplySandboxEditorProgressiveSlotPropertyCommand(
                                                *context,
                                                SandboxEditorProgressiveSlotPropertyCommand{
                                                    .StableEntityId =
                                                        inspector.Entity.StableEntityId,
                                                    .PresentationKey =
                                                        slot.PresentationKey,
                                                    .Semantic = slot.Semantic,
                                                    .SourceKind =
                                                        IsSurfaceTextureSemantic(
                                                            slot.Semantic)
                                                            ? ProgressiveSlotSourceKind::PropertyBake
                                                            : ProgressiveSlotSourceKind::PropertyBuffer,
                                                    .Domain =
                                                        option.Descriptor.Domain,
                                                    .ExpectedValueKind =
                                                        option.Descriptor.ExpectedValueKind,
                                                    .PropertyName =
                                                        option.Descriptor.PropertyName,
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
                    if (!progressive.Jobs.empty())
                    {
                        ImGui::Text("Derived jobs: %zu", progressive.Jobs.size());
                        for (const SandboxEditorProgressiveJobModel& job :
                             progressive.Jobs)
                        {
                            ImGui::BulletText("%s %s %.0f%% deps=%zu %s",
                                              job.Name.c_str(),
                                              std::string(ToString(job.Status)).c_str(),
                                              job.NormalizedProgress * 100.0f,
                                              job.Dependencies.size(),
                                              job.Diagnostic.c_str());
                        }
                    }
                    DrawDiagnostics(progressive.Diagnostics);
                    DrawDiagnostics(inspector.Diagnostics);
                }
                ImGui::End();
            }

            if (windowId == "scene.selection" &&
                BeginFixedWindow("Selection Details", open, ImVec2(0.0f, 0.0f)))
            {
                ImGui::Text("Selected entities: %zu", frame.Selection.SelectedStableIds.size());
                for (const SandboxEditorEntityRow& row : frame.Selection.SelectedEntities)
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
                                DebugNameForSandboxEditorGeometryDomain(primitive.Domain),
                                DebugNameForSandboxEditorPrimitiveKind(primitive.Kind));
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
                    context != nullptr && context->CommandHistory != nullptr;
                if (!historyControlsAvailable || !frame.Document.CanUndo)
                    ImGui::BeginDisabled();
                if (ImGui::Button("Undo") && historyControlsAvailable)
                    (void)context->CommandHistory->Undo();
                if (!historyControlsAvailable || !frame.Document.CanUndo)
                    ImGui::EndDisabled();
                ImGui::SameLine();
                if (!historyControlsAvailable || !frame.Document.CanRedo)
                    ImGui::BeginDisabled();
                if (ImGui::Button("Redo") && historyControlsAvailable)
                    (void)context->CommandHistory->Redo();
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
                        ApplySandboxEditorNewSceneCommand(*context);
                }
                ImGui::SameLine();
                if (ImGui::Button("Close scene") &&
                    frame.SceneFile.LifecycleEnabled &&
                    context != nullptr &&
                    lastSceneFileResult != nullptr)
                {
                    *lastSceneFileResult =
                        ApplySandboxEditorCloseSceneCommand(*context);
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
                    *lastSceneFileResult = ApplySandboxEditorSceneSaveCommand(
                        *context,
                        SandboxEditorSceneFileCommand{
                            .Path = std::string(scenePathBuffer->data()),
                        });
                }
                ImGui::SameLine();
                if (ImGui::Button("Open path") && sceneControlsAvailable)
                {
                    *lastSceneFileResult = ApplySandboxEditorSceneLoadCommand(
                        *context,
                        SandboxEditorSceneFileCommand{
                            .Path = std::string(scenePathBuffer->data()),
                        });
                }
                if (!sceneControlsAvailable)
                    ImGui::EndDisabled();
                ImGui::TextWrapped("%s", frame.SceneFile.StatusText.c_str());
                const SandboxEditorSceneFileResult* result =
                    lastSceneFileResult != nullptr && lastSceneFileResult->has_value()
                        ? &**lastSceneFileResult
                        : frame.SceneFile.LastResult.has_value()
                            ? &*frame.SceneFile.LastResult
                            : nullptr;
                if (result != nullptr)
                {
                    ImGui::Text("Last scene command: %s",
                                DebugNameForSandboxEditorCommandStatus(
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
                const bool importControlsAvailable =
                    frame.FileImport.Enabled &&
                    context != nullptr &&
                    importPathBuffer != nullptr &&
                    importPayloadKind != nullptr &&
                    lastImportResult != nullptr;
                if (!importControlsAvailable)
                    ImGui::BeginDisabled();
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
                    if (ImGui::BeginCombo(
                            "Payload hint",
                            DebugNameForSandboxEditorAssetPayloadKind(*importPayloadKind)))
                    {
                        for (const SandboxEditorAssetPayloadKind kind : kImportPayloadKinds)
                        {
                            const bool selected = *importPayloadKind == kind;
                            if (ImGui::Selectable(
                                    DebugNameForSandboxEditorAssetPayloadKind(kind),
                                    selected))
                            {
                                *importPayloadKind = kind;
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                }
                else
                {
                    ImGui::TextDisabled("Payload hint is not bound.");
                }
                if (ImGui::Button("Import asset") && importControlsAvailable)
                {
                    *lastImportResult = ApplySandboxEditorFileImportCommand(
                        *context,
                        SandboxEditorFileImportCommand{
                            .Path = std::string(importPathBuffer->data()),
                            .PayloadKind = *importPayloadKind,
                        });
                }
                if (!importControlsAvailable)
                    ImGui::EndDisabled();
                ImGui::TextWrapped("%s", frame.FileImport.StatusText.c_str());
                const SandboxEditorFileImportResult* result =
                    lastImportResult != nullptr && lastImportResult->has_value()
                        ? &**lastImportResult
                        : frame.FileImport.LastResult.has_value()
                            ? &*frame.FileImport.LastResult
                            : nullptr;
                if (result != nullptr)
                {
                    ImGui::Text("Last import: %s",
                                DebugNameForSandboxEditorCommandStatus(
                                    result->Status));
                    ImGui::Text("Payload: %s",
                                DebugNameForSandboxEditorAssetPayloadKind(
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
                    if (result->GeneratedTextureAssetsCreated > 0u)
                    {
                        ImGui::Text("Generated textures: %llu",
                                    static_cast<unsigned long long>(
                                        result->GeneratedTextureAssetsCreated));
                    }
                    if (result->TextureUploadRequests > 0u)
                    {
                        ImGui::Text("Texture upload requests: %llu",
                                    static_cast<unsigned long long>(
                                        result->TextureUploadRequests));
                    }
                    if (result->GeneratedTextureUploadRequests > 0u)
                    {
                        ImGui::Text("Generated texture upload requests: %llu",
                                    static_cast<unsigned long long>(
                                        result->GeneratedTextureUploadRequests));
                    }
                }
                DrawAssetImportQueue(frame.AssetImportQueue, context);
                DrawDiagnostics(frame.FileImport.Diagnostics);
                ImGui::End();
            }

            if (windowId == "view.frame_graph" &&
                BeginFixedWindow("Frame Graph", open, ImVec2(0.0f, 0.0f)))
            {
                if (!frame.RenderGraph.Enabled)
                {
                    ImGui::TextDisabled("Renderer frame graph diagnostics are unavailable.");
                    DrawDiagnostics(frame.RenderGraph.Diagnostics);
                }
                else
                {
                    ImGui::TextWrapped("%s",
                                       frame.RenderGraph.StatusText.c_str());
                    ImGui::Text("Compile: %s (%llu us)",
                                frame.RenderGraph.CompileSucceeded ? "yes" : "no",
                                static_cast<unsigned long long>(
                                    frame.RenderGraph.CompileTimeMicros));
                    ImGui::Text("Execute: %s (%llu us), device=%s",
                                frame.RenderGraph.ExecuteSucceeded ? "yes" : "no",
                                static_cast<unsigned long long>(
                                    frame.RenderGraph.ExecuteTimeMicros),
                                frame.RenderGraph.DeviceOperational ? "operational" : "not operational");
                    ImGui::Text("Passes: %u live, %u culled",
                                frame.RenderGraph.PassCount,
                                frame.RenderGraph.CulledPassCount);
                    ImGui::Text("Resources: %u, barriers=%u, transient=%llu bytes",
                                frame.RenderGraph.ResourceCount,
                                frame.RenderGraph.BarrierCount,
                                static_cast<unsigned long long>(
                                    frame.RenderGraph.TransientMemoryEstimateBytes));
                    ImGui::Text("Queue handoffs: %u, timeline edges=%u signals=%u waits=%u ownership=%u",
                                frame.RenderGraph.QueueHandoffEdgeCount,
                                frame.RenderGraph.CrossQueueTimelineEdgeCount,
                                frame.RenderGraph.CrossQueueTimelineSignalCount,
                                frame.RenderGraph.CrossQueueTimelineWaitCount,
                                frame.RenderGraph.CrossQueueOwnershipTransferCount);
                    ImGui::Text("Command passes: recorded=%u skipped=%u nonOperational=%u unavailable=%u",
                                frame.RenderGraph.CommandPassesRecorded,
                                frame.RenderGraph.CommandPassesSkipped,
                                frame.RenderGraph.CommandPassesSkippedNonOperational,
                                frame.RenderGraph.CommandPassesSkippedUnavailable);
                    ImGui::Text("Async compute frames: %u",
                                frame.RenderGraph.AsyncComputeUtilizedFrames);
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
                        for (const SandboxEditorRenderGraphPassModel& pass :
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
                                DebugNameForSandboxEditorCameraControllerKind(
                                    frame.CameraRender.MainCameraControllerKind));
                }
                else
                {
                    ImGui::TextDisabled("Main camera: not registered");
                }

                if (context != nullptr &&
                    frame.CameraRender.CameraControlsAvailable)
                {
                    ImGui::TextDisabled("Viewport controls: RMB/MMB drag rotates; WASD pans/moves; Shift accelerates; scroll zooms.");
                    if (ImGui::Button("Orbit"))
                    {
                        (void)ApplySandboxEditorCameraControllerCommand(
                            *context,
                            SandboxEditorCameraControllerCommand{
                                .Kind = SandboxEditorCameraControllerKind::Orbit,
                            });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Fly"))
                    {
                        (void)ApplySandboxEditorCameraControllerCommand(
                            *context,
                            SandboxEditorCameraControllerCommand{
                                .Kind = SandboxEditorCameraControllerKind::Fly,
                            });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Free look"))
                    {
                        (void)ApplySandboxEditorCameraControllerCommand(
                            *context,
                            SandboxEditorCameraControllerCommand{
                                .Kind = SandboxEditorCameraControllerKind::FreeLook,
                            });
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Top down"))
                    {
                        (void)ApplySandboxEditorCameraControllerCommand(
                            *context,
                            SandboxEditorCameraControllerCommand{
                                .Kind = SandboxEditorCameraControllerKind::TopDown,
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
                                DebugNameForSandboxEditorGeometryDomain(
                                    frame.Visualization.SelectedDomain));

                    if (frame.Visualization.SpatialDebug.HasBinding)
                    {
                        ImGui::Text("Spatial debug: %s key=%llu",
                                    DebugNameForSandboxEditorSpatialDebugKind(
                                        frame.Visualization.SpatialDebug.Kind),
                                    static_cast<unsigned long long>(
                                        frame.Visualization.SpatialDebug.RegistryKey));
                    }
                    else
                    {
                        ImGui::TextDisabled("Spatial debug: disabled");
                    }

                    if (context != nullptr &&
                        frame.Visualization.GeometryDomainControlsAvailable)
                    {
                        if (ImGui::Button("Enable BVH debug"))
                        {
                            (void)ApplySandboxEditorSpatialDebugBindingCommand(
                                *context,
                                SandboxEditorSpatialDebugBindingCommand{
                                    .StableEntityId =
                                        frame.Visualization.SelectedStableId,
                                    .EnableBinding = true,
                                });
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Clear debug"))
                        {
                            (void)ApplySandboxEditorSpatialDebugBindingCommand(
                                *context,
                                SandboxEditorSpatialDebugBindingCommand{
                                    .StableEntityId =
                                        frame.Visualization.SelectedStableId,
                                    .EnableBinding = false,
                                });
                        }

                        if (frame.Visualization.Visualization.HasConfig)
                        {
                            ImGui::Text("Visualization: %s",
                                        DebugNameForSandboxEditorVisualizationColorSource(
                                            frame.Visualization.Visualization.Source));
                        }
                        else
                        {
                            ImGui::TextDisabled("Visualization: material/default");
                        }
                        if (frame.Visualization.AdapterBindingControlsAvailable)
                        {
                            if (frame.Visualization.AdapterBinding.HasBinding)
                            {
                                ImGui::Text(
                                    "Adapter: %s key=%llu buffer=%llu",
                                    DebugNameForSandboxEditorVisualizationAdapterBindingKind(
                                        frame.Visualization.AdapterBinding.Kind),
                                    static_cast<unsigned long long>(
                                        frame.Visualization.AdapterBinding.AdapterKey),
                                    static_cast<unsigned long long>(
                                        frame.Visualization.AdapterBinding.BufferBDA));
                            }
                            else
                            {
                                ImGui::TextDisabled("Adapter: no runtime binding");
                            }
                        }

                        if (ImGui::Button("Uniform color"))
                        {
                            (void)ApplySandboxEditorVisualizationConfigCommand(
                                *context,
                                MakeUniformVisualizationConfigCommandFromModel(
                                    frame.Visualization.SelectedStableId,
                                    frame.Visualization.Visualization,
                                    SandboxEditorVisualizationTarget::Entity,
                                    frame.Visualization.Visualization.Color));
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Clear vis"))
                        {
                            (void)ApplySandboxEditorVisualizationConfigCommand(
                                *context,
                                SandboxEditorVisualizationConfigCommand{
                                    .StableEntityId =
                                        frame.Visualization.SelectedStableId,
                                    .Target =
                                        SandboxEditorVisualizationTarget::Entity,
                                    .EnableConfig = false,
                                });
                        }
                        DrawUniformVisualizationColorEdit(
                            frame.Visualization.Visualization,
                            *context,
                            frame.Visualization.SelectedStableId,
                            SandboxEditorVisualizationTarget::Entity,
                            true);
                        DrawScalarVisualizationControls(
                            frame.Visualization.Visualization,
                            *context,
                            frame.Visualization.SelectedStableId,
                            SandboxEditorVisualizationTarget::Entity,
                            true);
                        DrawVisualizationPropertyPresets(
                            frame.Visualization.Properties,
                            frame.Visualization.Visualization,
                            *context,
                            frame.Visualization.SelectedStableId,
                            SandboxEditorVisualizationTarget::Entity,
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
        Runtime::SandboxEditorSession Session{};
        Runtime::EditorUiHost Host{};
        BuiltinWindowHandles BuiltinHandles{};
        std::array<char, 1024> ImportPathBuffer{};
        std::array<char, 1024> ScenePathBuffer{};
        Runtime::SandboxEditorAssetPayloadKind ImportPayloadKind{
            Runtime::SandboxEditorAssetPayloadKind::Unknown};
        std::array<char, 8192> RenderRecipeDraftBuffer{};
        std::int32_t TextureBakeSourceIndex{0};
        std::int32_t TextureBakeTargetSemanticIndex{0};
        std::int32_t TextureBakeEncoderIndex{0};
        std::int32_t TextureBakeWidth{64};
        std::int32_t TextureBakeHeight{64};
        std::int32_t UvAtlasResolution{1024};
        std::int32_t UvAtlasPadding{2};
        float UvAtlasTexelsPerUnit{0.0f};
        bool UvAtlasForceRegenerate{true};
        bool UvAtlasPreserveAuthored{false};
        const Runtime::SandboxEditorPreparedFrameView* ActivePreparedFrame{
            nullptr};

        Impl()
        {
            for (std::size_t index = 0u; index < kBuiltinWindows.size(); ++index)
            {
                const BuiltinWindowSpec& spec = kBuiltinWindows[index];
                BuiltinHandles[index] = Host.RegisterWindow(
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

        void DrawBuiltinWindow(
            const std::string_view id,
            bool& open)
        {
            if (ActivePreparedFrame == nullptr)
                return;

            TextureBakeUiState textureBakeState{
                .LastUvRegenerationResult =
                    &ActivePreparedFrame->LastUvRegenerationResult,
                .SourceIndex = &TextureBakeSourceIndex,
                .TargetSemanticIndex = &TextureBakeTargetSemanticIndex,
                .EncoderIndex = &TextureBakeEncoderIndex,
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
                ActivePreparedFrame->Frame,
                &ActivePreparedFrame->Context,
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
            if (!Session.IsAttached())
                return;

            if (!Session.PrepareFrame(
                    BuildModelRequest(Host.Windows(), BuiltinHandles),
                    std::string{ImportPathBuffer.data()},
                    ImportPayloadKind,
                    std::string{ScenePathBuffer.data()}))
            {
                return;
            }

            (void)Session.VisitPreparedFrame(
                [this](Runtime::SandboxEditorPreparedFrameView frame)
                {
                    ActivePreparedFrame = &frame;
                    DrawMainMenuBar(&Host.Windows());
                    (void)Host.Windows().DrawOpenWindows();
                    ActivePreparedFrame = nullptr;
                });
        }

        Runtime::EditorWindowHandle RegisterEditorWindow(
            EditorWindowDescriptor descriptor)
        {
            auto draw = std::move(descriptor.Draw);
            return Host.RegisterWindow(
                Runtime::EditorWindowDescriptor{
                    .Id = std::move(descriptor.Id),
                    .MenuPath = std::move(descriptor.MenuPath),
                    .Title = std::move(descriptor.Title),
                    .OpenByDefault = descriptor.OpenByDefault,
                    .Draw =
                        [this, draw = std::move(draw)](bool& open)
                        {
                            if (draw && ActivePreparedFrame != nullptr)
                            {
                                draw(open, ActivePreparedFrame->Context);
                            }
                        },
                    .OpenStateChanged =
                        std::move(descriptor.OpenStateChanged),
                });
        }

        void Attach(Runtime::Engine& engine)
        {
            Detach();
            Session.Attach(engine);
            Host.Attach(
                engine,
                Runtime::EditorUiHostDescriptor{
                    .ToggleActionDebugName =
                        "Sandbox.Editor.ToggleVisibility",
                    .DrawFrame =
                        [this]
                        {
                            DrawFrame();
                        },
                });
        }

        void Detach()
        {
            ActivePreparedFrame = nullptr;
            Host.Detach();
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

    void EditorShell::Attach(Runtime::Engine& engine)
    {
        m_Impl->Attach(engine);
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
        return m_Impl->Host.UnregisterWindow(handle);
    }

    Runtime::EditorUiVisibilityCommandResult
    EditorShell::ApplyEditorUiVisibilityCommand(
        const Runtime::EditorUiVisibilityCommand command) noexcept
    {
        return m_Impl->Host.ApplyVisibilityCommand(command);
    }

    bool EditorShell::IsEditorVisible() const noexcept
    {
        return m_Impl->Host.IsVisible();
    }

    std::vector<Runtime::EditorWindowMenuEntry>
    EditorShell::BuildEditorWindowMenuModel() const
    {
        return m_Impl->Host.BuildWindowMenuModel();
    }

    bool EditorShell::SetEditorWindowOpen(
        const std::string_view id,
        const bool open)
    {
        return m_Impl->Host.SetWindowOpen(id, open);
    }

    bool EditorShell::IsAttached() const noexcept
    {
        return m_Impl->Host.IsAttached();
    }

    const Runtime::SandboxEditorPanelFrame&
    EditorShell::GetLastFrame() const noexcept
    {
        return m_Impl->Session.LastFrame();
    }
}
