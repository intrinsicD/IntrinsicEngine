module;
#include <functional>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <imgui.h>

module Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
// `ToString(JobState)` for the queued UV-job readout.
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.VisualizationRecipes;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.ParameterizationConfig;

#include "Sandbox.PanelSupport.hpp"

extern "C++"
{
namespace Extrinsic::Sandbox::Editor
{
    using namespace Extrinsic::Runtime;

    extern "C++"
    {
        SandboxEditorFrame::SandboxEditorFrame(const Runtime::EditorWorkspaceSnapshot& frame)
            : Runtime::EditorWorkspaceSnapshot(frame)
        {
        }

        SandboxEditorContext::SandboxEditorContext(
            const Runtime::EditorWorkspaceSnapshotPreparedFrame& workspace,
            const Runtime::EditorSceneEditingPreparedFrame& scene,
            const Runtime::EditorProcessingCommands& processing,
                const Runtime::EditorPointFieldPreparedFrame& pointFields,
                const Runtime::EditorPointAnalysisPreparedFrame& pointAnalysis,
                const Runtime::EditorPointSetPreparedFrame& pointSet,
                const Runtime::EditorPointConstructionPreparedFrame& pointConstruction,
                Runtime::EditorPointCloudServicePreparedFrame& pointCloudService,
                const Runtime::EditorNormalPreparedFrame& normals,
                const Runtime::EditorRegistrationPreparedFrame& registration,
                const Runtime::EditorMeshFieldPreparedFrame& meshFields,
                const Runtime::EditorMeshTopologyPreparedFrame& meshTopology,
                const Runtime::EditorParameterizationPreparedFrame& parameterization,
            const Runtime::EditorVisualizationEditingPreparedFrame& visualization,
            const Runtime::EditorRenderRecipeEditingPreparedFrame& renderRecipe,
            SandboxEditorFrame& frame)
            : SceneCommands(scene.Commands),
              Processing(processing),
              PointFields(pointFields), PointAnalysis(pointAnalysis),
              PointSet(pointSet), PointConstruction(pointConstruction),
              PointCloudService(&pointCloudService), Normals(normals),
              Registration(registration),
              MeshFields(meshFields),
              MeshTopology(meshTopology),
              Parameterization(parameterization),
              VisualizationCommands(visualization.Commands),
              RenderRecipeCommands(renderRecipe.Commands),
              SnapshotQueries(workspace.SnapshotQueries),
              AssetImportQueueCommands(scene.AssetImportQueueCommands),
              DocumentCommands(scene.DocumentCommands),
              RenderRecipeDraft(renderRecipe.Draft),
              SceneAvailable(scene.SceneAvailable),
              ProcessingConfigCommandsAvailable(
                  Runtime::AreEditorProcessingConfigCommandsAvailable(processing)),
              RenderRecipeCommandsAvailable(
                  renderRecipe.CommandsAvailable),
              RenderArtifactCommandsAvailable(
                  renderRecipe.ArtifactCommandsAvailable),
              Selection(&frame.Selection),
              Document(&frame.Document),
              ModelBuildStats(&frame.ModelBuildStats)
        {
        }

    }

        [[nodiscard]] Runtime::EditorWorkspaceSnapshot BuildProcessingInputWorkspace(
            const SandboxEditorContext& context)
        {
            return Runtime::BuildEditorWorkspaceSnapshot(
                context.SnapshotQueries,
                {.Hierarchy = true, .Inspector = false, .Selection = true,
                 .Document = false, .SceneFile = false, .FileImport = false,
                 .AssetImportQueue = false, .RenderGraph = false,
                 .RenderRecipe = false, .CameraRender = false, .Visualization = false});
        }

        bool SynchronizeProcessingEntity(
            const Runtime::EditorSelectionModel& selection,
            std::optional<std::vector<std::uint32_t>>& previousSelection,
            std::uint32_t& entity,
            const std::size_t slot)
        {
            const auto selected = slot < selection.SelectedEntities.size()
                ? selection.SelectedEntities[slot].StableEntityId : 0u;
            if (previousSelection && previousSelection->size() == selection.SelectedEntities.size() &&
                std::equal(previousSelection->begin(), previousSelection->end(), selection.SelectedEntities.begin(),
                    [](const auto id, const auto& row) { return id == row.StableEntityId; }))
                return false;
            previousSelection.emplace();
            for (const auto& row : selection.SelectedEntities)
                previousSelection->push_back(row.StableEntityId);
            // Explicit input choices persist until the scene selection changes.
            return std::exchange(entity, selected) != selected;
        }

    bool DrawProcessingEntity(const char* label, const SandboxEditorContext& context,
        std::uint32_t& entity, std::optional<std::vector<std::uint32_t>>& previousSelection,
        const std::optional<Runtime::EditorDomainWindowKind> domain, const std::size_t slot)
    {
        const auto workspace = BuildProcessingInputWorkspace(context);
        bool changed = SynchronizeProcessingEntity(workspace.Selection, previousSelection, entity, slot);
        std::string preview = entity ? std::to_string(entity) : "Choose entity";
        for (const auto& row : workspace.Hierarchy)
            if (row.StableEntityId == entity) preview = row.Name + " (" + std::to_string(entity) + ")";
        if (ImGui::BeginCombo(label, preview.c_str()))
        {
            if (ImGui::Selectable("Choose entity", entity == 0))
            { entity = 0; changed = true; }
            for (const auto& row : workspace.Hierarchy)
            {
                if (Runtime::GetEditorPointInputCatalog(context.Processing, row.StableEntityId).Entries.empty())
                    continue;
                if (domain && !Runtime::BuildEditorDomainWindowModel(context.SnapshotQueries,
                        *domain, nullptr, row.StableEntityId).DomainMatches)
                    continue;
                const auto title = row.Name + " (" + std::to_string(row.StableEntityId) + ")";
                if (ImGui::Selectable(title.c_str(), entity == row.StableEntityId))
                { entity = row.StableEntityId; changed = true; }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    void DrawProcessingCpuBackend()
    {
        if (ImGui::BeginCombo("Backend", "CPU"))
        {
            ImGui::Selectable("CPU", true);
            ImGui::EndCombo();
        }
    }

    bool DrawProcessingPropertyName(const char* label, std::string& name)
    {
        std::array<char, 512> buffer{};
        std::copy_n(name.c_str(), std::min(name.size(), buffer.size() - 1), buffer.data());
        if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
        name = buffer.data();
        return true;
    }

    bool DrawProcessingPointInput(const char* label,
        const std::function<Runtime::GeometryPropertyCatalogSnapshot()>& getCatalog,
        Runtime::GeometryPropertyRef& property,
        const std::optional<Runtime::GeometryElementDomain> domain)
    {
        const auto preview = std::string(Runtime::ToString(property.Domain)) + ": " + property.Name;
        if (!ImGui::BeginCombo(label, preview.c_str())) return false;
        bool changed = false;
        auto previousDomain = Runtime::GeometryElementDomain::Unknown;
        for (const auto& row : getCatalog().Entries)
        {
            if (domain && row.Ref.Domain != *domain) continue;
            if (row.Ref.Domain != previousDomain)
            {
                ImGui::SeparatorText(std::string(Runtime::ToString(row.Ref.Domain)).c_str());
                previousDomain = row.Ref.Domain;
            }
            const auto title = std::string(Runtime::ToString(row.Ref.Domain)) + ": " + row.Ref.Name +
                               " (" + std::to_string(row.ElementCount) + ")";
            if (ImGui::Selectable(title.c_str(), row.Ref == property))
            { property = row.Ref; changed = true; }
        }
        ImGui::EndCombo();
        return changed;
    }

    bool DrawProcessingPropertyInput(const char* label,
        const Runtime::EditorPropertyCatalogModel& catalog, Runtime::GeometryPropertyRef& property)
    {
        bool changed = false;
        if (ImGui::BeginCombo(label, property.Name.c_str()))
        {
            for (const auto& row : catalog.Rows)
            {
                if (!row.Bindable || row.Descriptor.Domain != property.Domain ||
                    row.ValueKind != property.ValueKind) continue;
                if (ImGui::Selectable(row.Name.c_str(), row.Descriptor == property))
                {
                    property = row.Descriptor;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    Runtime::EditorCommandStatus ShowProcessingProperty(
        const SandboxEditorContext& context, const std::uint32_t entity,
        const Runtime::GeometryPropertyRef& property)
    {
        Runtime::VisualizationRecipe recipe;
        using Kind = Geometry::PropertyValueKind;
        if (property.ValueKind == Kind::Vec2 || property.ValueKind == Kind::Vec3 ||
            property.ValueKind == Kind::Vec4)
            recipe.Data = Runtime::ColorVisualizationRecipe{.Source=property, .OutputName=property.Name+".colors"};
        else if (property.ValueKind == Kind::Bool || property.ValueKind == Kind::UInt32 ||
                 property.ValueKind == Kind::Int32)
            recipe.Data = Runtime::LabelVisualizationRecipe{.Source=property, .OutputName=property.Name+".colors"};
        else
            recipe.Data = Runtime::ScalarVisualizationRecipe{.Source=property, .OutputName=property.Name+".colors"};
        return Runtime::ApplyEditorVisualizationRecipeCommand(context.VisualizationCommands,
            {.StableEntityId=entity, .Recipe=std::move(recipe)});
    }

    bool DrawProcessingPropertyShowButton(const SandboxEditorContext& context,
        const std::uint32_t entity, const Runtime::GeometryPropertyRef& property,
        std::string& diagnostic)
    {
        const auto label = "Show " + property.Name;
        if (!ImGui::Button(label.c_str())) return false;
        diagnostic = Runtime::DebugNameForEditorCommandStatus(ShowProcessingProperty(context, entity, property));
        return true;
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

    void DrawDomainWindowHeader(
        const Runtime::EditorDomainWindowModel& model)
    {
        ImGui::Text(
            "Expected domain: %s",
            Runtime::DebugNameForEditorGeometryDomain(
                model.ExpectedDomain));
        if (model.HasSelectedEntity)
        {
            ImGui::Text(
                "Selected: %s (%u)",
                model.SelectedEntity.Name.c_str(),
                model.SelectedStableId);
            ImGui::Text(
                "Selected domain: %s",
                Runtime::DebugNameForEditorGeometryDomain(
                    model.SelectedDomain));
        }
        else
        {
            ImGui::TextDisabled("Selected: none");
        }
        DrawDiagnostics(model.Diagnostics);
    }

    [[nodiscard]] bool DomainWindowReady(
        const Runtime::EditorDomainWindowModel& model) noexcept
    {
        return model.HasSelectedEntity && model.DomainMatches;
    }

    void DrawVec3(const char* label, const glm::vec3 value)
    {
        ImGui::Text("%s: %.3f, %.3f, %.3f", label, value.x, value.y, value.z);
    }

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

    namespace
    {
        void DrawUvRegenerationStatus(
            const EditorUvDiagnosticsModel& uv,
            const std::optional<EditorUvRegenerationCommandResult>& lastResult)
        {
            if (uv.UvRegenerationJob.has_value())
            {
                const EditorJobModel& job = *uv.UvRegenerationJob;
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

            const EditorUvRegenerationCommandResult& result = *lastResult;
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
    }

    void DrawSandboxUvRegenerationControls(
        const EditorTextureBakeControlsModel& model,
        const SandboxEditorContext* const context,
        const SandboxUvRegenerationControls& controls)
    {
        std::optional<EditorUvRegenerationCommandResult>& lastResult =
            *controls.LastResult;
        std::optional<EditorUvRegenerationCommandResult>& lastExtentAdoption =
            *controls.LastExtentAdoption;
        std::int32_t& bakeWidth = *controls.BakeWidth;
        std::int32_t& bakeHeight = *controls.BakeHeight;
        std::int32_t& bakePadding = *controls.BakePadding;
        std::int32_t& uvResolution = *controls.UvResolution;
        std::int32_t& uvPadding = *controls.UvPadding;
        float& uvTexelsPerUnit = *controls.UvTexelsPerUnit;
        bool& uvForceRegenerate = *controls.UvForceRegenerate;
        bool& uvPreserveAuthored = *controls.UvPreserveAuthored;

        const auto clampAtlasParameters = [&]()
        {
            uvResolution = std::clamp<std::int32_t>(uvResolution, 1, 16384);
            uvPadding = std::clamp<std::int32_t>(uvPadding, 0, uvResolution - 1);
            if (!std::isfinite(uvTexelsPerUnit) || uvTexelsPerUnit < 0.0f)
                uvTexelsPerUnit = 0.0f;
        };
        clampAtlasParameters();

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
        clampAtlasParameters();

        const bool canRegenerateUvs =
            model.Uv.UvRegenerationAvailable &&
            context != nullptr &&
            model.SelectedStableId != 0u;
        if (!canRegenerateUvs)
            ImGui::BeginDisabled();
        if (ImGui::Button("Regenerate UVs") && canRegenerateUvs)
        {
            lastExtentAdoption.reset();
            if (context->Parameterization.ResultSinks.DismissUvRegenerationResult)
                context->Parameterization.ResultSinks.DismissUvRegenerationResult();
            // A queued atlas job returns `Pending` here and reports its real
            // outcome through the session sink, which the panel re-reads from
            // the parameterization results snapshot on a later frame.
            lastResult = ApplyEditorUvRegenerationCommand(
                context->Parameterization.Commands,
                EditorUvRegenerationCommand{
                    .StableEntityId = model.SelectedStableId,
                    .PreserveValidAuthoredUvs = uvPreserveAuthored,
                    .ForceRegenerate = uvForceRegenerate,
                    .Resolution = static_cast<std::uint32_t>(uvResolution),
                    .Padding = static_cast<std::uint32_t>(uvPadding),
                    .TexelsPerUnit = uvTexelsPerUnit,
                },
                context->Parameterization.ResultSinks.UvRegeneration);
        }
        if (lastResult.has_value())
        {
            if (!lastResult->Succeeded())
            {
                lastExtentAdoption.reset();
            }
            else if (!lastExtentAdoption.has_value() ||
                     lastExtentAdoption->AtlasWidth != lastResult->AtlasWidth ||
                     lastExtentAdoption->AtlasHeight != lastResult->AtlasHeight)
            {
                bakeWidth = std::clamp<std::int32_t>(
                    static_cast<std::int32_t>(lastResult->AtlasWidth), 1, 8192);
                bakeHeight = std::clamp<std::int32_t>(
                    static_cast<std::int32_t>(lastResult->AtlasHeight), 1, 8192);
                bakePadding = std::clamp<std::int32_t>(uvPadding, 0, 32);
                lastExtentAdoption = *lastResult;
            }
        }
        if (!canRegenerateUvs)
            ImGui::EndDisabled();
        DrawUvRegenerationStatus(model.Uv, lastResult);
        // Drawn after every reader above: dismissal clears the panel copy, the
        // once-per-result extent latch, and the session slot that would
        // otherwise restore the panel copy on the next frame.
        if (lastResult.has_value() &&
            DrawDismissLastResultButton("Dismiss UV result"))
        {
            lastResult.reset();
            lastExtentAdoption.reset();
            if (context != nullptr &&
                context->Parameterization.ResultSinks
                    .DismissUvRegenerationResult)
            {
                context->Parameterization.ResultSinks
                    .DismissUvRegenerationResult();
            }
        }
    }

    [[nodiscard]] EditorVisualizationConfigCommand
    MakeVisualizationConfigCommandFromModel(
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
            .UseBakedTexture = model.UseBakedTexture,
        };
    }

    [[nodiscard]] EditorVisualizationConfigCommand
    MakeUniformVisualizationConfigCommandFromModel(
        const std::uint32_t stableEntityId,
        const EditorVisualizationConfigModel& model,
        const EditorVisualizationTarget target,
        const glm::vec4 color)
    {
        auto command = MakeVisualizationConfigCommandFromModel(stableEntityId, model, target);
        command.Source = kUniformColorSource;
        command.Color = color;
        command.UseBakedTexture = false;
        return command;
    }

    bool DrawDismissLastResultButton(const char* const label)
    {
        return ImGui::SmallButton(label);
    }

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

}

} // extern "C++"

extern "C++"
{
namespace Extrinsic::Sandbox::Editor
{
    using ParameterizationSolverStatus = decltype(
        Runtime::EditorParameterizationResult{}.ParameterizationStatus);
        [[nodiscard]] bool IsFiniteVec2(const glm::vec2 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool IsSupportedParameterizationStrategy(
            const Runtime::EditorParameterizationStrategy strategy) noexcept
        {
            const auto options = SandboxParameterizationStrategyOptions();
            return std::any_of(
                options.begin(),
                options.end(),
                [strategy](const SandboxParameterizationStrategyOption& option)
                {
                    return option.Strategy == strategy &&
                           !option.StableToken.empty();
                });
        }

        [[nodiscard]] const char* ParameterizationSolverStatusLabel(
            const ParameterizationSolverStatus status) noexcept
        {
            switch (status)
            {
            case ParameterizationSolverStatus::Success:
                return "success";
            case ParameterizationSolverStatus::InvalidInput:
                return "invalid input";
            case ParameterizationSolverStatus::SolverFailed:
                return "solver failed";
            }
            return "unsupported";
        }
    std::array<SandboxParameterizationStrategyOption, 4u>
    SandboxParameterizationStrategyOptions() noexcept
    {
        using Strategy = Runtime::EditorParameterizationStrategy;
        return {
            SandboxParameterizationStrategyOption{
                .Strategy = Strategy::Lscm,
                .Label = "LSCM",
                .StableToken = "lscm",
            },
            SandboxParameterizationStrategyOption{
                .Strategy = Strategy::HarmonicCotangent,
                .Label = "Harmonic (cotangent)",
                .StableToken = "harmonic_cotangent",
            },
            SandboxParameterizationStrategyOption{
                .Strategy = Strategy::TutteUniform,
                .Label = "Tutte (uniform)",
                .StableToken = "tutte_uniform",
            },
            SandboxParameterizationStrategyOption{
                .Strategy = Strategy::Bff,
                .Label = "Boundary First Flattening",
                .StableToken = "bff",
            },
        };
    }

    std::optional<SandboxParameterizationPanelApplyRequest>
    BuildSandboxParameterizationPanelApplyRequest(
        const std::uint32_t stableEntityId,
        const SandboxParameterizationPanelConfig& config)
    {
        if (stableEntityId == 0u ||
            !IsSupportedParameterizationStrategy(config.Strategy) ||
            Runtime::StableTokenForEditorParameterizationStrategy(
                config.Strategy).empty())
        {
            return std::nullopt;
        }
        return SandboxParameterizationPanelApplyRequest{
            .Config =
                Runtime::EditorParameterizationConfigCommand{
                    .Config = config,
                    .SourceId = "sandbox.parameterization.panel",
                },
            .Execute =
                Runtime::EditorConfiguredParameterizationCommand{
                    .StableEntityId = stableEntityId,
                },
        };
    }

    SandboxParameterizationPanelActionResult
    ApplySandboxParameterizationPanelAction(
        const SandboxEditorContext& context,
        const std::uint32_t stableEntityId,
        const SandboxParameterizationPanelConfig& config)
    {
        const auto request = BuildSandboxParameterizationPanelApplyRequest(
            stableEntityId,
            config);
        if (!request.has_value())
        {
            SandboxParameterizationPanelActionResult rejected{};
            rejected.Config.Status =
                Runtime::EditorParameterizationConfigStatus::PreviewRejected;
            rejected.Config.Message =
                "Parameterization panel request is invalid or unsupported.";
            return rejected;
        }

        SandboxParameterizationPanelActionResult result{};
        result.Config = Runtime::ApplyEditorParameterizationConfigCommand(
            context.Parameterization.Commands,
            request->Config);
        if (result.Config.Succeeded())
        {
            result.Execution =
                Runtime::ApplyEditorConfiguredParameterizationCommand(
                    context.Parameterization.Commands,
                    request->Execute,
                    context.Parameterization.ResultSinks.Parameterization);
        }
        return result;
    }

    glm::vec2 ProjectSandboxParameterizationUvPoint(
        const SandboxParameterizationUvProjection& projection,
        const glm::vec2 uv) noexcept
    {
        const glm::vec2 centered = uv - projection.UvCenter;
        return projection.PaneCenter + projection.Pan +
               glm::vec2{
                   centered.x * projection.Scale * projection.Zoom,
                   -centered.y * projection.Scale * projection.Zoom,
               };
    }

    SandboxParameterizationUvProjection
    BuildSandboxParameterizationUvProjection(
        const Runtime::EditorParameterizationViewModel& model,
        const SandboxParameterizationUvPane& pane)
    {
        SandboxParameterizationUvProjection projection{};
        projection.Zoom = pane.Zoom;
        projection.Pan = pane.Pan;

        if (!model.HasUvCoordinates || !model.HasFiniteUvBounds ||
            model.UVs.empty())
        {
            projection.Message = "Selected mesh has no finite UV coordinates.";
            return projection;
        }
        if (!IsFiniteVec2(pane.Min) || !IsFiniteVec2(pane.Max) ||
            !IsFiniteVec2(pane.Pan) || !std::isfinite(pane.Padding) ||
            !std::isfinite(pane.Zoom) || pane.Zoom <= 0.0f)
        {
            projection.Message = "UV pane transform is invalid.";
            return projection;
        }

        const glm::vec2 paneSize = pane.Max - pane.Min;
        const float padding = std::max(0.0f, pane.Padding);
        const glm::vec2 available = paneSize - glm::vec2{padding * 2.0f};
        if (!IsFiniteVec2(available) || available.x <= 0.0f ||
            available.y <= 0.0f)
        {
            projection.Message = "UV pane is too small to draw.";
            return projection;
        }

        glm::vec2 uvMin = model.UVs.front();
        glm::vec2 uvMax = model.UVs.front();
        for (const glm::vec2 uv : model.UVs)
        {
            if (!IsFiniteVec2(uv))
            {
                projection.Message = "UV coordinates contain non-finite values.";
                return projection;
            }
            uvMin.x = std::min(uvMin.x, uv.x);
            uvMin.y = std::min(uvMin.y, uv.y);
            uvMax.x = std::max(uvMax.x, uv.x);
            uvMax.y = std::max(uvMax.y, uv.y);
        }

        if (pane.IncludeUnitSquare)
        {
            // Fit the visible checker/grid together with the mesh so its unit
            // square stays a useful reference for compact UV islands.
            uvMin.x = std::min(uvMin.x, 0.0f);
            uvMin.y = std::min(uvMin.y, 0.0f);
            uvMax.x = std::max(uvMax.x, 1.0f);
            uvMax.y = std::max(uvMax.y, 1.0f);
        }

        for (const auto& triangle : model.Triangles)
        {
            if (triangle[0] >= model.UVs.size() ||
                triangle[1] >= model.UVs.size() ||
                triangle[2] >= model.UVs.size())
            {
                projection.Message = "UV topology references an invalid vertex.";
                return projection;
            }
        }

        constexpr float kSpanEpsilon = 1.0e-8f;
        const glm::vec2 span = uvMax - uvMin;
        float scaleX = std::numeric_limits<float>::max();
        float scaleY = std::numeric_limits<float>::max();
        if (span.x > kSpanEpsilon)
            scaleX = available.x / span.x;
        if (span.y > kSpanEpsilon)
            scaleY = available.y / span.y;
        float scale = std::min(scaleX, scaleY);
        if (scale == std::numeric_limits<float>::max())
            scale = std::min(available.x, available.y);
        if (!std::isfinite(scale) || scale <= 0.0f)
        {
            projection.Message = "UV bounds cannot be fitted to the pane.";
            return projection;
        }

        projection.PaneCenter = pane.Min + paneSize * 0.5f;
        projection.UvCenter = uvMin + span * 0.5f;
        if (!IsFiniteVec2(projection.PaneCenter) ||
            !IsFiniteVec2(projection.UvCenter))
        {
            projection.Message = "UV projection center is non-finite.";
            return projection;
        }
        projection.Scale = scale;
        projection.Triangles = model.Triangles;
        projection.Vertices.reserve(model.UVs.size());
        projection.FitsPane = true;
        const glm::vec2 fitMin = pane.Min + glm::vec2{padding - 0.5f};
        const glm::vec2 fitMax = pane.Max - glm::vec2{padding - 0.5f};
        for (const glm::vec2 uv : model.UVs)
        {
            const glm::vec2 point =
                ProjectSandboxParameterizationUvPoint(projection, uv);
            if (!IsFiniteVec2(point))
            {
                projection.Vertices.clear();
                projection.Message =
                    "UV projection produced a non-finite pane coordinate.";
                return projection;
            }
            projection.Vertices.push_back(point);
            projection.FitsPane = projection.FitsPane &&
                                  point.x >= fitMin.x && point.x <= fitMax.x &&
                                  point.y >= fitMin.y && point.y <= fitMax.y;
        }
        projection.Valid = true;
        return projection;
    }

    SandboxParameterizationResultSummary
    BuildSandboxParameterizationResultSummary(
        const Runtime::EditorParameterizationResult& result)
    {
        const auto& diagnostics = result.Diagnostics;
        return SandboxParameterizationResultSummary{
            .Succeeded = result.Succeeded(),
            .HasDiagnostics = diagnostics.VertexStorageCount > 0u ||
                              diagnostics.LiveFaceCount > 0u ||
                              diagnostics.EvaluatedFaceCount > 0u ||
                              diagnostics.SkippedFaceCount > 0u,
            .StrategyToken = result.StrategyToken,
            .CommandStatus =
                Runtime::DebugNameForEditorCommandStatus(result.Status),
            .SolverStatus = ParameterizationSolverStatusLabel(
                result.ParameterizationStatus),
            .Message = result.Message,
            .EvaluatedFaceCount = diagnostics.EvaluatedFaceCount,
            .SkippedFaceCount = diagnostics.SkippedFaceCount,
            .FlippedElementCount = diagnostics.FlippedElementCount,
            .BoundaryEdgeCount = diagnostics.BoundaryEdgeCount,
            .MeanConformalDistortion = diagnostics.MeanConformalDistortion,
            .MeanAreaDistortion = diagnostics.MeanAreaDistortion,
            .MeanStretch = diagnostics.MeanStretch,
        };
    }

}
}
