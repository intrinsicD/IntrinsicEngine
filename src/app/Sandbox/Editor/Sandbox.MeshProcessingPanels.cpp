module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <imgui.h>

module Extrinsic.Sandbox.Editor.MeshProcessingPanels;

import Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.VisualizationRecipes;

namespace Extrinsic::Sandbox::Editor
{
    namespace
    {
        constexpr std::array<Runtime::EditorMeshDenoiseStage, 1>
            kMeshDenoiseStages{{
                Runtime::EditorMeshDenoiseStage::FullBilateral,
            }};
        constexpr std::array<Runtime::EditorMeshCurvatureOutput, 4>
            kMeshCurvatureOutputs{{
                Runtime::EditorMeshCurvatureOutput::All,
                Runtime::EditorMeshCurvatureOutput::Mean,
                Runtime::EditorMeshCurvatureOutput::Gaussian,
                Runtime::EditorMeshCurvatureOutput::PrincipalDirections,
            }};
        constexpr std::array<Runtime::CurvatureSegmentationMethod, 3>
            kCurvatureSegmentationMethods{{
                Runtime::CurvatureSegmentationMethod::FeatureBoundaryCurves,
                Runtime::CurvatureSegmentationMethod::FeatureAlignedPatches,
                Runtime::CurvatureSegmentationMethod::CurvatureGmm,
            }};
        constexpr std::array<
            Runtime::CurvatureSegmentationSelectionMode,
            2>
            kCurvatureSegmentationSelectionModes{{
                Runtime::CurvatureSegmentationSelectionMode::FixedCount,
                Runtime::CurvatureSegmentationSelectionMode::Automatic,
            }};
        constexpr std::string_view kCurvatureRegionColorProperty =
            "f:curvature_region_color";
        constexpr std::string_view kCurvatureFeaturePatchColorProperty =
            "e:curvature_feature_patch_color";
        constexpr std::array<std::string_view, 4>
            kCurvatureScalarProperties{{
                "v:mean_curvature",
                "v:gaussian_curvature",
                "v:min_principal_curvature",
                "v:max_principal_curvature",
            }};
        constexpr std::array<const char*, 4>
            kCurvatureScalarLabels{{
                "Mean curvature",
                "Gaussian curvature",
                "Minimum principal curvature",
                "Maximum principal curvature",
            }};
        constexpr std::array<Runtime::EditorMeshRemeshMode, 2>
            kMeshRemeshModes{{
                Runtime::EditorMeshRemeshMode::Uniform,
                Runtime::EditorMeshRemeshMode::Adaptive,
            }};
        constexpr std::array<Runtime::EditorMeshRemeshSizingLaw, 2>
            kMeshRemeshSizingLaws{{
                Runtime::EditorMeshRemeshSizingLaw::MeanCurvature,
                Runtime::EditorMeshRemeshSizingLaw::ErrorBoundedTaubin,
            }};
        constexpr std::array<Runtime::EditorMeshSubdivideOperator, 3>
            kMeshSubdivideOperators{{
                Runtime::EditorMeshSubdivideOperator::Loop,
                Runtime::EditorMeshSubdivideOperator::CatmullClark,
                Runtime::EditorMeshSubdivideOperator::Sqrt3,
            }};
        constexpr std::array<Runtime::EditorMeshSimplifyMetric, 2>
            kMeshSimplifyMetrics{{
                Runtime::EditorMeshSimplifyMetric::ClassicalQEM,
                Runtime::EditorMeshSimplifyMetric::FA_QEM,
            }};
        constexpr std::array<const char*, 6> kDenoiseStatusNames{{
            "Success",
            "EmptyMesh",
            "NonManifoldInput",
            "DegenerateGeometry",
            "NonFiniteInput",
            "InvalidParams",
        }};
        template <typename Enum, std::size_t N>
        [[nodiscard]] const char* IndexedName(
            const Enum value,
            const std::array<const char*, N>& names) noexcept
        {
            const std::size_t index = static_cast<std::size_t>(value);
            return index < names.size() ? names[index] : "Unknown";
        }

        [[nodiscard]] bool DomainWindowReady(
            const Runtime::EditorDomainWindowModel& model) noexcept
        {
            return model.HasSelectedEntity && model.DomainMatches;
        }

        [[nodiscard]] const char* MeshDenoiseStageName(
            const Runtime::EditorMeshDenoiseStage stage) noexcept
        {
            return stage ==
                    Runtime::EditorMeshDenoiseStage::FullBilateral
                ? "Full bilateral"
                : "Unknown";
        }

        void DrawDiagnostics(
            const std::vector<Runtime::EditorDiagnostic>& diagnostics)
        {
            for (const Runtime::EditorDiagnostic& diagnostic : diagnostics)
            {
                ImGui::TextDisabled(
                    "%s: %s",
                    Runtime::DebugNameForEditorDiagnosticCode(
                        diagnostic.Code),
                    diagnostic.Message.c_str());
            }
        }

        // Dismissal clears both the panel result and the session slot that
        // rebuilds it. Draw this control after all readers of the panel result.
        template <typename ResultT>
        void DrawDismissLastResultButton(
            const char* const label,
            std::optional<ResultT>& panelResult,
            const Runtime::EditorGeometryProcessingResultSlot slot,
            const SandboxEditorContext& context)
        {
            if (!ImGui::SmallButton(label))
                return;
            panelResult.reset();
            if (context.MethodResultSinks.DismissResult)
                context.MethodResultSinks.DismissResult(slot);
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

        template <typename T, std::size_t N>
        [[nodiscard]] T FromIndex(
            const std::array<T, N>& values,
            const std::int32_t index) noexcept
        {
            return values[static_cast<std::size_t>(std::clamp(
                index, 0, static_cast<std::int32_t>(N - 1u)))];
        }

        template <typename Result, typename Sink>
        void PublishCommandResult(
            std::optional<Result>& destination,
            Result result,
            const Sink& sink)
        {
            destination = result;
            if (sink)
                sink(std::move(result));
        }

        void ShowCurvatureSegmentationVisualization(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId)
        {
            using SurfaceDomain = decltype(
                Runtime::EditorRenderHintModel{}.SurfaceDomainValue);
            using EdgeDomain = decltype(
                Runtime::EditorRenderHintModel{}.EdgeDomainValue);
            (void)Runtime::ApplyEditorRenderHintCommand(
                context.VisualizationCommands,
                Runtime::EditorRenderHintCommand{
                    .StableEntityId = stableEntityId,
                    .SetSurface = true,
                    .EnableSurface = true,
                    .SurfaceDomain = static_cast<SurfaceDomain>(1),
                    .SetEdges = true,
                    .EnableEdges = true,
                    .EdgeDomain = static_cast<EdgeDomain>(1),
                    .SetUniformEdgeWidth = true,
                    .UniformEdgeWidth = 2.0f,
                });
            (void)Runtime::ApplyEditorVisualizationPropertyCommand(
                context.VisualizationCommands,
                Runtime::EditorVisualizationPropertyCommand{
                    .StableEntityId = stableEntityId,
                    .Target = Runtime::EditorVisualizationTarget::Surface,
                    .Domain =
                        Runtime::EditorVisualizationPropertyDomain::MeshFaces,
                    .Preset =
                        Runtime::EditorVisualizationPropertyPreset::ColorBuffer,
                    .PropertyName =
                        std::string{kCurvatureRegionColorProperty},
                });
            (void)Runtime::ApplyEditorVisualizationPropertyCommand(
                context.VisualizationCommands,
                Runtime::EditorVisualizationPropertyCommand{
                    .StableEntityId = stableEntityId,
                    .Target = Runtime::EditorVisualizationTarget::Edges,
                    .Domain =
                        Runtime::EditorVisualizationPropertyDomain::MeshEdges,
                    .Preset =
                        Runtime::EditorVisualizationPropertyPreset::ColorBuffer,
                    .PropertyName =
                        std::string{kCurvatureFeaturePatchColorProperty},
                });
        }

        [[nodiscard]] Runtime::EditorCommandStatus
        ShowCurvatureScalarVisualization(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            const std::string_view propertyName)
        {
            using SurfaceDomain = decltype(
                Runtime::EditorRenderHintModel{}.SurfaceDomainValue);
            const Runtime::EditorCommandStatus renderHintStatus =
                Runtime::ApplyEditorRenderHintCommand(
                context.VisualizationCommands,
                Runtime::EditorRenderHintCommand{
                    .StableEntityId = stableEntityId,
                    .SetSurface = true,
                    .EnableSurface = true,
                    .SurfaceDomain = static_cast<SurfaceDomain>(0),
                });
            if (renderHintStatus != Runtime::EditorCommandStatus::Applied &&
                renderHintStatus != Runtime::EditorCommandStatus::NoChange)
            {
                return renderHintStatus;
            }
            return Runtime::ApplyEditorVisualizationPropertyCommand(
                context.VisualizationCommands,
                Runtime::EditorVisualizationPropertyCommand{
                    .StableEntityId = stableEntityId,
                    .Target = Runtime::EditorVisualizationTarget::Surface,
                    .Domain =
                        Runtime::EditorVisualizationPropertyDomain::MeshVertices,
                    .Preset =
                        Runtime::EditorVisualizationPropertyPreset::Scalar,
                    .PropertyName = std::string{propertyName},
                    .ScalarAutoRange = true,
                });
        }
    }

    struct MeshProcessingPanels::Impl
    {
        struct DenoiseState
        {
            std::optional<Runtime::EditorMeshDenoiseResult> LastResult{};
            std::int32_t Stage{0};
            std::int32_t NormalIterations{5};
            std::int32_t VertexIterations{10};
            float SigmaSpatial{0.0f};
            float SigmaRange{0.0f};
            bool PreserveBoundary{true};
        };

        struct CurvatureState
        {
            std::optional<Runtime::EditorMeshCurvatureResult> LastResult{};
            std::int32_t Output{0};
            bool PublishPrincipalDirections{true};
            std::int32_t ScalarVisualization{0};
            bool AutoVisualizeCurvature{true};
            std::optional<std::uint32_t>
                PendingCurvatureVisualizationStableEntityId{};
            std::string PendingCurvatureVisualizationProperty{};
            std::optional<Runtime::EditorCommandStatus>
                LastCurvatureVisualizationStatus{};
            Runtime::CurvatureSegmentationConfig SegmentationConfig{};
            std::optional<Runtime::EditorCurvatureSegmentationResult>
                LastSegmentationResult{};
            std::optional<std::uint32_t>
                LastSegmentationStableEntityId{};
            std::optional<Runtime::RuntimeEngineConfigApplyResult>
                LastSegmentationConfigApply{};
            bool SegmentationConfigInitialized{false};
            bool SegmentationConfigDirty{false};
            bool AutoVisualizeSegmentation{true};
        };

        struct RemeshState
        {
            std::optional<Runtime::EditorMeshRemeshResult> LastResult{};
            std::int32_t Mode{0};
            std::int32_t SizingLaw{0};
            std::int32_t Iterations{1};
            float TargetEdgeLength{0.0f};
            bool ProjectToSurface{false};
        };

        struct SubdivideState
        {
            std::optional<Runtime::EditorMeshSubdivideResult> LastResult{};
            std::int32_t Operator{0};
            std::int32_t Iterations{1};
            bool PreserveLoopFeatures{false};
        };

        struct SimplifyState
        {
            std::optional<Runtime::EditorMeshSimplifyResult> LastResult{};
            std::int32_t Metric{1};
            std::int32_t TargetFaces{0};
            float MaxError{0.0f};
            bool PreserveBoundary{true};
            float FeatureAngleThresholdDegrees{45.0f};
            float NormalWeight{1.0f};
            float BoundaryWeight{1.0f};
            float CurvatureWeight{1.0f};
            bool PreserveSharpFeatures{true};
            bool PreserveUvSeams{true};
        };

        struct NormalsState
        {
            std::optional<Runtime::EditorNormalEstimationResult> LastResult{};
            Runtime::NormalEstimationConfig Draft{};
            std::string LastApplied{}, ConfigDiagnostic{}, VisualizationDiagnostic{};
        };

        struct RegistrationState
        {
            std::optional<Runtime::EditorRegistrationResult> LastResult{};
            std::string ConfigDiagnostic{};
            Runtime::RegistrationConfig Draft{};
            std::string LastApplied{};
        };

        using DrawWindow = void (Impl::*)(
            bool&,
            const SandboxEditorContext&);
        using DrawDomainControls = void (Impl::*)(
            const Runtime::EditorDomainWindowModel&,
            const SandboxEditorContext&);

        EditorShell* Shell{nullptr};
        std::vector<Runtime::EditorWindowHandle> Handles{};
        int CachedModelFrame{-1};
        std::array<
            std::optional<Runtime::EditorDomainWindowModel>,
            3u>
            CachedDomainModels{};
        DenoiseState Denoise{};
        CurvatureState Curvature{};
        Runtime::GeodesicsConfig GeodesicsConfig{};
        bool GeodesicsInitialized{false};
        bool GeodesicsDirty{false};
        int GeodesicsSourceVertex{0};
        std::optional<Runtime::EditorGeodesicsResult> GeodesicsResult{};
        std::string GeodesicsMessage{};
        RemeshState Remesh{};
        SubdivideState Subdivide{};
        SimplifyState Simplify{};
        RegistrationState Registration{};
        NormalsState Normals{};

        void Register(EditorShell& editorShell);
        void Unregister();
        void RegisterWindow(
            std::string id,
            std::vector<std::string> menuPath,
            std::string title,
            DrawWindow draw);
        void ResetModelCache();
        [[nodiscard]] const Runtime::EditorDomainWindowModel&
        GetDomainWindowModel(
            const SandboxEditorContext& context,
            Runtime::EditorDomainWindowKind kind);
        void DrawDomainWindow(
            bool& open,
            const SandboxEditorContext& context,
            Runtime::EditorDomainWindowKind kind,
            const char* title,
            DrawDomainControls draw);

        void DrawDenoiseWindow(bool&, const SandboxEditorContext&);
        void DrawCurvatureWindow(bool&, const SandboxEditorContext&);
        void DrawGeodesicsWindow(bool&, const SandboxEditorContext&);
        void DrawGeodesicsControls(const Runtime::EditorDomainWindowModel&,
                                   const SandboxEditorContext&);
        void DrawRemeshWindow(bool&, const SandboxEditorContext&);
        void DrawSubdivideWindow(bool&, const SandboxEditorContext&);
        void DrawSimplifyWindow(bool&, const SandboxEditorContext&);
        void DrawNormalsWindow(bool&, const SandboxEditorContext&);
        void DrawRegistrationWindow(bool&, const SandboxEditorContext&);

        void DrawDenoiseControls(
            const Runtime::EditorDomainWindowModel&,
            const SandboxEditorContext&);
        void DrawCurvatureControls(
            const Runtime::EditorDomainWindowModel&,
            const SandboxEditorContext&);
        void DrawCurvatureSegmentationControls(
            const Runtime::EditorDomainWindowModel&,
            const SandboxEditorContext&);
        void DrawRemeshControls(
            const Runtime::EditorDomainWindowModel&,
            const SandboxEditorContext&);
        void DrawSubdivideControls(
            const Runtime::EditorDomainWindowModel&,
            const SandboxEditorContext&);
        void DrawSimplifyControls(
            const Runtime::EditorDomainWindowModel&,
            const SandboxEditorContext&);
    };

    void MeshProcessingPanels::Impl::Register(
        EditorShell& editorShell)
    {
        Unregister();
        Shell = &editorShell;
        RegisterWindow("mesh.processing.denoise", {"Mesh", "Processing"},
                       "Denoise", &Impl::DrawDenoiseWindow);
        RegisterWindow("mesh.processing.geodesics", {"Mesh", "Geodesics"},
                       "Virtual Source Propagation", &Impl::DrawGeodesicsWindow);
        RegisterWindow("mesh.processing.curvature", {"Mesh", "Processing"},
                       "Curvature", &Impl::DrawCurvatureWindow);
        RegisterWindow("mesh.processing.remesh", {"Mesh", "Processing"},
                       "Remesh", &Impl::DrawRemeshWindow);
        RegisterWindow("mesh.processing.subdivide", {"Mesh", "Processing"},
                       "Subdivide", &Impl::DrawSubdivideWindow);
        RegisterWindow("mesh.processing.simplify", {"Mesh", "Processing"},
                       "Simplify", &Impl::DrawSimplifyWindow);
        RegisterWindow("view.normal_estimation", {"View"}, "Normal Estimation", &Impl::DrawNormalsWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.vertices.normals", "Mesh"}, {"graph.processing.vertices.normals", "Graph"},
                  {"pointcloud.processing.vertices.normals", "PointCloud"}}})
            Handles.push_back(Shell->RegisterEditorWindow({
                .Id = id, .MenuPath = {domain, "Processing", "Vertices"}, .Title = "Normals",
                .Draw = [](bool& open, const SandboxEditorContext&) { open = false; },
                .OpenStateChanged = [this, id](bool open) {
                    if (!open) return;
                    (void)Shell->SetEditorWindowOpen("view.normal_estimation", true);
                    (void)Shell->SetEditorWindowOpen(id, false);
                }}));
        RegisterWindow("view.registration", {"View"},
                       "ICP Registration", &Impl::DrawRegistrationWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.registration", "Mesh"}, {"graph.processing.registration", "Graph"},
                  {"pointcloud.processing.registration", "PointCloud"}}})
            Handles.push_back(Shell->RegisterEditorWindow({
                .Id = id, .MenuPath = {domain, "Processing"}, .Title = "ICP Registration",
                .Draw = [](bool& open, const SandboxEditorContext&) { open = false; },
                .OpenStateChanged = [this, id](bool open) {
                    if (!open) return;
                    (void)Shell->SetEditorWindowOpen("view.registration", true);
                    (void)Shell->SetEditorWindowOpen(id, false);
                }}));
    }

    void MeshProcessingPanels::Impl::Unregister()
    {
        if (Shell != nullptr)
        {
            for (const Runtime::EditorWindowHandle handle : Handles)
                (void)Shell->UnregisterEditorWindow(handle);
        }
        Handles.clear();
        Shell = nullptr;
        ResetModelCache();
        Denoise.LastResult.reset();
        Curvature.LastResult.reset();
        GeodesicsInitialized = false;
        GeodesicsDirty = false;
        GeodesicsResult.reset();
        GeodesicsMessage.clear();
        Curvature.LastSegmentationResult.reset();
        Curvature.LastSegmentationStableEntityId.reset();
        Curvature.LastSegmentationConfigApply.reset();
        Curvature.PendingCurvatureVisualizationStableEntityId.reset();
        Curvature.PendingCurvatureVisualizationProperty.clear();
        Curvature.LastCurvatureVisualizationStatus.reset();
        Curvature.SegmentationConfigInitialized = false;
        Curvature.SegmentationConfigDirty = false;
        Remesh.LastResult.reset();
        Subdivide.LastResult.reset();
        Simplify.LastResult.reset();
        Registration.LastResult.reset();
        Normals = {};
    }

    void MeshProcessingPanels::Impl::RegisterWindow(
        std::string id,
        std::vector<std::string> menuPath,
        std::string title,
        const DrawWindow draw)
    {
        Handles.push_back(Shell->RegisterEditorWindow(
            EditorWindowDescriptor{
                .Id = std::move(id),
                .MenuPath = std::move(menuPath),
                .Title = std::move(title),
                .OpenByDefault = false,
                .Draw =
                    [this, draw](
                        bool& open,
                        const SandboxEditorContext& context)
                    {
                        (this->*draw)(open, context);
                    },
                .OpenStateChanged =
                    [this](bool)
                    {
                        ResetModelCache();
                    },
            }));
    }

    void MeshProcessingPanels::Impl::ResetModelCache()
    {
        CachedModelFrame = -1;
        for (auto& model : CachedDomainModels)
            model.reset();
    }

    const Runtime::EditorDomainWindowModel&
    MeshProcessingPanels::Impl::GetDomainWindowModel(
        const SandboxEditorContext& context,
        const Runtime::EditorDomainWindowKind kind)
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
            model = Runtime::BuildEditorDomainWindowModel(
                context.SnapshotQueries,
                kind,
                context.ModelBuildStats);
        }
        else if (context.ModelBuildStats != nullptr)
        {
            ++context.ModelBuildStats->DomainWindowModelCacheHits;
        }
        return *model;
    }

    void MeshProcessingPanels::Impl::DrawDomainWindow(
        bool& open,
        const SandboxEditorContext& context,
        const Runtime::EditorDomainWindowKind kind,
        const char* title,
        const DrawDomainControls draw)
    {
        ImGui::SetNextWindowSize(
            ImVec2(340.0f, 300.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title, &open))
        {
            const Runtime::EditorDomainWindowModel& model =
                GetDomainWindowModel(context, kind);
            // The header already includes processing diagnostics; render them
            // only once.
            DrawDomainWindowHeader(model);
            if (!DomainWindowReady(model) ||
                !model.Processing.HasSelectedEntity)
            {
                ImGui::TextDisabled(
                    "Select a matching domain entity to inspect processing affordances.");
            }
            else
            {
                (this->*draw)(model, context);
            }
        }
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawDenoiseWindow(
        bool& open, const SandboxEditorContext& context)
    {
        DrawDomainWindow(
            open, context, Runtime::EditorDomainWindowKind::Mesh,
            "Mesh / Processing / Denoise", &Impl::DrawDenoiseControls);
    }

    void MeshProcessingPanels::Impl::DrawCurvatureWindow(
        bool& open, const SandboxEditorContext& context)
    {
        DrawDomainWindow(
            open, context, Runtime::EditorDomainWindowKind::Mesh,
            "Mesh / Processing / Curvature", &Impl::DrawCurvatureControls);
    }

    void MeshProcessingPanels::Impl::DrawRemeshWindow(
        bool& open, const SandboxEditorContext& context)
    {
        DrawDomainWindow(
            open, context, Runtime::EditorDomainWindowKind::Mesh,
            "Mesh / Processing / Remesh", &Impl::DrawRemeshControls);
    }

    void MeshProcessingPanels::Impl::DrawSubdivideWindow(
        bool& open, const SandboxEditorContext& context)
    {
        DrawDomainWindow(
            open, context, Runtime::EditorDomainWindowKind::Mesh,
            "Mesh / Processing / Subdivide", &Impl::DrawSubdivideControls);
    }

    void MeshProcessingPanels::Impl::DrawSimplifyWindow(
        bool& open, const SandboxEditorContext& context)
    {
        DrawDomainWindow(
            open, context, Runtime::EditorDomainWindowKind::Mesh,
            "Mesh / Processing / Simplify", &Impl::DrawSimplifyControls);
    }

    void MeshProcessingPanels::Impl::DrawDenoiseControls(
        const Runtime::EditorDomainWindowModel& model,
        const SandboxEditorContext& context)
    {
        const Runtime::EditorGeometryProcessingModel& processing =
            model.Processing;
        if (context.GeometryResults.LastMeshDenoiseResult.has_value())
            Denoise.LastResult = *context.GeometryResults.LastMeshDenoiseResult;
        ImGui::SeparatorText("Denoise");
        if (!processing.MeshDenoiseAvailable)
        {
            ImGui::TextDisabled(
                "Mesh denoise is unavailable for this selection.");
            return;
        }

        Denoise.Stage = std::clamp(
            Denoise.Stage, 0,
            static_cast<std::int32_t>(kMeshDenoiseStages.size() - 1u));
        const Runtime::EditorMeshDenoiseStage stage =
            FromIndex(kMeshDenoiseStages, Denoise.Stage);
        if (ImGui::BeginCombo(
                "Stage##MeshDenoise",
                MeshDenoiseStageName(stage)))
        {
            for (std::size_t i = 0u; i < kMeshDenoiseStages.size(); ++i)
            {
                const bool selected =
                    Denoise.Stage == static_cast<std::int32_t>(i);
                if (ImGui::Selectable(
                        MeshDenoiseStageName(kMeshDenoiseStages[i]),
                        selected))
                {
                    Denoise.Stage = static_cast<std::int32_t>(i);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        Denoise.NormalIterations =
            std::clamp(Denoise.NormalIterations, 1, 4096);
        Denoise.VertexIterations =
            std::clamp(Denoise.VertexIterations, 1, 4096);
        Denoise.SigmaSpatial =
            std::clamp(Denoise.SigmaSpatial, 0.0f, 1.0e6f);
        Denoise.SigmaRange =
            std::clamp(Denoise.SigmaRange, 0.0f, 1.0e6f);
        ImGui::DragInt(
            "Normal iterations##MeshDenoise", &Denoise.NormalIterations,
            1.0f, 1, 4096);
        ImGui::DragInt(
            "Vertex iterations##MeshDenoise", &Denoise.VertexIterations,
            1.0f, 1, 4096);
        ImGui::DragFloat(
            "Sigma spatial##MeshDenoise", &Denoise.SigmaSpatial,
            0.01f, 0.0f, 1.0e6f);
        ImGui::DragFloat(
            "Sigma range##MeshDenoise", &Denoise.SigmaRange,
            0.01f, 0.0f, 1.0e6f);
        ImGui::Checkbox(
            "Preserve boundary##MeshDenoise", &Denoise.PreserveBoundary);

        if (ImGui::Button("Denoise##MeshDenoise"))
        {
            PublishCommandResult(
                Denoise.LastResult,
                Runtime::ApplyEditorMeshDenoiseCommand(
                    context.GeometryCommands,
                    Runtime::EditorMeshDenoiseCommand{
                        .StableEntityId = model.SelectedStableId,
                        .Stage = stage,
                        .NormalIterations = static_cast<std::uint32_t>(
                            Denoise.NormalIterations),
                        .VertexIterations = static_cast<std::uint32_t>(
                            Denoise.VertexIterations),
                        .SigmaSpatial = static_cast<double>(
                            Denoise.SigmaSpatial),
                        .SigmaRange = static_cast<double>(Denoise.SigmaRange),
                        .PreserveBoundary = Denoise.PreserveBoundary,
                    }),
                context.MethodResultSinks.MeshDenoise);
        }

        const auto& result = Denoise.LastResult.has_value()
            ? Denoise.LastResult
            : processing.LastMeshDenoiseResult;
        if (!result.has_value())
        {
            ImGui::TextDisabled("Last denoise run: none");
            return;
        }
        ImGui::Text(
            "Last denoise run: %s",
            Runtime::DebugNameForEditorCommandStatus(result->Status));
        ImGui::Text(
            "Geometry status: %s",
            IndexedName(result->DenoiseStatus, kDenoiseStatusNames));
        ImGui::Text("Stage: %s", MeshDenoiseStageName(result->Stage));
        // NoChange still means the kernel executed, so its diagnostics remain
        // relevant.
        const bool kernelRan =
            result->Succeeded() ||
            result->Status == Runtime::EditorCommandStatus::NoChange;
        if (kernelRan)
        {
            ImGui::Text(
                "Written: %zu / %zu  moved: %zu  deleted: %zu",
                result->WrittenCount, result->VertexSlotCount,
                result->MovedVertexCount,
                result->SkippedDeletedVertexCount);
            ImGui::Text(
                "Iterations: normals=%u  vertices=%u",
                result->NormalIterations, result->VertexIterations);
            ImGui::Text(
                "Faces: processed=%zu  degenerate=%zu  nonfinite=%zu  deleted=%zu",
                result->ProcessedFaceCount, result->DegenerateFaceCount,
                result->NonFiniteFaceCount,
                result->SkippedDeletedFaceCount);
            ImGui::Text(
                "Pinned boundary vertices: %zu (%.1f%%)",
                result->PinnedBoundaryVertexCount,
                result->PinnedBoundaryRatio() * 100.0);
            ImGui::Text(
                "Sigma used: spatial=%.6f  range=%.6f",
                result->SigmaSpatialUsed, result->SigmaRangeUsed);
        }
        if (!result->Message.empty())
            ImGui::TextWrapped("%s", result->Message.c_str());
        DrawDismissLastResultButton(
            "Dismiss##MeshDenoise",
            Denoise.LastResult,
            Runtime::EditorGeometryProcessingResultSlot::MeshDenoise,
            context);
    }

    void MeshProcessingPanels::Impl::DrawCurvatureControls(
        const Runtime::EditorDomainWindowModel& model,
        const SandboxEditorContext& context)
    {
        const Runtime::EditorGeometryProcessingModel& processing =
            model.Processing;
        if (context.GeometryResults.LastMeshCurvatureResult.has_value())
            Curvature.LastResult = *context.GeometryResults.LastMeshCurvatureResult;
        // The interactive runtime executes curvature on the job lane, so the
        // Compute command normally returns Pending before its properties exist.
        // Complete the requested visualization when that terminal result is
        // projected into a later prepared frame.
        if (Curvature.PendingCurvatureVisualizationStableEntityId.has_value() &&
            Curvature.LastResult.has_value() &&
            Curvature.LastResult->Status !=
                Runtime::EditorCommandStatus::Pending)
        {
            if (Curvature.LastResult->Status ==
                    Runtime::EditorCommandStatus::Applied ||
                Curvature.LastResult->Status ==
                    Runtime::EditorCommandStatus::NoChange)
            {
                Curvature.LastCurvatureVisualizationStatus =
                    ShowCurvatureScalarVisualization(
                        context,
                        *Curvature.PendingCurvatureVisualizationStableEntityId,
                        Curvature.PendingCurvatureVisualizationProperty);
            }
            Curvature.PendingCurvatureVisualizationStableEntityId.reset();
            Curvature.PendingCurvatureVisualizationProperty.clear();
        }
        ImGui::SeparatorText("Curvature");
        if (!processing.MeshCurvatureAvailable)
        {
            ImGui::TextDisabled(
                "Mesh curvature is unavailable for this selection.");
            return;
        }

        Curvature.Output = std::clamp(
            Curvature.Output, 0,
            static_cast<std::int32_t>(kMeshCurvatureOutputs.size() - 1u));
        const Runtime::EditorMeshCurvatureOutput output =
            FromIndex(kMeshCurvatureOutputs, Curvature.Output);
        if (ImGui::BeginCombo(
                "Output##MeshCurvature",
                Runtime::DebugNameForEditorMeshCurvatureOutput(output)))
        {
            for (std::size_t i = 0u; i < kMeshCurvatureOutputs.size(); ++i)
            {
                const bool selected =
                    Curvature.Output == static_cast<std::int32_t>(i);
                if (ImGui::Selectable(
                        Runtime::DebugNameForEditorMeshCurvatureOutput(
                            kMeshCurvatureOutputs[i]),
                        selected))
                {
                    Curvature.Output = static_cast<std::int32_t>(i);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (!processing.MeshCurvatureDirectionsAvailable)
            ImGui::BeginDisabled();
        ImGui::Checkbox(
            "Principal directions##MeshCurvature",
            &Curvature.PublishPrincipalDirections);
        if (!processing.MeshCurvatureDirectionsAvailable)
            ImGui::EndDisabled();

        Curvature.ScalarVisualization = std::clamp(
            Curvature.ScalarVisualization,
            0,
            static_cast<std::int32_t>(
                kCurvatureScalarProperties.size() - 1u));
        if (ImGui::BeginCombo(
                "Display scalar##MeshCurvature",
                kCurvatureScalarLabels[static_cast<std::size_t>(
                    Curvature.ScalarVisualization)]))
        {
            for (std::size_t i = 0u;
                 i < kCurvatureScalarProperties.size(); ++i)
            {
                const bool selected =
                    Curvature.ScalarVisualization ==
                    static_cast<std::int32_t>(i);
                if (ImGui::Selectable(
                        kCurvatureScalarLabels[i], selected))
                {
                    Curvature.ScalarVisualization =
                        static_cast<std::int32_t>(i);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        (void)ImGui::Checkbox(
            "Show scalar after compute##MeshCurvature",
            &Curvature.AutoVisualizeCurvature);

        if (ImGui::Button("Compute##MeshCurvature"))
        {
            Runtime::EditorMeshCurvatureResult commandResult =
                Runtime::ApplyEditorMeshCurvatureCommand(
                    context.GeometryCommands,
                    Runtime::EditorMeshCurvatureCommand{
                        .StableEntityId = model.SelectedStableId,
                        .Output = output,
                        .PublishPrincipalDirections =
                            Curvature.PublishPrincipalDirections,
                    });
            if (Curvature.AutoVisualizeCurvature)
            {
                const std::string_view property =
                    kCurvatureScalarProperties[static_cast<std::size_t>(
                        Curvature.ScalarVisualization)];
                Curvature.LastCurvatureVisualizationStatus =
                    ShowCurvatureScalarVisualization(
                    context,
                    model.SelectedStableId,
                    property);
                const Runtime::EditorCommandStatus visualizationStatus =
                    *Curvature.LastCurvatureVisualizationStatus;
                if (commandResult.Status ==
                        Runtime::EditorCommandStatus::Pending &&
                    visualizationStatus !=
                        Runtime::EditorCommandStatus::Applied &&
                    visualizationStatus !=
                        Runtime::EditorCommandStatus::NoChange)
                {
                    Curvature.PendingCurvatureVisualizationStableEntityId =
                        model.SelectedStableId;
                    Curvature.PendingCurvatureVisualizationProperty = property;
                }
                else
                {
                    Curvature.PendingCurvatureVisualizationStableEntityId.reset();
                    Curvature.PendingCurvatureVisualizationProperty.clear();
                }
            }
            PublishCommandResult(
                Curvature.LastResult,
                std::move(commandResult),
                context.MethodResultSinks.MeshCurvature);
        }
        DrawCurvatureSegmentationControls(model, context);
        const auto& result = Curvature.LastResult.has_value()
            ? Curvature.LastResult
            : processing.LastMeshCurvatureResult;
        if (!result.has_value())
        {
            ImGui::TextDisabled("Last curvature run: none");
            return;
        }
        ImGui::Text(
            "Last curvature run: %s",
            Runtime::DebugNameForEditorCommandStatus(result->Status));
        if (Curvature.LastCurvatureVisualizationStatus.has_value())
        {
            ImGui::Text(
                "Scalar visualization: %s",
                Runtime::DebugNameForEditorCommandStatus(
                    *Curvature.LastCurvatureVisualizationStatus));
        }
        ImGui::Text(
            "Output: %s",
            Runtime::DebugNameForEditorMeshCurvatureOutput(
                result->Output));
        // NoChange still means the kernel executed, so its counters remain
        // relevant.
        if (result->Succeeded() ||
            result->Status == Runtime::EditorCommandStatus::NoChange)
        {
            ImGui::Text(
                "Vertices: %zu  scalar values: %zu  changed: %zu  nonfinite scalars: %zu",
                result->VertexSlotCount, result->ScalarWrittenCount,
                result->ChangedValueCount,
                result->NonFiniteScalarCount);
            ImGui::Text(
                "Directions: %s  values: %zu  nonfinite: %zu",
                result->DirectionsPublished ? "published" : "not published",
                result->DirectionWrittenCount,
                result->NonFiniteDirectionCount);
        }
        if (!result->Message.empty())
            ImGui::TextWrapped("%s", result->Message.c_str());
        DrawDismissLastResultButton(
            "Dismiss##MeshCurvature",
            Curvature.LastResult,
            Runtime::EditorGeometryProcessingResultSlot::MeshCurvature,
            context);
    }

    void MeshProcessingPanels::Impl::DrawCurvatureSegmentationControls(
        const Runtime::EditorDomainWindowModel& model,
        const SandboxEditorContext& context)
    {
        ImGui::SeparatorText("Curvature segmentation");
        if (!model.Processing.CurvatureSegmentationAvailable)
        {
            ImGui::TextDisabled(
                "Signed-curvature segmentation requires editable mesh faces and edges.");
            return;
        }

        if (!Curvature.SegmentationConfigInitialized)
        {
            if (const auto active =
                    Runtime::GetEditorCurvatureSegmentationConfig(
                        context.GeometryCommands))
            {
                Curvature.SegmentationConfig = *active;
            }
            Curvature.SegmentationConfigInitialized = true;
            Curvature.SegmentationConfigDirty = false;
        }

        Runtime::CurvatureSegmentationConfig& config =
            Curvature.SegmentationConfig;
        bool changed = false;
        if (ImGui::BeginCombo(
                "Method##CurvatureSegmentation",
                Runtime::DebugNameForCurvatureSegmentationMethod(
                    config.Method)))
        {
            for (const auto method : kCurvatureSegmentationMethods)
            {
                const bool selected = method == config.Method;
                if (ImGui::Selectable(
                        Runtime::DebugNameForCurvatureSegmentationMethod(
                            method),
                        selected))
                {
                    config.Method = method;
                    changed = true;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        const bool boundaryCurves = config.Method ==
            Runtime::CurvatureSegmentationMethod::FeatureBoundaryCurves;
        if (!boundaryCurves)
        {
            if (ImGui::BeginCombo(
                    "Component selection##CurvatureSegmentation",
                    Runtime::DebugNameForCurvatureSegmentationSelectionMode(
                        config.SelectionMode)))
            {
                for (const auto mode :
                     kCurvatureSegmentationSelectionModes)
                {
                    const bool selected = mode == config.SelectionMode;
                    if (ImGui::Selectable(
                            Runtime::
                                DebugNameForCurvatureSegmentationSelectionMode(
                                    mode),
                            selected))
                    {
                        config.SelectionMode = mode;
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (config.SelectionMode ==
                Runtime::CurvatureSegmentationSelectionMode::FixedCount)
            {
                changed |= ImGui::InputScalar(
                    "Components##CurvatureSegmentation",
                    ImGuiDataType_U32,
                    &config.FixedComponentCount);
            }
            else
            {
                changed |= ImGui::InputScalar(
                    "Minimum components##CurvatureSegmentation",
                    ImGuiDataType_U32,
                    &config.AutomaticMinComponents);
                changed |= ImGui::InputScalar(
                    "Maximum components##CurvatureSegmentation",
                    ImGuiDataType_U32,
                    &config.AutomaticMaxComponents);
                changed |= ImGui::InputDouble(
                    "Curvature fit tolerance##CurvatureSegmentation",
                    &config.AutomaticFitTolerance,
                    0.01,
                    0.1,
                    "%.6g");
                changed |= ImGui::InputDouble(
                    "Complexity weight##CurvatureSegmentation",
                    &config.AutomaticComplexityWeight,
                    0.05,
                    0.5,
                    "%.6g");
            }
        }

        if (boundaryCurves || config.Method ==
            Runtime::CurvatureSegmentationMethod::FeatureAlignedPatches)
        {
            changed |= ImGui::InputDouble(
                "Feature radius / diagonal##CurvatureSegmentation",
                &config.FeatureBaseRadiusRatio,
                0.001,
                0.01,
                "%.6g");
            changed |= ImGui::InputDouble(
                "Hard dihedral degrees##CurvatureSegmentation",
                &config.HardDihedralThresholdDegrees,
                1.0,
                5.0,
                "%.6g");
            if (!boundaryCurves)
            {
                changed |= ImGui::InputDouble(
                    "Patch complexity cost##CurvatureSegmentation",
                    &config.PatchComplexityCost,
                    0.05,
                    0.25,
                    "%.6g");
            }
            else
            {
                ImGui::TextWrapped(
                    "Experimental curves_v1: adoption quality checks have not passed. "
                    "Uses the fixed comparison profile; GMM settings do not apply.");
            }
            ImGui::TextDisabled(
                "Final hard/soft/closure boundaries render in red/gold/blue; retained candidates remain inspectable properties.");
        }
        else
        {
            changed |= ImGui::InputDouble(
                "Spatial regularization##CurvatureSegmentation",
                &config.SpatialWeight,
                0.05,
                0.5,
                "%.6g");
            changed |= ImGui::InputDouble(
                "Feature sensitivity##CurvatureSegmentation",
                &config.FeatureSensitivity,
                0.1,
                1.0,
                "%.6g");
            changed |= ImGui::InputScalar(
                "Minimum region faces##CurvatureSegmentation",
                ImGuiDataType_U32,
                &config.MinimumRegionFaces);
        }
        (void)ImGui::Checkbox(
            "Show clusters and boundaries after run##CurvatureSegmentation",
            &Curvature.AutoVisualizeSegmentation);

        if (!boundaryCurves && ImGui::TreeNode("Advanced GMM and optimizer controls"))
        {
            changed |= ImGui::InputScalar(
                "EM iterations##CurvatureSegmentation",
                ImGuiDataType_U32,
                &config.MaxEmIterations);
            changed |= ImGui::InputDouble(
                "EM relative tolerance##CurvatureSegmentation",
                &config.EmRelativeTolerance,
                1.0e-7,
                1.0e-6,
                "%.6g");
            changed |= ImGui::InputDouble(
                "Covariance floor##CurvatureSegmentation",
                &config.CovarianceFloor,
                1.0e-6,
                1.0e-5,
                "%.6g");
            changed |= ImGui::InputScalar(
                "Seed##CurvatureSegmentation",
                ImGuiDataType_U32,
                &config.Seed);
            changed |= ImGui::InputScalar(
                "Spatial iterations##CurvatureSegmentation",
                ImGuiDataType_U32,
                &config.MaxSpatialIterations);
            ImGui::TreePop();
        }

        // Preserve valid inactive settings when inspecting the fixed profile.
        if (!boundaryCurves)
        {
            config.FixedComponentCount = std::clamp(
                config.FixedComponentCount, 1u, 1024u);
            config.AutomaticMinComponents = std::clamp(
                config.AutomaticMinComponents, 1u, 1024u);
            config.AutomaticMaxComponents = std::clamp(
                config.AutomaticMaxComponents,
                config.AutomaticMinComponents,
                1024u);
            config.AutomaticFitTolerance = std::clamp(
                config.AutomaticFitTolerance, 1.0e-12, 1.0e12);
            config.AutomaticComplexityWeight = std::clamp(
                config.AutomaticComplexityWeight, 0.0, 1.0e12);
            config.MaxEmIterations = std::clamp(
                config.MaxEmIterations, 1u, 100000u);
            config.EmRelativeTolerance = std::clamp(
                config.EmRelativeTolerance, 0.0, 1.0);
            config.CovarianceFloor = std::clamp(
                config.CovarianceFloor, 1.0e-15, 1.0e6);
            config.SpatialWeight = std::clamp(
                config.SpatialWeight, 0.0, 1.0e12);
            config.FeatureSensitivity = std::clamp(
                config.FeatureSensitivity, 0.0, 1.0e12);
            config.MaxSpatialIterations = std::clamp(
                config.MaxSpatialIterations, 1u, 100000u);
            config.MinimumRegionFaces = std::max(
                config.MinimumRegionFaces, 1u);
        }
        config.FeatureBaseRadiusRatio = std::clamp(
            config.FeatureBaseRadiusRatio, 1.0e-12, 1.0);
        config.HardDihedralThresholdDegrees = std::clamp(
            config.HardDihedralThresholdDegrees, 0.0, 180.0);
        if (!boundaryCurves)
        {
            config.PatchComplexityCost = std::clamp(
                config.PatchComplexityCost, 0.0, 1.0e12);
        }
        Curvature.SegmentationConfigDirty |= changed;

        const bool configCommandsAvailable =
            context.GeometryConfigCommandsAvailable;
        ImGui::BeginDisabled(
            !configCommandsAvailable ||
            !Curvature.SegmentationConfigDirty);
        if (ImGui::Button("Apply configuration##CurvatureSegmentation"))
        {
            Curvature.LastSegmentationConfigApply =
                Runtime::ApplyEditorCurvatureSegmentationConfig(
                    context.GeometryCommands,
                    config,
                    "sandbox.curvature_segmentation.panel");
            if (Curvature.LastSegmentationConfigApply->Succeeded())
                Curvature.SegmentationConfigDirty = false;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!configCommandsAvailable);
        if (ImGui::Button("Reload active##CurvatureSegmentation"))
        {
            Curvature.SegmentationConfigInitialized = false;
            Curvature.SegmentationConfigDirty = false;
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!configCommandsAvailable);
        if (ImGui::Button("Run segmentation##CurvatureSegmentation"))
        {
            Curvature.LastSegmentationConfigApply =
                Runtime::ApplyEditorCurvatureSegmentationConfig(
                    context.GeometryCommands,
                    config,
                    "sandbox.curvature_segmentation.panel.run");
            if (Curvature.LastSegmentationConfigApply->Succeeded())
            {
                Curvature.SegmentationConfigDirty = false;
                Curvature.LastSegmentationResult =
                    Runtime::
                        ApplyEditorConfiguredCurvatureSegmentationCommand(
                            context.GeometryCommands,
                            model.SelectedStableId);
                if (Curvature.LastSegmentationResult->Succeeded())
                {
                    Curvature.LastSegmentationStableEntityId =
                        model.SelectedStableId;
                    if (Curvature.AutoVisualizeSegmentation)
                    {
                        ShowCurvatureSegmentationVisualization(
                            context, model.SelectedStableId);
                    }
                }
                else
                    Curvature.LastSegmentationStableEntityId.reset();
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(
            !Curvature.LastSegmentationResult.has_value() ||
            !Curvature.LastSegmentationResult->Succeeded() ||
            !Curvature.LastSegmentationStableEntityId.has_value());
        if (ImGui::Button("Show result##CurvatureSegmentation"))
        {
            ShowCurvatureSegmentationVisualization(
                context,
                *Curvature.LastSegmentationStableEntityId);
        }
        ImGui::EndDisabled();

        if (Curvature.LastSegmentationConfigApply.has_value() &&
            !Curvature.LastSegmentationConfigApply->Succeeded())
        {
            ImGui::TextDisabled(
                "The configuration was rejected; inspect config diagnostics.");
        }
        if (!Curvature.LastSegmentationResult.has_value())
        {
            ImGui::TextDisabled("Last segmentation run: none");
            return;
        }

        const Runtime::EditorCurvatureSegmentationResult& result =
            *Curvature.LastSegmentationResult;
        ImGui::Text(
            "Last segmentation run: %s",
            Runtime::DebugNameForEditorCommandStatus(result.Status));
        ImGui::Text(
            "Method: requested=%s actual=%s",
            Runtime::DebugNameForCurvatureSegmentationMethod(
                result.RequestedMethod),
            Runtime::DebugNameForCurvatureSegmentationMethod(
                result.ActualMethod));
        if (result.BoundaryDiagnostics.has_value())
        {
            const auto& boundary = *result.BoundaryDiagnostics;
            ImGui::Text("Regions: %zu  boundaries: %zu", boundary.RegionCount, boundary.BoundaryCount);
            ImGui::Text("Boundary roles: hard=%zu soft=%zu closure=%zu",
                boundary.HardBoundaryCount, boundary.SoftBoundaryCount, boundary.ClosureBoundaryCount);
            ImGui::Text("Cleanup: %u merges, %u remaining small regions",
                boundary.AreaMerges, boundary.UnmergeableSmallRegions);
            ImGui::Text("Energy: optimized=%.6g, after cleanup=%.6g",
                boundary.OptimizedEnergy, boundary.FinalEnergy);
            ImGui::Text("Partition: %.3f ms", boundary.TotalMilliseconds);
        }
        else if (result.PatchDiagnostics.has_value() &&
            result.FeatureDiagnostics.has_value() &&
            result.PatchDiagnostics->Succeeded() &&
            result.FeatureDiagnostics->Succeeded())
        {
            const auto& patch = *result.PatchDiagnostics;
            const auto& feature = *result.FeatureDiagnostics;
            ImGui::Text(
                "Parts: %zu  GMM components: %u  seeds: %zu -> provisional: %zu",
                patch.FinalRegionCount,
                patch.SelectedComponentCount,
                patch.SeedCount,
                patch.ProvisionalRegionCount);
            ImGui::Text(
                "Features: hard=%zu soft=%zu  final boundaries=%zu",
                feature.HardFeatureEdgeCount,
                feature.RetainedSoftEdgeCount,
                patch.FinalBoundaryEdgeCount);
            ImGui::Text(
                "Boundary roles: hard=%zu soft=%zu closure=%zu",
                patch.HardBoundaryEdgeCount,
                patch.SoftBoundaryEdgeCount,
                patch.ClosureBoundaryEdgeCount);
            ImGui::Text(
                "Energy: %.6g -> %.6g  merges=%zu refinement moves=%zu",
                patch.InitialEnergy,
                patch.FinalEnergy,
                patch.AcceptedMergeCount,
                patch.AcceptedRefinementMoveCount);
        }
        else if (result.Diagnostics.Succeeded())
        {
            ImGui::Text(
                "Components: selected=%u active=%u  connected regions=%u",
                result.Diagnostics.SelectedComponentCount,
                result.Diagnostics.ActiveComponentCount,
                result.Diagnostics.ConnectedRegionCount);
            ImGui::Text(
                "Boundaries: %zu  changed property values: %zu",
                result.Diagnostics.BoundaryEdgeCount,
                result.ChangedValueCount);
            ImGui::Text(
                "GMM: %s after %u iterations  normalized RMS=%.6g",
                result.Diagnostics.GmmConverged
                    ? "converged"
                    : "iteration limit",
                result.Diagnostics.GmmIterations,
                result.Diagnostics.NormalizedRmsFit);
            ImGui::Text(
                "Spatial: %u iterations, %zu moves, %zu small-region merges",
                result.Diagnostics.SpatialIterations,
                result.Diagnostics.SpatialLabelMoves,
                result.Diagnostics.SmallRegionsMerged);
            ImGui::Text(
                "Energy: %.6g -> %.6g",
                result.Diagnostics.InitialEnergy,
                result.Diagnostics.FinalEnergy);
            if (ImGui::TreeNode("Model candidates"))
            {
                for (const auto& candidate :
                     result.Diagnostics.Candidates)
                {
                    ImGui::BulletText(
                        "k=%u%s  fit=%s  rms=%.6g  BIC=%.6g",
                        candidate.ComponentCount,
                        candidate.Selected ? " (selected)" : "",
                        candidate.FitSucceeded ? "ok" : "failed",
                        candidate.NormalizedRmsFit,
                        candidate.BayesianInformationCriterion);
                }
                ImGui::TreePop();
            }
        }
        if (!result.Message.empty())
            ImGui::TextWrapped("%s", result.Message.c_str());
        ImGui::TextDisabled(
            "Feature lines and boundaries are non-destructive edge properties, not UV seams.");
        if (ImGui::SmallButton("Dismiss##CurvatureSegmentation"))
            Curvature.LastSegmentationResult.reset();
    }

    void MeshProcessingPanels::Impl::DrawRemeshControls(
        const Runtime::EditorDomainWindowModel& model,
        const SandboxEditorContext& context)
    {
        const Runtime::EditorGeometryProcessingModel& processing =
            model.Processing;
        if (context.GeometryResults.LastMeshRemeshResult.has_value())
            Remesh.LastResult = *context.GeometryResults.LastMeshRemeshResult;
        ImGui::SeparatorText("Remesh");
        if (!processing.MeshRemeshAvailable)
        {
            ImGui::TextDisabled(
                "Mesh remesh is unavailable for this selection.");
            return;
        }

        Remesh.Mode = std::clamp(
            Remesh.Mode, 0,
            static_cast<std::int32_t>(kMeshRemeshModes.size() - 1u));
        Remesh.SizingLaw = std::clamp(
            Remesh.SizingLaw, 0,
            static_cast<std::int32_t>(kMeshRemeshSizingLaws.size() - 1u));
        Remesh.Iterations = std::clamp(Remesh.Iterations, 1, 64);
        Remesh.TargetEdgeLength =
            std::clamp(Remesh.TargetEdgeLength, 0.0f, 1.0e6f);

        const Runtime::EditorMeshRemeshMode mode =
            FromIndex(kMeshRemeshModes, Remesh.Mode);
        if (ImGui::BeginCombo(
                "Mode##MeshRemesh",
                Runtime::DebugNameForEditorMeshRemeshMode(mode)))
        {
            for (std::size_t i = 0u; i < kMeshRemeshModes.size(); ++i)
            {
                const auto option = kMeshRemeshModes[i];
                const bool available =
                    option == Runtime::EditorMeshRemeshMode::Uniform
                    ? processing.MeshRemeshUniformAvailable
                    : processing.MeshRemeshAdaptiveAvailable;
                if (!available)
                    ImGui::BeginDisabled();
                const bool selected =
                    Remesh.Mode == static_cast<std::int32_t>(i);
                if (ImGui::Selectable(
                        Runtime::DebugNameForEditorMeshRemeshMode(option),
                        selected))
                {
                    Remesh.Mode = static_cast<std::int32_t>(i);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
                if (!available)
                    ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        ImGui::DragInt(
            "Iterations##MeshRemesh", &Remesh.Iterations, 1.0f, 1, 64);
        ImGui::DragFloat(
            "Target edge length##MeshRemesh", &Remesh.TargetEdgeLength,
            0.01f, 0.0f, 1.0e6f);

        const bool adaptive =
            mode == Runtime::EditorMeshRemeshMode::Adaptive;
        if (!adaptive)
            ImGui::BeginDisabled();
        const Runtime::EditorMeshRemeshSizingLaw sizingLaw =
            FromIndex(kMeshRemeshSizingLaws, Remesh.SizingLaw);
        if (ImGui::BeginCombo(
                "Sizing law##MeshRemesh",
                Runtime::DebugNameForEditorMeshRemeshSizingLaw(
                    sizingLaw)))
        {
            for (std::size_t i = 0u; i < kMeshRemeshSizingLaws.size(); ++i)
            {
                const auto option = kMeshRemeshSizingLaws[i];
                const bool available =
                    option != Runtime::EditorMeshRemeshSizingLaw::
                                  ErrorBoundedTaubin ||
                    processing.MeshRemeshErrorBoundedSizingAvailable;
                if (!available)
                    ImGui::BeginDisabled();
                const bool selected =
                    Remesh.SizingLaw == static_cast<std::int32_t>(i);
                if (ImGui::Selectable(
                        Runtime::DebugNameForEditorMeshRemeshSizingLaw(
                            option),
                        selected))
                {
                    Remesh.SizingLaw = static_cast<std::int32_t>(i);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
                if (!available)
                    ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        if (!adaptive)
            ImGui::EndDisabled();

        if (!processing.MeshRemeshProjectToSurfaceAvailable)
            ImGui::BeginDisabled();
        ImGui::Checkbox(
            "Project to surface##MeshRemesh", &Remesh.ProjectToSurface);
        if (!processing.MeshRemeshProjectToSurfaceAvailable)
            ImGui::EndDisabled();

        const bool modeAvailable =
            mode == Runtime::EditorMeshRemeshMode::Uniform
            ? processing.MeshRemeshUniformAvailable
            : processing.MeshRemeshAdaptiveAvailable;
        const bool sizingAvailable =
            sizingLaw != Runtime::EditorMeshRemeshSizingLaw::
                             ErrorBoundedTaubin ||
            processing.MeshRemeshErrorBoundedSizingAvailable;
        const bool projectionAvailable =
            !Remesh.ProjectToSurface ||
            processing.MeshRemeshProjectToSurfaceAvailable;
        const bool canRun =
            modeAvailable && sizingAvailable && projectionAvailable;
        if (!canRun)
            ImGui::BeginDisabled();
        if (ImGui::Button("Remesh##MeshRemesh"))
        {
            PublishCommandResult(
                Remesh.LastResult,
                Runtime::ApplyEditorMeshRemeshCommand(
                    context.GeometryCommands,
                    Runtime::EditorMeshRemeshCommand{
                        .StableEntityId = model.SelectedStableId,
                        .Mode = mode,
                        .SizingLaw = sizingLaw,
                        .Iterations = static_cast<std::uint32_t>(
                            Remesh.Iterations),
                        .TargetEdgeLength = static_cast<double>(
                            Remesh.TargetEdgeLength),
                        .ProjectToSurface = Remesh.ProjectToSurface,
                    }),
                context.MethodResultSinks.MeshRemesh);
        }
        if (!canRun)
            ImGui::EndDisabled();

        const auto& result = Remesh.LastResult.has_value()
            ? Remesh.LastResult
            : processing.LastMeshRemeshResult;
        if (!result.has_value())
        {
            ImGui::TextDisabled("Last remesh run: none");
            return;
        }
        ImGui::Text(
            "Last remesh run: %s",
            Runtime::DebugNameForEditorCommandStatus(result->Status));
        ImGui::Text(
            "Mode: %s  sizing: %s",
            Runtime::DebugNameForEditorMeshRemeshMode(result->Mode),
            Runtime::DebugNameForEditorMeshRemeshSizingLaw(
                result->SizingLaw));
        // NoChange still means the operation executed, so its counters remain
        // relevant.
        if (result->Succeeded() ||
            result->Status == Runtime::EditorCommandStatus::NoChange)
        {
            ImGui::Text(
                "Vertices: %zu -> %zu  faces: %zu -> %zu",
                result->InputVertexCount, result->OutputVertexCount,
                result->InputFaceCount, result->OutputFaceCount);
            ImGui::Text(
                "Iterations: %u / %u  splits=%zu  collapses=%zu  flips=%zu",
                result->IterationsPerformed, result->IterationsRequested,
                result->SplitCount, result->CollapseCount, result->FlipCount);
        }
        if (!result->Message.empty())
            ImGui::TextWrapped("%s", result->Message.c_str());
        DrawDismissLastResultButton(
            "Dismiss##MeshRemesh",
            Remesh.LastResult,
            Runtime::EditorGeometryProcessingResultSlot::MeshRemesh,
            context);
    }

    void MeshProcessingPanels::Impl::DrawSubdivideControls(
        const Runtime::EditorDomainWindowModel& model,
        const SandboxEditorContext& context)
    {
        const Runtime::EditorGeometryProcessingModel& processing =
            model.Processing;
        if (context.GeometryResults.LastMeshSubdivideResult.has_value())
            Subdivide.LastResult = *context.GeometryResults.LastMeshSubdivideResult;
        ImGui::SeparatorText("Subdivide");
        if (!processing.MeshSubdivideAvailable)
        {
            ImGui::TextDisabled(
                "Mesh subdivision is unavailable for this selection.");
            return;
        }

        Subdivide.Operator = std::clamp(
            Subdivide.Operator, 0,
            static_cast<std::int32_t>(kMeshSubdivideOperators.size() - 1u));
        Subdivide.Iterations = std::clamp(Subdivide.Iterations, 1, 10);
        const Runtime::EditorMeshSubdivideOperator op =
            FromIndex(kMeshSubdivideOperators, Subdivide.Operator);
        if (ImGui::BeginCombo(
                "Operator##MeshSubdivide",
                Runtime::DebugNameForEditorMeshSubdivideOperator(op)))
        {
            for (std::size_t i = 0u; i < kMeshSubdivideOperators.size(); ++i)
            {
                const auto option = kMeshSubdivideOperators[i];
                const bool available =
                    option == Runtime::EditorMeshSubdivideOperator::Loop
                    ? processing.MeshSubdivideLoopAvailable
                    : option == Runtime::EditorMeshSubdivideOperator::
                                    CatmullClark
                          ? processing.MeshSubdivideCatmullClarkAvailable
                          : processing.MeshSubdivideSqrt3Available;
                if (!available)
                    ImGui::BeginDisabled();
                const bool selected =
                    Subdivide.Operator == static_cast<std::int32_t>(i);
                if (ImGui::Selectable(
                        Runtime::DebugNameForEditorMeshSubdivideOperator(
                            option),
                        selected))
                {
                    Subdivide.Operator = static_cast<std::int32_t>(i);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
                if (!available)
                    ImGui::EndDisabled();
            }
            ImGui::EndCombo();
        }
        ImGui::DragInt(
            "Iterations##MeshSubdivide", &Subdivide.Iterations,
            1.0f, 1, 10);
        const bool loop =
            op == Runtime::EditorMeshSubdivideOperator::Loop;
        if (!loop)
            Subdivide.PreserveLoopFeatures = false;
        const bool featureToggleAvailable =
            loop && processing.MeshSubdivideLoopFeatureEdgesAvailable;
        if (!featureToggleAvailable)
            ImGui::BeginDisabled();
        ImGui::Checkbox(
            "Preserve Loop features##MeshSubdivide",
            &Subdivide.PreserveLoopFeatures);
        if (!featureToggleAvailable)
            ImGui::EndDisabled();

        const bool operatorAvailable =
            op == Runtime::EditorMeshSubdivideOperator::Loop
            ? processing.MeshSubdivideLoopAvailable
            : op == Runtime::EditorMeshSubdivideOperator::CatmullClark
                  ? processing.MeshSubdivideCatmullClarkAvailable
                  : processing.MeshSubdivideSqrt3Available;
        const bool canRun =
            operatorAvailable &&
            (!Subdivide.PreserveLoopFeatures ||
             processing.MeshSubdivideLoopFeatureEdgesAvailable);
        if (!canRun)
            ImGui::BeginDisabled();
        if (ImGui::Button("Subdivide##MeshSubdivide"))
        {
            PublishCommandResult(
                Subdivide.LastResult,
                Runtime::ApplyEditorMeshSubdivideCommand(
                    context.GeometryCommands,
                    Runtime::EditorMeshSubdivideCommand{
                        .StableEntityId = model.SelectedStableId,
                        .Operator = op,
                        .Iterations = static_cast<std::uint32_t>(
                            Subdivide.Iterations),
                        .PreserveLoopFeatureEdges =
                            Subdivide.PreserveLoopFeatures,
                    }),
                context.MethodResultSinks.MeshSubdivide);
        }
        if (!canRun)
            ImGui::EndDisabled();

        const auto& result = Subdivide.LastResult.has_value()
            ? Subdivide.LastResult
            : processing.LastMeshSubdivideResult;
        if (!result.has_value())
        {
            ImGui::TextDisabled("Last subdivide run: none");
            return;
        }
        ImGui::Text(
            "Last subdivide run: %s",
            Runtime::DebugNameForEditorCommandStatus(result->Status));
        ImGui::Text(
            "Operator: %s",
            Runtime::DebugNameForEditorMeshSubdivideOperator(
                result->Operator));
        if (result->Succeeded())
        {
            ImGui::Text(
                "Vertices: %zu -> %zu  faces: %zu -> %zu",
                result->InputVertexCount, result->OutputVertexCount,
                result->InputFaceCount, result->OutputFaceCount);
            ImGui::Text(
                "Iterations: %u / %u  Loop features: %s",
                result->IterationsPerformed, result->IterationsRequested,
                result->PreserveLoopFeatureEdges ? "yes" : "no");
        }
        if (!result->Message.empty())
            ImGui::TextWrapped("%s", result->Message.c_str());
        DrawDismissLastResultButton(
            "Dismiss##MeshSubdivide",
            Subdivide.LastResult,
            Runtime::EditorGeometryProcessingResultSlot::MeshSubdivide,
            context);
    }

    void MeshProcessingPanels::Impl::DrawSimplifyControls(
        const Runtime::EditorDomainWindowModel& model,
        const SandboxEditorContext& context)
    {
        const Runtime::EditorGeometryProcessingModel& processing =
            model.Processing;
        if (context.GeometryResults.LastMeshSimplifyResult.has_value())
            Simplify.LastResult = *context.GeometryResults.LastMeshSimplifyResult;
        ImGui::SeparatorText("Simplify");
        if (!processing.MeshSimplifyAvailable)
        {
            ImGui::TextDisabled(
                "Mesh simplification is unavailable for this selection.");
            return;
        }

        Simplify.Metric = std::clamp(
            Simplify.Metric, 0,
            static_cast<std::int32_t>(kMeshSimplifyMetrics.size() - 1u));
        Simplify.TargetFaces = std::max(Simplify.TargetFaces, 0);
        Simplify.MaxError = std::max(Simplify.MaxError, 0.0f);
        const Runtime::EditorMeshSimplifyMetric metric =
            FromIndex(kMeshSimplifyMetrics, Simplify.Metric);
        if (ImGui::BeginCombo(
                "Metric##MeshSimplify",
                Runtime::DebugNameForEditorMeshSimplifyMetric(metric)))
        {
            for (std::size_t i = 0u; i < kMeshSimplifyMetrics.size(); ++i)
            {
                const bool selected =
                    Simplify.Metric == static_cast<std::int32_t>(i);
                if (ImGui::Selectable(
                        Runtime::DebugNameForEditorMeshSimplifyMetric(
                            kMeshSimplifyMetrics[i]),
                        selected))
                {
                    Simplify.Metric = static_cast<std::int32_t>(i);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::DragInt(
            "Target faces##MeshSimplify", &Simplify.TargetFaces,
            1.0f, 0, 1000000000);
        ImGui::DragFloat(
            "Max error (0 = unlimited)##MeshSimplify", &Simplify.MaxError,
            0.001f, 0.0f, 1.0e30f, "%.6g");
        ImGui::Checkbox(
            "Preserve boundary##MeshSimplify", &Simplify.PreserveBoundary);

        const bool faQem =
            metric == Runtime::EditorMeshSimplifyMetric::FA_QEM;
        if (!faQem)
            ImGui::BeginDisabled();
        if (ImGui::CollapsingHeader(
                "Feature-aware (FA-QEM) weights##MeshSimplify"))
        {
            ImGui::DragFloat(
                "Feature angle (deg)##MeshSimplify",
                &Simplify.FeatureAngleThresholdDegrees,
                0.5f, 0.0f, 180.0f, "%.1f");
            ImGui::DragFloat(
                "Normal weight##MeshSimplify", &Simplify.NormalWeight,
                0.01f, 0.0f, 1000.0f, "%.3f");
            ImGui::DragFloat(
                "Boundary weight##MeshSimplify", &Simplify.BoundaryWeight,
                0.01f, 0.0f, 1000.0f, "%.3f");
            ImGui::DragFloat(
                "Curvature weight##MeshSimplify", &Simplify.CurvatureWeight,
                0.01f, 0.0f, 1000.0f, "%.3f");
            ImGui::Checkbox(
                "Preserve sharp features##MeshSimplify",
                &Simplify.PreserveSharpFeatures);
            ImGui::Checkbox(
                "Preserve UV seams##MeshSimplify",
                &Simplify.PreserveUvSeams);
        }
        if (!faQem)
            ImGui::EndDisabled();

        const bool canRun =
            Simplify.TargetFaces > 0 || Simplify.MaxError > 0.0f;
        if (!canRun)
            ImGui::BeginDisabled();
        if (ImGui::Button("Simplify##MeshSimplify"))
        {
            PublishCommandResult(
                Simplify.LastResult,
                Runtime::ApplyEditorMeshSimplifyCommand(
                    context.GeometryCommands,
                    Runtime::EditorMeshSimplifyCommand{
                        .StableEntityId = model.SelectedStableId,
                        .Metric = metric,
                        .TargetFaces = static_cast<std::size_t>(
                            Simplify.TargetFaces),
                        .MaxError = static_cast<double>(Simplify.MaxError),
                        .PreserveBoundary = Simplify.PreserveBoundary,
                        .FeatureAngleThresholdDegrees = static_cast<double>(
                            Simplify.FeatureAngleThresholdDegrees),
                        .NormalWeight = static_cast<double>(
                            Simplify.NormalWeight),
                        .BoundaryWeight = static_cast<double>(
                            Simplify.BoundaryWeight),
                        .CurvatureWeight = static_cast<double>(
                            Simplify.CurvatureWeight),
                        .PreserveSharpFeatures =
                            Simplify.PreserveSharpFeatures,
                        .PreserveUvSeams = Simplify.PreserveUvSeams,
                    }),
                context.MethodResultSinks.MeshSimplify);
        }
        if (!canRun)
            ImGui::EndDisabled();

        const auto& result = Simplify.LastResult.has_value()
            ? Simplify.LastResult
            : processing.LastMeshSimplifyResult;
        if (!result.has_value())
        {
            ImGui::TextDisabled("Last simplify run: none");
            return;
        }
        ImGui::Text(
            "Last simplify run: %s",
            Runtime::DebugNameForEditorCommandStatus(result->Status));
        ImGui::Text(
            "Metric: %s",
            Runtime::DebugNameForEditorMeshSimplifyMetric(
                result->Metric));
        if (result->Succeeded() ||
            result->Status == Runtime::EditorCommandStatus::NoChange)
        {
            ImGui::Text(
                "Vertices: %zu -> %zu  faces: %zu -> %zu",
                result->InputVertexCount, result->OutputVertexCount,
                result->InputFaceCount, result->OutputFaceCount);
            ImGui::Text(
                "Collapses: %zu  max error: %.6g",
                result->CollapseCount, result->MaxCollapseError);
            ImGui::Text(
                "Rejected: topology %zu  quality %zu",
                result->CollapsesRejectedTopology,
                result->CollapsesRejectedQuality);
            ImGui::Text(
                "Pinned: sharp features %zu  UV seams %zu",
                result->SharpFeatureVerticesPinned,
                result->SeamVerticesPinned);
        }
        if (!result->Message.empty())
            ImGui::TextWrapped("%s", result->Message.c_str());
        DrawDismissLastResultButton(
            "Dismiss##MeshSimplify",
            Simplify.LastResult,
            Runtime::EditorGeometryProcessingResultSlot::MeshSimplify,
            context);
    }

    void MeshProcessingPanels::Impl::DrawNormalsWindow(bool &open, const SandboxEditorContext &context)
    {
        if (context.GeometryResults.LastNormalEstimationResult)
            Normals.LastResult = context.GeometryResults.LastNormalEstimationResult;
        ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Normal Estimation", &open))
        {
            ImGui::End();
            return;
        }
        const auto active = Runtime::GetEditorNormalEstimationConfig(context.GeometryCommands)
                                .value_or(Runtime::NormalEstimationConfig{});
        const auto serialized = Runtime::SerializeNormalEstimationConfig(active);
        if (serialized != Normals.LastApplied)
        {
            Normals.Draft = active;
            Normals.LastApplied = serialized;
            Normals.ConfigDiagnostic.clear();
        }
        auto &config = Normals.Draft;
        bool changed = false;
        const auto workspace =
            Runtime::BuildEditorWorkspaceSnapshot(context.SnapshotQueries, {.Hierarchy = true,
                                                                            .Inspector = false,
                                                                            .Selection = false,
                                                                            .Document = false,
                                                                            .SceneFile = false,
                                                                            .FileImport = false,
                                                                            .AssetImportQueue = false,
                                                                            .RenderGraph = false,
                                                                            .RenderRecipe = false,
                                                                            .CameraRender = false,
                                                                            .Visualization = false});
        if (context.Selection && !context.Selection->SelectedStableIds.empty() &&
            ImGui::Button("Use selected entity"))
        {
            config.StableEntityId = context.Selection->SelectedStableIds.front();
            changed = true;
        }
        std::string entityName =
            config.StableEntityId ? std::to_string(config.StableEntityId) : "Choose entity";
        for (const auto &row : workspace.Hierarchy)
            if (row.StableEntityId == config.StableEntityId)
                entityName = row.Name;
        if (ImGui::BeginCombo("Entity##Normals", entityName.c_str()))
        {
            for (const auto &row : workspace.Hierarchy)
            {
                if (Runtime::GetEditorNormalEstimationInputCatalog(context.GeometryCommands,
                                                                   row.StableEntityId)
                        .Entries.empty())
                    continue;
                const auto title = row.Name + " (" + std::to_string(row.StableEntityId) + ")";
                if (ImGui::Selectable(title.c_str(), row.StableEntityId == config.StableEntityId))
                {
                    config.StableEntityId = row.StableEntityId;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        const auto inputName =
            std::string(Runtime::ToString(config.Positions.Domain)) + ": " + config.Positions.Name;
        if (ImGui::BeginCombo("Positions##Normals", inputName.c_str()))
        {
            const auto catalog = Runtime::GetEditorNormalEstimationInputCatalog(context.GeometryCommands, config.StableEntityId);
            auto previousDomain = Runtime::GeometryElementDomain::Unknown;
            for (const auto &row : catalog.Entries)
            {
                if (row.Ref.Domain != previousDomain)
                {
                    ImGui::SeparatorText(std::string(Runtime::ToString(row.Ref.Domain)).c_str());
                    previousDomain = row.Ref.Domain;
                }
                const auto label = std::string(Runtime::ToString(row.Ref.Domain)) + ": " + row.Ref.Name +
                                   " (" + std::to_string(row.ElementCount) + ")";
                if (ImGui::Selectable(label.c_str(), row.Ref == config.Positions))
                {
                    config.Positions = row.Ref;
                    config.Output.Domain = row.Ref.Domain;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        std::array<char, 512> output{};
        std::copy_n(config.Output.Name.c_str(), std::min(config.Output.Name.size(), output.size() - 1),
                    output.data());
        if (ImGui::InputText("Output property##Normals", output.data(), output.size()))
        {
            config.Output.Name = output.data();
            changed = true;
        }
        if (ImGui::BeginCombo("Method##Normals", Runtime::ToString(config.Method)))
        {
            for (auto method : {Runtime::NormalEstimationMethod::PointSetPCA,
                                Runtime::NormalEstimationMethod::MeshFaceWeighted,
                                Runtime::NormalEstimationMethod::GraphNeighborhood})
            {
                auto candidate = config;
                candidate.Method = method;
                const auto readiness =
                    Runtime::PreviewEditorNormalEstimationCommand(context.GeometryCommands, candidate);
                ImGui::BeginDisabled(!readiness.Ready);
                if (ImGui::Selectable(Runtime::ToString(method), config.Method == method))
                {
                    config.Method = method;
                    changed = true;
                }
                ImGui::EndDisabled();
                if (!readiness.Ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip("%s", readiness.Diagnostic.c_str());
            }
            ImGui::EndCombo();
        }
        if (config.Method == Runtime::NormalEstimationMethod::PointSetPCA)
        {
            ImGui::TextWrapped(
                "PCA fits local planes to spatial neighbors on the selected element domain. Radius mode uses "
                "all neighbors within the radius; otherwise k nearest neighbors are used.");
            int backend = int(config.Backend);
            if (ImGui::Combo("Neighbors##Normals", &backend, "CPU KD-tree\0CPU LBVH (cached)\0"))
            {
                config.Backend = Runtime::NormalEstimationBackend(backend);
                changed = true;
            }
            changed |= ImGui::Checkbox("Use radius##Normals", &config.UseRadiusSearch);
            if (config.UseRadiusSearch)
                changed |= ImGui::InputFloat("Radius##Normals", &config.Radius);
            else
                changed |= ImGui::InputScalar("Neighbors k##Normals", ImGuiDataType_U32, &config.KNeighbors);
            changed |=
                ImGui::InputScalar("Minimum neighbors##Normals", ImGuiDataType_U32, &config.MinimumNeighbors);
            int orientation = int(config.Orientation);
            if (ImGui::Combo("Orientation##Normals", &orientation, "Unoriented\0Minimum spanning tree\0"))
            {
                config.Orientation = decltype(config.Orientation)(orientation);
                changed = true;
            }
        }
        else if (config.Method == Runtime::NormalEstimationMethod::MeshFaceWeighted)
        {
            ImGui::TextWrapped("Average incident polygon face normals using mesh topology and the selected "
                               "vertex positions.");
            int weighting = int(config.Weighting);
            if (ImGui::Combo("Weighting##Normals", &weighting, "Uniform\0Area\0Angle\0Area and angle\0Max\0"))
            {
                config.Weighting = decltype(config.Weighting)(weighting);
                changed = true;
            }
        }
        else
        {
            ImGui::TextWrapped("Fit normals from incident edge neighbors. Compatible mesh adjacency is "
                               "accepted as well as graph adjacency.");
            changed |= ImGui::Checkbox("Orient toward fallback##Normals", &config.OrientTowardFallback);
        }
        changed |= ImGui::InputFloat3("Fallback normal##Normals", &config.FallbackNormal.x);
        if (ImGui::TreeNode("Numerical tolerances##Normals"))
        {
            changed |= ImGui::InputDouble("Degenerate length epsilon##Normals",
                                          &config.DegenerateNormalLengthEpsilon, 0, 0, "%.8g");
            changed |= ImGui::InputDouble("Collinear eigenvalue ratio##Normals",
                                          &config.CollinearEigenvalueRatioEpsilon, 0, 0, "%.8g");
            ImGui::TreePop();
        }
        if (changed)
        {
            const auto applied = Runtime::ApplyEditorNormalEstimationConfig(context.GeometryCommands, config);
            Normals.ConfigDiagnostic =
                applied.Succeeded() ? "" : "Controls were rejected by normal config validation.";
        }
        if (!Normals.ConfigDiagnostic.empty())
            ImGui::TextWrapped("%s", Normals.ConfigDiagnostic.c_str());
        const auto readiness =
            Runtime::PreviewEditorNormalEstimationCommand(context.GeometryCommands, config);
        if (!readiness.Ready)
            ImGui::TextWrapped("%s", readiness.Diagnostic.c_str());
        ImGui::BeginDisabled(!context.GeometryConfigCommandsAvailable || !readiness.Ready ||
                             !Normals.ConfigDiagnostic.empty());
        if (ImGui::Button("Estimate normals"))
            PublishCommandResult(Normals.LastResult,
                                 Runtime::ApplyEditorConfiguredNormalEstimation(context.GeometryCommands),
                                 context.MethodResultSinks.NormalEstimation);
        ImGui::SameLine();
        if (ImGui::Button("Show normal vectors"))
        {
            const auto status = Runtime::ApplyEditorVisualizationRecipeCommand(
                context.VisualizationCommands,
                {.StableEntityId = config.StableEntityId,
                 .Recipe = {.Data = Runtime::VectorFieldVisualizationRecipe{
                                .Source = readiness.Resolved.Output,
                                .PositionSource = readiness.Resolved.Positions,
                                .OutputName = readiness.Resolved.Output.Name + ".vectors"}}});
            Normals.VisualizationDiagnostic = Runtime::DebugNameForEditorCommandStatus(status);
        }
        ImGui::EndDisabled();
        if (!Normals.VisualizationDiagnostic.empty())
            ImGui::Text("Vector display: %s", Normals.VisualizationDiagnostic.c_str());
        if (Normals.LastResult)
        {
            const auto &result = *Normals.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s", Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Method: %s", Runtime::ToString(result.Method));
            if (result.Method == Runtime::NormalEstimationMethod::PointSetPCA)
                ImGui::Text("Requested: %s", Runtime::ToString(result.RequestedBackend));
            if (!result.ActualBackend.empty())
                ImGui::Text("Ran: %s", result.ActualBackend.c_str());
            ImGui::Text("Live / total: %zu / %zu; valid: %zu; fallback: %zu", result.LiveCount,
                        result.SlotCount, result.ValidCount, result.FallbackCount);
            ImGui::Text("Written: %zu; changed: %zu; cached index reused: %s", result.WrittenCount,
                        result.ChangedCount, result.IndexReused ? "yes" : "no");
            ImGui::TextWrapped("%s", result.Message.c_str());
            DrawDismissLastResultButton("Dismiss##Normals", Normals.LastResult,
                                        Runtime::EditorGeometryProcessingResultSlot::NormalEstimation,
                                        context);
        }
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawRegistrationWindow(
        bool& open, const SandboxEditorContext& context)
    {
        if (context.GeometryResults.LastRegistrationResult.has_value())
            Registration.LastResult = *context.GeometryResults.LastRegistrationResult;
        ImGui::SetNextWindowSize(
            ImVec2(360.0f, 320.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("ICP Registration", &open))
        {
            ImGui::End();
            return;
        }

        ImGui::TextWrapped("Align named point samples from mesh, graph or point-cloud domains. Applies an undoable transform to the source entity.");
        const auto activeConfig = Runtime::GetEditorRegistrationConfig(context.GeometryCommands).value_or(Runtime::RegistrationConfig{});
        const auto activeText = Runtime::SerializeRegistrationConfig(activeConfig);
        if (activeText != Registration.LastApplied)
        { Registration.Draft = activeConfig; Registration.LastApplied = activeText; Registration.ConfigDiagnostic.clear(); }
        auto& config = Registration.Draft;
        bool changed = false;
        const auto workspace = Runtime::BuildEditorWorkspaceSnapshot(context.SnapshotQueries,
            {.Hierarchy = true, .Inspector = false, .Selection = false, .Document = false,
             .SceneFile = false, .FileImport = false, .AssetImportQueue = false,
             .RenderGraph = false, .RenderRecipe = false, .CameraRender = false, .Visualization = false});
        auto entityChoice = [&](const char* label, std::uint32_t& id) {
            std::string preview = id ? std::to_string(id) : "Choose entity";
            for (const auto& row : workspace.Hierarchy)
                if (row.StableEntityId == id) preview = row.Name + " (" + std::to_string(id) + ")";
            if (ImGui::BeginCombo(label, preview.c_str()))
            {
                for (const auto& row : workspace.Hierarchy)
                {
                    const auto catalog = Runtime::GetEditorRegistrationInputCatalog(context.GeometryCommands, row.StableEntityId);
                    if (catalog.Entries.empty()) continue;
                    const auto title = row.Name + " (" + std::to_string(row.StableEntityId) + ")";
                    if (ImGui::Selectable(title.c_str(), id == row.StableEntityId)) { id = row.StableEntityId; changed = true; }
                }
                ImGui::EndCombo();
            }
        };
        if (context.Selection && context.Selection->SelectedStableIds.size() >= 2 &&
            ImGui::Button("Use two selected entities"))
        {
            config.SourceStableEntityId = context.Selection->SelectedStableIds[0];
            config.TargetStableEntityId = context.Selection->SelectedStableIds[1];
            changed = true;
        }
        entityChoice("Source##ICP", config.SourceStableEntityId);
        entityChoice("Target##ICP", config.TargetStableEntityId);
        if (ImGui::Button("Swap source and target"))
        {
            std::swap(config.SourceStableEntityId, config.TargetStableEntityId);
            std::swap(config.SourcePositions, config.TargetPositions);
            changed = true;
        }
        const auto sourceCatalog = Runtime::GetEditorRegistrationInputCatalog(context.GeometryCommands, config.SourceStableEntityId);
        const auto targetCatalog = Runtime::GetEditorRegistrationInputCatalog(context.GeometryCommands, config.TargetStableEntityId);
        auto propertyChoice = [&](const char* label, const auto& catalog, Runtime::GeometryPropertyRef& ref,
                                  Runtime::GeometryElementDomain required = Runtime::GeometryElementDomain::Unknown) {
            const auto preview = std::string(Runtime::ToString(ref.Domain)) + ": " + ref.Name;
            if (ImGui::BeginCombo(label, preview.c_str()))
            {
                for (const auto& row : catalog.Entries)
                {
                    if (required != Runtime::GeometryElementDomain::Unknown && row.Ref.Domain != required) continue;
                    const auto title = std::string(Runtime::ToString(row.Ref.Domain)) + ": " + row.Ref.Name +
                        " (" + std::to_string(row.ElementCount) + ")";
                    if (ImGui::Selectable(title.c_str(), row.Ref == ref)) { ref = row.Ref; changed = true; }
                }
                ImGui::EndCombo();
            }
        };
        propertyChoice("Source positions##ICP", sourceCatalog, config.SourcePositions);
        propertyChoice("Target positions##ICP", targetCatalog, config.TargetPositions);
        int variant = int(config.Variant);
        if (ImGui::Combo("Variant##ICP", &variant, "Point to point\0Point to plane\0"))
        { config.Variant = Runtime::EditorICPVariant(variant); changed = true; }
        if (config.Variant == Runtime::EditorICPVariant::PointToPlane)
            propertyChoice("Target normals##ICP", targetCatalog, config.TargetNormals, config.TargetPositions.Domain);
        int backend = int(config.Backend);
        if (ImGui::Combo("Correspondences##ICP", &backend, "CPU KD-tree (reference)\0CPU LBVH (cached)\0Vulkan LBVH (CPU solve)\0"))
        { config.Backend = Runtime::RegistrationBackend(backend); changed = true; }
        changed |= ImGui::InputScalar("Max iterations##ICP", ImGuiDataType_U32, &config.MaxIterations);
        changed |= ImGui::InputDouble("Max distance (0 = 1e6)##ICP", &config.MaxCorrespondenceDistance);
        changed |= ImGui::InputDouble("Inlier ratio##ICP", &config.InlierRatio);
        changed |= ImGui::InputDouble("Convergence threshold##ICP", &config.ConvergenceThreshold, 0, 0, "%.8g");
        bool applyTrajectory = false;
        std::uint32_t step = std::uint32_t(config.TrajectoryStep);
        if (ImGui::InputScalar("Apply trajectory step (0 = start)##ICP", ImGuiDataType_U32, &step))
        { config.TrajectoryStep = step; changed = true; }
        applyTrajectory = ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Button("Use final pose"))
        { config.TrajectoryStep = config.MaxIterations; changed = true; }
        if (changed)
        {
            const auto applied = Runtime::ApplyEditorRegistrationConfig(context.GeometryCommands, config);
            Registration.ConfigDiagnostic = applied.Status == Runtime::RuntimeEngineConfigApplyStatus::Rejected
                ? "Registration controls were rejected by config validation." : "";
        }
        if (!Registration.ConfigDiagnostic.empty()) ImGui::TextWrapped("%s", Registration.ConfigDiagnostic.c_str());
        const auto readiness = Runtime::PreviewEditorRegistrationCommand(context.GeometryCommands, config);
        if (!readiness.Ready) ImGui::TextWrapped("%s", readiness.Diagnostic.c_str());
        ImGui::BeginDisabled(!context.GeometryConfigCommandsAvailable || !Registration.ConfigDiagnostic.empty() || !readiness.Ready);
        const bool runFinal = ImGui::Button("Run ICP##ICP");
        if (runFinal)
        {
            config.TrajectoryStep = config.MaxIterations;
            (void)Runtime::ApplyEditorRegistrationConfig(context.GeometryCommands, config);
        }
        if (runFinal || (applyTrajectory && readiness.Ready && Registration.ConfigDiagnostic.empty()))
            PublishCommandResult(Registration.LastResult,
                Runtime::ApplyEditorConfiguredRegistrationCommand(context.GeometryCommands), context.MethodResultSinks.Registration);
        ImGui::EndDisabled();

        if (!Registration.LastResult.has_value())
        {
            ImGui::TextDisabled("Last ICP run: none");
        }
        else
        {
            const Runtime::EditorRegistrationResult& result =
                *Registration.LastResult;
            ImGui::Text(
                "Last ICP run: %s",
                Runtime::DebugNameForEditorCommandStatus(
                    result.Status));
            // Accepted requests expose both requested and effective variants;
            // keeping both visible makes any runtime-contract mismatch obvious.
            ImGui::Text(
                "Variant: %s (ran %s)",
                Runtime::DebugNameForEditorICPVariant(result.Variant),
                Runtime::DebugNameForEditorICPVariant(result.EffectiveVariant));
            if (result.HasResult)
                ImGui::Text("Correspondences: %s (ran %s)", Runtime::ToString(result.RequestedBackend), Runtime::ToString(result.ActualBackend));
            else
                ImGui::Text("Requested correspondences: %s", Runtime::ToString(result.RequestedBackend));
            ImGui::Text("Target index: %s", result.TargetIndexReused ? "reused" : "new / reference");
            if (!result.BackendDiagnostic.empty()) ImGui::TextWrapped("%s", result.BackendDiagnostic.c_str());
            if (result.Succeeded() && result.HasResult)
            {
                ImGui::Text(
                    "Points: %zu -> %zu  target normals: %zu",
                    result.SourcePointCount, result.TargetPointCount,
                    result.TargetNormalCount);
                ImGui::Text(
                    "Iterations: %zu  final RMSE: %.6g  converged: %s",
                    result.IterationsPerformed, result.FinalRMSE,
                    result.Converged ? "yes" : "no");
                ImGui::Text(
                    "Trajectory step: %zu / %zu  inliers: %zu",
                    result.AppliedStep, result.TrajectoryLength,
                    result.FinalInlierCount);
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
            DrawDismissLastResultButton(
                "Dismiss##Registration",
                Registration.LastResult,
                Runtime::EditorGeometryProcessingResultSlot::Registration,
                context);
        }
        ImGui::End();
    }

    MeshProcessingPanels::MeshProcessingPanels()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    MeshProcessingPanels::~MeshProcessingPanels()
    {
        m_Impl->Unregister();
    }

    void MeshProcessingPanels::Register(EditorShell& editorShell)
    {
        m_Impl->Register(editorShell);
    }

    void MeshProcessingPanels::Unregister()
    {
        m_Impl->Unregister();
    }

}

namespace Extrinsic::Sandbox::Editor
{
    void MeshProcessingPanels::Impl::DrawGeodesicsWindow(bool& open,
                                                         const SandboxEditorContext& context)
    {
        DrawDomainWindow(open, context, Runtime::EditorDomainWindowKind::Mesh,
                         "Mesh / Geodesics / Virtual Source Propagation",
                         &Impl::DrawGeodesicsControls);
    }
    void MeshProcessingPanels::Impl::DrawGeodesicsControls(
        const Runtime::EditorDomainWindowModel& model, const SandboxEditorContext& context)
    {
        if (!GeodesicsDirty)
        {
            if (auto config = Runtime::GetEditorGeodesicsConfig(context.GeometryCommands))
            {
                GeodesicsConfig = *config;
                GeodesicsInitialized = true;
            }
        }
        if (!GeodesicsInitialized)
        {
            ImGui::TextDisabled("Geodesics configuration is unavailable.");
            return;
        }
        ImGui::TextWrapped(
            "Approximate surface distance from source vertices. Pick a vertex and add "
            "it, or enter its index below.");
        if (model.Primitive.HasVertexId && ImGui::Button("Add picked vertex"))
        {
            GeodesicsConfig.SourceVertices.push_back(model.Primitive.Primitive.VertexId);
            GeodesicsDirty = true;
        }
        const auto selected = Runtime::ReadEditorPrimitiveSelection(
            context.GeometryCommands, model.SelectedStableId, Runtime::GeometryElementDomain::MeshVertex);
        ImGui::BeginDisabled(!selected.Usable() || selected.Indices.empty());
        if (ImGui::Button("Use selected vertices as sources"))
        {
            GeodesicsConfig.SourceVertices = selected.Indices;
            GeodesicsDirty = true;
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Select vertices in Mesh / Selection, then copy them here.");
        ImGui::InputInt("Source vertex", &GeodesicsSourceVertex);
        if (ImGui::Button("Add source") && GeodesicsSourceVertex >= 0)
        {
            GeodesicsConfig.SourceVertices.push_back(
                static_cast<std::uint32_t>(GeodesicsSourceVertex));
            GeodesicsDirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear sources"))
        {
            GeodesicsConfig.SourceVertices.clear();
            GeodesicsDirty = true;
        }
        if (GeodesicsDirty)
        {
            auto& vertices = GeodesicsConfig.SourceVertices;
            std::sort(vertices.begin(), vertices.end());
            vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());
        }
        std::string sourceText = "Sources:";
        for (auto vertex : GeodesicsConfig.SourceVertices)
            sourceText += " " + std::to_string(vertex);
        ImGui::TextWrapped("%s", sourceText.c_str());
        if (ImGui::BeginCombo("Position property", GeodesicsConfig.PositionProperty.c_str()))
        {
            for (const auto& row : model.PropertyCatalog.Rows)
            {
                if (row.Domain != Runtime::EditorPropertyCatalogDomain::MeshVertices ||
                    row.ValueKind != decltype(row.ValueKind)::Vec3 || row.Internal)
                    continue;
                if (ImGui::Selectable(row.Name.c_str(),
                                      row.Name == GeodesicsConfig.PositionProperty))
                {
                    GeodesicsConfig.PositionProperty = row.Name;
                    GeodesicsDirty = true;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::InputScalar("Expansion budget", ImGuiDataType_U32,
                               &GeodesicsConfig.MaxHalfedgeExpansions))
            GeodesicsDirty = true;
        if (ImGui::Button("Compute geodesics"))
        {
            const auto applied =
                Runtime::ApplyEditorGeodesicsConfig(context.GeometryCommands, GeodesicsConfig);
            if (applied.Succeeded())
            {
                GeodesicsDirty = false;
                GeodesicsResult = Runtime::ApplyEditorConfiguredGeodesicsCommand(
                    context.GeometryCommands, model.SelectedStableId);
                GeodesicsMessage = GeodesicsResult->Message;
                if (GeodesicsResult->Succeeded())
                {
                    const auto status = ShowCurvatureScalarVisualization(
                        context, model.SelectedStableId, "v:geodesic_distance");
                    if (status != Runtime::EditorCommandStatus::Applied &&
                        status != Runtime::EditorCommandStatus::NoChange)
                        GeodesicsMessage += " Distance display could not be enabled.";
                }
            }
            else
                GeodesicsMessage = "Geodesics config was rejected; check source indices, position "
                                   "property, and expansion budget.";
        }
        ImGui::TextWrapped("%s", GeodesicsMessage.c_str());
        if (GeodesicsResult)
        {
            const auto& diagnostics = GeodesicsResult->Diagnostics;
            ImGui::Text("Sources: %zu | Expansions: %zu | Triangle updates: %zu",
                        diagnostics.SourceCount, diagnostics.HalfedgeExpansions,
                        diagnostics.TriangleUpdates);
            ImGui::Text("Unreachable vertices: %zu", diagnostics.UnreachableVertexCount);
        }
    }
}
