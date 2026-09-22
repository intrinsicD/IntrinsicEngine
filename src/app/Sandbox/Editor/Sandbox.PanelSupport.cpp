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

    bool DrawProcessingScalarOutput(const char* label, Runtime::GeometryPropertyRef& ref)
    {
        bool changed = DrawProcessingPropertyName(label, ref.Name);
        ImGui::PushID(label);
        using K = decltype(ref.ValueKind);
        constexpr std::array kinds{K::Bool, K::Int32, K::UInt32, K::UInt64, K::Float, K::Double};
        int selected = -1;
        for (unsigned i = 0; i < kinds.size(); ++i) if (ref.ValueKind == kinds[i]) selected = int(i);
        if (ImGui::Combo("Storage", &selected, "Bool\0Int32\0UInt32\0UInt64\0Float\0Double\0"))
        { ref.ValueKind = kinds[unsigned(selected)]; changed = true; }
        ImGui::PopID();
        return changed;
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
        const Runtime::EditorPropertyCatalogModel& catalog, Runtime::GeometryPropertyRef& property,
        bool (*accepts)(const Runtime::GeometryPropertyRef&), const std::uint32_t maxComponents)
    {
        bool changed = false;
        const auto displayName = [accepts](const Runtime::GeometryPropertyRef& ref) {
            return accepts ? std::string{Runtime::ToString(ref.Domain)} + ": " + ref.Name : ref.Name;
        };
        const std::string selectedName = displayName(property);
        if (ImGui::BeginCombo(label, selectedName.c_str()))
        {
            for (const auto& row : catalog.Rows)
            {
                if (!row.Bindable || (accepts ? (!accepts(row.Descriptor) ||
                    Runtime::GeometryPropertyComponentCount(row.ValueKind) > maxComponents) :
                    row.Descriptor.Domain != property.Domain || row.ValueKind != property.ValueKind)) continue;
                if (accepts) ImGui::PushID(static_cast<int>(row.Descriptor.Domain));
                const std::string rowName = displayName(row.Descriptor);
                if (ImGui::Selectable(rowName.c_str(), row.Descriptor == property))
                {
                    property = row.Descriptor;
                    changed = true;
                }
                if (accepts) ImGui::PopID();
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

    namespace
    {
        inline constexpr std::array<Runtime::GeometryPresentationSlotSemantic, 5>
            kTextureBakeTargetSemantics{{
                Runtime::GeometryPresentationSlotSemantic::Albedo,
                Runtime::GeometryPresentationSlotSemantic::Normal,
                Runtime::GeometryPresentationSlotSemantic::Roughness,
                Runtime::GeometryPresentationSlotSemantic::Metallic,
                Runtime::GeometryPresentationSlotSemantic::ScalarField,
            }};

        inline constexpr std::array<Runtime::PropertyTextureBakeEncoding, 8>
            kTextureBakeEncoders{{
                Runtime::PropertyTextureBakeEncoding::Auto,
                Runtime::PropertyTextureBakeEncoding::RgbaColor,
                Runtime::PropertyTextureBakeEncoding::Normal,
                Runtime::PropertyTextureBakeEncoding::ScalarColormap,
                Runtime::PropertyTextureBakeEncoding::LinearScalar,
                Runtime::PropertyTextureBakeEncoding::LabelPalette,
                Runtime::PropertyTextureBakeEncoding::Vector2,
                Runtime::PropertyTextureBakeEncoding::Vector3,
            }};

        inline constexpr std::array<Runtime::PropertyTextureBakeStorage, 3>
            kTextureBakeStorageModes{{
                Runtime::PropertyTextureBakeStorage::Auto,
                Runtime::PropertyTextureBakeStorage::RawFloat,
                Runtime::PropertyTextureBakeStorage::EncodedRgba,
            }};

        inline constexpr std::array<const char*, 3>
            kTextureBakeStorageNames{{
                "auto (raw except normals/labels)",
                "raw float texture",
                "encoded RGBA texture",
            }};

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

    namespace
    {
        // Every control reapplies the full copied model so unrelated styling survives.
        void SubmitScalarVisualizationConfig(
            const EditorVisualizationConfigModel& next,
            const SandboxEditorContext& context,
            const std::uint32_t selectedStableId,
            const EditorVisualizationTarget target,
            const bool canEditVisualization)
        {
            if (canEditVisualization)
                (void)ApplyEditorVisualizationConfigCommand(
                    context.VisualizationCommands,
                    MakeVisualizationConfigCommandFromModel(selectedStableId, next, target));
        }
    }

    void DrawScalarFieldColorControls(
        const EditorVisualizationConfigModel& visualization,
        const SandboxEditorContext& context,
        const std::uint32_t selectedStableId,
        const EditorVisualizationTarget target,
        const bool canEditVisualization)
    {
        ImGui::SeparatorText("Scalar field");
        ImGui::Text("Property: %s",
                    visualization.ScalarFieldName.empty()
                        ? "<none>"
                        : visualization.ScalarFieldName.c_str());

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
                static_cast<decltype(next.ScalarColormap)>(colormapIndex);
            SubmitScalarVisualizationConfig(next, context, selectedStableId, target,
                                            canEditVisualization);
        }

        bool autoRange = visualization.ScalarAutoRange;
        if (ImGui::Checkbox("Auto range", &autoRange))
        {
            EditorVisualizationConfigModel next = visualization;
            next.ScalarAutoRange = autoRange;
            SubmitScalarVisualizationConfig(next, context, selectedStableId, target,
                                            canEditVisualization);
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
                SubmitScalarVisualizationConfig(next, context, selectedStableId, target,
                                                canEditVisualization);
            }
        }
    }

    void DrawScalarFieldBinAndIsolineControls(
        const EditorVisualizationConfigModel& visualization,
        const SandboxEditorContext& context,
        const std::uint32_t selectedStableId,
        const EditorVisualizationTarget target,
        const bool canEditVisualization)
    {
        int binCount = static_cast<int>(visualization.ScalarBinCount);
        if (ImGui::DragInt("Bins (0 = continuous)", &binCount, 0.25f, 0, 64) &&
            binCount >= 0)
        {
            EditorVisualizationConfigModel next = visualization;
            next.ScalarBinCount = static_cast<std::uint32_t>(binCount);
            SubmitScalarVisualizationConfig(next, context, selectedStableId, target,
                                            canEditVisualization);
        }

        ImGui::SeparatorText("Isolines");
        int isolineCount = static_cast<int>(visualization.IsolineCount);
        if (ImGui::DragInt("Count##isolines", &isolineCount, 0.25f, 0, 256) &&
            isolineCount >= 0)
        {
            EditorVisualizationConfigModel next = visualization;
            next.IsolineCount = static_cast<std::uint32_t>(isolineCount);
            SubmitScalarVisualizationConfig(next, context, selectedStableId, target,
                                            canEditVisualization);
        }
        float isolineWidth = visualization.IsolineWidth;
        if (ImGui::DragFloat("Width##isolines", &isolineWidth, 0.05f, 0.1f, 16.0f) &&
            isolineWidth > 0.0f)
        {
            EditorVisualizationConfigModel next = visualization;
            next.IsolineWidth = isolineWidth;
            SubmitScalarVisualizationConfig(next, context, selectedStableId, target,
                                            canEditVisualization);
        }
        glm::vec4 isolineColor = visualization.IsolineColor;
        if (ImGui::ColorEdit4("Color##isolines", &isolineColor.x))
        {
            EditorVisualizationConfigModel next = visualization;
            next.IsolineColor = isolineColor;
            SubmitScalarVisualizationConfig(next, context, selectedStableId, target,
                                            canEditVisualization);
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
                SubmitScalarVisualizationConfig(next, context, selectedStableId, target,
                                                canEditVisualization);
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
                SubmitScalarVisualizationConfig(next, context, selectedStableId, target,
                                                canEditVisualization);
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
                SubmitScalarVisualizationConfig(next, context, selectedStableId, target,
                                                canEditVisualization);
            }
        }
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

    void DrawTextureBakeControls(
        const EditorTextureBakeControlsModel& model,
        const SandboxEditorContext* context,
        TextureBakeUiState* state,
        TextureBakeMutationUiState& mutation)
    {
        using ColormapType = decltype(EditorVisualizationConfigModel{}.ScalarColormap);
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
        std::int32_t fallbackWidth{
            static_cast<std::int32_t>(model.DefaultWidth)};
        std::int32_t fallbackHeight{
            static_cast<std::int32_t>(model.DefaultHeight)};
        std::int32_t fallbackPadding{2};
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
        auto* lastUvExtentAdoption =
            state != nullptr &&
                    state->LastUvExtentAdoption != nullptr
                ? state->LastUvExtentAdoption
                : &fallbackUvExtentAdoption;
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
        std::int32_t& bakePadding =
            state != nullptr && state->Padding != nullptr
                ? *state->Padding
                : fallbackPadding;
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

        DrawSandboxUvRegenerationControls(
            model,
            context,
            SandboxUvRegenerationControls{
                .LastResult = lastUvRegenerationResult,
                .LastExtentAdoption = lastUvExtentAdoption,
                .BakeWidth = &bakeWidth,
                .BakeHeight = &bakeHeight,
                .BakePadding = &bakePadding,
                .UvResolution = &uvResolution,
                .UvPadding = &uvPadding,
                .UvTexelsPerUnit = &uvTexelsPerUnit,
                .UvForceRegenerate = &uvForceRegenerate,
                .UvPreserveAuthored = &uvPreserveAuthored,
            });

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
        const PropertyTextureBakeRepresentation resolvedRepresentation =
            selectedSource != nullptr
                ? ResolveEditorTextureBakeTargetRepresentation(
                      selectedSource->ResolvedExpectedValueKind(),
                      kTextureBakeStorageModes[
                          static_cast<std::size_t>(storageIndex)],
                      kTextureBakeEncoders[
                          static_cast<std::size_t>(encoderIndex)],
                      consumers)
                : PropertyTextureBakeRepresentation{};
        const bool paddingSupported =
            resolvedRepresentation.Storage ==
            PropertyTextureBakeStorage::EncodedRgba;
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
                    .PaddingTexels = paddingSupported
                        ? static_cast<std::uint32_t>(bakePadding)
                        : 0u,
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
        auto& renameTarget = mutation.RenameTarget;
        auto& renameBuffer = mutation.RenameBuffer;
        auto& mutationDiagnostic = mutation.MutationDiagnostic;
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

    namespace
    {
        void DrawUvRegenerationStatus(
            const EditorUvDiagnosticsModel& uv,
            const std::optional<EditorUvRegenerationCommandResult>& lastResult)
        {
            if (uv.UvRegenerationJob.has_value())
            {
                const EditorJobRecord& job = *uv.UvRegenerationJob;
                ImGui::Text("UV job: %s %.0f%%",
                            std::string(ToString(job.State)).c_str(),
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

        ImGui::Checkbox("Force regenerate", &uvForceRegenerate);
        ImGui::SameLine();
        ImGui::Checkbox("Preserve valid authored", &uvPreserveAuthored);
        ImGui::InputInt("UV resolution", &uvResolution);
        ImGui::InputInt("UV padding", &uvPadding);
        ImGui::InputFloat("Texels per unit", &uvTexelsPerUnit, 0.0f, 0.0f, "%.3f");
        clampAtlasParameters();

        const EditorUvRegenerationCommand command{
            .StableEntityId = model.SelectedStableId,
            .PreserveValidAuthoredUvs = uvPreserveAuthored,
            .ForceRegenerate = uvForceRegenerate,
            .Resolution = static_cast<std::uint32_t>(uvResolution),
            .Padding = static_cast<std::uint32_t>(uvPadding),
            .TexelsPerUnit = uvTexelsPerUnit,
        };
        const auto readiness = PreviewEditorUvRegenerationCommand(
            context != nullptr ? context->Parameterization.Commands : EditorProcessingCommands{}, command);
        if (DrawProcessingActionButton("Regenerate UVs", readiness))
        {
            lastExtentAdoption.reset();
            if (context->Parameterization.ResultSinks.DismissUvRegenerationResult)
                context->Parameterization.ResultSinks.DismissUvRegenerationResult();
            // A queued atlas job returns `Pending` here and reports its real
            // outcome through the session sink, which the panel re-reads from
            // the parameterization results snapshot on a later frame.
            lastResult = ApplyEditorUvRegenerationCommand(
                context->Parameterization.Commands,
                command,
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

    bool DrawProcessingActionButton(const char* const label, const ActionReadiness& readiness)
    {
        ImGui::BeginDisabled(!readiness.Enabled);
        const bool clicked = ImGui::Button(label);
        ImGui::EndDisabled();
        if (!readiness.Enabled)
            DrawDisabledReasonTooltip(readiness.DisabledReason);
        return clicked;
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

    SandboxParameterizationPanelActionResult
    ApplySandboxParameterizationPanelAction(
        const SandboxEditorContext& context,
        const std::uint32_t stableEntityId,
        const Runtime::ParameterizationConfig& config)
    {
        SandboxParameterizationPanelActionResult result{};
        if (stableEntityId == 0u)
        {
            result.Config.Status = Runtime::RuntimeEngineConfigApplyStatus::Rejected;
            return result;
        }
        result.Config = Runtime::ApplyEditorParameterizationConfig(
            context.Parameterization.Commands, config, "sandbox.parameterization.panel");
        if (result.Config.Succeeded())
            result.Execution = Runtime::ApplyEditorConfiguredParameterizationCommand(
                context.Parameterization.Commands, {.StableEntityId = stableEntityId},
                context.Parameterization.ResultSinks.Parameterization);
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
