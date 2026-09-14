module;
#include <functional>
#include <span>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>

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

import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.VisualizationRecipes;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.RenderRecipeEditingOperations;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.PointCloudConsolidationTypes;

#include "Sandbox.PanelSupport.hpp"

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

        [[nodiscard]] const char* MeshDenoiseStageName(
            const Runtime::EditorMeshDenoiseStage stage) noexcept
        {
            return stage ==
                    Runtime::EditorMeshDenoiseStage::FullBilateral
                ? "Full bilateral"
                : "Unknown";
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

        template <typename State, typename Preview, typename Apply, typename Execute, typename Sink>
        void DrawProcessingExecution(const SandboxEditorContext& context, State& state, bool changed,
            Preview preview, Apply apply, Execute execute, const Sink& sink,
            const char* button, const char* controlsRejected, const char* executionRejected)
        {
            if (changed)
                state.ConfigDiagnostic = apply(state.Draft).Succeeded() ? "" : controlsRejected;
            if (!state.ConfigDiagnostic.empty()) ImGui::TextWrapped("%s", state.ConfigDiagnostic.c_str());
            const auto readiness = preview(state.Draft);
            if (!readiness.Ready) ImGui::TextWrapped("%s", readiness.Diagnostic.c_str());
            ImGui::BeginDisabled(!context.ProcessingConfigCommandsAvailable || !readiness.Ready ||
                                 !state.ConfigDiagnostic.empty());
            if (ImGui::Button(button))
            {
                if (apply(state.Draft).Succeeded())
                    PublishCommandResult(state.LastResult, execute(), sink);
                else state.ConfigDiagnostic = executionRejected;
            }
            ImGui::EndDisabled();
        }

        void ShowCurvatureSegmentationVisualization(
            const SandboxEditorContext& context,
            const std::uint32_t stableEntityId,
            const Runtime::CurvatureSegmentationConfig& config)
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
                        config.RegionColors.Name,
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
                        config.FeatureColors.Name,
                });
        }


    }

    struct MeshProcessingPanels::Impl
    {
        struct DenoiseState
        {
            ProcessingEntityInput Input{};
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
            Runtime::MeshCurvatureConfig Config{};
            bool Initialized{false};
            bool Dirty{false};
            std::optional<std::vector<std::uint32_t>> LastSelectedEntity{};
            std::optional<Runtime::EditorMeshCurvatureResult> LastResult{};
            std::string ConfigDiagnostic{}, VisualizationDiagnostic{};
        };
        struct SegmentationState
        {
            ProcessingEntityInput Input{};
            Runtime::CurvatureSegmentationConfig Config{};
            std::optional<Runtime::EditorCurvatureSegmentationResult> LastResult{};
            std::optional<Runtime::RuntimeEngineConfigApplyResult> LastConfigApply{};
            bool Initialized{false};
            bool Dirty{false};
            bool AutoVisualize{true};
            std::string VisualizationDiagnostic{};
        };

        struct RemeshState
        {
            ProcessingEntityInput Input{};
            std::optional<Runtime::EditorMeshRemeshResult> LastResult{};
            std::int32_t Mode{0};
            std::int32_t SizingLaw{0};
            std::int32_t Iterations{1};
            float TargetEdgeLength{0.0f};
            bool ProjectToSurface{false};
        };

        struct SubdivideState
        {
            ProcessingEntityInput Input{};
            std::optional<Runtime::EditorMeshSubdivideResult> LastResult{};
            std::int32_t Operator{0};
            std::int32_t Iterations{1};
            bool PreserveLoopFeatures{false};
        };

        struct SimplifyState
        {
            ProcessingEntityInput Input{};
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

        struct NormalsState : ProcessingDraftState<Runtime::NormalEstimationConfig, Runtime::EditorNormalEstimationResult>
        {
            bool OpenFacePreset{false};
        };

        using OutliersState = ProcessingDraftState<Runtime::OutlierAnalysisConfig, Runtime::EditorOutlierAnalysisResult>;
        using KeypointsState = ProcessingDraftState<Runtime::KeypointAnalysisConfig, Runtime::EditorKeypointAnalysisResult>;
        struct DescriptorsState : ProcessingDraftState<Runtime::DescriptorAnalysisConfig, Runtime::EditorDescriptorAnalysisResult>
        {
            std::array<char,256> Prefix{"fpfh"};
            int DisplayBin{};
            bool FollowDisplayBin{false};
        };
        using DensityState = ProcessingDraftState<Runtime::KernelDensityConfig, Runtime::EditorKernelDensityResult>;
        using DensityWeightsState = ProcessingDraftState<Runtime::DensityWeightConfig, Runtime::EditorDensityWeightResult>;
        using ConstructionState = ProcessingDraftState<Runtime::PointConstructionConfig, Runtime::EditorPointConstructionResult>;
        using SpacingState = ProcessingDraftState<Runtime::PointSpacingConfig, Runtime::EditorPointSpacingResult>;
        using BilateralState = ProcessingDraftState<Runtime::BilateralFilterConfig, Runtime::EditorBilateralFilterResult>;

        struct RegistrationState
        {
            std::optional<std::vector<std::uint32_t>> LastSelectedSource{}, LastSelectedTarget{};
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
        SegmentationState Segmentation{};
        ProcessingEntityInput GeodesicsInput{};
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
        OutliersState Outliers{};
        KeypointsState Keypoints{};
        DescriptorsState Descriptors{};
        DensityState Density{};
        DensityWeightsState DensityWeights{};
        ConstructionState Construction{};
        SpacingState Spacing{};
        BilateralState Bilateral{};

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
            Runtime::EditorDomainWindowKind kind,
            std::optional<std::uint32_t> entity = std::nullopt);
        void DrawDomainWindow(
            bool& open,
            const SandboxEditorContext& context,
            Runtime::EditorDomainWindowKind kind,
            const char* title,
            ProcessingEntityInput& input, DrawDomainControls draw);

        void DrawDenoiseWindow(bool&, const SandboxEditorContext&);
        void DrawCurvatureWindow(bool&, const SandboxEditorContext&);
        void DrawSegmentationWindow(bool&, const SandboxEditorContext&);
        void DrawGeodesicsWindow(bool&, const SandboxEditorContext&);
        void DrawGeodesicsControls(const Runtime::EditorDomainWindowModel&,
                                   const SandboxEditorContext&);
        void DrawRemeshWindow(bool&, const SandboxEditorContext&);
        void DrawSubdivideWindow(bool&, const SandboxEditorContext&);
        void DrawSimplifyWindow(bool&, const SandboxEditorContext&);
        void DrawNormalsWindow(bool&, const SandboxEditorContext&);
        void DrawOutliersWindow(bool&, const SandboxEditorContext&);
        void DrawKeypointsWindow(bool&, const SandboxEditorContext&);
        void DrawDescriptorsWindow(bool&, const SandboxEditorContext&);
        void DrawDensityWindow(bool&, const SandboxEditorContext&);
        void DrawDensityWeightsWindow(bool&, const SandboxEditorContext&);
        void DrawConstructionWindow(bool&, const SandboxEditorContext&);
        void DrawSpacingWindow(bool&, const SandboxEditorContext&);
        void DrawBilateralWindow(bool&, const SandboxEditorContext&);
        void DrawRegistrationWindow(bool&, const SandboxEditorContext&);

        void DrawDenoiseControls(
            const Runtime::EditorDomainWindowModel&,
            const SandboxEditorContext&);
        void DrawCurvatureControls(const SandboxEditorContext&);
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
        RegisterWindow("mesh.processing.segmentation", {"Mesh", "Processing"},
                       "Curvature Segmentation", &Impl::DrawSegmentationWindow);
        RegisterWindow("mesh.processing.remesh", {"Mesh", "Processing"},
                       "Remesh", &Impl::DrawRemeshWindow);
        RegisterWindow("mesh.processing.subdivide", {"Mesh", "Processing"},
                       "Subdivide", &Impl::DrawSubdivideWindow);
        RegisterWindow("mesh.processing.simplify", {"Mesh", "Processing"},
                       "Simplify", &Impl::DrawSimplifyWindow);
        RegisterWindow("view.outlier_analysis", {"View"}, "Outlier Analysis", &Impl::DrawOutliersWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.outliers", "Mesh"}, {"graph.processing.outliers", "Graph"},
                  {"pointcloud.processing.remove_outliers", "PointCloud"}}})
            Handles.push_back(Shell->RegisterEditorWindow({
                .Id=id, .MenuPath={domain,"Processing"}, .Title="Outlier Analysis",
                .Draw=[](bool& open,const SandboxEditorContext&){open=false;},
                .OpenStateChanged=[this,id](bool open){
                    if(!open)return;
                    (void)Shell->SetEditorWindowOpen("view.outlier_analysis",true);
                    (void)Shell->SetEditorWindowOpen(id,false);
                }}));
        RegisterWindow("view.keypoint_analysis", {"View"}, "ISS Keypoint Analysis", &Impl::DrawKeypointsWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.keypoints", "Mesh"}, {"graph.processing.keypoints", "Graph"},
                  {"pointcloud.processing.keypoints", "PointCloud"}}})
            Handles.push_back(Shell->RegisterEditorWindow({
                .Id=id, .MenuPath={domain,"Processing"}, .Title="ISS Keypoint Analysis",
                .Draw=[](bool& open,const SandboxEditorContext&){open=false;},
                .OpenStateChanged=[this,id](bool open){
                    if(!open)return;
                    (void)Shell->SetEditorWindowOpen("view.keypoint_analysis",true);
                    (void)Shell->SetEditorWindowOpen(id,false);
                }}));
        RegisterWindow("view.descriptor_analysis", {"View"}, "FPFH Descriptor Analysis", &Impl::DrawDescriptorsWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.descriptors", "Mesh"}, {"graph.processing.descriptors", "Graph"},
                  {"pointcloud.processing.descriptors", "PointCloud"}}})
            Handles.push_back(Shell->RegisterEditorWindow({
                .Id=id, .MenuPath={domain,"Processing"}, .Title="FPFH Descriptor Analysis",
                .Draw=[](bool& open,const SandboxEditorContext&){open=false;},
                .OpenStateChanged=[this,id](bool open){
                    if(!open)return;
                    (void)Shell->SetEditorWindowOpen("view.descriptor_analysis",true);
                    (void)Shell->SetEditorWindowOpen(id,false);
                }}));
        RegisterWindow("view.kernel_density", {"View"}, "Kernel Density", &Impl::DrawDensityWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.kernel_density", "Mesh"}, {"graph.processing.kernel_density", "Graph"},
                  {"pointcloud.processing.kernel_density", "PointCloud"}}})
            Handles.push_back(Shell->RegisterEditorWindow({
                .Id=id, .MenuPath={domain,"Processing"}, .Title="Kernel Density",
                .Draw=[](bool& open,const SandboxEditorContext&){open=false;},
                .OpenStateChanged=[this,id](bool open){
                    if(!open)return;
                    (void)Shell->SetEditorWindowOpen("view.kernel_density",true);
                    (void)Shell->SetEditorWindowOpen(id,false);
                }}));
        RegisterWindow("view.density_weights", {"View"}, "Compact Density Weights", &Impl::DrawDensityWeightsWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.density_weights", "Mesh"}, {"graph.processing.density_weights", "Graph"},
                  {"pointcloud.processing.density_weights", "PointCloud"}}})
            Handles.push_back(Shell->RegisterEditorWindow({
                .Id=id, .MenuPath={domain,"Processing"}, .Title="Compact Density Weights",
                .Draw=[](bool& open,const SandboxEditorContext&){open=false;},
                .OpenStateChanged=[this,id](bool open){
                    if(!open)return;
                    (void)Shell->SetEditorWindowOpen("view.density_weights",true);
                    (void)Shell->SetEditorWindowOpen(id,false);
                }}));
        RegisterWindow("view.point_construction", {"View"}, "Construct from Points", &Impl::DrawConstructionWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.point_construction", "Mesh"}, {"graph.processing.point_construction", "Graph"},
                  {"pointcloud.processing.point_construction", "PointCloud"}}})
            Handles.push_back(Shell->RegisterEditorWindow({
                .Id=id, .MenuPath={domain,"Processing"}, .Title="Construct from Points",
                .Draw=[](bool& open,const SandboxEditorContext&){open=false;},
                .OpenStateChanged=[this,id](bool open){
                    if(!open)return;
                    (void)Shell->SetEditorWindowOpen("view.point_construction",true);
                    (void)Shell->SetEditorWindowOpen(id,false);
                }}));
        RegisterWindow("view.point_spacing", {"View"}, "Point Spacing and Radii", &Impl::DrawSpacingWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.point_spacing", "Mesh"}, {"graph.processing.point_spacing", "Graph"},
                  {"pointcloud.processing.point_spacing", "PointCloud"}}})
            Handles.push_back(Shell->RegisterEditorWindow({
                .Id=id, .MenuPath={domain,"Processing"}, .Title="Point Spacing and Radii",
                .Draw=[](bool& open,const SandboxEditorContext&){open=false;},
                .OpenStateChanged=[this,id](bool open){
                    if(!open)return;
                    (void)Shell->SetEditorWindowOpen("view.point_spacing",true);
                    (void)Shell->SetEditorWindowOpen(id,false);
                }}));
        RegisterWindow("view.bilateral_filter", {"View"}, "Bilateral Point Filter", &Impl::DrawBilateralWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.bilateral_filter", "Mesh"}, {"graph.processing.bilateral_filter", "Graph"},
                  {"pointcloud.processing.bilateral_filter", "PointCloud"}}})
            Handles.push_back(Shell->RegisterEditorWindow({
                .Id=id, .MenuPath={domain,"Processing"}, .Title="Bilateral Point Filter",
                .Draw=[](bool& open,const SandboxEditorContext&){open=false;},
                .OpenStateChanged=[this,id](bool open){
                    if(!open)return;
                    (void)Shell->SetEditorWindowOpen("view.bilateral_filter",true);
                    (void)Shell->SetEditorWindowOpen(id,false);
                }}));
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
        Handles.push_back(Shell->RegisterEditorWindow({
            .Id = "mesh.processing.faces.normals", .MenuPath = {"Mesh", "Processing", "Faces"},
            .Title = "Normals",
            .Draw = [](bool& open, const SandboxEditorContext&) { open = false; },
            .OpenStateChanged = [this](bool open) {
                if (!open) return;
                Normals.OpenFacePreset = true;
                (void)Shell->SetEditorWindowOpen("view.normal_estimation", true);
                (void)Shell->SetEditorWindowOpen("mesh.processing.faces.normals", false);
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
        Denoise.Input = {};
        Curvature.LastResult.reset();
        GeodesicsInput = {};
        GeodesicsInitialized = false;
        GeodesicsDirty = false;
        GeodesicsResult.reset();
        GeodesicsMessage.clear();
        Curvature = {};
        Segmentation = {};
        Remesh.LastResult.reset();
        Remesh.Input = {};
        Subdivide.LastResult.reset();
        Subdivide.Input = {};
        Simplify.LastResult.reset();
        Simplify.Input = {};
        Registration = {};
        Normals = {};
        Outliers = {};
        Keypoints = {};
        Descriptors = {};
        Density = {};
        DensityWeights = {};
        Construction = {};
        Spacing = {};
        Bilateral = {};
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
        const Runtime::EditorDomainWindowKind kind, const std::optional<std::uint32_t> entity)
    {
        const int frame = ImGui::GetFrameCount();
        if (CachedModelFrame != frame)
        {
            CachedModelFrame = frame;
            for (auto& model : CachedDomainModels)
                model.reset();
        }
        auto& model = CachedDomainModels[static_cast<std::size_t>(kind)];
        if (!model.has_value() || (entity && model->SelectedStableId != *entity))
        {
            model = Runtime::BuildEditorDomainWindowModel(
                context.SnapshotQueries,
                kind,
                context.ModelBuildStats, entity);
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
        ProcessingEntityInput& input, const DrawDomainControls draw)
    {
        ImGui::SetNextWindowSize(
            ImVec2(340.0f, 300.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin(title, &open))
        {
            DrawProcessingEntity("Entity##Processing", context, input.Entity, input.PreviousSelection, kind);
            const auto& model = GetDomainWindowModel(context, kind, input.Entity);
            DrawProcessingCpuBackend();
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
            "Mesh / Processing / Denoise", Denoise.Input, &Impl::DrawDenoiseControls);
    }

    void MeshProcessingPanels::Impl::DrawCurvatureWindow(
        bool& open, const SandboxEditorContext& context)
    {
        ImGui::SetNextWindowSize(ImVec2(460.0f, 640.0f), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Mesh / Processing / Curvature", &open))
            DrawCurvatureControls(context);
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawSegmentationWindow(
        bool& open, const SandboxEditorContext& context)
    {
        DrawDomainWindow(open, context, Runtime::EditorDomainWindowKind::Mesh,
            "Mesh / Processing / Curvature Segmentation", Segmentation.Input, &Impl::DrawCurvatureSegmentationControls);
    }

    void MeshProcessingPanels::Impl::DrawRemeshWindow(
        bool& open, const SandboxEditorContext& context)
    {
        DrawDomainWindow(
            open, context, Runtime::EditorDomainWindowKind::Mesh,
            "Mesh / Processing / Remesh", Remesh.Input, &Impl::DrawRemeshControls);
    }

    void MeshProcessingPanels::Impl::DrawSubdivideWindow(
        bool& open, const SandboxEditorContext& context)
    {
        DrawDomainWindow(
            open, context, Runtime::EditorDomainWindowKind::Mesh,
            "Mesh / Processing / Subdivide", Subdivide.Input, &Impl::DrawSubdivideControls);
    }

    void MeshProcessingPanels::Impl::DrawSimplifyWindow(
        bool& open, const SandboxEditorContext& context)
    {
        DrawDomainWindow(
            open, context, Runtime::EditorDomainWindowKind::Mesh,
            "Mesh / Processing / Simplify", Simplify.Input, &Impl::DrawSimplifyControls);
    }

    void MeshProcessingPanels::Impl::DrawDenoiseControls(
        const Runtime::EditorDomainWindowModel& model,
        const SandboxEditorContext& context)
    {
        const Runtime::EditorGeometryProcessingModel& processing =
            model.Processing;
        if (context.MeshTopology.Results.LastMeshDenoiseResult.has_value())
            Denoise.LastResult = *context.MeshTopology.Results.LastMeshDenoiseResult;
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
                    context.MeshTopology.Commands,
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
                    },
                    context.MeshTopology.ResultSinks.MeshDenoise),
                context.MeshTopology.ResultSinks.MeshDenoise);
        }

        const auto& result = Denoise.LastResult;
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
        DrawDismissLastResultButton("Dismiss##MeshDenoise", Denoise.LastResult, Runtime::EditorMeshTopologyResultSlot::MeshDenoise, context.MeshTopology.ResultSinks.DismissResult);
    }

    void MeshProcessingPanels::Impl::DrawCurvatureControls(const SandboxEditorContext& context)
    {
        if (context.MeshFields.Results.LastMeshCurvatureResult.has_value())
            Curvature.LastResult = *context.MeshFields.Results.LastMeshCurvatureResult;
        if (!Curvature.Initialized || !Curvature.Dirty)
        {
            if (auto active = Runtime::GetEditorMeshCurvatureConfig(context.MeshFields.Commands))
            {
                Curvature.Config = *active;
                Curvature.Initialized = true;
            }
        }
        auto& config = Curvature.Config;
        bool changed = DrawProcessingEntity("Entity##MeshCurvature", context,
            config.StableEntityId, Curvature.LastSelectedEntity, Runtime::EditorDomainWindowKind::Mesh);
        const auto& model = GetDomainWindowModel(context, Runtime::EditorDomainWindowKind::Mesh, config.StableEntityId);
        const auto& processing = model.Processing;
        DrawProcessingCpuBackend();
        ImGui::SeparatorText("Input properties");
        changed |= DrawProcessingPropertyInput("Positions##MeshCurvature", model.PropertyCatalog, config.Positions);
        ImGui::SeparatorText("Output properties");
        changed |= DrawProcessingPropertyName("Mean curvature", config.Mean.Name);
        changed |= DrawProcessingPropertyName("Gaussian curvature", config.Gaussian.Name);
        changed |= DrawProcessingPropertyName("Minimum principal curvature", config.MinPrincipal.Name);
        changed |= DrawProcessingPropertyName("Maximum principal curvature", config.MaxPrincipal.Name);
        changed |= DrawProcessingPropertyName("First principal direction", config.Direction1.Name);
        changed |= DrawProcessingPropertyName("Second principal direction", config.Direction2.Name);
        if (ImGui::BeginCombo("Output##MeshCurvature", Runtime::DebugNameForEditorMeshCurvatureOutput(config.Output)))
        {
            for (const auto output : kMeshCurvatureOutputs)
                if (ImGui::Selectable(Runtime::DebugNameForEditorMeshCurvatureOutput(output), config.Output == output))
                {
                    config.Output = output;
                    changed = true;
                }
            ImGui::EndCombo();
        }
        changed |= ImGui::Checkbox("Principal directions##MeshCurvature", &config.PublishPrincipalDirections);
        Curvature.Dirty |= changed;
        if (changed)
        {
            const auto applied = Runtime::ApplyEditorMeshCurvatureConfig(context.MeshFields.Commands, config);
            Curvature.ConfigDiagnostic = applied.Succeeded() ? "" : "Invalid curvature property bindings.";
            if (applied.Succeeded()) Curvature.Dirty = false;
        }
        ImGui::BeginDisabled(!processing.MeshCurvatureAvailable || !context.ProcessingConfigCommandsAvailable ||
            !Curvature.ConfigDiagnostic.empty());
        if (ImGui::Button("Compute##MeshCurvature"))
            PublishCommandResult(Curvature.LastResult,
                Runtime::ApplyEditorMeshCurvatureCommand(context.MeshFields.Commands, config,
                    context.MeshFields.ResultSinks.MeshCurvature),
                context.MeshFields.ResultSinks.MeshCurvature);
        ImGui::EndDisabled();
        ImGui::SeparatorText("Display output properties");
        for (const auto* output : {&config.Mean, &config.Gaussian, &config.MinPrincipal,
                                  &config.MaxPrincipal, &config.Direction1, &config.Direction2})
            DrawProcessingPropertyShowButton(context, config.StableEntityId, *output, Curvature.VisualizationDiagnostic);
        if (!processing.MeshCurvatureAvailable)
            ImGui::TextDisabled("Select a mesh to compute curvature.");
        if (!Curvature.ConfigDiagnostic.empty()) ImGui::TextWrapped("%s", Curvature.ConfigDiagnostic.c_str());
        if (!Curvature.VisualizationDiagnostic.empty()) ImGui::Text("Display: %s", Curvature.VisualizationDiagnostic.c_str());
        const auto& result = Curvature.LastResult;
        if (!result.has_value())
        {
            ImGui::TextDisabled("Last curvature run: none");
            return;
        }
        ImGui::Text(
            "Last curvature run: %s",
            Runtime::DebugNameForEditorCommandStatus(result->Status));
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
        if (Curvature.LastResult.has_value() && DrawDismissLastResultButton("Dismiss##MeshCurvature"))
        {
            Curvature.LastResult.reset();
            if (context.MeshFields.ResultSinks.DismissResult)
                context.MeshFields.ResultSinks.DismissResult();
        }
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

        if (!Segmentation.Initialized)
        {
            if (const auto active =
                    Runtime::GetEditorCurvatureSegmentationConfig(
                        context.MeshFields.Commands))
            {
                Segmentation.Config = *active;
            }
            Segmentation.Initialized = true;
            Segmentation.Dirty = false;
        }

        Runtime::CurvatureSegmentationConfig& config =
            Segmentation.Config;
        bool changed = false;
        ImGui::SeparatorText("Input properties");
        changed |= DrawProcessingPropertyInput("Positions##Segmentation", model.PropertyCatalog, config.Positions);
        ImGui::SeparatorText("Output properties");
        changed |= DrawProcessingPropertyName("Components##Segmentation", config.Components.Name);
        changed |= DrawProcessingPropertyName("Regions##Segmentation", config.Regions.Name);
        changed |= DrawProcessingPropertyName("RegionColors##Segmentation", config.RegionColors.Name);
        changed |= DrawProcessingPropertyName("Boundaries##Segmentation", config.Boundaries.Name);
        changed |= DrawProcessingPropertyName("BoundaryColors##Segmentation", config.BoundaryColors.Name);
        changed |= DrawProcessingPropertyName("HardFeatures##Segmentation", config.HardFeatures.Name);
        changed |= DrawProcessingPropertyName("FeatureConfidence##Segmentation", config.FeatureConfidence.Name);
        changed |= DrawProcessingPropertyName("BoundaryRoles##Segmentation", config.BoundaryRoles.Name);
        changed |= DrawProcessingPropertyName("FeatureColors##Segmentation", config.FeatureColors.Name);

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
            &Segmentation.AutoVisualize);

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
        Segmentation.Dirty |= changed;

        const bool configCommandsAvailable =
            context.ProcessingConfigCommandsAvailable;
        ImGui::BeginDisabled(
            !configCommandsAvailable ||
            !Segmentation.Dirty);
        if (ImGui::Button("Apply configuration##CurvatureSegmentation"))
        {
            Segmentation.LastConfigApply =
                Runtime::ApplyEditorCurvatureSegmentationConfig(
                    context.MeshFields.Commands,
                    config,
                    "sandbox.curvature_segmentation.panel");
            if (Segmentation.LastConfigApply->Succeeded())
                Segmentation.Dirty = false;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!configCommandsAvailable);
        if (ImGui::Button("Reload active##CurvatureSegmentation"))
        {
            Segmentation.Initialized = false;
            Segmentation.Dirty = false;
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!configCommandsAvailable);
        if (ImGui::Button("Run segmentation##CurvatureSegmentation"))
        {
            Segmentation.LastConfigApply =
                Runtime::ApplyEditorCurvatureSegmentationConfig(
                    context.MeshFields.Commands,
                    config,
                    "sandbox.curvature_segmentation.panel.run");
            if (Segmentation.LastConfigApply->Succeeded())
            {
                Segmentation.Dirty = false;
                Segmentation.LastResult =
                    Runtime::
                        ApplyEditorConfiguredCurvatureSegmentationCommand(
                            context.MeshFields.Commands,
                            model.SelectedStableId);
                if (Segmentation.LastResult->Succeeded())
                {
                    if (Segmentation.AutoVisualize)
                    {
                        ShowCurvatureSegmentationVisualization(
                            context, model.SelectedStableId, config);
                    }
                }
            }
        }
        ImGui::EndDisabled();
        ImGui::SeparatorText("Display output properties");
        for (const auto* output : {&config.Components, &config.Regions, &config.RegionColors, &config.Boundaries, &config.BoundaryColors, &config.HardFeatures, &config.FeatureConfidence, &config.BoundaryRoles, &config.FeatureColors})
            DrawProcessingPropertyShowButton(context, model.SelectedStableId, *output, Segmentation.VisualizationDiagnostic);
        if (!Segmentation.VisualizationDiagnostic.empty()) ImGui::Text("Display: %s", Segmentation.VisualizationDiagnostic.c_str());

        if (Segmentation.LastConfigApply.has_value() &&
            !Segmentation.LastConfigApply->Succeeded())
        {
            ImGui::TextDisabled(
                "The configuration was rejected; inspect config diagnostics.");
        }
        if (!Segmentation.LastResult.has_value())
        {
            ImGui::TextDisabled("Last segmentation run: none");
            return;
        }

        const Runtime::EditorCurvatureSegmentationResult& result =
            *Segmentation.LastResult;
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
            Segmentation.LastResult.reset();
    }

    void MeshProcessingPanels::Impl::DrawRemeshControls(
        const Runtime::EditorDomainWindowModel& model,
        const SandboxEditorContext& context)
    {
        const Runtime::EditorGeometryProcessingModel& processing =
            model.Processing;
        if (context.MeshTopology.Results.LastMeshRemeshResult.has_value())
            Remesh.LastResult = *context.MeshTopology.Results.LastMeshRemeshResult;
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
                    context.MeshTopology.Commands,
                    Runtime::EditorMeshRemeshCommand{
                        .StableEntityId = model.SelectedStableId,
                        .Mode = mode,
                        .SizingLaw = sizingLaw,
                        .Iterations = static_cast<std::uint32_t>(
                            Remesh.Iterations),
                        .TargetEdgeLength = static_cast<double>(
                            Remesh.TargetEdgeLength),
                        .ProjectToSurface = Remesh.ProjectToSurface,
                    },
                    context.MeshTopology.ResultSinks.MeshRemesh),
                context.MeshTopology.ResultSinks.MeshRemesh);
        }
        if (!canRun)
            ImGui::EndDisabled();

        const auto& result = Remesh.LastResult;
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
        DrawDismissLastResultButton("Dismiss##MeshRemesh", Remesh.LastResult, Runtime::EditorMeshTopologyResultSlot::MeshRemesh, context.MeshTopology.ResultSinks.DismissResult);
    }

    void MeshProcessingPanels::Impl::DrawSubdivideControls(
        const Runtime::EditorDomainWindowModel& model,
        const SandboxEditorContext& context)
    {
        const Runtime::EditorGeometryProcessingModel& processing =
            model.Processing;
        if (context.MeshTopology.Results.LastMeshSubdivideResult.has_value())
            Subdivide.LastResult = *context.MeshTopology.Results.LastMeshSubdivideResult;
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
                    context.MeshTopology.Commands,
                    Runtime::EditorMeshSubdivideCommand{
                        .StableEntityId = model.SelectedStableId,
                        .Operator = op,
                        .Iterations = static_cast<std::uint32_t>(
                            Subdivide.Iterations),
                        .PreserveLoopFeatureEdges =
                            Subdivide.PreserveLoopFeatures,
                    },
                    context.MeshTopology.ResultSinks.MeshSubdivide),
                context.MeshTopology.ResultSinks.MeshSubdivide);
        }
        if (!canRun)
            ImGui::EndDisabled();

        const auto& result = Subdivide.LastResult;
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
        DrawDismissLastResultButton("Dismiss##MeshSubdivide", Subdivide.LastResult, Runtime::EditorMeshTopologyResultSlot::MeshSubdivide, context.MeshTopology.ResultSinks.DismissResult);
    }

    void MeshProcessingPanels::Impl::DrawSimplifyControls(
        const Runtime::EditorDomainWindowModel& model,
        const SandboxEditorContext& context)
    {
        const Runtime::EditorGeometryProcessingModel& processing =
            model.Processing;
        if (context.MeshTopology.Results.LastMeshSimplifyResult.has_value())
            Simplify.LastResult = *context.MeshTopology.Results.LastMeshSimplifyResult;
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
                    context.MeshTopology.Commands,
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
                    },
                    context.MeshTopology.ResultSinks.MeshSimplify),
                context.MeshTopology.ResultSinks.MeshSimplify);
        }
        if (!canRun)
            ImGui::EndDisabled();

        const auto& result = Simplify.LastResult;
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
        DrawDismissLastResultButton("Dismiss##MeshSimplify", Simplify.LastResult, Runtime::EditorMeshTopologyResultSlot::MeshSimplify, context.MeshTopology.ResultSinks.DismissResult);
    }

    void MeshProcessingPanels::Impl::DrawNormalsWindow(bool &open, const SandboxEditorContext &context)
    {
        if (context.Normals.Results.LastNormalEstimationResult)
            Normals.LastResult = context.Normals.Results.LastNormalEstimationResult;
        ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Normal Estimation", &open))
        {
            ImGui::End();
            return;
        }
        const auto active = Runtime::GetEditorNormalEstimationConfig(context.Normals.Commands)
                                .value_or(Runtime::NormalEstimationConfig{});
        const auto serialized = Runtime::SerializeNormalEstimationConfig(active);
        Normals.Synchronize(active, serialized);
        auto &config = Normals.Draft;
        bool changed = false;
        if (Normals.OpenFacePreset)
        {
            Normals.OpenFacePreset = false;
            config.Method = Runtime::NormalEstimationMethod::MeshFaceNormals;
            config.Positions = {Runtime::GeometryElementDomain::MeshVertex, "v:position",
                                decltype(config.Positions.ValueKind)::Vec3};
            config.Output = {Runtime::GeometryElementDomain::MeshFace, "f:normal",
                             decltype(config.Output.ValueKind)::Vec3};
            changed = true;
        }
        changed |= DrawProcessingEntity("Entity##Normals", context,
            config.StableEntityId, Normals.LastSelectedEntity);
        DrawProcessingCpuBackend();
        if (DrawProcessingPointInput("Positions##Normals", [&] { return Runtime::GetEditorPointInputCatalog(context.Processing, config.StableEntityId); }, config.Positions, config.Method == Runtime::NormalEstimationMethod::MeshFaceNormals
                    ? std::optional{Runtime::GeometryElementDomain::MeshVertex} : std::nullopt))
        {
            config.Output.Domain = config.Method == Runtime::NormalEstimationMethod::MeshFaceNormals
                ? Runtime::GeometryElementDomain::MeshFace : config.Positions.Domain;
            changed = true;
        }
        changed |= DrawProcessingPropertyName("Output property##Normals", config.Output.Name);
        if (ImGui::BeginCombo("Method##Normals", Runtime::ToString(config.Method)))
        {
            for (auto method : {Runtime::NormalEstimationMethod::PointSetPCA,
                                Runtime::NormalEstimationMethod::MeshFaceWeighted,
                                Runtime::NormalEstimationMethod::GraphNeighborhood,
                                Runtime::NormalEstimationMethod::MeshFaceNormals})
            {
                auto candidate = config;
                candidate.Method = method;
                candidate.Output.Domain = method == Runtime::NormalEstimationMethod::MeshFaceNormals
                    ? Runtime::GeometryElementDomain::MeshFace : candidate.Positions.Domain;
                if (candidate.Output.Name == "v:normal" || candidate.Output.Name == "f:normal")
                    candidate.Output.Name = method == Runtime::NormalEstimationMethod::MeshFaceNormals
                        ? "f:normal" : "v:normal";
                const auto readiness =
                    Runtime::PreviewEditorNormalEstimationCommand(context.Normals.Commands, candidate);
                ImGui::BeginDisabled(!readiness.Ready);
                if (ImGui::Selectable(Runtime::ToString(method), config.Method == method))
                {
                    config = candidate;
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
            if (ImGui::Combo("Acceleration##Normals", &backend, "CPU KD-tree\0CPU LBVH (cached)\0Vulkan LBVH (CPU fit)\0"))
            {
                config.Backend = Runtime::NormalEstimationBackend(backend);
                changed = true;
            }
            if (config.Backend == Runtime::NormalEstimationBackend::VulkanLBVH)
            {
                ImGui::TextWrapped("GPU neighborhood queries; PCA and orientation run on CPU. "
                                   "Dense radius neighborhoods exceeding 1024 candidates are rejected.");
                changed |= ImGui::InputScalar("GPU query batch size##Normals", ImGuiDataType_U32,
                                              &config.GpuQueryBatchSize);
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
        else if (config.Method == Runtime::NormalEstimationMethod::MeshFaceNormals)
        {
            ImGui::TextWrapped("Compute one object-space normal per polygon from its full face ring. "
                               "Face winding determines the direction.");
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
            const auto applied = Runtime::ApplyEditorNormalEstimationConfig(context.Normals.Commands, config);
            Normals.ConfigDiagnostic =
                applied.Succeeded() ? "" : "Controls were rejected by normal config validation.";
        }
        if (!Normals.ConfigDiagnostic.empty())
            ImGui::TextWrapped("%s", Normals.ConfigDiagnostic.c_str());
        const auto readiness =
            Runtime::PreviewEditorNormalEstimationCommand(context.Normals.Commands, config);
        if (!readiness.Ready)
            ImGui::TextWrapped("%s", readiness.Diagnostic.c_str());
        ImGui::BeginDisabled(!context.ProcessingConfigCommandsAvailable || !readiness.Ready ||
                             !Normals.ConfigDiagnostic.empty());
        if (ImGui::Button("Estimate normals"))
            PublishCommandResult(Normals.LastResult,
                                 Runtime::ApplyEditorConfiguredNormalEstimation(context.Normals.Commands, context.Normals.ResultSinks.NormalEstimation),
                                 context.Normals.ResultSinks.NormalEstimation);
        ImGui::EndDisabled();
        const auto outputProperty = config.Output;
        ImGui::SameLine();
        if (config.Method == Runtime::NormalEstimationMethod::MeshFaceNormals)
        {
            if (ImGui::Button("Show face normals"))
            {
                const auto hint = Runtime::ApplyEditorRenderHintCommand(
                    context.VisualizationCommands,
                    {.StableEntityId = config.StableEntityId,
                     .SetSurface = true, .EnableSurface = true,
                     .SurfaceDomain = decltype(Runtime::EditorRenderHintCommand{}.SurfaceDomain)::Face});
                const auto status = Runtime::ApplyEditorVisualizationPropertyCommand(
                    context.VisualizationCommands,
                    {.StableEntityId = config.StableEntityId,
                     .Target = Runtime::EditorVisualizationTarget::Surface,
                     .Domain = Runtime::EditorVisualizationPropertyDomain::MeshFaces,
                     .Preset = Runtime::EditorVisualizationPropertyPreset::ColorBuffer,
                     .PropertyName = outputProperty.Name});
                (void)hint;
                Normals.VisualizationDiagnostic = Runtime::DebugNameForEditorCommandStatus(status);
            }
        }
        else if (ImGui::Button("Show normals"))
        {
            const auto status = ShowProcessingProperty(context, config.StableEntityId, outputProperty);
            Normals.VisualizationDiagnostic = Runtime::DebugNameForEditorCommandStatus(status);
        }
        if (!Normals.VisualizationDiagnostic.empty())
            ImGui::Text("Normal display: %s", Normals.VisualizationDiagnostic.c_str());
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
            if (DrawDismissLastResultButton("Dismiss##Normals"))
            {
                Normals.LastResult.reset();
                if (context.Normals.ResultSinks.DismissResult) context.Normals.ResultSinks.DismissResult();
            }
        }
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawOutliersWindow(bool &open, const SandboxEditorContext &context)
    {
        if (context.PointAnalysis.Results.LastOutlierAnalysisResult)
            Outliers.LastResult = context.PointAnalysis.Results.LastOutlierAnalysisResult;
        ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Outlier Analysis", &open))
        {
            ImGui::End();
            return;
        }
        const auto active = Runtime::GetEditorOutlierAnalysisConfig(context.PointAnalysis.Commands)
                                .value_or(Runtime::OutlierAnalysisConfig{});
        const auto serialized = Runtime::SerializeOutlierAnalysisConfig(active);
        Outliers.Synchronize(active, serialized);
        auto &config = Outliers.Draft;
        bool changed = false;
        changed |= DrawProcessingEntity("Entity##Outliers", context,
            config.StableEntityId, Outliers.LastSelectedEntity);
        DrawProcessingCpuBackend();
        if (DrawProcessingPointInput("Positions##Outliers", [&] { return Runtime::GetEditorPointInputCatalog(context.Processing, config.StableEntityId); }, config.Positions))
        {
            config.Mask.Domain = config.Score.Domain = config.Positions.Domain;
            changed = true;
        }
        for (auto [label, ref] : {std::pair{"Mask property", &config.Mask}, std::pair{"Score property", &config.Score}})
            changed |= DrawProcessingPropertyName(label, ref->Name);
        int method=int(config.Method), backend=int(config.Backend);
        if(ImGui::Combo("Method",&method,"Statistical\0Radius\0Local distance ratio\0")) {config.Method=Runtime::OutlierAnalysisMethod(method);changed=true;}
        if(ImGui::Combo("Acceleration",&backend,"CPU octree\0CPU LBVH (cached)\0Vulkan LBVH\0")) {config.Backend=Runtime::OutlierAnalysisBackend(backend);changed=true;}
        if(config.Method==Runtime::OutlierAnalysisMethod::Statistical)
        {
            changed |= ImGui::InputScalar("Neighbors k",ImGuiDataType_U32,&config.KNeighbors);
            changed |= ImGui::InputFloat("Standard deviation multiplier",&config.StdDevMultiplier);
            ImGui::TextWrapped("Mark mean neighbor distances above the global mean plus this multiple of the population standard deviation.");
        }
        else if(config.Method==Runtime::OutlierAnalysisMethod::LocalDistanceRatio)
        {
            changed |= ImGui::InputScalar("Neighbors k",ImGuiDataType_U32,&config.KNeighbors);
            changed |= ImGui::InputFloat("Score threshold",&config.ScoreThreshold);
            ImGui::TextWrapped("Compare each sample's mean neighbor distance to its neighbors' mean distances. Scores above the threshold are marked. This is a density-deviation heuristic, not a calibrated probability.");
        }
        else
        {
            changed |= ImGui::InputFloat("Radius",&config.Radius);
            changed |= ImGui::InputScalar("Minimum neighbors",ImGuiDataType_U32,&config.MinimumNeighbors);
            ImGui::TextWrapped("Mark samples with fewer than this many other samples inside the inclusive radius.");
        }
        if(config.Backend==Runtime::OutlierAnalysisBackend::VulkanLBVH)
            changed |= ImGui::InputScalar("GPU query batch size",ImGuiDataType_U32,&config.GpuQueryBatchSize);
        if(changed)
        {
            config.Operation=Runtime::OutlierAnalysisOperation::Analyze;
            const auto applied=Runtime::ApplyEditorOutlierAnalysisConfig(context.PointAnalysis.Commands,config);
            Outliers.ConfigDiagnostic=applied.Succeeded()?"":"Controls were rejected by outlier config validation.";
        }
        if(!Outliers.ConfigDiagnostic.empty())ImGui::TextWrapped("%s",Outliers.ConfigDiagnostic.c_str());
        auto analyze=config;analyze.Operation=Runtime::OutlierAnalysisOperation::Analyze;
        const auto readiness=Runtime::PreviewEditorOutlierAnalysisCommand(context.PointAnalysis.Commands,analyze);
        if(!readiness.Ready)ImGui::TextWrapped("%s",readiness.Diagnostic.c_str());
        const auto execute=[&](Runtime::OutlierAnalysisConfig request){
            const auto applied=Runtime::ApplyEditorOutlierAnalysisConfig(context.PointAnalysis.Commands,request);
            if(applied.Succeeded())
                PublishCommandResult(Outliers.LastResult,Runtime::ApplyEditorConfiguredOutlierAnalysis(context.PointAnalysis.Commands, context.PointAnalysis.ResultSinks.OutlierAnalysis),context.PointAnalysis.ResultSinks.OutlierAnalysis);
            else Outliers.ConfigDiagnostic="Outlier config was rejected.";
        };
        ImGui::BeginDisabled(!context.ProcessingConfigCommandsAvailable || !readiness.Ready || !Outliers.ConfigDiagnostic.empty());
        if(ImGui::Button("Detect outliers"))execute(analyze);
        ImGui::EndDisabled();
        ImGui::TextWrapped("Detection writes a mask (1 = outlier) and a score. Geometry stays in source order.");
        auto remove=config;remove.Operation=Runtime::OutlierAnalysisOperation::RemoveMarked;
        const auto removal=Runtime::PreviewEditorOutlierAnalysisCommand(context.PointAnalysis.Commands,remove);
        ImGui::BeginDisabled(!context.ProcessingConfigCommandsAvailable || !removal.Ready || !Outliers.ConfigDiagnostic.empty());
        if(ImGui::Button("Remove marked points"))execute(remove);
        ImGui::EndDisabled();
        if(!removal.Ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s",removal.Diagnostic.c_str());
        ImGui::TextWrapped("Removal compacts point clouds and supports Undo. Detect again after changing positions or the mask.");
        auto mask=config.Mask,
             score=config.Score;
        if(ImGui::Button("Show mask"))
            Outliers.VisualizationDiagnostic=Runtime::DebugNameForEditorCommandStatus(ShowProcessingProperty(context, config.StableEntityId, mask));
        ImGui::SameLine();
        if(ImGui::Button("Show score"))
            Outliers.VisualizationDiagnostic=Runtime::DebugNameForEditorCommandStatus(ShowProcessingProperty(context, config.StableEntityId, score));
        if(!Outliers.VisualizationDiagnostic.empty())ImGui::Text("Display: %s",Outliers.VisualizationDiagnostic.c_str());
        if(Outliers.LastResult)
        {
            const auto& result=*Outliers.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s",Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Requested: %s; ran: %s",Runtime::ToString(result.RequestedBackend),result.ActualBackend.c_str());
            ImGui::Text("Live / total: %zu / %zu; outliers: %zu",result.LiveCount,result.SlotCount,result.RejectedCount);
            if(result.Method==Runtime::OutlierAnalysisMethod::Statistical)
                ImGui::Text("Mean %.5g; standard deviation %.5g; threshold %.5g",double(result.MeanDistance),double(result.StdDevDistance),double(result.DistanceThreshold));
            ImGui::TextWrapped("%s",result.Message.c_str());
            DrawDismissLastResultButton("Dismiss##Outliers", Outliers.LastResult, Runtime::EditorPointAnalysisResultSlot::OutlierAnalysis, context.PointAnalysis.ResultSinks.DismissResult);
        }
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawKeypointsWindow(bool &open, const SandboxEditorContext &context)
    {
        if (context.PointAnalysis.Results.LastKeypointAnalysisResult)
            Keypoints.LastResult = context.PointAnalysis.Results.LastKeypointAnalysisResult;
        ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("ISS Keypoint Analysis", &open))
        {
            ImGui::End();
            return;
        }
        const auto active = Runtime::GetEditorKeypointAnalysisConfig(context.PointAnalysis.Commands)
                                .value_or(Runtime::KeypointAnalysisConfig{});
        const auto serialized = Runtime::SerializeKeypointAnalysisConfig(active);
        Keypoints.Synchronize(active, serialized);
        auto &config = Keypoints.Draft;
        bool changed = false;
        changed |= DrawProcessingEntity("Entity##Keypoints", context,
            config.StableEntityId, Keypoints.LastSelectedEntity);
        DrawProcessingCpuBackend();
        if (DrawProcessingPointInput("Positions##Keypoints", [&] { return Runtime::GetEditorPointInputCatalog(context.Processing, config.StableEntityId); }, config.Positions))
        {
            config.Mask.Domain = config.Score.Domain = config.Positions.Domain;
            changed = true;
        }
        for (auto [label, ref] : {std::pair{"Mask property", &config.Mask}, std::pair{"Saliency property", &config.Score}})
            changed |= DrawProcessingPropertyName(label, ref->Name);
        int backend=int(config.Backend);
        if(ImGui::Combo("Acceleration",&backend,"CPU KD-tree\0CPU LBVH (cached)\0Vulkan LBVH\0")) {config.Backend=Runtime::KeypointAnalysisBackend(backend);changed=true;}
        changed |= ImGui::InputFloat("Salient radius (0 = automatic)",&config.SalientRadius);
        changed |= ImGui::InputFloat("Suppression radius (0 = automatic)",&config.NonMaxRadius);
        changed |= ImGui::InputDouble("Eigenvalue ratio 2 / 1",&config.Gamma21);
        changed |= ImGui::InputDouble("Eigenvalue ratio 3 / 2",&config.Gamma32);
        changed |= ImGui::InputScalar("Minimum neighbors",ImGuiDataType_U32,&config.MinimumNeighbors);
        ImGui::TextWrapped("Centroid-PCA saliency with radius suppression. Automatic radii use 6 and 4 times mean nearest-neighbor spacing. Equal scores keep the lowest source index.");
        if(config.Backend==Runtime::KeypointAnalysisBackend::VulkanLBVH)
        {
            changed |= ImGui::InputScalar("GPU query batch size",ImGuiDataType_U32,&config.GpuQueryBatchSize);
            changed |= ImGui::InputScalar("Complete radius capacity",ImGuiDataType_U32,&config.GpuRadiusCapacity);
            ImGui::TextWrapped("Radius support must fit the selected capacity (up to 1024). Overflow retains previous outputs. Scale, covariance and suppression run on CPU.");
        }
        DrawProcessingExecution(context, Keypoints, changed,
            [&](const auto& request) { return Runtime::PreviewEditorKeypointAnalysisCommand(context.PointAnalysis.Commands, request); },
            [&](const auto& request) { return Runtime::ApplyEditorKeypointAnalysisConfig(context.PointAnalysis.Commands, request); },
            [&] { return Runtime::ApplyEditorConfiguredKeypointAnalysis(context.PointAnalysis.Commands, context.PointAnalysis.ResultSinks.KeypointAnalysis); },
            context.PointAnalysis.ResultSinks.KeypointAnalysis, "Detect keypoints",
            "Controls were rejected by keypoint config validation.", "Keypoint config was rejected.");
        ImGui::TextWrapped("Detection writes a mask (1 = retained keypoint) and a score. Geometry stays in source order.");
        auto mask=config.Mask,
             score=config.Score;
        if(ImGui::Button("Show mask"))
            Keypoints.VisualizationDiagnostic=Runtime::DebugNameForEditorCommandStatus(ShowProcessingProperty(context, config.StableEntityId, mask));
        ImGui::SameLine();
        if(ImGui::Button("Show saliency"))
            Keypoints.VisualizationDiagnostic=Runtime::DebugNameForEditorCommandStatus(ShowProcessingProperty(context, config.StableEntityId, score));
        if(!Keypoints.VisualizationDiagnostic.empty())ImGui::Text("Display: %s",Keypoints.VisualizationDiagnostic.c_str());
        if(Keypoints.LastResult)
        {
            const auto& result=*Keypoints.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s",Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Requested: %s; ran: %s",Runtime::ToString(result.RequestedBackend),result.ActualBackend.c_str());
            ImGui::Text("Live / total: %zu / %zu; keypoints: %zu",result.LiveCount,result.SlotCount,result.KeypointCount);
            ImGui::Text("Spacing: %.5g; salient / suppression radii: %.5g / %.5g",double(result.Scale.MeanSpacing),double(result.Scale.SalientRadius),double(result.Scale.NonMaxRadius));
            ImGui::Text("GPU batches: %zu; largest indexed support: %zu",result.GpuQueryBatches,result.MaximumNeighbors);
            ImGui::TextWrapped("%s",result.Message.c_str());
            DrawDismissLastResultButton("Dismiss##Keypoints", Keypoints.LastResult, Runtime::EditorPointAnalysisResultSlot::KeypointAnalysis, context.PointAnalysis.ResultSinks.DismissResult);
        }
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawDescriptorsWindow(bool &open, const SandboxEditorContext &context)
    {
        if (context.PointAnalysis.Results.LastDescriptorAnalysisResult)
            Descriptors.LastResult = context.PointAnalysis.Results.LastDescriptorAnalysisResult;
        ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("FPFH Descriptor Analysis", &open))
        {
            ImGui::End();
            return;
        }
        const auto active = Runtime::GetEditorDescriptorAnalysisConfig(context.PointAnalysis.Commands)
                                .value_or(Runtime::DescriptorAnalysisConfig{});
        const auto serialized = Runtime::SerializeDescriptorAnalysisConfig(active);
        if (Descriptors.Synchronize(active, serialized))
            Descriptors.FollowDisplayBin = false;
        auto &config = Descriptors.Draft;
        bool changed = false;
        if (DrawProcessingEntity("Entity##Descriptors", context, config.StableEntityId, Descriptors.LastSelectedEntity))
        { changed = true; Descriptors.FollowDisplayBin = false; }
        DrawProcessingCpuBackend();
        if (DrawProcessingPointInput("Positions##Descriptors", [&] { return Runtime::GetEditorPointInputCatalog(context.Processing, config.StableEntityId); }, config.Positions))
        {
            config.Normals.Domain = config.Positions.Domain;
            for (auto& output : config.Outputs) output.Domain = config.Positions.Domain;
            changed = true;
        }
        if(ImGui::BeginCombo("Normals##Descriptors",config.Normals.Name.c_str()))
        {
            const auto catalog=Runtime::GetEditorPointInputCatalog(context.Processing,config.StableEntityId);
            for(const auto& row:catalog.Entries)
                if(row.Ref.Domain==config.Positions.Domain && ImGui::Selectable(row.Ref.Name.c_str(),row.Ref==config.Normals))
                {config.Normals=row.Ref;changed=true;}
            ImGui::EndCombo();
        }
        if(ImGui::TreeNode("Histogram output properties"))
        {
            ImGui::InputText("Output prefix",Descriptors.Prefix.data(),Descriptors.Prefix.size());
            if(ImGui::Button("Name all 33 bins"))
            {config.Outputs=Runtime::MakeDescriptorOutputProperties(config.Positions.Domain,Descriptors.Prefix.data());changed=true;}
            constexpr std::array blocks{"alpha","phi","theta"};
            for(unsigned i=0;i<33;++i)
            {
                const auto label=std::string(blocks[i/11])+" bin "+std::to_string(i%11);
                changed |= DrawProcessingPropertyName(label.c_str(), config.Outputs[i].Name);
            }
            ImGui::TreePop();
        }
        int backend=int(config.Backend);
        if(ImGui::Combo("Acceleration",&backend,"CPU KD-tree\0CPU LBVH (cached)\0Vulkan LBVH\0")) {config.Backend=Runtime::DescriptorAnalysisBackend(backend);changed=true;}
        changed |= ImGui::InputFloat("Feature radius (0 = automatic)",&config.FeatureRadius);
        changed |= ImGui::InputScalar("Maximum neighbors (0 = all)",ImGuiDataType_U32,&config.MaxNeighbors);
        ImGui::TextWrapped("FPFH uses three eleven-bin histograms and nonzero normals. Automatic radius is five times mean nearest-neighbor spacing. A neighbor cap keeps the lowest source IDs within the radius.");
        if(config.Backend==Runtime::DescriptorAnalysisBackend::VulkanLBVH)
        {
            changed |= ImGui::InputScalar("GPU query batch size",ImGuiDataType_U32,&config.GpuQueryBatchSize);
            changed |= ImGui::InputScalar("Radius result capacity",ImGuiDataType_U32,&config.GpuRadiusCapacity);
            ImGui::TextWrapped("Uncapped radius support must fit the selected capacity (up to 1024). A neighbor cap within that capacity can use the exact lowest-ID prefix even in denser neighborhoods. Scale, SPFH and FPFH run on CPU.");
        }
        DrawProcessingExecution(context, Descriptors, changed,
            [&](const auto& request) { return Runtime::PreviewEditorDescriptorAnalysisCommand(context.PointAnalysis.Commands, request); },
            [&](const auto& request) { return Runtime::ApplyEditorDescriptorAnalysisConfig(context.PointAnalysis.Commands, request); },
            [&] { return Runtime::ApplyEditorConfiguredDescriptorAnalysis(context.PointAnalysis.Commands, context.PointAnalysis.ResultSinks.DescriptorAnalysis); },
            context.PointAnalysis.ResultSinks.DescriptorAnalysis, "Compute FPFH descriptors",
            "Controls were rejected by descriptor config validation.", "Descriptor config was rejected.");
        ImGui::TextWrapped("Writes 33 named float histogram properties in one undoable operation. Each nonempty eleven-bin block sums to 100.");
        const bool displayBinChanged =
            ImGui::SliderInt("Display histogram bin", &Descriptors.DisplayBin, 0, 32,
                             "%d", ImGuiSliderFlags_AlwaysClamp);
        const auto score=config.Outputs[Descriptors.DisplayBin];
        ImGui::Text("Property: %s",score.Name.c_str());
        if(ImGui::Button("Show histogram bin") ||
           (displayBinChanged && Descriptors.FollowDisplayBin))
        {
            const auto status = ShowProcessingProperty(context, config.StableEntityId, score);
            Descriptors.VisualizationDiagnostic = Runtime::DebugNameForEditorCommandStatus(status);
            if (status == Runtime::EditorCommandStatus::Applied ||
                status == Runtime::EditorCommandStatus::NoChange)
                Descriptors.FollowDisplayBin = true;
        }
        if(!Descriptors.VisualizationDiagnostic.empty())ImGui::Text("Display: %s",Descriptors.VisualizationDiagnostic.c_str());
        if(Descriptors.LastResult)
        {
            const auto& result=*Descriptors.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s",Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Requested: %s; ran: %s",Runtime::ToString(result.RequestedBackend),result.ActualBackend.c_str());
            ImGui::Text("Live / total: %zu / %zu; rows written: %zu",result.LiveCount,result.SlotCount,result.WrittenCount);
            ImGui::Text("Spacing: %.5g; feature radius: %.5g",double(result.Scale.MeanSpacing),double(result.Scale.FeatureRadius));
            ImGui::Text("GPU batches: %zu; largest indexed support: %zu",result.GpuQueryBatches,result.MaximumNeighbors);
            ImGui::TextWrapped("%s",result.Message.c_str());
            DrawDismissLastResultButton("Dismiss##Descriptors", Descriptors.LastResult, Runtime::EditorPointAnalysisResultSlot::DescriptorAnalysis, context.PointAnalysis.ResultSinks.DismissResult);
        }
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawDensityWindow(bool &open, const SandboxEditorContext &context)
    {
        if (context.PointFields.Results.LastKernelDensityResult)
            Density.LastResult = context.PointFields.Results.LastKernelDensityResult;
        ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Kernel Density", &open))
        {
            ImGui::End();
            return;
        }
        const auto active = Runtime::GetEditorKernelDensityConfig(context.PointFields.Commands)
                                .value_or(Runtime::KernelDensityConfig{});
        const auto serialized = Runtime::SerializeKernelDensityConfig(active);
        Density.Synchronize(active, serialized);
        auto &config = Density.Draft;
        bool changed = false;
        changed |= DrawProcessingEntity("Entity##Density", context,
            config.StableEntityId, Density.LastSelectedEntity);
        DrawProcessingCpuBackend();
        if (DrawProcessingPointInput("Positions##Density", [&] { return Runtime::GetEditorKernelDensityInputCatalog(context.PointFields.Commands, config.StableEntityId); }, config.Positions))
        {
            config.Density.Domain = config.Positions.Domain;
            changed = true;
        }
        changed |= DrawProcessingPropertyName("Density property", config.Density.Name);
        int backend=int(config.Backend);
        if(ImGui::Combo("Acceleration",&backend,"CPU octree\0CPU LBVH (cached)\0Vulkan LBVH\0")) {config.Backend=Runtime::KernelDensityBackend(backend);changed=true;}
        changed |= ImGui::InputScalar("Neighbors k",ImGuiDataType_U32,&config.KNeighbors);
        changed |= ImGui::InputFloat("Bandwidth (0 = automatic)",&config.Bandwidth);
        ImGui::TextWrapped("Local Gaussian average over nearest candidates. Automatic bandwidth uses nearest-other spacing. Distances use the selected property coordinates.");
        if(config.Backend==Runtime::KernelDensityBackend::VulkanLBVH)
            changed |= ImGui::InputScalar("GPU query batch size",ImGuiDataType_U32,&config.GpuQueryBatchSize);
        DrawProcessingExecution(context, Density, changed,
            [&](const auto& c) { return Runtime::PreviewEditorKernelDensityCommand(context.PointFields.Commands, c); },
            [&](const auto& c) { return Runtime::ApplyEditorKernelDensityConfig(context.PointFields.Commands, c); },
            [&] { return Runtime::ApplyEditorConfiguredKernelDensity(context.PointFields.Commands, context.PointFields.ResultSinks.KernelDensity); },
            context.PointFields.ResultSinks.KernelDensity, "Estimate density",
            "Controls were rejected by density config validation.", "Density config was rejected.");
        ImGui::TextWrapped("Vulkan computes neighbors; bandwidth and Gaussian evaluation run on CPU. The named density property supports Undo.");
        if (ImGui::Button("Show density"))
            Density.VisualizationDiagnostic = Runtime::DebugNameForEditorCommandStatus(
                ShowProcessingProperty(context, config.StableEntityId, config.Density));
        if(!Density.VisualizationDiagnostic.empty())ImGui::Text("Display: %s",Density.VisualizationDiagnostic.c_str());
        if(Density.LastResult)
        {
            const auto& result=*Density.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s",Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Requested: %s; ran: %s",Runtime::ToString(result.RequestedBackend),result.ActualBackend.c_str());
            ImGui::Text("Live / total: %zu / %zu",result.LiveCount,result.SlotCount);
            ImGui::Text("Bandwidth %.5g; density min / mean / max: %.5g / %.5g / %.5g",double(result.UsedBandwidth),double(result.MinDensity),double(result.MeanDensity),double(result.MaxDensity));
            ImGui::TextWrapped("%s",result.Message.c_str());
            DrawDismissLastResultButton("Dismiss##Density", Density.LastResult, Runtime::EditorPointFieldResultSlot::KernelDensity, context.PointFields.ResultSinks.DismissResult);
        }
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawDensityWeightsWindow(bool &open, const SandboxEditorContext &context)
    {
        if (context.PointAnalysis.Results.LastDensityWeightResult)
            DensityWeights.LastResult = context.PointAnalysis.Results.LastDensityWeightResult;
        ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Compact Density Weights", &open))
        {
            ImGui::End();
            return;
        }
        const auto active = Runtime::GetEditorDensityWeightConfig(context.PointAnalysis.Commands)
                                .value_or(Runtime::DensityWeightConfig{});
        const auto serialized = Runtime::SerializeDensityWeightConfig(active);
        DensityWeights.Synchronize(active, serialized);
        auto &config = DensityWeights.Draft;
        bool changed = false;
        changed |= DrawProcessingEntity("Entity##DensityWeights", context,
            config.StableEntityId, DensityWeights.LastSelectedEntity);
        DrawProcessingCpuBackend();
        if (DrawProcessingPointInput("Positions##DensityWeights", [&] { return Runtime::GetEditorPointInputCatalog(context.Processing, config.StableEntityId); }, config.Positions))
        {
            config.Weights.Domain = config.Positions.Domain;
            changed = true;
        }
        for (auto [label, ref] : {std::pair{"Weight property", &config.Weights}})
            changed |= DrawProcessingPropertyName(label, ref->Name);
        int backend=int(config.Backend);
        if(ImGui::Combo("Acceleration",&backend,"CPU KD-tree\0CPU LBVH (cached)\0Vulkan LBVH\0")) {config.Backend=Runtime::DensityWeightBackend(backend);changed=true;}
        changed |= ImGui::InputDouble("Support radius",&config.SupportRadius);
        int kernel=int(config.Kernel),mode=int(config.Mode);
        if(ImGui::Combo("Kernel",&kernel,"Gaussian (sigma = h/4)\0LOP theta\0Wendland C2\0"))
        {config.Kernel=decltype(config.Kernel)(kernel);changed=true;}
        if(ImGui::Combo("Weight",&mode,"Direct\0Reciprocal\0"))
        {config.Mode=decltype(config.Mode)(mode);changed=true;}
        ImGui::TextWrapped("Sums compact kernel contributions inside the support radius, with a leading 1. An isolated sample has weight 1. Units follow the selected position property.");
        if(config.Backend==Runtime::DensityWeightBackend::VulkanLBVH)
        {
            changed |= ImGui::InputScalar("GPU query batch size",ImGuiDataType_U32,&config.GpuQueryBatchSize);
            changed |= ImGui::InputScalar("GPU radius capacity",ImGuiDataType_U32,&config.GpuRadiusCapacity);
            ImGui::TextWrapped("Vulkan collects complete conservative radius candidates. Overflow leaves the previous output unchanged. Subnormal coordinate components are unsupported.");
        }
        DrawProcessingExecution(context, DensityWeights, changed,
            [&](const auto& request) { return Runtime::PreviewEditorDensityWeightCommand(context.PointAnalysis.Commands, request); },
            [&](const auto& request) { return Runtime::ApplyEditorDensityWeightConfig(context.PointAnalysis.Commands, request); },
            [&] { return Runtime::ApplyEditorConfiguredDensityWeight(context.PointAnalysis.Commands, context.PointAnalysis.ResultSinks.DensityWeight); },
            context.PointAnalysis.ResultSinks.DensityWeight, "Compute compact weights",
            "Controls were rejected by density config validation.", "Density config was rejected.");
        ImGui::TextWrapped("Vulkan computes radius candidates; strict support and kernel reduction run on CPU. The named weight property supports Undo.");
        auto density=config.Weights;
        if(ImGui::Button("Show weights"))
            DensityWeights.VisualizationDiagnostic=Runtime::DebugNameForEditorCommandStatus(ShowProcessingProperty(context, config.StableEntityId, density));
        if(!DensityWeights.VisualizationDiagnostic.empty())ImGui::Text("Display: %s",DensityWeights.VisualizationDiagnostic.c_str());
        if(DensityWeights.LastResult)
        {
            const auto& result=*DensityWeights.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s",Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Requested: %s; ran: %s",Runtime::ToString(result.RequestedBackend),result.ActualBackend.c_str());
            ImGui::Text("Live / total: %zu / %zu",result.LiveCount,result.SlotCount);
            ImGui::Text("Weight min / max: %.5g / %.5g",double(result.MinWeight),double(result.MaxWeight));
            ImGui::Text("Contributions: %zu; largest candidate row: %zu",result.Diagnostics.NeighborContributionCount,result.MaximumNeighbors);
            ImGui::Text("Query radius %.5g; GPU batches %zu",double(result.QueryRadius),result.GpuQueryBatches);
            ImGui::Text("Cache reused: %s; CPU %.3f ms; GPU neighborhoods %.3f ms",result.IndexReused?"yes":"no",result.CpuComputeMilliseconds,result.GpuNeighborhoodMilliseconds);
            ImGui::TextWrapped("%s",result.Message.c_str());
            DrawDismissLastResultButton("Dismiss##DensityWeights", DensityWeights.LastResult, Runtime::EditorPointAnalysisResultSlot::DensityWeight, context.PointAnalysis.ResultSinks.DismissResult);
        }
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawConstructionWindow(bool& open,
                                                            const SandboxEditorContext& context)
    {
        if (context.PointConstruction.Results.LastPointConstructionResult)
            Construction.LastResult = context.PointConstruction.Results.LastPointConstructionResult;
        ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Construct from Points", &open))
        {
            ImGui::End();
            return;
        }
        const auto active = Runtime::GetEditorPointConstructionConfig(context.PointConstruction.Commands)
                                .value_or(Runtime::PointConstructionConfig{});
        const auto serialized = Runtime::SerializePointConstructionConfig(active);
        Construction.Synchronize(active, serialized);
        auto& config = Construction.Draft;
        bool changed = false;
        changed |= DrawProcessingEntity("Entity##Construction", context,
            config.StableEntityId, Construction.LastSelectedEntity);
        DrawProcessingCpuBackend();
        if (DrawProcessingPointInput("Positions##Construction", [&] { return Runtime::GetEditorPointInputCatalog(context.Processing, config.StableEntityId); }, config.Positions))
        {
            config.Normals.Domain = config.Positions.Domain;
            changed = true;
        }
        int method = int(config.Method), backend = int(config.Backend);
        if (ImGui::Combo("Method", &method, "Hoppe surface\0kNN graph\0"))
        {
            config.Method = Runtime::PointConstructionMethod(method);
            changed = true;
        }
        if (ImGui::Combo("Acceleration", &backend, "CPU reference\0CPU LBVH (cached)\0Vulkan LBVH\0"))
        {
            config.Backend = Runtime::PointConstructionBackend(backend);
            changed = true;
        }
        std::array<char, 257> outputName{};
        std::copy_n(config.OutputName.c_str(),
                    std::min(config.OutputName.size(), outputName.size() - 1), outputName.data());
        if (ImGui::InputText("Output entity", outputName.data(), outputName.size()))
        {
            config.OutputName = outputName.data();
            changed = true;
        }
        const auto integer = [&](const char* label, std::uint32_t& value)
        {
            int draft = int(value);
            if (ImGui::InputInt(label, &draft))
            {
                value = std::uint32_t(std::max(0, draft));
                changed = true;
            }
        };
        integer("Neighbors k", config.KNeighbors);
        if (config.Method == Runtime::PointConstructionMethod::Hoppe)
        {
            changed |= ImGui::Checkbox("Estimate normals (CPU)", &config.EstimateNormals);
            if (config.EstimateNormals)
                integer("Normal neighbors", config.NormalKNeighbors);
            else
            {
                const auto label = std::string(Runtime::ToString(config.Normals.Domain)) + ": " +
                                   config.Normals.Name;
                if (ImGui::BeginCombo("Normals", label.c_str()))
                {
                    const auto catalog = Runtime::GetEditorPointInputCatalog(
                        context.Processing, config.StableEntityId);
                    for (const auto& row : catalog.Entries)
                    {
                        if (row.Ref.Domain != config.Positions.Domain &&
                            config.Positions.Domain != Runtime::GeometryElementDomain::Unknown)
                            continue;
                        const auto name =
                            std::string(Runtime::ToString(row.Ref.Domain)) + ": " + row.Ref.Name;
                        if (ImGui::Selectable(name.c_str(), row.Ref == config.Normals))
                        {
                            config.Normals = row.Ref;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            integer("Grid resolution", config.Resolution);
            integer("Maximum grid vertices", config.MaxGridVertices);
            changed |= ImGui::InputFloat("Bounding-box padding", &config.BoundingBoxPadding);
            changed |= ImGui::InputFloat("Normal agreement power", &config.NormalAgreementPower);
            changed |= ImGui::InputFloat("Kernel sigma scale", &config.KernelSigmaScale);
            ImGui::TextWrapped("Point-anchored tangent-plane field; k > 1 uses k+1 weighted "
                               "samples. Normal estimation and surface extraction run on the CPU.");
        }
        else
        {
            changed |= ImGui::Checkbox("Mutual neighbors", &config.Mutual);
            changed |= ImGui::InputFloat("Minimum pair distance", &config.MinDistanceEpsilon, 0, 0,
                                         "%.6g");
            ImGui::TextWrapped("Uses k+1 candidates before self/near-duplicate filtering. Creates "
                               "an undirected union or mutual graph.");
        }
        integer("Query batch size", config.GpuQueryBatchSize);
        ImGui::TextWrapped("Creates a separate, selectable entity in the source's current world "
                           "position. Distances are measured in the input property's coordinates.");
        if (changed)
        {
            const auto applied =
                Runtime::ApplyEditorPointConstructionConfig(context.PointConstruction.Commands, config);
            Construction.ConfigDiagnostic =
                applied.Succeeded() ? ""
                                    : "Controls were rejected by construction config validation.";
        }
        if (!Construction.ConfigDiagnostic.empty())
            ImGui::TextWrapped("%s", Construction.ConfigDiagnostic.c_str());
        const auto readiness =
            Runtime::PreviewEditorPointConstructionCommand(context.PointConstruction.Commands, config);
        if (!readiness.Ready)
            ImGui::TextWrapped("%s", readiness.Diagnostic.c_str());
        ImGui::BeginDisabled(!context.ProcessingConfigCommandsAvailable || !readiness.Ready ||
                             !Construction.ConfigDiagnostic.empty());
        if (ImGui::Button("Construct"))
        {
            const auto applied = Runtime::ApplyEditorPointConstructionConfig(
                context.PointConstruction.Commands, readiness.Resolved);
            if (applied.Succeeded())
                PublishCommandResult(
                    Construction.LastResult,
                    Runtime::ApplyEditorConfiguredPointConstruction(
                        context.PointConstruction.Commands,
                        context.PointConstruction.ResultSinks.PointConstruction),
                    context.PointConstruction.ResultSinks.PointConstruction);
            else
                Construction.ConfigDiagnostic = "Construction config was rejected.";
        }
        ImGui::EndDisabled();
        if (Construction.LastResult)
        {
            const auto& result = *Construction.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s", Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Requested: %s; ran: %s", Runtime::ToString(result.RequestedBackend),
                        result.ActualBackend.c_str());
            ImGui::Text("Input samples / queries: %zu / %zu", result.InputCount, result.QueryCount);
            ImGui::Text("Output vertices / edges / faces: %zu / %zu / %zu",
                        result.OutputVertexCount, result.OutputEdgeCount, result.OutputFaceCount);
            ImGui::Text("GPU batches: %zu; cache reused: %s", result.GpuQueryBatches,
                        result.IndexReused ? "yes" : "no");
            ImGui::Text("CPU %.3f ms; GPU batch latency %.3f ms", result.CpuComputeMilliseconds,
                        result.GpuNeighborhoodMilliseconds);
            ImGui::TextWrapped("%s", result.Message.c_str());
            DrawDismissLastResultButton("Dismiss##Construction", Construction.LastResult, Runtime::EditorPointConstructionResultSlot::PointConstruction, context.PointConstruction.ResultSinks.DismissResult);
        }
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawSpacingWindow(bool &open, const SandboxEditorContext &context)
    {
        if (context.PointFields.Results.LastPointSpacingResult)
            Spacing.LastResult = context.PointFields.Results.LastPointSpacingResult;
        ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Point Spacing and Radii", &open))
        {
            ImGui::End();
            return;
        }
        const auto active = Runtime::GetEditorPointSpacingConfig(context.PointFields.Commands)
                                .value_or(Runtime::PointSpacingConfig{});
        const auto serialized = Runtime::SerializePointSpacingConfig(active);
        Spacing.Synchronize(active, serialized);
        auto &config = Spacing.Draft;
        bool changed = false;
        changed |= DrawProcessingEntity("Entity##Spacing", context,
            config.StableEntityId, Spacing.LastSelectedEntity);
        DrawProcessingCpuBackend();
        if (DrawProcessingPointInput("Positions##Spacing", [&] { return Runtime::GetEditorPointSpacingInputCatalog(context.PointFields.Commands, config.StableEntityId); }, config.Positions))
        {
            config.Radii.Domain = config.Positions.Domain;
            changed = true;
        }
        changed |= DrawProcessingPropertyName("Radii property", config.Radii.Name);
        int backend=int(config.Backend);
        if(ImGui::Combo("Acceleration",&backend,"CPU octree\0CPU LBVH (cached)\0Vulkan LBVH\0")) {config.Backend=Runtime::PointSpacingBackend(backend);changed=true;}
        changed |= ImGui::InputScalar("Neighbors k",ImGuiDataType_U32,&config.KNeighbors);
        changed |= ImGui::InputFloat("Radius scale",&config.ScaleFactor);
        ImGui::TextWrapped("Radius = scale times mean retained neighbor distance. Nearest-other spacing is reported separately. Values use the selected property coordinates; coverage is not guaranteed.");
        if(config.Backend==Runtime::PointSpacingBackend::VulkanLBVH)
            changed |= ImGui::InputScalar("GPU query batch size",ImGuiDataType_U32,&config.GpuQueryBatchSize);
        DrawProcessingExecution(context, Spacing, changed,
            [&](const auto& c) { return Runtime::PreviewEditorPointSpacingCommand(context.PointFields.Commands, c); },
            [&](const auto& c) { return Runtime::ApplyEditorPointSpacingConfig(context.PointFields.Commands, c); },
            [&] { return Runtime::ApplyEditorConfiguredPointSpacing(context.PointFields.Commands, context.PointFields.ResultSinks.PointSpacing); },
            context.PointFields.ResultSinks.PointSpacing, "Estimate radii",
            "Controls were rejected by radii config validation.", "Spacing config was rejected.");
        ImGui::TextWrapped("Vulkan computes neighbors; spacing and radii are evaluated on CPU. Undo restores the named radius property. Show radii maps values to colors; point rendering currently expects pixel sizes.");
        if (ImGui::Button("Show radii"))
            Spacing.VisualizationDiagnostic = Runtime::DebugNameForEditorCommandStatus(
                ShowProcessingProperty(context, config.StableEntityId, config.Radii));
        if(!Spacing.VisualizationDiagnostic.empty())ImGui::Text("Display: %s",Spacing.VisualizationDiagnostic.c_str());
        if(Spacing.LastResult)
        {
            const auto& result=*Spacing.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s",Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Requested: %s; ran: %s",Runtime::ToString(result.RequestedBackend),result.ActualBackend.c_str());
            ImGui::Text("Live / total: %zu / %zu",result.LiveCount,result.SlotCount);
            ImGui::Text("Radius min / mean / max: %.5g / %.5g / %.5g",double(result.MinRadius),double(result.MeanRadius),double(result.MaxRadius));
            ImGui::Text("Nearest spacing min / mean / max: %.5g / %.5g / %.5g", double(result.Statistics.MinSpacing), double(result.Statistics.AverageSpacing), double(result.Statistics.MaxSpacing));
            ImGui::Text("Centroid: %.5g / %.5g / %.5g; bounds diagonal: %.5g", double(result.Statistics.Centroid.x), double(result.Statistics.Centroid.y), double(result.Statistics.Centroid.z), double(result.Statistics.BoundingBoxDiagonal));
            ImGui::TextWrapped("%s",result.Message.c_str());
            DrawDismissLastResultButton("Dismiss##Spacing", Spacing.LastResult, Runtime::EditorPointFieldResultSlot::PointSpacing, context.PointFields.ResultSinks.DismissResult);
        }
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawBilateralWindow(bool &open, const SandboxEditorContext &context)
    {
        if (context.PointSet.Results.LastBilateralFilterResult)
            Bilateral.LastResult = context.PointSet.Results.LastBilateralFilterResult;
        ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Bilateral Point Filter", &open))
        {
            ImGui::End();
            return;
        }
        const auto active = Runtime::GetEditorBilateralFilterConfig(context.PointSet.Commands)
                                .value_or(Runtime::BilateralFilterConfig{});
        const auto serialized = Runtime::SerializeBilateralFilterConfig(active);
        Bilateral.Synchronize(active, serialized);
        auto &config = Bilateral.Draft;
        bool changed = false;
        changed |= DrawProcessingEntity("Entity##Bilateral", context,
            config.StableEntityId, Bilateral.LastSelectedEntity);
        DrawProcessingCpuBackend();
        if (DrawProcessingPointInput("Positions##Bilateral", [&] { return Runtime::GetEditorBilateralFilterInputCatalog(context.PointSet.Commands, config.StableEntityId); }, config.Positions))
        {
            config.Output.Domain = config.Normals.Domain = config.Positions.Domain;
            changed = true;
        }
        const auto normalsLabel=std::string(Runtime::ToString(config.Normals.Domain))+": "+config.Normals.Name;
        if(ImGui::BeginCombo("Normals##Bilateral",normalsLabel.c_str()))
        {
            const auto catalog=Runtime::GetEditorBilateralFilterInputCatalog(context.PointSet.Commands,config.StableEntityId);
            for(const auto& row:catalog.Entries)
                if(row.Ref.Domain==config.Positions.Domain && ImGui::Selectable(row.Ref.Name.c_str(),row.Ref==config.Normals))
                {config.Normals=row.Ref;changed=true;}
            ImGui::EndCombo();
        }
        if(ImGui::Button("Write to input positions")){config.Output=config.Positions;changed=true;}
        for (auto [label, ref] : {std::pair{"Output positions", &config.Output}})
            changed |= DrawProcessingPropertyName(label, ref->Name);
        int backend=int(config.Backend);
        if(ImGui::Combo("Acceleration",&backend,"CPU octree\0CPU LBVH (cached)\0Vulkan LBVH\0")) {config.Backend=Runtime::BilateralFilterBackend(backend);changed=true;}
        changed |= ImGui::InputScalar("Neighbors k",ImGuiDataType_U32,&config.KNeighbors);
        changed |= ImGui::InputFloat("Spatial sigma (0 = automatic)",&config.SpatialSigma);
        changed |= ImGui::InputFloat("Normal sigma",&config.NormalSigma);
        changed |= ImGui::InputScalar("Iterations",ImGuiDataType_U32,&config.Iterations);
        ImGui::TextWrapped("Filters positions along fixed input normals. Each pass rebuilds neighborhoods; automatic spatial sigma is resolved once.");
        if(config.Backend==Runtime::BilateralFilterBackend::VulkanLBVH)
            changed |= ImGui::InputScalar("GPU query batch size",ImGuiDataType_U32,&config.GpuQueryBatchSize);
        DrawProcessingExecution(context, Bilateral, changed,
            [&](const auto& request) { return Runtime::PreviewEditorBilateralFilterCommand(context.PointSet.Commands, request); },
            [&](const auto& request) { return Runtime::ApplyEditorBilateralFilterConfig(context.PointSet.Commands, request); },
            [&] { return Runtime::ApplyEditorConfiguredBilateralFilter(context.PointSet.Commands, context.PointSet.ResultSinks.BilateralFilter); },
            context.PointSet.ResultSinks.BilateralFilter, "Filter positions",
            "Controls were rejected by filter config validation.", "Bilateral config was rejected.");
        ImGui::TextWrapped("Vulkan computes neighbors; weights and position updates run on CPU. Only the final result is published. Choose the input position property as output to update the displayed geometry; Undo restores it.");
        if(Bilateral.LastResult)
        {
            const auto& result=*Bilateral.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s",Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Requested: %s; ran: %s",Runtime::ToString(result.RequestedBackend),result.ActualBackend.c_str());
            ImGui::Text("Live / total: %zu / %zu",result.LiveCount,result.SlotCount);
            ImGui::Text("Passes: %u; spatial sigma: %.5g",result.CompletedIterations,double(result.SpatialSigmaUsed));
            ImGui::Text("Last-pass displacement mean / max: %.5g / %.5g",double(result.Diagnostics.AverageDisplacement),double(result.Diagnostics.MaxDisplacement));
            ImGui::Text("Degenerate normals: %zu; private index builds: %zu",result.Diagnostics.DegenerateNormals,result.WorkspaceBuilds);
            ImGui::TextWrapped("%s",result.Message.c_str());
            DrawDismissLastResultButton("Dismiss##Bilateral", Bilateral.LastResult, Runtime::EditorPointSetResultSlot::BilateralFilter, context.PointSet.ResultSinks.DismissResult);
        }
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawRegistrationWindow(
        bool& open, const SandboxEditorContext& context)
    {
        if (context.Registration.Results.LastRegistrationResult.has_value())
            Registration.LastResult = *context.Registration.Results.LastRegistrationResult;
        ImGui::SetNextWindowSize(
            ImVec2(360.0f, 320.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("ICP Registration", &open))
        {
            ImGui::End();
            return;
        }

        ImGui::TextWrapped("Align named point samples from mesh, graph or point-cloud domains. Applies an undoable transform to the source entity.");
        const auto activeConfig = Runtime::GetEditorRegistrationConfig(context.Registration.Commands).value_or(Runtime::RegistrationConfig{});
        const auto activeText = Runtime::SerializeRegistrationConfig(activeConfig);
        if (activeText != Registration.LastApplied)
        { Registration.Draft = activeConfig; Registration.LastApplied = activeText; Registration.ConfigDiagnostic.clear(); }
        auto& config = Registration.Draft;
        bool changed = false;
        changed |= DrawProcessingEntity("Source##ICP", context,
            config.SourceStableEntityId, Registration.LastSelectedSource);
        changed |= DrawProcessingEntity("Target##ICP", context,
            config.TargetStableEntityId, Registration.LastSelectedTarget, std::nullopt, 1u);
        DrawProcessingCpuBackend();
        if (ImGui::Button("Swap source and target"))
        {
            std::swap(config.SourceStableEntityId, config.TargetStableEntityId);
            std::swap(config.SourcePositions, config.TargetPositions);
            changed = true;
        }
        const auto sourceCatalog = Runtime::GetEditorRegistrationInputCatalog(context.Registration.Commands, config.SourceStableEntityId);
        const auto targetCatalog = Runtime::GetEditorRegistrationInputCatalog(context.Registration.Commands, config.TargetStableEntityId);
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
        if (ImGui::Combo("Acceleration##ICP", &backend, "CPU KD-tree (reference)\0CPU LBVH (cached)\0Vulkan LBVH (CPU solve)\0"))
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
            const auto applied = Runtime::ApplyEditorRegistrationConfig(context.Registration.Commands, config);
            Registration.ConfigDiagnostic = applied.Status == Runtime::RuntimeEngineConfigApplyStatus::Rejected
                ? "Registration controls were rejected by config validation." : "";
        }
        if (!Registration.ConfigDiagnostic.empty()) ImGui::TextWrapped("%s", Registration.ConfigDiagnostic.c_str());
        const auto readiness = Runtime::PreviewEditorRegistrationCommand(context.Registration.Commands, config);
        if (!readiness.Ready) ImGui::TextWrapped("%s", readiness.Diagnostic.c_str());
        ImGui::BeginDisabled(!context.ProcessingConfigCommandsAvailable || !Registration.ConfigDiagnostic.empty() || !readiness.Ready);
        const bool runFinal = ImGui::Button("Run ICP##ICP");
        if (runFinal)
        {
            config.TrajectoryStep = config.MaxIterations;
            (void)Runtime::ApplyEditorRegistrationConfig(context.Registration.Commands, config);
        }
        if (runFinal || (applyTrajectory && readiness.Ready && Registration.ConfigDiagnostic.empty()))
            PublishCommandResult(Registration.LastResult,
                Runtime::ApplyEditorConfiguredRegistrationCommand(
                    context.Registration.Commands, context.Registration.ResultSinks.Registration),
                context.Registration.ResultSinks.Registration);
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
            if (DrawDismissLastResultButton("Dismiss##Registration"))
            {
                Registration.LastResult.reset();
                if (context.Registration.ResultSinks.DismissResult)
                    context.Registration.ResultSinks.DismissResult();
            }
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
        ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Mesh / Geodesics / Virtual Source Propagation", &open))
        {
            ImGui::End();
            return;
        }
        const auto previousEntity = GeodesicsInput.Entity;
        DrawProcessingEntity("Entity##Geodesics", context, GeodesicsInput.Entity,
                             GeodesicsInput.PreviousSelection, Runtime::EditorDomainWindowKind::Mesh);
        DrawProcessingCpuBackend();
        if (GeodesicsInitialized && previousEntity != GeodesicsInput.Entity)
        {
            // Vertex indices belong to the previous mesh, even when the new
            // mesh happens to have slots with the same indices.
            GeodesicsConfig.SourceVertices.clear();
            GeodesicsDirty = true;
            GeodesicsResult.reset();
            GeodesicsMessage.clear();
            GeodesicsSourceVertex = 0;
            // Publish the reset even when the new selection has no mesh UI.
            GeodesicsDirty = !Runtime::ApplyEditorGeodesicsConfig(
                context.MeshFields.Commands, GeodesicsConfig).Succeeded();
        }
        const auto& model = GetDomainWindowModel(context, Runtime::EditorDomainWindowKind::Mesh,
                                                GeodesicsInput.Entity);
        if (model.DomainMatches && model.Processing.HasSelectedEntity)
            DrawGeodesicsControls(model, context);
        else
            ImGui::TextDisabled("Choose a mesh entity to compute geodesic distances.");
        ImGui::End();
    }
    void MeshProcessingPanels::Impl::DrawGeodesicsControls(
        const Runtime::EditorDomainWindowModel& model, const SandboxEditorContext& context)
    {
        if (!GeodesicsDirty)
        {
            if (auto config = Runtime::GetEditorGeodesicsConfig(context.MeshFields.Commands))
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
            context.Processing, model.SelectedStableId, Runtime::GeometryElementDomain::MeshVertex);
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
                    row.ValueKind != decltype(row.ValueKind)::Vec3 || !row.Bindable)
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
        ImGui::SeparatorText("Output properties");
        GeodesicsDirty |= DrawProcessingPropertyName("Distance property", GeodesicsConfig.DistanceProperty);
        GeodesicsDirty |= DrawProcessingPropertyName("Source mask property", GeodesicsConfig.SourceMaskProperty);
        if (ImGui::InputScalar("Expansion budget", ImGuiDataType_U32,
                               &GeodesicsConfig.MaxHalfedgeExpansions))
            GeodesicsDirty = true;
        if (GeodesicsDirty)
        {
            const auto applied = Runtime::ApplyEditorGeodesicsConfig(
                context.MeshFields.Commands, GeodesicsConfig);
            if (applied.Succeeded())
                GeodesicsDirty = false;
            else
                GeodesicsMessage = "Geodesics config was rejected; check property names and expansion budget.";
        }
        ImGui::BeginDisabled(GeodesicsDirty || GeodesicsConfig.SourceVertices.empty() ||
                             !context.ProcessingConfigCommandsAvailable);
        if (ImGui::Button("Compute geodesics"))
        {
            GeodesicsResult = Runtime::ApplyEditorConfiguredGeodesicsCommand(
                context.MeshFields.Commands, model.SelectedStableId);
            GeodesicsMessage = GeodesicsResult->Message;
        }
        ImGui::EndDisabled();
        ImGui::SeparatorText("Display output properties");
        ImGui::TextDisabled("Unreachable distances are shown in gray.");
        DrawProcessingPropertyShowButton(context, model.SelectedStableId,
            {Runtime::GeometryElementDomain::MeshVertex, GeodesicsConfig.DistanceProperty, Geometry::PropertyValueKind::Double}, GeodesicsMessage);
        DrawProcessingPropertyShowButton(context, model.SelectedStableId,
            {Runtime::GeometryElementDomain::MeshVertex, GeodesicsConfig.SourceMaskProperty, Geometry::PropertyValueKind::Bool}, GeodesicsMessage);
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
