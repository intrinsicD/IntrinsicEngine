module;

#include <functional>
#include <span>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <imgui.h>
#include <implot.h>
#include <glm/glm.hpp>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>

module Extrinsic.Sandbox.Editor.MeshProcessingPanels;

import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.PointSamplingOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.ScalarRidgeOperations;
import Extrinsic.Sandbox.Editor.Shell;
import Extrinsic.Runtime.SceneInteractionModule;

import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorPropertyWidgets;
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

#include "Sandbox.PanelSupport.hpp"
#include "Sandbox.PointSamplingControls.hpp"
#include "Sandbox.RegistrationTracePlots.hpp"

namespace Extrinsic::Sandbox::Editor
{
    namespace
    {
        constexpr std::array<Runtime::EditorMeshDenoiseStage, 1>
            kMeshDenoiseStages{{
                Runtime::EditorMeshDenoiseStage::FullBilateral,
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

        template <typename State, typename Request, typename Apply, typename Execute, typename Sink>
        void ApplyProcessingExecution(State& state, const Request& request,
            Apply apply, Execute execute, const Sink& sink, const char* rejected)
        {
            const bool applied = apply(request).Succeeded();
            state.ConfigDiagnostic = applied ? "" : rejected;
            if (applied)
                PublishCommandResult(state.LastResult, execute(), sink);
        }

        // `watch(draft)` names the run being submitted as {entity, output property name}; the panel's
        // run slot captures it at submit, before the editable draft can change.
        template <typename State, typename Preview, typename Apply, typename Execute, typename Sink,
                  typename Watch = std::nullptr_t>
        void DrawProcessingExecution(const Runtime::EditorProcessingCommands& commands, State& state, bool changed,
            Preview preview, Apply apply, Execute execute, const Sink& sink,
            const char* button, const char* controlsRejected, const char* executionRejected, Watch watch = nullptr)
        {
            if (changed)
                state.ConfigDiagnostic = apply(state.Draft).Succeeded() ? "" : controlsRejected;
            if (!state.ConfigDiagnostic.empty()) ImGui::TextWrapped("%s", state.ConfigDiagnostic.c_str());
            const auto method = preview(state.Draft);
            const auto readiness = Runtime::ResolveEditorProcessingActionReadiness(
                commands, method);
            if (!readiness.Enabled) ImGui::TextWrapped("%s", readiness.DisabledReason.c_str());
            if (DrawProcessingActionButton(button, readiness))
            {
                if constexpr (!std::is_null_pointer_v<Watch>)
                {
                    auto [entity, output] = watch(state.Draft); // named before the run, from what is submitted
                    ApplyQueuedProcessingExecution(commands, state, state.Draft, apply, execute, sink, executionRejected,
                                                   entity, std::move(output));
                }
                else
                    ApplyProcessingExecution(state, state.Draft, apply, execute, sink, executionRejected);
            }
        }

        template<class State,class Sink>
        bool DrawPointScalarTransaction(const Runtime::EditorProcessingCommands& commands,
            Runtime::EditorPointScalarTransactionHandle& run,State& state,const Sink& sink)
        {
            if(!run){state.Run.AwaitingAccept(false);return false;}
            const auto snapshot=Runtime::SnapshotEditorPointScalar(commands,run);
            const auto phase=snapshot.Phase;
            state.Run.AwaitingAccept(phase==Runtime::EditorGpuTransactionPhase::ReadyToAccept);
            const bool active=phase==Runtime::EditorGpuTransactionPhase::Running||phase==Runtime::EditorGpuTransactionPhase::Accepting||phase==Runtime::EditorGpuTransactionPhase::ReadyToAccept;
            // External producers deliver their diagnostics through their own completion sink.
            constexpr bool scalarDiagnostics=requires { Runtime::UpdateEditorPointScalarResult(*state.LastResult,snapshot); };
            if(!active){
                if constexpr(scalarDiagnostics) {
                    if(state.LastResult){auto result=*state.LastResult;Runtime::UpdateEditorPointScalarResult(result,snapshot);
                        PublishCommandResult(state.LastResult,std::move(result),sink);}
                }
                run.reset();return false;}
            ImGui::TextWrapped("%s",snapshot.Message.c_str());
            if constexpr(scalarDiagnostics)
                ImGui::Text("Input upload: %llu bytes; residency hits: %llu; CPU readback: %llu bytes",
                    static_cast<unsigned long long>(snapshot.GpuInputUploadBytes),static_cast<unsigned long long>(snapshot.GpuInputCacheHits),static_cast<unsigned long long>(snapshot.CpuStageReadbackBytes));
            if(phase==Runtime::EditorGpuTransactionPhase::ReadyToAccept){
                ImGui::BeginDisabled(!snapshot.CanAccept);
                if(ImGui::Button("Accept"))(void)Runtime::AcceptEditorPointScalar(commands,run);
                ImGui::EndDisabled();
                if(!snapshot.AcceptRefusalReason.empty())ImGui::TextWrapped("%s",snapshot.AcceptRefusalReason.c_str());}
            if(ImGui::Button(phase==Runtime::EditorGpuTransactionPhase::Running?"Stop":"Discard")){
                Runtime::DiscardEditorPointScalar(commands,run);
                state.Run.Forget();} // a stopped or discarded run never reads as finished
            return true;
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
            OperationRunSlot Run{};
            ProcessingEntityInput Input{};
            std::optional<Runtime::EditorMeshDenoiseResult> LastResult{};
            std::int32_t Stage{0};
            std::int32_t NormalIterations{5};
            std::int32_t VertexIterations{10};
            float SigmaSpatial{0.0f};
            float SigmaRange{0.0f};
            bool PreserveBoundary{true};
        };

        using CurvatureState = ProcessingDraftState<Runtime::MeshCurvatureConfig, Runtime::EditorMeshCurvatureResult>;
        using GeodesicsState = ProcessingDraftState<Runtime::GeodesicsConfig, Runtime::EditorGeodesicsResult>;
        struct SegmentationState : ProcessingDraftState<Runtime::CurvatureSegmentationConfig, Runtime::EditorCurvatureSegmentationResult>
        {
            ProcessingEntityInput Input{};
            bool Dirty{false};
            bool AutoVisualize{true};
        };

        struct RemeshState
        {
            OperationRunSlot Run{};
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
            OperationRunSlot Run{};
            ProcessingEntityInput Input{};
            std::optional<Runtime::EditorMeshSubdivideResult> LastResult{};
            std::int32_t Operator{0};
            std::int32_t Iterations{1};
            bool PreserveLoopFeatures{false};
            Runtime::GeometryPropertyRef FeatureEdges{Runtime::GeometryElementDomain::MeshEdge, "e:feature", Geometry::PropertyValueKind::Bool};
        };

        struct SimplifyState
        {
            OperationRunSlot Run{};
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
            // UI-067: the live view of the last run, its preview and plots.
            Runtime::EditorRegistrationProgressHandle Progress{};
            std::uint32_t RunMaxIterations{0u}; // iteration cap of the run in flight, captured at its start
            std::chrono::steady_clock::time_point RunStarted{}; // when that run was started
            OperationRunSlot Run{};
            bool LivePreview{true}, PlotBySeconds{false}, PreviewShown{false};
            std::uint64_t PreviewRevision{0u};
        };

        struct PointSamplingState
        {
            OperationRunSlot Run{};
            std::optional<std::vector<std::uint32_t>> LastSelected{};
            Runtime::PointSamplingOperationConfig Draft{};
            std::string LastApplied{}, ConfigDiagnostic{};
            std::array<char, 128> WeightsName{}, RankName{}, SelectedName{};
            // Written by the (possibly deferred) completion of the last run.
            std::shared_ptr<std::optional<Runtime::EditorPointSamplingResult>> LastResult =
                std::make_shared<std::optional<Runtime::EditorPointSamplingResult>>();
        };

        struct CoherentPointDriftState
        {
            std::optional<std::vector<std::uint32_t>> LastSelectedSource{}, LastSelectedTarget{};
            Runtime::CoherentPointDriftConfig Draft{};
            std::string LastApplied{}, ConfigDiagnostic{};
            Runtime::EditorCoherentPointDriftRunHandle Run{};
            std::uint32_t RunMaxIterations{0u}; // iteration cap of the run in flight, captured at its start
            OperationRunSlot ProgressSlot{};
            std::string RunMessage{}, ExportMessage{};
            std::optional<Runtime::EditorCoherentPointDriftResult> LastResult{};
            bool LivePreview{true};
            bool ShowSubsamples{true};
            bool PlotBySeconds{false};
            std::uint64_t PreviewRevision{0u};
            std::array<char, 128> DisplacementName{};
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
        ProcessingDraftState<Runtime::ScalarGradientConfig, Runtime::EditorScalarGradientResult> Gradient{};
        std::uint32_t GradientEntity{};
        ProcessingDraftState<Runtime::PropertySmoothingConfig, Runtime::EditorPropertySmoothingResult> Smoothing{};
        // Vulkan smoothing completes on a later frame; the shared mailbox outlives the panel.
        std::shared_ptr<std::optional<Runtime::EditorPropertySmoothingResult>> SmoothingCompletion{
            std::make_shared<std::optional<Runtime::EditorPropertySmoothingResult>>()};
        // The interactive Vulkan run (ADR 0030): its preview shows in the viewport until the
        // user accepts or discards it.
        Runtime::EditorPropertySmoothingTransactionHandle SmoothingTransaction{};
        std::uint32_t SmoothingEntity{};
        ProcessingDraftState<Runtime::HarmonicFieldConfig, Runtime::EditorHarmonicFieldResult> Harmonic{};
        std::uint32_t HarmonicEntity{};
        ProcessingDraftState<Runtime::LaplacianEigenbasisConfig, Runtime::EditorLaplacianEigenbasisResult> Eigenbasis{};
        std::uint32_t EigenbasisEntity{};
        int SelectedEigenvector{};
        GeodesicsState Geodesics{};
        std::uint32_t GeodesicsEntity{0u};
        int GeodesicsSourceVertex{0};
        Runtime::EditorScalarRidgeCommand ScalarRidges{};
        std::optional<std::vector<std::uint32_t>> ScalarRidgesSelection{};
        std::optional<Runtime::EditorScalarRidgeResult> ScalarRidgesResult{};
        std::string ScalarRidgesVisualizationDiagnostic{};
        RemeshState Remesh{};
        SubdivideState Subdivide{};
        SimplifyState Simplify{};
        RegistrationState Registration{};
        CoherentPointDriftState CoherentPointDrift{};
        PointSamplingState PointSampling{};
        NormalsState Normals{};
        // The interactive Vulkan vertex-normals run (RUNTIME-296, ADR 0030): the device result
        // waits for Accept or Discard; its completion lands on a later frame.
        Runtime::EditorNormalTransactionHandle NormalTransaction{};
        std::shared_ptr<std::optional<Runtime::EditorNormalEstimationResult>> NormalCompletion{
            std::make_shared<std::optional<Runtime::EditorNormalEstimationResult>>()};
        OutliersState Outliers{};
        Runtime::EditorOutlierTransactionHandle OutlierTransaction{};
        Runtime::EditorPointScalarTransactionHandle DensityTransaction{}, SpacingTransaction{}, WeightTransaction{}, KeypointTransaction{};
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
        // IDs and targets must be string literals retained by the callback.
        void RegisterRedirectWindow(const char* id,
                                    std::vector<std::string> menuPath,
                                    std::string title, const char* target);
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
        void DrawGradientWindow(bool&, const SandboxEditorContext&);
        void DrawSmoothingWindow(bool&, const SandboxEditorContext&);
        void DrawSmoothingTransaction(const SandboxEditorContext&);
        void DrawHarmonicFieldWindow(bool&, const SandboxEditorContext&);
        void DrawEigenbasisWindow(bool&, const SandboxEditorContext&);
        void DrawGeodesicsWindow(bool&, const SandboxEditorContext&);
        void DrawGeodesicsControls(const Runtime::EditorDomainWindowModel&,
                                   const SandboxEditorContext&);
        void DrawScalarRidgesWindow(bool&, const SandboxEditorContext&);
        void DrawRemeshWindow(bool&, const SandboxEditorContext&);
        void DrawSubdivideWindow(bool&, const SandboxEditorContext&);
        void DrawSimplifyWindow(bool&, const SandboxEditorContext&);
        void DrawNormalsWindow(bool&, const SandboxEditorContext&);
        void DrawNormalTransaction(const SandboxEditorContext&);
        void DrawOutliersWindow(bool&, const SandboxEditorContext&);
        void DrawKeypointsWindow(bool&, const SandboxEditorContext&);
        void DrawDescriptorsWindow(bool&, const SandboxEditorContext&);
        void DrawDensityWindow(bool&, const SandboxEditorContext&);
        void DrawDensityWeightsWindow(bool&, const SandboxEditorContext&);
        void DrawConstructionWindow(bool&, const SandboxEditorContext&);
        void DrawSpacingWindow(bool&, const SandboxEditorContext&);
        void DrawBilateralWindow(bool&, const SandboxEditorContext&);
        void DrawRegistrationWindow(bool&, const SandboxEditorContext&);
        void DrawCoherentPointDriftWindow(bool&, const SandboxEditorContext&);
        void DrawPointSamplingWindow(bool&, const SandboxEditorContext&);
        void ClearCoherentPointDriftPreview();
        void DrawRegistrationProgress(const Runtime::EditorRegistrationProgressSnapshot& live, std::uint32_t maxIterations);

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
        RegisterWindow("view.property_smoothing", {"View"}, "Smooth Property", &Impl::DrawSmoothingWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.property_smoothing", "Mesh"}, {"graph.processing.property_smoothing", "Graph"},
                  {"pointcloud.processing.property_smoothing", "PointCloud"}}})
            RegisterRedirectWindow(id, {domain, "Processing"}, "Smooth Property", "view.property_smoothing");
        RegisterWindow("view.harmonic_field", {"View"}, "Harmonic Field", &Impl::DrawHarmonicFieldWindow);
        RegisterWindow("view.laplacian_eigenbasis", {"View"}, "Spectral Modes", &Impl::DrawEigenbasisWindow);
        for (const auto& [id, domain] : std::initializer_list<std::pair<const char*, const char*>>
                 {{"mesh.processing.laplacian_eigenbasis", "Mesh"}, {"graph.processing.laplacian_eigenbasis", "Graph"},
                  {"pointcloud.processing.laplacian_eigenbasis", "PointCloud"}})
            RegisterRedirectWindow(id, {domain, "Processing"}, "Spectral Modes", "view.laplacian_eigenbasis");
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.harmonic_field", "Mesh"}, {"graph.processing.harmonic_field", "Graph"},
                  {"pointcloud.processing.harmonic_field", "PointCloud"}}})
            RegisterRedirectWindow(id, {domain, "Processing"}, "Harmonic Field", "view.harmonic_field");
        RegisterWindow("mesh.processing.denoise", {"Mesh", "Processing"},
                       "Denoise", &Impl::DrawDenoiseWindow);
        RegisterWindow("mesh.processing.faces.scalar_gradient", {"Mesh", "Processing", "Faces"},
                       "Scalar Field Gradient", &Impl::DrawGradientWindow);
        RegisterWindow("mesh.processing.geodesics", {"Mesh", "Geodesics"},
                       "Virtual Source Propagation", &Impl::DrawGeodesicsWindow);
        RegisterWindow("mesh.processing.curvature", {"Mesh", "Processing"},
                       "Curvature", &Impl::DrawCurvatureWindow);
        RegisterWindow("mesh.processing.segmentation", {"Mesh", "Processing"},
                       "Curvature Segmentation", &Impl::DrawSegmentationWindow);
        RegisterWindow("mesh.processing.scalar_ridges", {"Mesh", "Processing"},
                       "Scalar Ridges", &Impl::DrawScalarRidgesWindow);
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
            RegisterRedirectWindow(id, {domain, "Processing"}, "Outlier Analysis",
                                   "view.outlier_analysis");
        RegisterWindow("view.keypoint_analysis", {"View"}, "ISS Keypoint Analysis", &Impl::DrawKeypointsWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.keypoints", "Mesh"}, {"graph.processing.keypoints", "Graph"},
                  {"pointcloud.processing.keypoints", "PointCloud"}}})
            RegisterRedirectWindow(id, {domain, "Processing"}, "ISS Keypoint Analysis",
                                   "view.keypoint_analysis");
        RegisterWindow("view.descriptor_analysis", {"View"}, "FPFH Descriptor Analysis", &Impl::DrawDescriptorsWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.descriptors", "Mesh"}, {"graph.processing.descriptors", "Graph"},
                  {"pointcloud.processing.descriptors", "PointCloud"}}})
            RegisterRedirectWindow(id, {domain, "Processing"}, "FPFH Descriptor Analysis",
                                   "view.descriptor_analysis");
        RegisterWindow("view.kernel_density", {"View"}, "Kernel Density", &Impl::DrawDensityWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.kernel_density", "Mesh"}, {"graph.processing.kernel_density", "Graph"},
                  {"pointcloud.processing.kernel_density", "PointCloud"}}})
            RegisterRedirectWindow(id, {domain, "Processing"}, "Kernel Density",
                                   "view.kernel_density");
        RegisterWindow("view.density_weights", {"View"}, "Compact Density Weights", &Impl::DrawDensityWeightsWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.density_weights", "Mesh"}, {"graph.processing.density_weights", "Graph"},
                  {"pointcloud.processing.density_weights", "PointCloud"}}})
            RegisterRedirectWindow(id, {domain, "Processing"}, "Compact Density Weights",
                                   "view.density_weights");
        RegisterWindow("view.point_construction", {"View"}, "Construct from Points", &Impl::DrawConstructionWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.point_construction", "Mesh"}, {"graph.processing.point_construction", "Graph"},
                  {"pointcloud.processing.point_construction", "PointCloud"}}})
            RegisterRedirectWindow(id, {domain, "Processing"}, "Construct from Points",
                                   "view.point_construction");
        RegisterWindow("view.point_spacing", {"View"}, "Point Spacing and Radii", &Impl::DrawSpacingWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.point_spacing", "Mesh"}, {"graph.processing.point_spacing", "Graph"},
                  {"pointcloud.processing.point_spacing", "PointCloud"}}})
            RegisterRedirectWindow(id, {domain, "Processing"}, "Point Spacing and Radii",
                                   "view.point_spacing");
        RegisterWindow("view.bilateral_filter", {"View"}, "Bilateral Point Filter", &Impl::DrawBilateralWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.bilateral_filter", "Mesh"}, {"graph.processing.bilateral_filter", "Graph"},
                  {"pointcloud.processing.bilateral_filter", "PointCloud"}}})
            RegisterRedirectWindow(id, {domain, "Processing"}, "Bilateral Point Filter",
                                   "view.bilateral_filter");
        RegisterWindow("view.normal_estimation", {"View"}, "Normal Estimation", &Impl::DrawNormalsWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.vertices.normals", "Mesh"}, {"graph.processing.vertices.normals", "Graph"},
                  {"pointcloud.processing.vertices.normals", "PointCloud"}}})
            RegisterRedirectWindow(id, {domain, "Processing", "Vertices"}, "Normals",
                                   "view.normal_estimation");
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
            RegisterRedirectWindow(id, {domain, "Processing"}, "ICP Registration",
                                   "view.registration");
        RegisterWindow("view.coherent_point_drift", {"View"}, "Coherent Point Drift", &Impl::DrawCoherentPointDriftWindow);
        RegisterWindow("view.point_sampling", {"View"}, "Point Sampling", &Impl::DrawPointSamplingWindow);
        for (const auto& [id, domain] : std::array<std::pair<const char*, const char*>, 3>{
                 {{"mesh.processing.coherent_point_drift", "Mesh"}, {"graph.processing.coherent_point_drift", "Graph"},
                  {"pointcloud.processing.coherent_point_drift", "PointCloud"}}})
            RegisterRedirectWindow(id, {domain, "Processing"}, "Coherent Point Drift", "view.coherent_point_drift");
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
        // A pending GPU result must not outlive the panel: its ring would block every later
        // run on that output. Discard is a no-op for terminal transactions.
        if (SmoothingTransaction) Runtime::DiscardEditorPropertySmoothing({}, SmoothingTransaction);
        SmoothingTransaction.reset();
        if (NormalTransaction) Runtime::DiscardEditorNormalEstimation({}, NormalTransaction);
        NormalTransaction.reset();
        ResetModelCache();
        Denoise.LastResult.reset();
        Denoise.Input = {};
        Gradient = {};
        GradientEntity = 0u;
        Smoothing = {};
        SmoothingEntity = 0u;
        Geodesics = {};
        GeodesicsEntity = 0u;
        GeodesicsSourceVertex = 0;
        ScalarRidges = {};
        ScalarRidgesSelection.reset();
        ScalarRidgesResult.reset();
        ScalarRidgesVisualizationDiagnostic.clear();
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
        if (OutlierTransaction) Runtime::DiscardEditorOutlierAnalysis({}, OutlierTransaction);
        OutlierTransaction.reset();
        for(auto* run:{&DensityTransaction,&SpacingTransaction,&WeightTransaction}){Runtime::DiscardEditorPointScalar({},*run);run->reset();}
        Outliers = {};
        if(KeypointTransaction)Runtime::DiscardEditorPointScalar({},KeypointTransaction);
        KeypointTransaction.reset();
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

    void MeshProcessingPanels::Impl::RegisterRedirectWindow(
        const char* id, std::vector<std::string> menuPath,
        std::string title, const char* target)
    {
        Handles.push_back(Shell->RegisterEditorWindow(
            EditorWindowDescriptor{
                .Id = id,
                .MenuPath = std::move(menuPath),
                .Title = std::move(title),
                .Draw = [](bool& open, const SandboxEditorContext&) { open = false; },
                .OpenStateChanged = [this, id, target](bool open)
                {
                    if (!open) return;
                    (void)Shell->SetEditorWindowOpen(target, true);
                    (void)Shell->SetEditorWindowOpen(id, false);
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
            (this->*draw)(model, context);
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
        if (context.MeshTopology.Results.LastMeshDenoiseResult.has_value())
            Denoise.LastResult = *context.MeshTopology.Results.LastMeshDenoiseResult;
        ImGui::SeparatorText("Denoise");
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
            std::clamp(Denoise.NormalIterations, 1, static_cast<int>(Runtime::kMeshDenoiseMaxIterations));
        Denoise.VertexIterations =
            std::clamp(Denoise.VertexIterations, 1, static_cast<int>(Runtime::kMeshDenoiseMaxIterations));
        Denoise.SigmaSpatial =
            std::clamp(Denoise.SigmaSpatial, 0.0f, 1.0e6f);
        Denoise.SigmaRange =
            std::clamp(Denoise.SigmaRange, 0.0f, 1.0e6f);
        ImGui::DragInt(
            "Normal iterations##MeshDenoise", &Denoise.NormalIterations,
            1.0f, 1, static_cast<int>(Runtime::kMeshDenoiseMaxIterations));
        ImGui::DragInt(
            "Vertex iterations##MeshDenoise", &Denoise.VertexIterations,
            1.0f, 1, static_cast<int>(Runtime::kMeshDenoiseMaxIterations));
        ImGui::DragFloat(
            "Sigma spatial##MeshDenoise", &Denoise.SigmaSpatial,
            0.01f, 0.0f, 1.0e6f);
        ImGui::DragFloat(
            "Sigma range##MeshDenoise", &Denoise.SigmaRange,
            0.01f, 0.0f, 1.0e6f);
        ImGui::Checkbox(
            "Preserve boundary##MeshDenoise", &Denoise.PreserveBoundary);

        const Runtime::EditorMeshDenoiseCommand command{
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
        };
        const auto readiness = Runtime::PreviewEditorMeshDenoiseCommand(
            context.MeshTopology.Commands, command);
        if (DrawProcessingActionButton("Denoise##MeshDenoise", readiness))
        {
            PublishCommandResult(
                Denoise.LastResult,
                Runtime::ApplyEditorMeshDenoiseCommand(
                    context.MeshTopology.Commands,
                    command,
                    context.MeshTopology.ResultSinks.MeshDenoise),
                context.MeshTopology.ResultSinks.MeshDenoise);
            Denoise.Run.WatchOutputIfQueued(Denoise.LastResult, command.StableEntityId, std::string{Runtime::kMeshDenoiseJobOutput});
        }

        const Runtime::EditorOutputRef denoiseDraft{command.StableEntityId, std::string{Runtime::kMeshDenoiseJobOutput}};
        Denoise.Run.Draw(context.MeshTopology.Commands, command.StableEntityId, "denoise_progress", &denoiseDraft);
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
        if (const auto active = Runtime::GetEditorMeshCurvatureConfig(context.MeshFields.Commands))
            Curvature.Synchronize(*active, Runtime::SerializeMeshCurvatureConfig(*active));
        auto& config = Curvature.Draft;
        const auto fields = Runtime::MeshCurvatureConfigFieldSpecs();
        const Runtime::MeshCurvatureConfig defaults{};
        bool changed = DrawProcessingEntity("Entity##MeshCurvature", context,
            config.StableEntityId, Curvature.LastSelectedEntity, Runtime::EditorDomainWindowKind::Mesh);
        const auto& model = GetDomainWindowModel(context, Runtime::EditorDomainWindowKind::Mesh, config.StableEntityId);
        DrawProcessingCpuBackend();
        ImGui::SeparatorText("Input properties");
        changed |= DrawProcessingPropertyInput("Positions##MeshCurvature", model.PropertyCatalog, config.Positions);
        DrawSpecFieldHint(fields, "positions");
        ImGui::SeparatorText("Output properties");
        changed |= DrawProcessingScalarOutput("Mean curvature", config.Mean);
        DrawSpecFieldHint(fields, "mean");
        changed |= DrawProcessingScalarOutput("Gaussian curvature", config.Gaussian);
        DrawSpecFieldHint(fields, "gaussian");
        changed |= DrawProcessingScalarOutput("Minimum principal curvature", config.MinPrincipal);
        DrawSpecFieldHint(fields, "min_principal");
        changed |= DrawProcessingScalarOutput("Maximum principal curvature", config.MaxPrincipal);
        DrawSpecFieldHint(fields, "max_principal");
        changed |= DrawProcessingPropertyName("First principal direction", config.Direction1.Name);
        DrawSpecFieldHint(fields, "direction1");
        changed |= DrawProcessingPropertyName("Second principal direction", config.Direction2.Name);
        DrawSpecFieldHint(fields, "direction2");
        changed |= DrawSpecEnumCombo("Output##MeshCurvature", fields, "output", config.Output, defaults.Output);
        DrawSpecCheckbox("Principal directions##MeshCurvature", fields, "publish_directions",
                         config.PublishPrincipalDirections, defaults.PublishPrincipalDirections, changed);
        const auto apply = [&](const auto& request) {
            return Runtime::ApplyEditorMeshCurvatureConfig(context.MeshFields.Commands, request);
        };
        if (changed)
            Curvature.ConfigDiagnostic = apply(config).Succeeded() ? "" : "Invalid curvature property bindings.";
        const auto readiness = Runtime::ResolveEditorProcessingActionReadiness(
            context.MeshFields.Commands,
            Runtime::PreviewEditorMeshCurvatureCommand(context.MeshFields.Commands, config));
        if (DrawProcessingActionButton("Compute##MeshCurvature", readiness))
        {
            // The job is filed under the serialized command (its identity is the dedupe key), so the
            // run is named by the same text; `Curvature.RunShowsItsOwnProgress` pins the match.
            ApplyQueuedProcessingExecution(context.MeshFields.Commands, Curvature, config, apply,
                [&] { return Runtime::ApplyEditorMeshCurvatureCommand(context.MeshFields.Commands, config,
                    context.MeshFields.ResultSinks.MeshCurvature); },
                context.MeshFields.ResultSinks.MeshCurvature, "Curvature configuration was rejected.",
                config.StableEntityId, Runtime::SerializeMeshCurvatureConfig(config));
        }
        const Runtime::EditorOutputRef curvatureDraft{config.StableEntityId, Runtime::SerializeMeshCurvatureConfig(config)};
        Curvature.Run.Draw(context.MeshFields.Commands, config.StableEntityId, "curvature_progress", &curvatureDraft);
        ImGui::SeparatorText("Display output properties");
        for (const auto* output : {&config.Mean, &config.Gaussian, &config.MinPrincipal,
                                  &config.MaxPrincipal, &config.Direction1, &config.Direction2})
            DrawProcessingPropertyShowButton(context, config.StableEntityId, *output, Curvature.VisualizationDiagnostic);
        if (!readiness.Enabled) ImGui::TextWrapped("%s", readiness.DisabledReason.c_str());
        if (!Curvature.ConfigDiagnostic.empty()) ImGui::TextWrapped("%s", Curvature.ConfigDiagnostic.c_str());
        DrawProcessingDisplayDiagnostic(Curvature.VisualizationDiagnostic);
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
        ImGui::SeparatorText("Property segmentation");
        const auto active = Runtime::GetEditorCurvatureSegmentationConfig(context.MeshFields.Commands);
        // Explicit Apply preserves unfinished edits until Apply or Reload.
        if (active && !Segmentation.Dirty)
            Segmentation.Synchronize(*active, Runtime::SerializeCurvatureSegmentationConfig(*active));
        auto& config = Segmentation.Draft;
        bool changed = false;
        ImGui::SeparatorText("Input properties");
        changed |= DrawProcessingPropertyInput("Positions##Segmentation", model.PropertyCatalog, config.Positions);
        bool computedCurvature = config.Features.empty();
        if (ImGui::Checkbox("Compute curvature features##Segmentation", &computedCurvature))
        {
            config.Features.clear();
            if (!computedCurvature) config.Features.emplace_back();
            changed = true;
        }
        const auto featureChannels = [&] {
            std::uint32_t count = 0u;
            for (const auto& feature : config.Features)
                count += Runtime::GeometryPropertyComponentCount(feature.ValueKind);
            return count;
        };
        for (std::size_t i = 0; i < config.Features.size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            changed |= DrawProcessingPropertyInput("Feature##Segmentation", model.PropertyCatalog,
                config.Features[i], Runtime::IsSegmentationFeatureBinding,
                3u - std::min(3u, featureChannels() - Runtime::GeometryPropertyComponentCount(config.Features[i].ValueKind)));
            ImGui::SameLine();
            if (config.Features.size() > 1u && ImGui::SmallButton("Remove"))
            {
                config.Features.erase(config.Features.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (!config.Features.empty() && config.Features.size() < 3u && featureChannels() < 3u && ImGui::Button("Add feature##Segmentation"))
        {
            config.Features.emplace_back();
            changed = true;
        }
        ImGui::TextWrapped("Property GMM accepts 1–3 numeric channels on mesh vertices or faces. "
                           "Vertex values are averaged onto faces. Feature-curve methods require computed curvature.");

        ImGui::SeparatorText("Output properties");
        changed |= DrawProcessingScalarOutput("Components##Segmentation", config.Components);
        changed |= DrawProcessingScalarOutput("Regions##Segmentation", config.Regions);
        changed |= DrawProcessingPropertyName("RegionColors##Segmentation", config.RegionColors.Name);
        changed |= DrawProcessingScalarOutput("Boundaries##Segmentation", config.Boundaries);
        changed |= DrawProcessingPropertyName("BoundaryColors##Segmentation", config.BoundaryColors.Name);
        changed |= DrawProcessingScalarOutput("HardFeatures##Segmentation", config.HardFeatures);
        changed |= DrawProcessingScalarOutput("FeatureConfidence##Segmentation", config.FeatureConfidence);
        changed |= DrawProcessingScalarOutput("BoundaryRoles##Segmentation", config.BoundaryRoles);
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

        constexpr auto rejected = "The configuration was rejected; inspect config diagnostics.";
        const auto apply = [&](const auto& draft, std::string source = "sandbox.curvature_segmentation.panel.run") {
            auto result = Runtime::ApplyEditorCurvatureSegmentationConfig(context.MeshFields.Commands, draft, std::move(source));
            if (result.Succeeded())
            {
                Segmentation.Dirty = false;
                Segmentation.LastApplied.clear();
                if (const auto applied = Runtime::GetEditorCurvatureSegmentationConfig(context.MeshFields.Commands))
                    Segmentation.Synchronize(*applied, Runtime::SerializeCurvatureSegmentationConfig(*applied));
            }
            return result;
        };
        const auto readiness = [&](Runtime::ActionReadiness method) {
            return Runtime::ResolveEditorProcessingActionReadiness(context.MeshFields.Commands, std::move(method));
        };
        if (DrawProcessingActionButton("Apply configuration##CurvatureSegmentation",
                readiness({Segmentation.Dirty, "No configuration changes to apply."})))
            Segmentation.ConfigDiagnostic = apply(config, "sandbox.curvature_segmentation.panel").Succeeded() ? "" : rejected;
        ImGui::SameLine();
        if (DrawProcessingActionButton("Reload active##CurvatureSegmentation",
                readiness({active.has_value(), "Active configuration is unavailable."})))
        {
            Segmentation.LastApplied.clear();
            Segmentation.Synchronize(*active, Runtime::SerializeCurvatureSegmentationConfig(*active));
            Segmentation.Dirty = false;
        }

        const auto runReadiness = readiness(Runtime::PreviewEditorCurvatureSegmentationCommand(
            context.MeshFields.Commands, {.StableEntityId = model.SelectedStableId, .Config = config}));
        if (!runReadiness.Enabled) ImGui::TextWrapped("%s", runReadiness.DisabledReason.c_str());
        if (DrawProcessingActionButton("Run segmentation##CurvatureSegmentation", runReadiness))
        {
            ApplyProcessingExecution(Segmentation, config, apply, [&] {
                auto result = Runtime::ApplyEditorConfiguredCurvatureSegmentationCommand(
                    context.MeshFields.Commands, model.SelectedStableId);
                if (result.Succeeded() && Segmentation.AutoVisualize)
                    ShowCurvatureSegmentationVisualization(context, model.SelectedStableId, config);
                return result;
            }, std::function<void(Runtime::EditorCurvatureSegmentationResult)>{}, rejected);
        }
        ImGui::SeparatorText("Display output properties");
        ImGui::BeginDisabled(!DomainWindowReady(model));
        for (const auto* output : {&config.Components, &config.Regions, &config.RegionColors, &config.Boundaries, &config.BoundaryColors, &config.HardFeatures, &config.FeatureConfidence, &config.BoundaryRoles, &config.FeatureColors})
            DrawProcessingPropertyShowButton(context, model.SelectedStableId, *output, Segmentation.VisualizationDiagnostic);
        ImGui::EndDisabled();
        DrawProcessingDisplayDiagnostic(Segmentation.VisualizationDiagnostic);

        if (!Segmentation.ConfigDiagnostic.empty())
            ImGui::TextDisabled("%s", Segmentation.ConfigDiagnostic.c_str());
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
        if (context.MeshTopology.Results.LastMeshRemeshResult.has_value())
            Remesh.LastResult = *context.MeshTopology.Results.LastMeshRemeshResult;
        ImGui::SeparatorText("Remesh");
        Remesh.Mode = std::clamp(
            Remesh.Mode, 0,
            static_cast<std::int32_t>(kMeshRemeshModes.size() - 1u));
        Remesh.SizingLaw = std::clamp(
            Remesh.SizingLaw, 0,
            static_cast<std::int32_t>(kMeshRemeshSizingLaws.size() - 1u));
        Remesh.Iterations = std::clamp(Remesh.Iterations, 1, 64);
        Remesh.TargetEdgeLength = std::clamp(Remesh.TargetEdgeLength, 0.0f, 1.0e6f);

        // Probe options independently of unrelated draft settings so a blocked
        // combination does not prevent choosing a supported alternative.
        const auto preview = [&](const Runtime::EditorMeshRemeshCommand& command) {
            return Runtime::PreviewEditorMeshRemeshCommand(context.MeshTopology.Commands, command);
        };
        if (ImGui::BeginCombo("Mode##MeshRemesh",
                Runtime::DebugNameForEditorMeshRemeshMode(FromIndex(kMeshRemeshModes, Remesh.Mode))))
        {
            for (std::size_t i = 0u; i < kMeshRemeshModes.size(); ++i)
            {
                const auto option = kMeshRemeshModes[i];
                const auto readiness = preview({.StableEntityId = model.SelectedStableId, .Mode = option});
                ImGui::BeginDisabled(!readiness.Enabled);
                const bool selected = Remesh.Mode == static_cast<std::int32_t>(i);
                if (ImGui::Selectable(Runtime::DebugNameForEditorMeshRemeshMode(option), selected))
                    Remesh.Mode = static_cast<std::int32_t>(i);
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::EndDisabled();
                DrawDisabledReasonTooltip(readiness.DisabledReason);
            }
            ImGui::EndCombo();
        }
        ImGui::DragInt("Iterations##MeshRemesh", &Remesh.Iterations, 1.0f, 1, static_cast<int>(Runtime::kMeshRemeshMaxIterations));
        ImGui::DragFloat("Target edge length##MeshRemesh", &Remesh.TargetEdgeLength, 0.01f, 0.0f, 1.0e6f);

        const auto mode = FromIndex(kMeshRemeshModes, Remesh.Mode);
        if (ImGui::BeginCombo("Sizing law##MeshRemesh",
                Runtime::DebugNameForEditorMeshRemeshSizingLaw(FromIndex(kMeshRemeshSizingLaws, Remesh.SizingLaw))))
        {
            for (std::size_t i = 0u; i < kMeshRemeshSizingLaws.size(); ++i)
            {
                const auto option = kMeshRemeshSizingLaws[i];
                const auto readiness = preview({.StableEntityId = model.SelectedStableId,
                    .Mode = mode, .SizingLaw = option});
                ImGui::BeginDisabled(!readiness.Enabled);
                const bool selected = Remesh.SizingLaw == static_cast<std::int32_t>(i);
                if (ImGui::Selectable(Runtime::DebugNameForEditorMeshRemeshSizingLaw(option), selected))
                    Remesh.SizingLaw = static_cast<std::int32_t>(i);
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::EndDisabled();
                DrawDisabledReasonTooltip(readiness.DisabledReason);
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Sizing law applies to adaptive remeshing.");
        const auto projection = preview({.StableEntityId = model.SelectedStableId,
            .Mode = mode, .ProjectToSurface = !Remesh.ProjectToSurface});
        ImGui::BeginDisabled(!projection.Enabled);
        ImGui::Checkbox("Project to surface##MeshRemesh", &Remesh.ProjectToSurface);
        ImGui::EndDisabled();
        DrawDisabledReasonTooltip(projection.DisabledReason);

        const Runtime::EditorMeshRemeshCommand command{
            .StableEntityId = model.SelectedStableId,
            .Mode = mode,
            .SizingLaw = FromIndex(kMeshRemeshSizingLaws, Remesh.SizingLaw),
            .Iterations = static_cast<std::uint32_t>(Remesh.Iterations),
            .TargetEdgeLength = static_cast<double>(Remesh.TargetEdgeLength),
            .ProjectToSurface = Remesh.ProjectToSurface,
        };
        if (DrawProcessingActionButton("Remesh##MeshRemesh", preview(command)))
        {
            PublishCommandResult(Remesh.LastResult,
                Runtime::ApplyEditorMeshRemeshCommand(context.MeshTopology.Commands,
                    command, context.MeshTopology.ResultSinks.MeshRemesh),
                context.MeshTopology.ResultSinks.MeshRemesh);
            Remesh.Run.WatchOutputIfQueued(Remesh.LastResult, command.StableEntityId, std::string{Runtime::kMeshRemeshJobOutput});
        }

        const Runtime::EditorOutputRef remeshDraft{command.StableEntityId, std::string{Runtime::kMeshRemeshJobOutput}};
        Remesh.Run.Draw(context.MeshTopology.Commands, command.StableEntityId, "remesh_progress", &remeshDraft);
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
        if (context.MeshTopology.Results.LastMeshSubdivideResult.has_value())
            Subdivide.LastResult = *context.MeshTopology.Results.LastMeshSubdivideResult;
        ImGui::SeparatorText("Subdivide");
        Subdivide.Operator = std::clamp(
            Subdivide.Operator, 0,
            static_cast<std::int32_t>(kMeshSubdivideOperators.size() - 1u));
        Subdivide.Iterations = std::clamp(Subdivide.Iterations, 1, 10);
        const auto preview = [&](const Runtime::EditorMeshSubdivideCommand& command) {
            return Runtime::PreviewEditorMeshSubdivideCommand(context.MeshTopology.Commands, command);
        };
        if (ImGui::BeginCombo("Operator##MeshSubdivide",
                Runtime::DebugNameForEditorMeshSubdivideOperator(FromIndex(kMeshSubdivideOperators, Subdivide.Operator))))
        {
            for (std::size_t i = 0u; i < kMeshSubdivideOperators.size(); ++i)
            {
                const auto option = kMeshSubdivideOperators[i];
                const auto readiness = preview({.StableEntityId = model.SelectedStableId, .Operator = option});
                ImGui::BeginDisabled(!readiness.Enabled);
                const bool selected = Subdivide.Operator == static_cast<std::int32_t>(i);
                if (ImGui::Selectable(Runtime::DebugNameForEditorMeshSubdivideOperator(option), selected))
                    Subdivide.Operator = static_cast<std::int32_t>(i);
                if (selected) ImGui::SetItemDefaultFocus();
                ImGui::EndDisabled();
                DrawDisabledReasonTooltip(readiness.DisabledReason);
            }
            ImGui::EndCombo();
        }
        ImGui::DragInt("Iterations##MeshSubdivide", &Subdivide.Iterations, 1.0f, 1, static_cast<int>(Runtime::kMeshSubdivideMaxIterations));
        const auto op = FromIndex(kMeshSubdivideOperators, Subdivide.Operator);
        if (op != Runtime::EditorMeshSubdivideOperator::Loop)
            Subdivide.PreserveLoopFeatures = false;
        if (op == Runtime::EditorMeshSubdivideOperator::Loop)
            DrawProcessingPropertyInput("Feature edges##MeshSubdivide", model.PropertyCatalog,
                Subdivide.FeatureEdges, [](const Runtime::GeometryPropertyRef& ref) {
                    return ref.Domain == Runtime::GeometryElementDomain::MeshEdge &&
                        Runtime::GeometryPropertyComponentCount(ref.ValueKind) == 1 &&
                        !Runtime::IsTopologyProperty(ref.Domain, ref.Name);
                });
        const auto feature = preview({.StableEntityId = model.SelectedStableId,
            .Operator = op, .PreserveLoopFeatureEdges = !Subdivide.PreserveLoopFeatures,
            .FeatureEdges = Subdivide.FeatureEdges});
        ImGui::BeginDisabled(!feature.Enabled);
        ImGui::Checkbox("Preserve Loop features##MeshSubdivide", &Subdivide.PreserveLoopFeatures);
        ImGui::EndDisabled();
        DrawDisabledReasonTooltip(feature.DisabledReason);

        const Runtime::EditorMeshSubdivideCommand command{
            .StableEntityId = model.SelectedStableId,
            .Operator = op,
            .Iterations = static_cast<std::uint32_t>(Subdivide.Iterations),
            .PreserveLoopFeatureEdges = Subdivide.PreserveLoopFeatures,
            .FeatureEdges = Subdivide.FeatureEdges,
        };
        if (DrawProcessingActionButton("Subdivide##MeshSubdivide", preview(command)))
        {
            PublishCommandResult(Subdivide.LastResult,
                Runtime::ApplyEditorMeshSubdivideCommand(context.MeshTopology.Commands,
                    command, context.MeshTopology.ResultSinks.MeshSubdivide),
                context.MeshTopology.ResultSinks.MeshSubdivide);
            Subdivide.Run.WatchOutputIfQueued(Subdivide.LastResult, command.StableEntityId, std::string{Runtime::kMeshSubdivideJobOutput});
        }

        const Runtime::EditorOutputRef subdivideDraft{command.StableEntityId, std::string{Runtime::kMeshSubdivideJobOutput}};
        Subdivide.Run.Draw(context.MeshTopology.Commands, command.StableEntityId, "subdivide_progress", &subdivideDraft);
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
        if (context.MeshTopology.Results.LastMeshSimplifyResult.has_value())
            Simplify.LastResult = *context.MeshTopology.Results.LastMeshSimplifyResult;
        ImGui::SeparatorText("Simplify");
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
            1.0f, 0, static_cast<int>(Runtime::kMeshSimplifyMaxTargetFaces));
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

        const Runtime::EditorMeshSimplifyCommand command{
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
        };
        const auto readiness = Runtime::PreviewEditorMeshSimplifyCommand(
            context.MeshTopology.Commands, command);
        if (DrawProcessingActionButton("Simplify##MeshSimplify", readiness))
        {
            PublishCommandResult(
                Simplify.LastResult,
                Runtime::ApplyEditorMeshSimplifyCommand(
                    context.MeshTopology.Commands,
                    command,
                    context.MeshTopology.ResultSinks.MeshSimplify),
                context.MeshTopology.ResultSinks.MeshSimplify);
            Simplify.Run.WatchOutputIfQueued(Simplify.LastResult, command.StableEntityId, std::string{Runtime::kMeshSimplifyJobOutput});
        }

        const Runtime::EditorOutputRef simplifyDraft{command.StableEntityId, std::string{Runtime::kMeshSimplifyJobOutput}};
        Simplify.Run.Draw(context.MeshTopology.Commands, command.StableEntityId, "simplify_progress", &simplifyDraft);
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
        if (*NormalCompletion) { Normals.LastResult = std::move(**NormalCompletion); NormalCompletion->reset(); }
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
                if ((method != Runtime::NormalEstimationMethod::PointSetPCA && candidate.Backend == Runtime::NormalEstimationBackend::VulkanLBVH) ||
                    (method == Runtime::NormalEstimationMethod::PointSetPCA && candidate.Backend == Runtime::NormalEstimationBackend::Vulkan))
                    candidate.Backend = method == Runtime::NormalEstimationMethod::PointSetPCA
                        ? Runtime::NormalEstimationBackend::VulkanLBVH
                        : Runtime::NormalEstimationBackend::Vulkan;
                candidate.Output.Domain = method == Runtime::NormalEstimationMethod::MeshFaceNormals
                    ? Runtime::GeometryElementDomain::MeshFace : candidate.Positions.Domain;
                if (candidate.Output.Name == "v:normal" || candidate.Output.Name == "f:normal")
                    candidate.Output.Name = method == Runtime::NormalEstimationMethod::MeshFaceNormals
                        ? "f:normal" : "v:normal";
                const auto readiness =
                    Runtime::PreviewEditorNormalEstimationCommand(context.Normals.Commands, candidate);
                ImGui::BeginDisabled(!readiness.Enabled);
                if (ImGui::Selectable(Runtime::ToString(method), config.Method == method))
                {
                    config = candidate;
                    // The residency backend belongs to the mesh methods alone.
                    if (method != Runtime::NormalEstimationMethod::MeshFaceWeighted &&
                        method != Runtime::NormalEstimationMethod::MeshFaceNormals &&
                        config.Backend == Runtime::NormalEstimationBackend::Vulkan)
                        config.Backend = Runtime::NormalEstimationBackend::CpuKDTree;
                    changed = true;
                }
                ImGui::EndDisabled();
                if (!readiness.Enabled)
                    DrawDisabledReasonTooltip(readiness.DisabledReason);
            }
            ImGui::EndCombo();
        }
        if (config.Method == Runtime::NormalEstimationMethod::PointSetPCA)
        {
            ImGui::TextWrapped(
                "PCA fits local planes to spatial neighbors on the selected element domain. Radius mode uses "
                "all neighbors within the radius; otherwise k nearest neighbors are used.");
            int backend = int(config.Backend);
            if (ImGui::Combo("Backend##Normals", &backend, "CPU KD-tree\0CPU LBVH (cached)\0Vulkan LBVH (resident PCA)\0"))
            {
                config.Backend = Runtime::NormalEstimationBackend(backend);
                changed = true;
            }
            if (config.Backend == Runtime::NormalEstimationBackend::VulkanLBVH)
            {
                ImGui::TextWrapped("GPU neighbors, covariance and normals; unoriented only. No viewport preview. "
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
            // Every non-Vulkan value is the CPU reference for this method.
            int backend = config.Backend == Runtime::NormalEstimationBackend::Vulkan ? 1 : 0;
            if (ImGui::Combo("Backend##Normals", &backend, "CPU reference\0Vulkan (GPU property residency, double precision)\0"))
            {
                config.Backend = backend == 1 ? Runtime::NormalEstimationBackend::Vulkan : Runtime::NormalEstimationBackend::CpuKDTree;
                changed = true;
            }
            if (config.Backend == Runtime::NormalEstimationBackend::Vulkan)
                ImGui::TextWrapped("The kernels read the resident positions and a per-topology bundle; the result waits "
                                   "for Accept (undoable) or Discard. Uniform, area and max weighting; no viewport preview.");
        }
        else if (config.Method == Runtime::NormalEstimationMethod::MeshFaceNormals)
        {
            ImGui::TextWrapped("Compute one object-space normal per polygon from its full face ring. "
                               "Face winding determines the direction.");
            int backend = config.Backend == Runtime::NormalEstimationBackend::Vulkan ? 1 : 0;
            if (ImGui::Combo("Backend##FaceNormals", &backend, "CPU reference\0Vulkan (GPU property residency, double precision)\0"))
            {
                config.Backend = backend == 1 ? Runtime::NormalEstimationBackend::Vulkan : Runtime::NormalEstimationBackend::CpuKDTree;
                changed = true;
            }
            if (config.Backend == Runtime::NormalEstimationBackend::Vulkan)
                ImGui::TextWrapped("The kernel reads the resident positions and a per-topology ring bundle; the result "
                                   "waits for Accept (undoable) or Discard. No viewport preview.");
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
        // A pending GPU result blocks the next run until it is accepted or discarded.
        using Phase = Runtime::EditorGpuTransactionPhase;
        const auto transaction = Runtime::SnapshotEditorNormalEstimation(context.Normals.Commands, NormalTransaction);
        const bool transactionPending = NormalTransaction && (transaction.Phase == Phase::Running ||
            transaction.Phase == Phase::ReadyToAccept || transaction.Phase == Phase::Accepting);
        if (changed)
            Normals.ConfigDiagnostic = Runtime::ApplyEditorNormalEstimationConfig(context.Normals.Commands, config).Succeeded()
                ? "" : "Controls were rejected by normal config validation.";
        if (!Normals.ConfigDiagnostic.empty()) ImGui::TextWrapped("%s", Normals.ConfigDiagnostic.c_str());
        auto readiness = Runtime::ResolveEditorProcessingActionReadiness(context.Normals.Commands,
            Runtime::PreviewEditorNormalEstimationCommand(context.Normals.Commands, config));
        if (transactionPending && readiness.Enabled)
            readiness = {.Enabled = false, .DisabledReason = "Accept or discard the pending GPU result first."};
        if (!readiness.Enabled) ImGui::TextWrapped("%s", readiness.DisabledReason.c_str());
        if (DrawProcessingActionButton("Estimate normals", readiness))
        {
            Normals.Run.ClearNote(); // an own submission replaces an earlier duplicate refusal
            if ((config.Backend == Runtime::NormalEstimationBackend::Vulkan || config.Backend == Runtime::NormalEstimationBackend::VulkanLBVH))
            {
                // Interactive Vulkan runs compute on the device and publish on Accept.
                Normals.ConfigDiagnostic = Runtime::ApplyEditorNormalEstimationConfig(context.Normals.Commands, config).Succeeded()
                    ? "" : "Normal config was rejected.";
                if (Normals.ConfigDiagnostic.empty())
                {
                    NormalTransaction.reset(); // a finished transaction is retired before the new one
                    Runtime::EditorNormalEstimationResult failure;
                    NormalTransaction = Runtime::StartEditorNormalEstimationTransaction(context.Normals.Commands, config, failure);
                    // A Pending refusal without a handle: the output's active run keeps its callback.
                    if (!NormalTransaction && failure.Status == Runtime::EditorCommandStatus::Pending)
                        Normals.Run.WatchDuplicate(config.StableEntityId, config.Output.Name, failure.Message);
                    else
                    {
                        Normals.LastResult = NormalTransaction
                            ? Runtime::SnapshotEditorNormalEstimation(context.Normals.Commands, NormalTransaction).Result : failure;
                        // A refused start leaves no handle; the sink keeps its result on screen.
                        if (!NormalTransaction && context.Normals.ResultSinks.NormalEstimation)
                            context.Normals.ResultSinks.NormalEstimation(failure);
                        Normals.Run.WatchOutputIfQueued(Normals.LastResult, config.StableEntityId, config.Output.Name);
                    }
                }
            }
            else
                ApplyQueuedProcessingExecution(context.Normals.Commands, Normals, config,
                    [&](const auto& request) { return Runtime::ApplyEditorNormalEstimationConfig(context.Normals.Commands, request); },
                    [&] { return Runtime::ApplyEditorConfiguredNormalEstimation(context.Normals.Commands, context.Normals.ResultSinks.NormalEstimation); },
                    context.Normals.ResultSinks.NormalEstimation, "Normal config was rejected.", config.StableEntityId, config.Output.Name);
        }
        Normals.Run.AwaitingAccept(NormalTransaction && transaction.Phase == Phase::ReadyToAccept);
        const Runtime::EditorOutputRef normalsDraft{config.StableEntityId, config.Output.Name};
        Normals.Run.Draw(context.Normals.Commands, config.StableEntityId, "normals_progress", &normalsDraft);
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
                     .PropertyName = outputProperty.Name,
                     .Interpretation = decltype(Runtime::EditorVisualizationPropertyCommand{}.Interpretation)::NormalDirection});
                (void)hint;
                Normals.VisualizationDiagnostic = Runtime::DebugNameForEditorCommandStatus(status);
            }
        }
        else
            DrawProcessingPropertyShowButton(context, config.StableEntityId, outputProperty, Normals.VisualizationDiagnostic,
                                             "Show normals", true);
        DrawProcessingDisplayDiagnostic(Normals.VisualizationDiagnostic, "Normal display");
        if (NormalTransaction) DrawNormalTransaction(context);
        if (Normals.LastResult)
        {
            const auto &result = *Normals.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s", Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Method: %s", Runtime::ToString(result.Method));
            if (result.Method == Runtime::NormalEstimationMethod::PointSetPCA ||
                result.Method == Runtime::NormalEstimationMethod::MeshFaceWeighted)
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
        if (DrawProcessingPointInput("Positions##Outliers", [&] { return Runtime::GetEditorPointInputCatalog(context.Processing, config.StableEntityId); }, config.Positions))
        {
            config.Mask.Domain = config.Score.Domain = config.Positions.Domain;
            changed = true;
        }
        for (auto [label, ref] : {std::pair{"Mask property", &config.Mask}, std::pair{"Score property", &config.Score}})
            changed |= DrawProcessingScalarOutput(label, *ref);
        int method=int(config.Method), backend=int(config.Backend);
        if(ImGui::Combo("Method",&method,"Statistical\0Radius\0Local distance ratio\0")) {config.Method=Runtime::OutlierAnalysisMethod(method);changed=true;}
        if(ImGui::Combo("Backend",&backend,"CPU octree\0CPU LBVH (cached)\0Vulkan LBVH\0")) {config.Backend=Runtime::OutlierAnalysisBackend(backend);changed=true;}
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
        const auto preview = Runtime::PreviewEditorOutlierAnalysisCommand(context.PointAnalysis.Commands, analyze);
        const auto readiness = Runtime::ResolveEditorProcessingActionReadiness(
            context.PointAnalysis.Commands, preview);
        if (!readiness.Enabled) ImGui::TextWrapped("%s", readiness.DisabledReason.c_str());
        const auto execute = [&](const Runtime::OutlierAnalysisConfig& request) {
            ApplyQueuedProcessingExecution(context.PointAnalysis.Commands, Outliers, request,
                [&](const auto& value) { return Runtime::ApplyEditorOutlierAnalysisConfig(context.PointAnalysis.Commands, value); },
                [&] { return Runtime::ApplyEditorConfiguredOutlierAnalysis(context.PointAnalysis.Commands, context.PointAnalysis.ResultSinks.OutlierAnalysis); },
                context.PointAnalysis.ResultSinks.OutlierAnalysis, "Outlier config was rejected.", request.StableEntityId, request.Mask.Name);
        };
        const auto transaction = Runtime::SnapshotEditorOutlierAnalysis(context.PointAnalysis.Commands, OutlierTransaction);
        const bool outlierActive = OutlierTransaction && (transaction.Phase == Runtime::EditorGpuTransactionPhase::Running ||
            transaction.Phase == Runtime::EditorGpuTransactionPhase::ReadyToAccept || transaction.Phase == Runtime::EditorGpuTransactionPhase::Accepting);
        if (OutlierTransaction && !outlierActive)
        {
            Outliers.LastResult = transaction.Result;
            if (context.PointAnalysis.ResultSinks.OutlierAnalysis)
                context.PointAnalysis.ResultSinks.OutlierAnalysis(transaction.Result);
            OutlierTransaction.reset();
        }
        ImGui::BeginDisabled(outlierActive);
        if (DrawProcessingActionButton("Detect outliers", readiness))
        {
            Outliers.Run.ClearNote(); // an own submission replaces an earlier duplicate refusal
            if (analyze.Backend == Runtime::OutlierAnalysisBackend::VulkanLBVH)
            {
                Runtime::EditorOutlierAnalysisResult result;
                OutlierTransaction = Runtime::StartEditorOutlierAnalysisTransaction(context.PointAnalysis.Commands, analyze, result);
                // A Pending refusal without a handle: the output's active run keeps its callback.
                if (!OutlierTransaction && result.Status == Runtime::EditorCommandStatus::Pending)
                    Outliers.Run.WatchDuplicate(config.StableEntityId, config.Mask.Name, result.Message);
                else
                {
                    Outliers.LastResult = result;
                    // A refused start leaves no handle; the sink keeps its result on screen.
                    if (!OutlierTransaction && context.PointAnalysis.ResultSinks.OutlierAnalysis)
                        context.PointAnalysis.ResultSinks.OutlierAnalysis(result);
                    Outliers.Run.WatchOutputIfQueued(Outliers.LastResult, config.StableEntityId, config.Mask.Name);
                }
            }
            else execute(analyze);
        }
        ImGui::EndDisabled();
        Outliers.Run.AwaitingAccept(OutlierTransaction && transaction.Phase == Runtime::EditorGpuTransactionPhase::ReadyToAccept);
        const Runtime::EditorOutputRef outliersDraft{config.StableEntityId, config.Mask.Name};
        Outliers.Run.Draw(context.PointAnalysis.Commands, config.StableEntityId, "outliers_progress", &outliersDraft);
        if (outlierActive)
        {
            ImGui::TextWrapped("%s", transaction.Result.Message.c_str());
            ImGui::Text("Input upload: %llu bytes; residency hits: %llu",
                static_cast<unsigned long long>(transaction.Result.GpuInputUploadBytes),
                static_cast<unsigned long long>(transaction.Result.GpuInputCacheHits));
            ImGui::BeginDisabled(!transaction.CanAccept);
            if (ImGui::Button("Accept##Outliers"))
                Outliers.LastResult = Runtime::AcceptEditorOutlierAnalysis(context.PointAnalysis.Commands, OutlierTransaction,
                    context.PointAnalysis.ResultSinks.OutlierAnalysis);
            ImGui::EndDisabled();
            if (!transaction.AcceptDisabledReason.empty()) ImGui::TextWrapped("%s", transaction.AcceptDisabledReason.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Discard##Outliers"))
            {
                Runtime::DiscardEditorOutlierAnalysis(context.PointAnalysis.Commands, OutlierTransaction);
                Outliers.Run.Forget();
                Outliers.LastResult = Runtime::SnapshotEditorOutlierAnalysis(context.PointAnalysis.Commands, OutlierTransaction).Result;
            }
            ImGui::TextWrapped("The score ring is available to the colormap; CPU fields change only on Accept.");
        }

        ImGui::TextWrapped("Detection writes a mask (1 = outlier) and a score. Geometry stays in source order.");
        auto remove = config;
        remove.Operation = Runtime::OutlierAnalysisOperation::RemoveMarked;
        const auto removal = Runtime::PreviewEditorOutlierAnalysisCommand(context.PointAnalysis.Commands, remove);
        const auto removalReadiness = Runtime::ResolveEditorProcessingActionReadiness(
            context.PointAnalysis.Commands, removal);
        if (DrawProcessingActionButton("Remove marked points", removalReadiness)) execute(remove);
        ImGui::TextWrapped("Removal compacts point clouds and supports Undo. Detect again after changing positions or the mask.");
        DrawProcessingPropertyShowButton(context, config.StableEntityId, config.Mask, Outliers.VisualizationDiagnostic, "Show mask");
        ImGui::SameLine();
        DrawProcessingPropertyShowButton(context, config.StableEntityId, config.Score, Outliers.VisualizationDiagnostic, "Show score");
        DrawProcessingDisplayDiagnostic(Outliers.VisualizationDiagnostic);
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
        int computeBackend=config.Backend==Runtime::KeypointAnalysisBackend::VulkanCompute?1:0;
        if(ImGui::Combo("Backend",&computeBackend,"CPU\0Vulkan\0"))
        {config.Backend=computeBackend?Runtime::KeypointAnalysisBackend::VulkanCompute:Runtime::KeypointAnalysisBackend::CpuKDTree;changed=true;}
        if (DrawProcessingPointInput("Positions##Keypoints", [&] { return Runtime::GetEditorPointInputCatalog(context.Processing, config.StableEntityId); }, config.Positions))
        {
            config.Mask.Domain = config.Score.Domain = config.Positions.Domain;
            changed = true;
        }
        for (auto [label, ref] : {std::pair{"Mask property", &config.Mask}, std::pair{"Saliency property", &config.Score}})
            changed |= DrawProcessingScalarOutput(label, *ref);
        if(config.Backend!=Runtime::KeypointAnalysisBackend::VulkanCompute)
        {
            int backend=int(config.Backend);
            if(ImGui::Combo("Acceleration",&backend,"CPU KD-tree\0CPU LBVH (cached)\0Vulkan LBVH neighborhoods\0"))
            {config.Backend=Runtime::KeypointAnalysisBackend(backend);changed=true;}
        }
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
            ImGui::TextWrapped("Neighborhood queries require complete support within capacity; scale, covariance and suppression run on CPU.");
        }
        else if(config.Backend==Runtime::KeypointAnalysisBackend::VulkanCompute)
            ImGui::TextWrapped("Spacing, covariance, saliency and suppression run on Vulkan in bounded traversal pages. Requires shader double precision.");
        const bool keypointActive=DrawPointScalarTransaction(context.PointAnalysis.Commands,
            KeypointTransaction,Keypoints,context.PointAnalysis.ResultSinks.KeypointAnalysis);
        ImGui::BeginDisabled(keypointActive);
        DrawProcessingExecution(context.PointAnalysis.Commands, Keypoints, changed,
            [&](const auto& request) { return Runtime::PreviewEditorKeypointAnalysisCommand(context.PointAnalysis.Commands, request); },
            [&](const auto& request) { return Runtime::ApplyEditorKeypointAnalysisConfig(context.PointAnalysis.Commands, request); },
            [&] {
                if(config.Backend==Runtime::KeypointAnalysisBackend::VulkanCompute) {
                    Runtime::EditorKeypointAnalysisResult result;
                    KeypointTransaction=Runtime::StartEditorKeypointAnalysisTransaction(context.PointAnalysis.Commands,config,result,context.PointAnalysis.ResultSinks.KeypointAnalysis);
                    return result;
                }
                return Runtime::ApplyEditorConfiguredKeypointAnalysis(context.PointAnalysis.Commands, context.PointAnalysis.ResultSinks.KeypointAnalysis);
            },
            context.PointAnalysis.ResultSinks.KeypointAnalysis, "Detect keypoints",
            "Controls were rejected by keypoint config validation.", "Keypoint config was rejected.",
            [](const auto& c) { return std::pair{c.StableEntityId, c.Mask.Name}; });
        ImGui::EndDisabled();
        const Runtime::EditorOutputRef keypointsDraft{config.StableEntityId, config.Mask.Name};
        Keypoints.Run.Draw(context.PointAnalysis.Commands, config.StableEntityId, "keypoints_progress", &keypointsDraft);
        ImGui::TextWrapped("Detection writes a mask (1 = retained keypoint) and a score. Geometry stays in source order.");
        if(KeypointTransaction)
            ImGui::TextWrapped("Detection is active. Vulkan previews the completed score; Accept publishes both CPU properties.");
        DrawProcessingPropertyShowButton(context, config.StableEntityId, config.Mask, Keypoints.VisualizationDiagnostic, "Show mask");
        ImGui::SameLine();
        DrawProcessingPropertyShowButton(context, config.StableEntityId, config.Score, Keypoints.VisualizationDiagnostic, "Show saliency");
        DrawProcessingDisplayDiagnostic(Keypoints.VisualizationDiagnostic);
        if(Keypoints.LastResult)
        {
            const auto& result=*Keypoints.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s",Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Requested: %s; ran: %s",Runtime::ToString(result.RequestedBackend),result.ActualBackend.c_str());
            ImGui::Text("Implementation: %s",result.ImplementationId.c_str());
            ImGui::Text("Input upload: %llu; cache hits: %llu; CPU upload/readback: %llu / %llu bytes",
                (unsigned long long)result.GpuInputUploadBytes,(unsigned long long)result.GpuInputCacheHits,(unsigned long long)result.CpuStageUploadBytes,(unsigned long long)result.CpuStageReadbackBytes);
            ImGui::Text("Live / total: %zu / %zu; keypoints: %zu",result.LiveCount,result.SlotCount,result.KeypointCount);
            ImGui::Text("Spacing: %.5g; salient / suppression radii: %.5g / %.5g",double(result.MeanSpacing),double(result.SalientRadius),double(result.NonMaxRadius));
            ImGui::Text("GPU %s: %zu; largest indexed support: %zu",
                result.RequestedBackend==Runtime::KeypointAnalysisBackend::VulkanCompute?"dispatches":"batches",
                result.GpuQueryBatches,result.MaximumNeighbors);
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
        if (DrawProcessingPointInput("Positions##Descriptors", [&] { return Runtime::GetEditorPointInputCatalog(context.Processing, config.StableEntityId); }, config.Positions))
        {
            config.Normals.Domain = config.Positions.Domain;
            for (auto& output : config.Outputs) output.Domain = config.Positions.Domain;
            changed = true;
        }
        changed |= DrawProcessingPointInput("Normals##Descriptors",
            [&] { return Runtime::GetEditorPointInputCatalog(context.Processing, config.StableEntityId); },
            config.Normals, config.Positions.Domain);
        if(ImGui::TreeNode("Histogram output properties"))
        {
            ImGui::InputText("Output prefix",Descriptors.Prefix.data(),Descriptors.Prefix.size());
            if(ImGui::Button("Name all 33 bins"))
            {config.Outputs=Runtime::MakeDescriptorOutputProperties(config.Positions.Domain,Descriptors.Prefix.data());changed=true;}
            constexpr std::array blocks{"alpha","phi","theta"};
            for(unsigned i=0;i<33;++i)
            {
                const auto label=std::string(blocks[i/11])+" bin "+std::to_string(i%11);
                changed |= DrawProcessingScalarOutput(label.c_str(), config.Outputs[i]);
            }
            ImGui::TreePop();
        }
        int backend=int(config.Backend);
        if(ImGui::Combo("Backend",&backend,"CPU KD-tree\0CPU LBVH (cached)\0Vulkan LBVH\0")) {config.Backend=Runtime::DescriptorAnalysisBackend(backend);changed=true;}
        changed |= ImGui::InputFloat("Feature radius (0 = automatic)",&config.FeatureRadius);
        changed |= ImGui::InputScalar("Maximum neighbors (0 = all)",ImGuiDataType_U32,&config.MaxNeighbors);
        ImGui::TextWrapped("FPFH uses three eleven-bin histograms and nonzero normals. Automatic radius is five times mean nearest-neighbor spacing. A neighbor cap keeps the lowest source IDs within the radius.");
        if(config.Backend==Runtime::DescriptorAnalysisBackend::VulkanLBVH)
        {
            changed |= ImGui::InputScalar("GPU query batch size",ImGuiDataType_U32,&config.GpuQueryBatchSize);
            changed |= ImGui::InputScalar("Radius result capacity",ImGuiDataType_U32,&config.GpuRadiusCapacity);
            ImGui::TextWrapped("Uncapped radius support must fit the selected capacity (up to 1024). A neighbor cap within that capacity can use the exact lowest-ID prefix even in denser neighborhoods. Scale, SPFH and FPFH run on CPU.");
        }
        DrawProcessingExecution(context.PointAnalysis.Commands, Descriptors, changed,
            [&](const auto& request) { return Runtime::PreviewEditorDescriptorAnalysisCommand(context.PointAnalysis.Commands, request); },
            [&](const auto& request) { return Runtime::ApplyEditorDescriptorAnalysisConfig(context.PointAnalysis.Commands, request); },
            [&] { return Runtime::ApplyEditorConfiguredDescriptorAnalysis(context.PointAnalysis.Commands, context.PointAnalysis.ResultSinks.DescriptorAnalysis); },
            context.PointAnalysis.ResultSinks.DescriptorAnalysis, "Compute FPFH descriptors",
            "Controls were rejected by descriptor config validation.", "Descriptor config was rejected.",
            [](const auto& c) { return std::pair{c.StableEntityId, c.Outputs[0].Name}; });
        const Runtime::EditorOutputRef descriptorsDraft{config.StableEntityId, config.Outputs[0].Name};
        Descriptors.Run.Draw(context.PointAnalysis.Commands, config.StableEntityId, "descriptors_progress", &descriptorsDraft);
        ImGui::TextWrapped("Writes 33 named float histogram properties in one undoable operation. Each nonempty eleven-bin block sums to 100.");
        const bool displayBinChanged =
            ImGui::SliderInt("Display histogram bin", &Descriptors.DisplayBin, 0, 32,
                             "%d", ImGuiSliderFlags_AlwaysClamp);
        const auto score=config.Outputs[Descriptors.DisplayBin];
        ImGui::Text("Property: %s",score.Name.c_str());
        if (const auto status = DrawProcessingPropertyShowButton(
                context, config.StableEntityId, score, Descriptors.VisualizationDiagnostic, "Show histogram bin",
                false, displayBinChanged && Descriptors.FollowDisplayBin))
        {
            if (*status == Runtime::EditorCommandStatus::Applied ||
                *status == Runtime::EditorCommandStatus::NoChange)
                Descriptors.FollowDisplayBin = true;
        }
        DrawProcessingDisplayDiagnostic(Descriptors.VisualizationDiagnostic);
        if(Descriptors.LastResult)
        {
            const auto& result=*Descriptors.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s",Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Requested: %s; ran: %s",Runtime::ToString(result.RequestedBackend),result.ActualBackend.c_str());
            ImGui::Text("Live / total: %zu / %zu; rows written: %zu",result.LiveCount,result.SlotCount,result.WrittenCount);
            ImGui::Text("Spacing: %.5g; feature radius: %.5g",double(result.MeanSpacing),double(result.FeatureRadius));
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
        if (DrawProcessingPointInput("Positions##Density", [&] { return Runtime::GetEditorKernelDensityInputCatalog(context.PointFields.Commands, config.StableEntityId); }, config.Positions))
        {
            config.Density.Domain = config.Positions.Domain;
            changed = true;
        }
        changed |= DrawProcessingScalarOutput("Density property", config.Density);
        int backend=int(config.Backend);
        if(ImGui::Combo("Backend",&backend,"CPU octree\0CPU LBVH (cached)\0Vulkan LBVH\0")) {config.Backend=Runtime::KernelDensityBackend(backend);changed=true;}
        changed |= ImGui::InputScalar("Neighbors k",ImGuiDataType_U32,&config.KNeighbors);
        changed |= ImGui::InputFloat("Bandwidth (0 = automatic)",&config.Bandwidth);
        ImGui::TextWrapped("Local Gaussian average over nearest candidates. Automatic bandwidth uses nearest-other spacing. Distances use the selected property coordinates.");
        if(config.Backend==Runtime::KernelDensityBackend::VulkanLBVH)
            changed |= ImGui::InputScalar("GPU query batch size",ImGuiDataType_U32,&config.GpuQueryBatchSize);
        const bool scalarActive=DrawPointScalarTransaction(context.PointFields.Commands,DensityTransaction,Density,context.PointFields.ResultSinks.KernelDensity);
        ImGui::BeginDisabled(scalarActive);
        DrawProcessingExecution(context.PointFields.Commands, Density, changed,
            [&](const auto& c) { return Runtime::PreviewEditorKernelDensityCommand(context.PointFields.Commands, c); },
            [&](const auto& c) { return Runtime::ApplyEditorKernelDensityConfig(context.PointFields.Commands, c); },
            [&] {
                if(config.Backend==Runtime::KernelDensityBackend::VulkanLBVH){Runtime::EditorKernelDensityResult result;
                    DensityTransaction=Runtime::StartEditorKernelDensityTransaction(context.PointFields.Commands,config,result);return result;}
                return Runtime::ApplyEditorConfiguredKernelDensity(context.PointFields.Commands,context.PointFields.ResultSinks.KernelDensity); },
            context.PointFields.ResultSinks.KernelDensity, "Estimate density",
            "Controls were rejected by density config validation.", "Density config was rejected.",
            [](const auto& c) { return std::pair{c.StableEntityId, c.Density.Name}; });
        ImGui::EndDisabled();
        const Runtime::EditorOutputRef densityDraft{config.StableEntityId, config.Density.Name};
        Density.Run.Draw(context.PointFields.Commands, config.StableEntityId, "density_progress", &densityDraft);
        ImGui::TextWrapped("Vulkan previews density on the device. Accept publishes the scalar with Undo; Discard retains CPU rows.");
        DrawProcessingPropertyShowButton(context, config.StableEntityId, config.Density, Density.VisualizationDiagnostic, "Show density");
        DrawProcessingDisplayDiagnostic(Density.VisualizationDiagnostic);
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
        if (DrawProcessingPointInput("Positions##DensityWeights", [&] { return Runtime::GetEditorPointInputCatalog(context.Processing, config.StableEntityId); }, config.Positions))
        {
            config.Weights.Domain = config.Positions.Domain;
            changed = true;
        }
        for (auto [label, ref] : {std::pair{"Weight property", &config.Weights}})
            changed |= DrawProcessingScalarOutput(label, *ref);
        int backend=int(config.Backend);
        if(ImGui::Combo("Backend",&backend,"CPU KD-tree\0CPU LBVH (cached)\0Vulkan LBVH\0")) {config.Backend=Runtime::DensityWeightBackend(backend);changed=true;}
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
        const bool scalarActive=DrawPointScalarTransaction(context.PointAnalysis.Commands,WeightTransaction,DensityWeights,context.PointAnalysis.ResultSinks.DensityWeight);
        ImGui::BeginDisabled(scalarActive);
        DrawProcessingExecution(context.PointAnalysis.Commands, DensityWeights, changed,
            [&](const auto& request) { return Runtime::PreviewEditorDensityWeightCommand(context.PointAnalysis.Commands, request); },
            [&](const auto& request) { return Runtime::ApplyEditorDensityWeightConfig(context.PointAnalysis.Commands, request); },
            [&] {
                if(config.Backend==Runtime::DensityWeightBackend::VulkanLBVH){Runtime::EditorDensityWeightResult result;
                    WeightTransaction=Runtime::StartEditorDensityWeightTransaction(context.PointAnalysis.Commands,config,result);return result;}
                return Runtime::ApplyEditorConfiguredDensityWeight(context.PointAnalysis.Commands,context.PointAnalysis.ResultSinks.DensityWeight); },
            context.PointAnalysis.ResultSinks.DensityWeight, "Compute compact weights",
            "Controls were rejected by density config validation.", "Density config was rejected.",
            [](const auto& c) { return std::pair{c.StableEntityId, c.Weights.Name}; });
        ImGui::EndDisabled();
        const Runtime::EditorOutputRef densityWeightsDraft{config.StableEntityId, config.Weights.Name};
        DensityWeights.Run.Draw(context.PointAnalysis.Commands, config.StableEntityId, "density_weights_progress", &densityWeightsDraft);
        ImGui::TextWrapped("Vulkan previews compact weights using double kernel sums. Accept publishes with Undo; Discard retains CPU rows.");
        DrawProcessingPropertyShowButton(context, config.StableEntityId, config.Weights, DensityWeights.VisualizationDiagnostic, "Show weights");
        DrawProcessingDisplayDiagnostic(DensityWeights.VisualizationDiagnostic);
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
        if (ImGui::Combo("Backend", &backend, "CPU reference\0CPU LBVH (cached)\0Vulkan LBVH\0"))
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
                changed |= DrawProcessingPointInput("Normals",
                    [&] { return Runtime::GetEditorPointInputCatalog(context.Processing, config.StableEntityId); },
                    config.Normals, config.Positions.Domain == Runtime::GeometryElementDomain::Unknown
                        ? std::nullopt : std::optional{config.Positions.Domain});
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
        const auto action = Runtime::ResolveEditorProcessingActionReadiness(
            context.PointConstruction.Commands, {readiness.Ready, readiness.Diagnostic});
        if (!action.Enabled)
            ImGui::TextWrapped("%s", action.DisabledReason.c_str());
        if (DrawProcessingActionButton("Construct", action))
        {
            ApplyQueuedProcessingExecution(context.PointConstruction.Commands, Construction, readiness.Resolved,
                [&](const auto& value) { return Runtime::ApplyEditorPointConstructionConfig(context.PointConstruction.Commands, value); },
                [&] { return Runtime::ApplyEditorConfiguredPointConstruction(context.PointConstruction.Commands, context.PointConstruction.ResultSinks.PointConstruction); },
                context.PointConstruction.ResultSinks.PointConstruction, "Construction config was rejected.",
                readiness.Resolved.StableEntityId, std::string("construct:") + std::string(Runtime::ToString(readiness.Resolved.Method)));
        }
        const Runtime::EditorOutputRef constructionDraft{
            config.StableEntityId, std::string("construct:") + std::string(Runtime::ToString(config.Method))};
        Construction.Run.Draw(context.PointConstruction.Commands, config.StableEntityId, "construction_progress",
                              &constructionDraft);
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
        if (DrawProcessingPointInput("Positions##Spacing", [&] { return Runtime::GetEditorPointSpacingInputCatalog(context.PointFields.Commands, config.StableEntityId); }, config.Positions))
        {
            config.Radii.Domain = config.Positions.Domain;
            changed = true;
        }
        changed |= DrawProcessingScalarOutput("Radii property", config.Radii);
        int backend=int(config.Backend);
        if(ImGui::Combo("Backend",&backend,"CPU octree\0CPU LBVH (cached)\0Vulkan LBVH\0")) {config.Backend=Runtime::PointSpacingBackend(backend);changed=true;}
        changed |= ImGui::InputScalar("Neighbors k",ImGuiDataType_U32,&config.KNeighbors);
        changed |= ImGui::InputFloat("Radius scale",&config.ScaleFactor);
        ImGui::TextWrapped("Radius = scale times mean retained neighbor distance. Nearest-other spacing is reported separately. Values use the selected property coordinates; coverage is not guaranteed.");
        if(config.Backend==Runtime::PointSpacingBackend::VulkanLBVH)
            changed |= ImGui::InputScalar("GPU query batch size",ImGuiDataType_U32,&config.GpuQueryBatchSize);
        const bool scalarActive=DrawPointScalarTransaction(context.PointFields.Commands,SpacingTransaction,Spacing,context.PointFields.ResultSinks.PointSpacing);
        ImGui::BeginDisabled(scalarActive);
        DrawProcessingExecution(context.PointFields.Commands, Spacing, changed,
            [&](const auto& c) { return Runtime::PreviewEditorPointSpacingCommand(context.PointFields.Commands, c); },
            [&](const auto& c) { return Runtime::ApplyEditorPointSpacingConfig(context.PointFields.Commands, c); },
            [&] {
                if(config.Backend==Runtime::PointSpacingBackend::VulkanLBVH){Runtime::EditorPointSpacingResult result;
                    SpacingTransaction=Runtime::StartEditorPointSpacingTransaction(context.PointFields.Commands,config,result);return result;}
                return Runtime::ApplyEditorConfiguredPointSpacing(context.PointFields.Commands,context.PointFields.ResultSinks.PointSpacing); },
            context.PointFields.ResultSinks.PointSpacing, "Estimate radii",
            "Controls were rejected by radii config validation.", "Spacing config was rejected.",
            [](const auto& c) { return std::pair{c.StableEntityId, c.Radii.Name}; });
        ImGui::EndDisabled();
        const Runtime::EditorOutputRef spacingDraft{config.StableEntityId, config.Radii.Name};
        Spacing.Run.Draw(context.PointFields.Commands, config.StableEntityId, "spacing_progress", &spacingDraft);
        ImGui::TextWrapped("Vulkan previews spacing and radii on the device. Accept publishes with Undo; Discard retains CPU rows. Show radii maps values to colors; point rendering currently expects pixel sizes.");
        DrawProcessingPropertyShowButton(context, config.StableEntityId, config.Radii, Spacing.VisualizationDiagnostic, "Show radii");
        DrawProcessingDisplayDiagnostic(Spacing.VisualizationDiagnostic);
        if(Spacing.LastResult)
        {
            const auto& result=*Spacing.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s",Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Requested: %s; ran: %s",Runtime::ToString(result.RequestedBackend),result.ActualBackend.c_str());
            ImGui::Text("Live / total: %zu / %zu",result.LiveCount,result.SlotCount);
            ImGui::Text("Radius min / mean / max: %.5g / %.5g / %.5g",double(result.MinRadius),double(result.MeanRadius),double(result.MaxRadius));
            ImGui::Text("Nearest spacing min / mean / max: %.5g / %.5g / %.5g", double(result.MinSpacing), double(result.AverageSpacing), double(result.MaxSpacing));
            ImGui::Text("Centroid: %.5g / %.5g / %.5g; bounds diagonal: %.5g", double(result.Centroid.x), double(result.Centroid.y), double(result.Centroid.z), double(result.BoundingBoxDiagonal));
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
        if (DrawProcessingPointInput("Positions##Bilateral", [&] { return Runtime::GetEditorBilateralFilterInputCatalog(context.PointSet.Commands, config.StableEntityId); }, config.Positions))
        {
            config.Output.Domain = config.Normals.Domain = config.Positions.Domain;
            changed = true;
        }
        changed |= DrawProcessingPointInput("Normals##Bilateral",
            [&] { return Runtime::GetEditorBilateralFilterInputCatalog(context.PointSet.Commands, config.StableEntityId); },
            config.Normals, config.Positions.Domain);
        if(ImGui::Button("Write to input positions")){config.Output=config.Positions;changed=true;}
        for (auto [label, ref] : {std::pair{"Output positions", &config.Output}})
            changed |= DrawProcessingPropertyName(label, ref->Name);
        int backend=int(config.Backend);
        if(ImGui::Combo("Backend",&backend,"CPU octree\0CPU LBVH (cached)\0Vulkan LBVH\0")) {config.Backend=Runtime::BilateralFilterBackend(backend);changed=true;}
        changed |= ImGui::InputScalar("Neighbors k",ImGuiDataType_U32,&config.KNeighbors);
        changed |= ImGui::InputFloat("Spatial sigma (0 = automatic)",&config.SpatialSigma);
        changed |= ImGui::InputFloat("Normal sigma",&config.NormalSigma);
        changed |= ImGui::InputScalar("Iterations",ImGuiDataType_U32,&config.Iterations);
        ImGui::TextWrapped("Filters positions along fixed input normals. Each pass rebuilds neighborhoods; automatic spatial sigma is resolved once.");
        if(config.Backend==Runtime::BilateralFilterBackend::VulkanLBVH)
            changed |= ImGui::InputScalar("GPU query batch size",ImGuiDataType_U32,&config.GpuQueryBatchSize);
        DrawProcessingExecution(context.PointSet.Commands, Bilateral, changed,
            [&](const auto& request) { return Runtime::PreviewEditorBilateralFilterCommand(context.PointSet.Commands, request); },
            [&](const auto& request) { return Runtime::ApplyEditorBilateralFilterConfig(context.PointSet.Commands, request); },
            [&] { return Runtime::ApplyEditorConfiguredBilateralFilter(context.PointSet.Commands, context.PointSet.ResultSinks.BilateralFilter); },
            context.PointSet.ResultSinks.BilateralFilter, "Filter positions",
            "Controls were rejected by filter config validation.", "Bilateral config was rejected.",
            [](const auto& c) { return std::pair{c.StableEntityId, c.Output.Name}; });
        const Runtime::EditorOutputRef bilateralDraft{config.StableEntityId, config.Output.Name};
        Bilateral.Run.Draw(context.PointSet.Commands, config.StableEntityId, "bilateral_progress", &bilateralDraft);
        ImGui::TextWrapped("Vulkan computes neighbors; weights and position updates run on CPU. Only the final result is published. Choose the input position property as output to update the displayed geometry; Undo restores it.");
        if(Bilateral.LastResult)
        {
            const auto& result=*Bilateral.LastResult;
            ImGui::Separator();
            ImGui::Text("Status: %s",Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text("Requested: %s; ran: %s",Runtime::ToString(result.RequestedBackend),result.ActualBackend.c_str());
            ImGui::Text("Live / total: %zu / %zu",result.LiveCount,result.SlotCount);
            ImGui::Text("Passes: %u; spatial sigma: %.5g",result.CompletedIterations,double(result.SpatialSigmaUsed));
            ImGui::Text("Last-pass displacement mean / max: %.5g / %.5g",double(result.AverageDisplacement),double(result.MaxDisplacement));
            ImGui::Text("Degenerate normals: %zu; private index builds: %zu",result.DegenerateNormals,result.WorkspaceBuilds);
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
        if (ImGui::Combo("Backend##ICP", &backend, "CPU KD-tree (reference)\0CPU LBVH (cached)\0Vulkan LBVH (CPU solve)\0"))
        { config.Backend = Runtime::RegistrationBackend(backend); changed = true; }
        if (config.Backend == Runtime::RegistrationBackend::VulkanLBVH)
            if (const auto reason = Runtime::RegistrationVulkanUnavailableReason(context.Registration.Commands); !reason.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "%s: the run will use the CPU.", reason.c_str());
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
            Registration.ConfigDiagnostic = applied.Succeeded()
                ? "" : "Registration controls were rejected by config validation.";
        }
        if (!Registration.ConfigDiagnostic.empty()) ImGui::TextWrapped("%s", Registration.ConfigDiagnostic.c_str());
        const auto preview = Runtime::PreviewEditorRegistrationCommand(context.Registration.Commands, config);
        const auto readiness = Runtime::ResolveEditorProcessingActionReadiness(
            context.Registration.Commands, preview);
        if (!readiness.Enabled) ImGui::TextWrapped("%s", readiness.DisabledReason.c_str());
        const auto live = Runtime::SnapshotEditorRegistrationProgress(Registration.Progress);
        const bool runFinal = DrawProcessingActionButton("Run ICP##ICP", readiness) && !live.Running;
        if (runFinal) config.TrajectoryStep = config.MaxIterations;
        if (runFinal || (applyTrajectory && readiness.Enabled && !live.Running))
        {
            Registration.Progress = Runtime::MakeEditorRegistrationProgress();
            Registration.RunMaxIterations = static_cast<std::uint32_t>(config.MaxIterations); // the run's cap, not the editable draft
            Registration.RunStarted = std::chrono::steady_clock::now();
            Registration.Run.WatchOutput(config.SourceStableEntityId, "registration_transform");
            ApplyQueuedProcessingExecution(context.Registration.Commands, Registration, config,
                [&](const auto& value) { return Runtime::ApplyEditorRegistrationConfig(context.Registration.Commands, value); },
                [&] {
                    return Runtime::ApplyEditorConfiguredRegistrationCommand(context.Registration.Commands,
                        context.Registration.ResultSinks.Registration, Registration.Progress);
                },
                context.Registration.ResultSinks.Registration, "Registration config was rejected.",
                config.SourceStableEntityId, "registration_transform");
        }
        DrawRegistrationProgress(live, Registration.RunMaxIterations);

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

    // UI-067: the running ICP job's iteration, cancel, moving-source preview and plots.
    void MeshProcessingPanels::Impl::DrawRegistrationProgress(
        const Runtime::EditorRegistrationProgressSnapshot& live, const std::uint32_t maxIterations)
    {
        auto& state = Registration;
        if (!state.Progress) return;
        // The run is the panel's own (not a job of the session), so the slot draws the bar the panel builds.
        Runtime::EditorOperationProgress bar{};
        if (live.Running)
            bar = MakeIterationProgress(live.Trace.size(), maxIterations,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - state.RunStarted).count(),
                "ICP iteration " + std::to_string(live.Trace.size()));
        state.Run.DrawLive(bar, OperationRunSlot::kAnyEntity,
                           [&] { Runtime::CancelEditorRegistration(state.Progress); }, "icp_progress");
        if (live.Running)
        {
            const auto* last = live.Trace.empty() ? nullptr : &live.Trace.back();
            ImGui::TextDisabled("RMSE %.6g   inliers %llu", last ? last->RMSE : 0.0,
                                static_cast<unsigned long long>(last ? last->InlierCount : 0u));
        }
        ImGui::Checkbox("Preview moving source##ICP", &state.LivePreview);
        auto* interaction = Shell != nullptr ? Shell->SceneInteraction() : nullptr;
        const bool show = state.LivePreview && live.Running && live.SourceWorld && !live.SourceWorld->empty();
        if (interaction != nullptr && show && live.Revision != state.PreviewRevision)
        {
            const auto& source = *live.SourceWorld;
            const std::size_t stride = std::max<std::size_t>(1u, source.size() / 20000u);
            glm::vec3 lo{std::numeric_limits<float>::max()}, hi{-std::numeric_limits<float>::max()};
            for (std::size_t i = 0; i < source.size(); i += stride) { lo = glm::min(lo, source[i]); hi = glm::max(hi, source[i]); }
            const float radius = std::max(1e-4f, 0.004f * glm::length(hi - lo));
            std::vector<Runtime::SceneInteractionModule::PreviewPoint> points;
            for (std::size_t i = 0; i < source.size(); i += stride)
                points.push_back({.Position = glm::vec3(live.Pose * glm::vec4(source[i], 1.0f)),
                                  .Color = {1.0f, 0.55f, 0.1f, 1.0f}, .Radius = radius, .DepthTested = true, .Sphere = true});
            interaction->SetPreviewOverlay("icp", points);
            state.PreviewRevision = live.Revision;
            state.PreviewShown = true;
        }
        else if (interaction != nullptr && !show && state.PreviewShown)
        {
            interaction->ClearPreviewOverlay("icp");
            state.PreviewShown = false;
            state.PreviewRevision = 0u;
        }
        if (live.Trace.empty()) return;
        std::vector<double> iteration, seconds;
        std::vector<RegistrationTraceSeries> series{{"RMSE", {}, true}, {"inliers"}, {"iteration time [s]"}};
        for (const auto& t : live.Trace)
        {
            iteration.push_back(double(t.Iteration));
            seconds.push_back(t.Seconds);
            series[0].Values.push_back(t.RMSE);
            series[1].Values.push_back(double(t.InlierCount));
            series[2].Values.push_back(t.IterationSeconds);
        }
        DrawRegistrationTracePlots("ICPTrace", iteration, seconds, series, state.PlotBySeconds);
    }

    void MeshProcessingPanels::Impl::ClearCoherentPointDriftPreview()
    {
        if (Shell != nullptr)
            if (auto* interaction = Shell->SceneInteraction()) interaction->ClearPreviewOverlay("coherent_point_drift");
        CoherentPointDrift.PreviewRevision = 0u;
    }

    // RUNTIME-273 run controls: start captures the operands, steps run on the job service,
    // the moving source is previewed as an overlay, and Apply publishes one undoable step.
    void MeshProcessingPanels::Impl::DrawCoherentPointDriftWindow(bool& open, const SandboxEditorContext& context)
    {
        auto& state = CoherentPointDrift;
        const auto& commands = context.Registration.Commands;
        ImGui::SetNextWindowSize(ImVec2(440.0f, 720.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Coherent Point Drift", &open))
        {
            ImGui::End();
            if (!open) ClearCoherentPointDriftPreview();
            return;
        }
        ImGui::TextWrapped("Probabilistic registration of a moving source onto a fixed target (Myronenko & Song 2010). "
                           "Soft correspondences and the outlier weight make it robust to noise, clutter and partial "
                           "overlap; affine and nonrigid variants fit deformations ICP cannot.");
        const auto active = Runtime::GetEditorCoherentPointDriftConfig(commands).value_or(Runtime::CoherentPointDriftConfig{});
        const auto activeText = Runtime::SerializeCoherentPointDriftConfig(active);
        if (activeText != state.LastApplied)
        {
            state.Draft = active;
            state.LastApplied = activeText;
            state.ConfigDiagnostic.clear();
            std::snprintf(state.DisplacementName.data(), state.DisplacementName.size(), "%s", active.DisplacementName.c_str());
        }
        auto& config = state.Draft;
        const auto fields = Runtime::CoherentPointDriftConfigFieldSpecs();
        const Runtime::CoherentPointDriftConfig defaults{};
        const auto hint = [&](std::string_view field) { DrawConfigFieldHint(Runtime::FindConfigFieldSpec(fields, field), {}); };
        bool changed = DrawProcessingEntity("Source (moving)##CPD", context, config.SourceStableEntityId, state.LastSelectedSource);
        hint("source");
        changed |= DrawProcessingEntity("Target (fixed)##CPD", context, config.TargetStableEntityId, state.LastSelectedTarget,
                                        std::nullopt, 1u);
        hint("target");
        if (ImGui::Button("Swap source and target##CPD"))
        {
            std::swap(config.SourceStableEntityId, config.TargetStableEntityId);
            std::swap(config.SourcePositions, config.TargetPositions);
            changed = true;
        }
        const auto propertyChoice = [&](const char* label, std::uint32_t entity, Runtime::GeometryPropertyRef& ref) {
            const auto catalog = Runtime::GetEditorRegistrationInputCatalog(commands, entity);
            const auto preview = std::string(Runtime::ToString(ref.Domain)) + ": " + ref.Name;
            if (ImGui::BeginCombo(label, preview.c_str()))
            {
                for (const auto& row : catalog.Entries)
                {
                    const auto title = std::string(Runtime::ToString(row.Ref.Domain)) + ": " + row.Ref.Name + " (" +
                                       std::to_string(row.ElementCount) + ")";
                    if (ImGui::Selectable(title.c_str(), row.Ref == ref)) { ref = row.Ref; changed = true; }
                }
                ImGui::EndCombo();
            }
        };
        propertyChoice("Source positions##CPD", config.SourceStableEntityId, config.SourcePositions);
        hint("source_positions");
        propertyChoice("Target positions##CPD", config.TargetStableEntityId, config.TargetPositions);
        hint("target_positions");

        ImGui::SeparatorText("Model");
        if (DrawSpecEnumCombo("Method##CPD", fields, "method", config.Method, defaults.Method))
        {
            // Keep the output storable: a transform only holds rigid results.
            if (config.Method != Runtime::CoherentPointDriftMethod::Rigid &&
                config.Output == Runtime::CoherentPointDriftOutput::SourceTransform)
                config.Output = Runtime::CoherentPointDriftOutput::DisplacementProperty;
            changed = true;
        }
        // The E-step is the backend choice (CPU variants or Vulkan); its tuning stays under Performance.
        changed |= DrawSpecEnumCombo("Backend (E-step)##CPD", fields, "e_step", config.EStep, defaults.EStep);
        if (config.EStep == Runtime::CoherentPointDriftEStep::Vulkan)
            if (const auto reason = Runtime::CoherentPointDriftVulkanUnavailableReason(commands); !reason.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "%s: the E-step will run on the CPU.", reason.c_str());
        changed |= DrawSpecInputDouble("Outlier weight w##CPD", fields, "outlier_weight", config.OutlierWeight, defaults.OutlierWeight);
        if (config.Method == Runtime::CoherentPointDriftMethod::Rigid)
        {
            DrawSpecCheckbox("Estimate scale##CPD", fields, "estimate_scale", config.EstimateScale, defaults.EstimateScale, changed);
            DrawSpecCheckbox("Allow reflection##CPD", fields, "allow_reflection", config.AllowReflection, defaults.AllowReflection, changed);
        }
        const bool bayesian = config.Method == Runtime::CoherentPointDriftMethod::Bayesian;
        if (config.Method == Runtime::CoherentPointDriftMethod::Nonrigid || bayesian)
        {
            changed |= DrawSpecInputDouble("Kernel width (beta)##CPD", fields, "beta", config.Beta, defaults.Beta);
            changed |= DrawSpecInputDouble("Smoothness (lambda)##CPD", fields, "lambda", config.Lambda, defaults.Lambda);
            changed |= DrawSpecInputUInt("Kernel rank##CPD", fields, "low_rank", config.LowRank, defaults.LowRank);
            if (config.LowRank == 0u)
                DrawSpecCheckbox("Automatic rank for large inputs##CPD", fields, "auto_low_rank", config.AutoLowRank,
                                 defaults.AutoLowRank, changed);
        }
        if (bayesian)
        {
            DrawSpecCheckbox("Estimate scale##CPD", fields, "estimate_scale", config.EstimateScale, defaults.EstimateScale, changed);
            changed |= DrawSpecInputDouble("Gamma##CPD", fields, "gamma", config.Gamma, defaults.Gamma);
            changed |= DrawSpecInputDouble("Kappa##CPD", fields, "kappa", config.Kappa, defaults.Kappa);
            changed |= DrawSpecInputUInt("Subsample##CPD", fields, "subsample", config.Subsample, defaults.Subsample);
            changed |= DrawSpecInputUInt("Target subsample##CPD", fields, "subsample_target", config.SubsampleTarget,
                                         defaults.SubsampleTarget);
            if ((config.Subsample > 0u || config.SubsampleTarget > 0u) && ImGui::TreeNode("Subsampling##CPD"))
            {
                changed |= DrawPointSamplingControls("CPDSubsample", fields, "subsample_", config.SubsampleSampling,
                                                     defaults.SubsampleSampling);
                ImGui::TreePop();
            }
        }
        const bool usesLandmarks = config.EStep == Runtime::CoherentPointDriftEStep::Nystrom ||
                                   ((config.LowRank > 0u || config.AutoLowRank) &&
                                    (config.Method == Runtime::CoherentPointDriftMethod::Nonrigid ||
                                     config.Method == Runtime::CoherentPointDriftMethod::Bayesian));
        if (usesLandmarks && ImGui::TreeNode("Landmarks##CPD"))
        {
            changed |= DrawPointSamplingControls("CPDLandmarks", fields, "landmark_", config.LandmarkSampling,
                                                 defaults.LandmarkSampling);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Performance##CPD"))
        {
            if (config.EStep != Runtime::CoherentPointDriftEStep::Reference && config.EStep != Runtime::CoherentPointDriftEStep::Dense)
                changed |= DrawSpecInputDouble("Error tolerance##CPD", fields, "e_step_tolerance", config.EStepTolerance,
                                               defaults.EStepTolerance, "%.1e");
            if (config.EStep == Runtime::CoherentPointDriftEStep::Nystrom)
            {
                changed |= DrawSpecInputUInt("Landmarks##CPD", fields, "nystrom_landmarks", config.NystromLandmarks,
                                             defaults.NystromLandmarks);
                changed |= DrawSpecInputDouble("Error limit##CPD", fields, "nystrom_error_limit", config.NystromErrorLimit,
                                               defaults.NystromErrorLimit, "%.1e");
            }
            if (config.EStep != Runtime::CoherentPointDriftEStep::Reference)
                changed |= DrawSpecInputUInt("Threads##CPD", fields, "threads", config.Threads, defaults.Threads);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Convergence##CPD"))
        {
            changed |= DrawSpecInputUInt("Max iterations##CPD", fields, "max_iterations", config.MaxIterations, defaults.MaxIterations);
            changed |= DrawSpecInputDouble("Tolerance##CPD", fields, "tolerance", config.Tolerance, defaults.Tolerance, "%.2e");
            changed |= DrawSpecInputDouble("Initial sigma^2##CPD", fields, "initial_sigma2", config.InitialSigma2,
                                           defaults.InitialSigma2, "%.4g");
            changed |= DrawSpecInputDouble("Sigma^2 floor##CPD", fields, "sigma2_floor", config.Sigma2Floor, defaults.Sigma2Floor, "%.2e");
            DrawSpecCheckbox("Normalize inputs##CPD", fields, "normalize", config.NormalizeInputs, defaults.NormalizeInputs, changed);
            ImGui::TreePop();
        }
        ImGui::SeparatorText("Output");
        changed |= DrawSpecEnumCombo("Write result to##CPD", fields, "output", config.Output, defaults.Output);
        if (config.Output == Runtime::CoherentPointDriftOutput::DisplacementProperty)
        {
            if (ImGui::InputText("Displacement property##CPD", state.DisplacementName.data(), state.DisplacementName.size()))
            {
                config.DisplacementName = state.DisplacementName.data();
                changed = true;
            }
            hint("displacement_name");
        }
        if (changed)
        {
            const auto applied = Runtime::ApplyEditorCoherentPointDriftConfig(commands, config);
            state.ConfigDiagnostic = applied.Succeeded() ? std::string{}
                : Runtime::PreviewEditorCoherentPointDriftCommand(commands, config).DisabledReason;
            if (!applied.Succeeded() && state.ConfigDiagnostic.empty()) state.ConfigDiagnostic = "Settings were rejected.";
        }
        if (!state.ConfigDiagnostic.empty()) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "%s", state.ConfigDiagnostic.c_str());

        ImGui::SeparatorText("Run");
        const auto snapshot = Runtime::SnapshotEditorCoherentPointDrift(state.Run);
        using Phase = Runtime::EditorCoherentPointDriftPhase;
        const bool hasRun = state.Run != nullptr;
        const bool running = hasRun && snapshot.Phase == Phase::Running;
        // Restart, Apply and Discard make this frame's snapshot stale; it must not redraw the preview.
        bool snapshotStale = false;
        const bool steppable = hasRun && (snapshot.Phase == Phase::Ready || snapshot.Phase == Phase::Paused);
        const bool applicable = hasRun && (snapshot.Phase == Phase::Paused || snapshot.Phase == Phase::Finished);
        const auto readiness = Runtime::ResolveEditorProcessingActionReadiness(
            commands, Runtime::PreviewEditorCoherentPointDriftCommand(commands, config));
        if (!hasRun || !steppable)
        {
            if (DrawProcessingActionButton(hasRun ? "Restart##CPD" : "Start##CPD", readiness) && !running)
            {
                ClearCoherentPointDriftPreview();
                snapshotStale = true;
                Runtime::EditorCoherentPointDriftResult failure;
                state.Run = Runtime::StartEditorCoherentPointDrift(commands, config, failure);
                state.RunMaxIterations = config.MaxIterations; // the run's cap, not the editable draft
                state.ProgressSlot.WatchOutput(config.SourceStableEntityId, "coherent_point_drift");
                state.RunMessage = state.Run ? std::string{} : failure.Message;
            }
            if (!readiness.Enabled && !readiness.DisabledReason.empty()) ImGui::TextWrapped("%s", readiness.DisabledReason.c_str());
        }
        if (hasRun)
        {
            const auto step = [&](std::uint32_t count) {
                const auto status = Runtime::StepEditorCoherentPointDrift(commands, state.Run, count);
                state.RunMessage = status == Runtime::EditorCommandStatus::Pending ? std::string{}
                                                                                  : "This run takes no more steps.";
            };
            ImGui::BeginDisabled(!steppable);
            if (ImGui::Button("Step##CPD")) step(1u);
            ImGui::SameLine();
            if (ImGui::Button("Step 10##CPD")) step(10u);
            ImGui::SameLine();
            if (ImGui::Button("Run to end##CPD")) step(0u);
            ImGui::EndDisabled();
            ImGui::BeginDisabled(!applicable);
            if (ImGui::Button("Apply##CPD"))
            {
                state.LastResult = Runtime::ApplyEditorCoherentPointDrift(commands, state.Run);
                if (state.LastResult->Succeeded()) { ClearCoherentPointDriftPreview(); snapshotStale = true; }
            }
            ImGui::EndDisabled();
            if (!applicable && hasRun && snapshot.Phase != Phase::Applied)
                DrawDisabledReasonTooltip("Apply publishes a paused or finished run.");
            ImGui::SameLine();
            if (ImGui::Button("Discard##CPD"))
            {
                if (running) Runtime::CancelEditorCoherentPointDrift(state.Run);
                state.Run.reset();
                ClearCoherentPointDriftPreview();
                snapshotStale = true;
            }
        }
        if (!state.RunMessage.empty()) ImGui::TextWrapped("%s", state.RunMessage.c_str());
        // Shown next to the buttons so a rejected apply (e.g. stale inputs) is not missed below the plots.
        if (state.LastResult)
        {
            const bool ok = state.LastResult->Succeeded();
            ImGui::PushStyleColor(ImGuiCol_Text, ok ? ImGui::GetStyleColorVec4(ImGuiCol_Text) : ImVec4(1.0f, 0.55f, 0.3f, 1.0f));
            ImGui::TextWrapped("Last apply: %s - %s", Runtime::DebugNameForEditorCommandStatus(state.LastResult->Status),
                               state.LastResult->Message.c_str());
            ImGui::PopStyleColor();
        }
        if (ImGui::Checkbox("Preview in viewport##CPD", &state.LivePreview) && !state.LivePreview)
            ClearCoherentPointDriftPreview();
        if (state.LivePreview && hasRun && (snapshot.SourceSamples || snapshot.TargetSamples))
        {
            ImGui::SameLine();
            if (ImGui::Checkbox("Show subsamples##CPD", &state.ShowSubsamples)) state.PreviewRevision = 0u; // redraw
        }

        if (hasRun)
        {
            const auto& r = snapshot.Result;
            ImGui::Text("Phase: %s   backend: %s", Runtime::ToString(snapshot.Phase), r.Backend.c_str());
            // UI-067: what a running step is doing right now, and for how long.
            Runtime::EditorOperationProgress iteration{};
            if (snapshot.Phase == Phase::Running)
            {
                const double elapsed = snapshot.RunStarted == std::chrono::steady_clock::time_point{} ? 0.0
                    : std::chrono::duration<double>(std::chrono::steady_clock::now() - snapshot.RunStarted).count();
                iteration = MakeIterationProgress(snapshot.Trace.size(), state.RunMaxIterations, elapsed,
                                                  "iteration " + std::to_string(snapshot.Trace.size() + 1u) +
                                                      (snapshot.Stage.empty() ? std::string{} : ": " + snapshot.Stage));
            }
            state.ProgressSlot.DrawLive(iteration, OperationRunSlot::kAnyEntity,
                                        [&] { Runtime::CancelEditorCoherentPointDrift(state.Run); }, "cpd_progress");
            ImGui::Text("Points: %zu -> %zu   iterations: %u   stop: %s", r.SourcePointCount, r.TargetPointCount, r.Iterations,
                        r.Termination.c_str());
            ImGui::Text("sigma^2: %.4g   matched: %.1f   mean move: %.4g", r.Sigma2, r.MatchedWeight, r.MeanDisplacement);
            if (!snapshot.Trace.empty())
                ImGui::Text("E-step: %s   error bound: %.2g", snapshot.Trace.back().EStep.c_str(), r.EStepErrorBound);
            if (r.EStepSampledError > 0.0)
                ImGui::Text("Nystroem sampled error: %.2g (estimate)", r.EStepSampledError);
            if (r.EStepDeviceIterations > 0u)
                ImGui::Text("GPU E-step iterations: %u of %u", r.EStepDeviceIterations, r.Iterations);
            if (r.EStepFallbacks > 0u)
                ImGui::TextWrapped("GPU E-step fell back to the CPU %u time(s)%s%s", r.EStepFallbacks,
                                   r.GpuDiagnostic.empty() ? "" : ": ", r.GpuDiagnostic.c_str());
            if (r.KernelRank > 0u)
                ImGui::Text("Kernel rank: %u   kernel error: %.2g", r.KernelRank, r.KernelApproximationError);
            if (!r.Message.empty() && (snapshot.Phase == Phase::Failed || snapshot.Phase == Phase::Cancelled))
                ImGui::TextWrapped("%s", r.Message.c_str());

            // Overlay the moving source (orange) whenever the run published new positions.
            if (state.LivePreview && !snapshotStale && Shell != nullptr && snapshot.Revision != state.PreviewRevision &&
                snapshot.Phase != Phase::Applied && snapshot.SourcePreview && snapshot.Target)
                if (auto* interaction = Shell->SceneInteraction())
                {
                    std::vector<Runtime::SceneInteractionModule::PreviewPoint> points;
                    const auto& preview = *snapshot.SourcePreview;
                    const std::size_t count = preview.size();
                    const std::size_t stride = std::max<std::size_t>(1u, count / 20000u);
                    glm::vec3 lo{std::numeric_limits<float>::max()}, hi{-std::numeric_limits<float>::max()};
                    for (const auto& p : *snapshot.Target) { lo = glm::min(lo, p); hi = glm::max(hi, p); }
                    const float radius = std::max(1e-4f, 0.004f * glm::length(hi - lo));
                    for (std::size_t i = 0; i < count; i += stride)
                        points.push_back({.Position = preview[i], .Color = {1.0f, 0.55f, 0.1f, 1.0f},
                                          .Radius = radius, .DepthTested = true, .Sphere = true});
                    // BCPD subsamples as larger spheres: the registered source samples follow the
                    // moving source (green), the target samples stay put (cyan).
                    if (state.ShowSubsamples)
                    {
                        if (snapshot.SourceSamples)
                            for (const std::uint32_t i : *snapshot.SourceSamples)
                                if (i < count)
                                    points.push_back({.Position = preview[i], .Color = {0.2f, 0.9f, 0.3f, 1.0f},
                                                      .Radius = 2.0f * radius, .DepthTested = true, .Sphere = true});
                        if (snapshot.TargetSamples)
                            for (const std::uint32_t j : *snapshot.TargetSamples)
                                if (j < snapshot.Target->size())
                                    points.push_back({.Position = (*snapshot.Target)[j], .Color = {0.2f, 0.75f, 1.0f, 1.0f},
                                                      .Radius = 2.0f * radius, .DepthTested = true, .Sphere = true});
                    }
                    interaction->SetPreviewOverlay("coherent_point_drift", points);
                    state.PreviewRevision = snapshot.Revision;
                }

            if (!snapshot.Trace.empty())
            {
                // UI-067: every per-iteration quantity, against iterations or elapsed seconds.
                std::vector<double> iteration, seconds;
                std::vector<RegistrationTraceSeries> series{{"sigma^2", {}, true}, {"objective"}, {"negative log-likelihood"},
                                                            {"matched weight"}, {"iteration time [s]"},
                                                            {"kernel evaluations", {}, true}};
                bool bounded = false;
                for (const auto& t : snapshot.Trace)
                {
                    iteration.push_back(double(t.Iteration));
                    seconds.push_back(t.Seconds);
                    series[0].Values.push_back(t.Sigma2);
                    series[1].Values.push_back(t.Objective);
                    series[2].Values.push_back(t.NegativeLogLikelihood);
                    series[3].Values.push_back(t.MatchedWeight);
                    series[4].Values.push_back(t.IterationSeconds);
                    series[5].Values.push_back(double(t.KernelEvaluations));
                    bounded = bounded || t.EStepErrorBound > 0.0;
                }
                if (bounded)
                {
                    series.push_back({"E-step error bound", {}, true});
                    for (const auto& t : snapshot.Trace) series.back().Values.push_back(t.EStepErrorBound);
                }
                DrawRegistrationTracePlots("CPDTrace", iteration, seconds, series, state.PlotBySeconds);
                if (ImGui::Button("Export trace (CSV)##CPD"))
                {
                    std::error_code error;
                    const auto directory = std::filesystem::current_path(error) / "exports";
                    std::filesystem::create_directories(directory, error);
                    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    std::tm local{};
                    localtime_r(&now, &local);
                    char stamp[32]{};
                    (void)std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
                    const auto path = directory / ("cpd-trace-" + std::string(stamp) + ".csv");
                    std::ofstream csv(path);
                    csv << "iteration,seconds,iteration_seconds,sigma2,negative_log_likelihood,objective,matched_weight,e_step,"
                           "e_step_error_bound,e_step_sampled_error,kernel_evaluations\n";
                    for (const auto& t : snapshot.Trace)
                        csv << t.Iteration << ',' << t.Seconds << ',' << t.IterationSeconds << ',' << t.Sigma2 << ','
                            << t.NegativeLogLikelihood << ',' << t.Objective << ',' << t.MatchedWeight << ',' << t.EStep << ','
                            << t.EStepErrorBound << ',' << t.EStepSampledError << ',' << t.KernelEvaluations << '\n';
                    state.ExportMessage = csv ? "Saved " + path.string() : "Could not write " + path.string();
                }
                if (!state.ExportMessage.empty()) ImGui::TextWrapped("%s", state.ExportMessage.c_str());
            }
        }
        ImGui::End();
        if (!open) ClearCoherentPointDriftPreview();
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

    void MeshProcessingPanels::InjectPropertySmoothingTransactionForTest(Runtime::EditorPropertySmoothingTransactionHandle transaction)
    {
        m_Impl->SmoothingTransaction = std::move(transaction);
    }

    void MeshProcessingPanels::InjectPointScalarTransactionForTest(unsigned method,Runtime::EditorPointScalarTransactionHandle transaction)
    {
        if(method==0){m_Impl->DensityTransaction=std::move(transaction);m_Impl->Density.LastResult.emplace();}
        else if(method==1){m_Impl->SpacingTransaction=std::move(transaction);m_Impl->Spacing.LastResult.emplace();}
        else if(method==2){m_Impl->WeightTransaction=std::move(transaction);m_Impl->DensityWeights.LastResult.emplace();}
    }

    void MeshProcessingPanels::InjectOutlierTransactionForTest(Runtime::EditorOutlierTransactionHandle transaction)
    {
        m_Impl->OutlierTransaction = std::move(transaction);
    }

    void MeshProcessingPanels::InjectNormalTransactionForTest(Runtime::EditorNormalTransactionHandle transaction)
    {
        m_Impl->NormalTransaction = std::move(transaction);
    }

    // Accept / Discard of the interactive Vulkan vertex-normals run (ADR 0030 decisions 6-7).
    // The viewport keeps the CPU normals until Accept; "Applied" appears only after the CPU
    // publication succeeded.
    void MeshProcessingPanels::Impl::DrawNormalTransaction(const SandboxEditorContext& context)
    {
        using Phase = Runtime::EditorGpuTransactionPhase;
        const auto& commands = context.Normals.Commands;
        const auto transaction = Runtime::SnapshotEditorNormalEstimation(commands, NormalTransaction);
        ImGui::SeparatorText("GPU result");
        ImGui::TextDisabled("State: %s%s", Runtime::ToString(transaction.Phase),
                            transaction.Phase == Phase::ReadyToAccept && transaction.Stale ? " (stale)" : "");
        ImGui::BeginDisabled(!transaction.CanAccept);
        if (ImGui::Button("Accept##Normals") && transaction.CanAccept)
            Normals.LastResult = Runtime::AcceptEditorNormalEstimation(commands, NormalTransaction,
                [completion = NormalCompletion](Runtime::EditorNormalEstimationResult result) { *completion = std::move(result); });
        ImGui::EndDisabled();
        if (!transaction.CanAccept && !transaction.AcceptDisabledReason.empty())
            DrawDisabledReasonTooltip(transaction.AcceptDisabledReason);
        ImGui::SameLine();
        ImGui::BeginDisabled(transaction.Phase == Phase::Applied || transaction.Phase == Phase::Discarded || transaction.Phase == Phase::Failed);
        if (ImGui::Button("Discard##Normals"))
        {
            Runtime::DiscardEditorNormalEstimation(commands, NormalTransaction);
            Normals.Run.Forget(); // a discarded result never reads as a finished run
            Normals.LastResult = Runtime::SnapshotEditorNormalEstimation(commands, NormalTransaction).Result;
        }
        ImGui::EndDisabled();
        if (!transaction.AcceptDisabledReason.empty()) ImGui::TextWrapped("%s", transaction.AcceptDisabledReason.c_str());
        else if (transaction.Phase == Phase::ReadyToAccept)
            ImGui::TextWrapped("The device result is resident (no viewport preview); Accept publishes it (undoable), Discard keeps the CPU normals.");
        ImGui::TextWrapped("Input uploads: %llu bytes; cache hits: %llu; CPU readback: %llu bytes",
            static_cast<unsigned long long>(transaction.Result.GpuInputUploadBytes),
            static_cast<unsigned long long>(transaction.Result.GpuInputCacheHits),
            static_cast<unsigned long long>(transaction.Result.CpuStageReadbackBytes));
        if (transaction.Result.GpuTopologyBytes || transaction.Result.GpuInputUploadBytes)
            ImGui::TextDisabled("Residency IO: positions upload %llu bytes; topology bundle %llu bytes (%s)",
                                static_cast<unsigned long long>(transaction.Result.GpuInputUploadBytes),
                                static_cast<unsigned long long>(transaction.Result.GpuTopologyBytes),
                                transaction.Result.GpuTopologyReused ? "resident" : "uploaded");
        if (transaction.Phase == Phase::Applied || transaction.Phase == Phase::Discarded || transaction.Phase == Phase::Failed)
        {
            // Terminal: the result line reports it; the next run may start.
            Normals.LastResult = transaction.Result;
            if (context.Normals.ResultSinks.NormalEstimation)
                context.Normals.ResultSinks.NormalEstimation(transaction.Result);
            NormalTransaction.reset();
        }
    }

    void MeshProcessingPanels::Unregister()
    {
        m_Impl->Unregister();
    }

}

namespace Extrinsic::Sandbox::Editor
{
    namespace
    {
        // Smooth Property controls take descriptions, ranges and enum names from the section's field table.
        const Runtime::PropertySmoothingConfig kSmoothingDefaults{};

        bool DrawVariationalFitSettings(Runtime::PropertySmoothingConfig& config, const Runtime::EditorPropertyCatalogModel& catalog)
        {
            namespace S = Geometry::Smoothing;
            const auto fields = Runtime::PropertySmoothingConfigFieldSpecs();
            const auto& d = kSmoothingDefaults.Filter;
            auto& f = config.Filter;
            bool changed = DrawSpecEnumCombo("Smoothness penalty", fields, "smoothness_penalty", f.SmoothnessPenalty, d.SmoothnessPenalty);
            changed |= DrawSpecEnumCombo("Smoothness order", fields, "smoothness_order", f.SmoothnessOrder, d.SmoothnessOrder);
            if (f.SmoothnessOrder == S::FitOrder::Second)
                changed |= DrawSpecInputDouble("Second-order weight (alpha0/alpha1)", fields, "second_order_weight", f.SecondOrderWeight, d.SecondOrderWeight);
            changed |= DrawSpecEnumCombo("Data penalty", fields, "data_penalty", f.DataPenalty, d.DataPenalty);
            if (f.SmoothnessPenalty != S::FitPenalty::Quadratic || f.DataPenalty != S::FitPenalty::Quadratic)
                changed |= DrawSpecInputDouble("Penalty delta (property units)", fields, "penalty_delta", f.PenaltyDelta, d.PenaltyDelta);
            changed |= DrawSpecEnumCombo("Data weight", fields, "fidelity", f.Fidelity, d.Fidelity);
            if (f.Fidelity == S::FitFidelity::FixedWeight) changed |= DrawSpecInputDouble("Fit weight", fields, "fit_weight", f.FitWeight, d.FitWeight);
            else changed |= DrawSpecInputDouble("Noise level (RMS, property units)", fields, "noise_level", f.NoiseLevel, d.NoiseLevel);
            if (DrawSpecEnumCombo("Tolerance bound", fields, "bound", f.Bound, d.Bound))
            {
                if (f.Bound == S::FitBound::PerRow && config.BoundRadii.Name.empty()) config.BoundRadii.Name = "tolerance";
                changed = true;
            }
            if (f.Bound == S::FitBound::Uniform)
                changed |= DrawSpecInputDouble("Bound radius (property units)", fields, "bound_radius", f.BoundRadius, d.BoundRadius);
            if (f.Bound != S::FitBound::None && Runtime::GeometryPropertyComponentCount(config.Input.ValueKind) > 1)
                changed |= DrawSpecEnumCombo("Bound shape", fields, "bound_norm", f.BoundNorm, d.BoundNorm);
            if (f.Bound == S::FitBound::PerRow)
            {
                changed |= DrawProcessingPropertyInput("Radius property##Smoothing", catalog, config.BoundRadii,
                    +[](const Runtime::GeometryPropertyRef& ref) {
                        return ref.ValueKind == Geometry::PropertyValueKind::Float || ref.ValueKind == Geometry::PropertyValueKind::Double;
                    });
                DrawConfigFieldHint(Runtime::FindConfigFieldSpec(fields, "bound_radii"), {});
            }
            changed |= DrawSpecEnumCombo("Fit solver", fields, "fit_solver", f.FitAlgorithm, d.FitAlgorithm);
            changed |= DrawSpecInputUInt("Maximum fit iterations", fields, "max_fit_iterations", f.MaxFitIterations, d.MaxFitIterations);
            changed |= DrawSpecInputDouble("Fit tolerance (relative)", fields, "fit_tolerance", f.FitTolerance, d.FitTolerance, "%.2e");
            return changed;
        }
    }

    void MeshProcessingPanels::Impl::DrawSmoothingWindow(bool& open, const SandboxEditorContext& context)
    {
        if (!ImGui::Begin("Smooth Property", &open)) { ImGui::End(); return; }
        const auto previous = SmoothingEntity;
        DrawProcessingEntity("Entity##PropertySmoothing", context, SmoothingEntity, Smoothing.LastSelectedEntity);
        if (previous != SmoothingEntity) Smoothing.LastResult.reset();
        if (*SmoothingCompletion) { Smoothing.LastResult = std::move(**SmoothingCompletion); SmoothingCompletion->reset(); }
        if (const auto active = Runtime::GetEditorPropertySmoothingConfig(context.MeshFields.Commands))
            Smoothing.Synchronize(*active, Runtime::SerializePropertySmoothingConfig(*active));
        const auto& model = GetDomainWindowModel(context, Runtime::EditorDomainWindowKind::Mesh, SmoothingEntity);
        if (!model.HasSelectedEntity || Smoothing.LastApplied.empty())
        { ImGui::TextDisabled("Select a geometry entity to smooth a property."); ImGui::End(); return; }
        auto& config = Smoothing.Draft;
        const auto before = config;
        const auto smoothable = +[](const Runtime::GeometryPropertyRef& ref) {
            using K = Geometry::PropertyValueKind;
            return ref.ValueKind == K::Float || ref.ValueKind == K::Double || ref.ValueKind == K::Vec2 ||
                   ref.ValueKind == K::Vec3 || ref.ValueKind == K::Vec4;
        };
        namespace S = Geometry::Smoothing;
        const auto fields = Runtime::PropertySmoothingConfigFieldSpecs();
        const auto& defaults = kSmoothingDefaults;
        const auto hint = [&](std::string_view field) { DrawConfigFieldHint(Runtime::FindConfigFieldSpec(fields, field), {}); };
        bool changed = DrawProcessingPropertyInput("Input property##Smoothing", model.PropertyCatalog, config.Input, smoothable);
        hint("input");
        changed |= DrawProcessingPropertyInput("Neighborhood positions##Smoothing", model.PropertyCatalog, config.Positions,
            +[](const Runtime::GeometryPropertyRef& ref) { return ref.ValueKind == Geometry::PropertyValueKind::Vec3; });
        hint("positions");
        ImGui::TextWrapped("Use positions on the input domain, or vertex/node positions to derive face centers and edge/halfedge midpoints.");
        changed |= DrawProcessingPropertyName("Output property##Smoothing", config.Output.Name);
        hint("output");
        if (ImGui::Button("Overwrite input property")) { config.Output = config.Input; changed = true; }
        if (config.Input.ValueKind == Geometry::PropertyValueKind::Float || config.Input.ValueKind == Geometry::PropertyValueKind::Double)
        {
            int kind = config.Output.ValueKind == Geometry::PropertyValueKind::Double ? 1 : 0;
            if (ImGui::Combo("Output storage", &kind, "float\0double\0"))
            { config.Output.ValueKind = kind ? Geometry::PropertyValueKind::Double : Geometry::PropertyValueKind::Float; changed = true; }
        }
        changed |= DrawSpecEnumCombo("Method", fields, "method", config.Filter.Method, defaults.Filter.Method);
        if (config.Filter.Method != S::PropertyFilter::VariationalFit)
            changed |= DrawSpecEnumCombo("Backend##Smoothing", fields, "backend", config.Backend, defaults.Backend);
        changed |= DrawSpecEnumCombo("Laplacian", fields, "laplacian", config.Filter.Laplacian, defaults.Filter.Laplacian);
        changed |= DrawSpecEnumCombo("Weights", fields, "weight", config.Weight, defaults.Weight);
        const bool fit = config.Filter.Method == S::PropertyFilter::VariationalFit;
        if (!fit) changed |= DrawSpecInputUInt("Iterations", fields, "iterations", config.Filter.Iterations, defaults.Filter.Iterations);
        if (config.Weight != S::PropertyWeight::Cotangent && config.Weight != S::PropertyWeight::MeshUniform)
        {
            changed |= DrawSpecInputUInt("Neighbors", fields, "neighbors", config.Neighbors, defaults.Neighbors);
            if (config.Weight != S::PropertyWeight::Uniform)
                changed |= DrawSpecInputDouble("Spatial sigma", fields, "spatial_sigma", config.SpatialSigma, defaults.SpatialSigma);
        }
        if (config.Filter.Method == S::PropertyFilter::SpectralHeat)
            changed |= DrawSpecInputDouble("Heat time", fields, "heat_time", config.Filter.HeatTime, defaults.Filter.HeatTime);
        else if (config.Filter.Method == S::PropertyFilter::Implicit)
        {
            changed |= DrawSpecInputDouble("Time step", fields, "time_step", config.Filter.TimeStep, defaults.Filter.TimeStep);
            changed |= DrawSpecEnumCombo("Solver", fields, "solver", config.Filter.Solver, defaults.Filter.Solver);
            // CG settings also govern the fallback when the Cholesky factorization fails.
            changed |= DrawSpecInputDouble("Solver tolerance", fields, "solver_tolerance", config.Filter.SolverTolerance, defaults.Filter.SolverTolerance);
            changed |= DrawSpecInputUInt("Maximum solver iterations", fields, "max_solver_iterations", config.Filter.MaxSolverIterations,
                                         defaults.Filter.MaxSolverIterations);
        }
        else if (fit) changed |= DrawVariationalFitSettings(config, model.PropertyCatalog);
        else changed |= DrawSpecInputDouble("Lambda", fields, "lambda", config.Filter.Lambda, defaults.Filter.Lambda);
        DrawSpecCheckbox("Pin mesh boundary", fields, "preserve_boundary", config.PreserveBoundary, defaults.PreserveBoundary, changed);
        if (config.Filter.Method == S::PropertyFilter::Taubin)
            changed |= DrawSpecInputDouble("Mu", fields, "mu", config.Filter.Mu, defaults.Filter.Mu);
        if (config.Filter.Method == S::PropertyFilter::Bilateral)
            changed |= DrawSpecInputDouble("Range sigma (property units)", fields, "range_sigma", config.Filter.RangeSigma, defaults.Filter.RangeSigma);
        const auto apply = [&](const auto& c) { return Runtime::ApplyEditorPropertySmoothingConfig(context.MeshFields.Commands, c); };
        if (changed)
        {
            // The edited choice wins; options it invalidates fall back so the draft stays runnable.
            Runtime::ReconcilePropertySmoothingConfig(config, before);
            Smoothing.LastResult.reset();
            Smoothing.ConfigDiagnostic = apply(config).Succeeded() ? "" : "Smoothing configuration was rejected.";
        }
        auto readiness = Runtime::PreviewEditorPropertySmoothingCommand(context.MeshFields.Commands, model.SelectedStableId, config);
        // A pending GPU result blocks the next run until it is accepted or discarded.
        using Phase = Runtime::EditorGpuTransactionPhase;
        const auto transaction = Runtime::SnapshotEditorPropertySmoothing(context.MeshFields.Commands, SmoothingTransaction);
        const bool hadTransaction = static_cast<bool>(SmoothingTransaction); // before a terminal phase retires it below
        const bool transactionPending = SmoothingTransaction && (transaction.Phase == Phase::Running ||
            transaction.Phase == Phase::ReadyToAccept || transaction.Phase == Phase::Accepting);
        if (transactionPending && readiness.Enabled)
            readiness = {.Enabled = false, .DisabledReason = "Accept or discard the pending GPU result first."};
        if (DrawProcessingActionButton("Smooth property", readiness))
        {
            Smoothing.Run.ClearNote(); // an own submission replaces an earlier duplicate refusal
            if (config.Backend == Runtime::PropertySmoothingBackend::Vulkan)
            {
                // Interactive Vulkan runs preview on the device and publish on Accept.
                Smoothing.ConfigDiagnostic = apply(config).Succeeded() ? "" : "Smoothing configuration was rejected.";
                if (Smoothing.ConfigDiagnostic.empty())
                {
                    // A finished transaction is retired before the new one takes its place.
                    SmoothingTransaction.reset();
                    Runtime::EditorPropertySmoothingResult failure;
                    SmoothingTransaction = Runtime::StartEditorPropertySmoothing(context.MeshFields.Commands, model.SelectedStableId, config, failure);
                    // A Pending refusal without a handle: the output's active run keeps its callback.
                    if (!SmoothingTransaction && failure.Status == Runtime::EditorCommandStatus::Pending)
                        Smoothing.Run.WatchDuplicate(model.SelectedStableId, config.Output.Name, failure.Message);
                    else
                    {
                        Smoothing.LastResult = SmoothingTransaction
                            ? Runtime::SnapshotEditorPropertySmoothing(context.MeshFields.Commands, SmoothingTransaction).Result : failure;
                        Smoothing.Run.WatchOutputIfQueued(Smoothing.LastResult, model.SelectedStableId, config.Output.Name);
                    }
                }
            }
            else
                ApplyQueuedProcessingExecution(context.MeshFields.Commands, Smoothing, config, apply,
                    [&] { return Runtime::ApplyEditorPropertySmoothingCommand(context.MeshFields.Commands, model.SelectedStableId, config,
                              [completion = SmoothingCompletion](Runtime::EditorPropertySmoothingResult result) { *completion = std::move(result); }); },
                    std::function<void(Runtime::EditorPropertySmoothingResult)>{}, "Smoothing configuration was rejected.",
                    model.SelectedStableId, config.Output.Name);
        }
        if (!readiness.Enabled && !readiness.DisabledReason.empty()) ImGui::TextWrapped("%s", readiness.DisabledReason.c_str());
        if (SmoothingTransaction) DrawSmoothingTransaction(context);
        // The GPU transaction reports its own jobs; a CPU run is found by its output. Either way the
        // finished run stays until the next one.
        // A transaction not started here (or after a reload) names its own run.
        if (transactionPending && !Smoothing.Run.WatchesOutput(transaction.StableEntityId, transaction.OutputName))
            Smoothing.Run.WatchOutput(transaction.StableEntityId, transaction.OutputName);
        Smoothing.Run.AwaitingAccept(hadTransaction && transaction.Phase == Phase::ReadyToAccept);
        Smoothing.Run.DrawLive(hadTransaction ? transaction.Progress : Smoothing.Run.Query(context.MeshFields.Commands),
                               model.SelectedStableId, {}, "smoothing_progress");
        DrawProcessingPropertyShowButton(context, model.SelectedStableId, config.Output, Smoothing.VisualizationDiagnostic);
        ImGui::TextDisabled(fit ? "CPU reference; penalties use the Euclidean norm over vector channels, bounds apply per channel."
                                : "Vectors are filtered componentwise without normalization.");
        if (Smoothing.LastResult)
            ImGui::TextDisabled("Requested: %s; ran: %s",
                Smoothing.LastResult->RequestedBackend == Runtime::PropertySmoothingBackend::Vulkan ? "Vulkan" : "CPU",
                Smoothing.LastResult->Status == Runtime::EditorCommandStatus::Pending ? "pending"
                : Smoothing.LastResult->Succeeded() ? Smoothing.LastResult->BackendId.c_str() : "not run");
        if (Smoothing.LastResult) ImGui::TextWrapped("%s", Smoothing.LastResult->Message.c_str());
        if (!Smoothing.ConfigDiagnostic.empty()) ImGui::TextWrapped("%s", Smoothing.ConfigDiagnostic.c_str());
        DrawProcessingDisplayDiagnostic(Smoothing.VisualizationDiagnostic);
        ImGui::End();
    }

    // Stop / Accept / Discard of the interactive Vulkan run (ADR 0030 decisions 6-7). The
    // preview stays in the viewport until the user decides; "Applied" appears only after the
    // CPU publication succeeded.
    void MeshProcessingPanels::Impl::DrawSmoothingTransaction(const SandboxEditorContext& context)
    {
        using Phase = Runtime::EditorGpuTransactionPhase;
        const auto& commands = context.MeshFields.Commands;
        // A fresh snapshot: the handle may have been replaced by a start this frame.
        const auto transaction = Runtime::SnapshotEditorPropertySmoothing(commands, SmoothingTransaction);
        ImGui::SeparatorText("GPU result");
        // The progress widget (drawn by the panel) reports a running or accepting job.
        if (transaction.Phase != Phase::Running && transaction.Phase != Phase::Accepting)
            ImGui::TextDisabled("State: %s%s", Runtime::ToString(transaction.Phase),
                                transaction.Phase == Phase::ReadyToAccept && transaction.Stale ? " (stale)" : "");
        ImGui::BeginDisabled(transaction.Phase != Phase::Running);
        if (ImGui::Button("Stop##Smoothing")) Runtime::StopEditorPropertySmoothing(SmoothingTransaction);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!transaction.CanAccept);
        if (ImGui::Button("Accept##Smoothing") && transaction.CanAccept)
            Smoothing.LastResult = Runtime::AcceptEditorPropertySmoothing(commands, SmoothingTransaction,
                [completion = SmoothingCompletion](Runtime::EditorPropertySmoothingResult result) { *completion = std::move(result); });
        ImGui::EndDisabled();
        if (!transaction.CanAccept && !transaction.AcceptDisabledReason.empty())
            DrawDisabledReasonTooltip(transaction.AcceptDisabledReason);
        ImGui::SameLine();
        ImGui::BeginDisabled(transaction.Phase == Phase::Applied || transaction.Phase == Phase::Discarded || transaction.Phase == Phase::Failed);
        if (ImGui::Button("Discard##Smoothing"))
        {
            Runtime::DiscardEditorPropertySmoothing(commands, SmoothingTransaction);
            Smoothing.Run.Forget(); // a discarded result is not a finished run
            Smoothing.LastResult = Runtime::SnapshotEditorPropertySmoothing(commands, SmoothingTransaction).Result;
        }
        ImGui::EndDisabled();
        if (!transaction.AcceptDisabledReason.empty()) ImGui::TextWrapped("%s", transaction.AcceptDisabledReason.c_str());
        else if (transaction.Phase == Phase::ReadyToAccept)
            ImGui::TextWrapped("The viewport shows the device result; Accept publishes it (undoable), Discard keeps the CPU property.");
        if (transaction.Phase == Phase::Applied || transaction.Phase == Phase::Discarded || transaction.Phase == Phase::Failed)
        {
            // Terminal: the result line below reports it; the next run may start.
            Smoothing.LastResult = transaction.Result;
            SmoothingTransaction.reset();
        }
    }

    void MeshProcessingPanels::Impl::DrawEigenbasisWindow(bool& open, const SandboxEditorContext& context)
    {
        namespace S = Geometry::Smoothing;
        using D = Runtime::GeometryElementDomain;
        if (!ImGui::Begin("Spectral Modes", &open)) { ImGui::End(); return; }
        const auto previous = EigenbasisEntity;
        DrawProcessingEntity("Entity##Eigenbasis", context, EigenbasisEntity, Eigenbasis.LastSelectedEntity);
        if (previous != EigenbasisEntity) Eigenbasis.LastResult.reset();
        if (const auto active = Runtime::GetEditorLaplacianEigenbasisConfig(context.MeshFields.Commands))
            Eigenbasis.Synchronize(*active, Runtime::SerializeLaplacianEigenbasisConfig(*active));
        const auto& model = GetDomainWindowModel(context, Runtime::EditorDomainWindowKind::Mesh, EigenbasisEntity);
        if (!model.HasSelectedEntity || Eigenbasis.LastApplied.empty())
        { ImGui::TextDisabled("Select a geometry entity to compute spectral modes."); ImGui::End(); return; }
        using Op = Runtime::ModalOperator;
        auto& config = Eigenbasis.Draft;
        const auto fields = Runtime::LaplacianEigenbasisConfigFieldSpecs();
        const Runtime::LaplacianEigenbasisConfig defaults{};
        bool changed = false;
        if (DrawSpecEnumCombo("Operator##Eigenbasis", fields, "operator", config.Operator, defaults.Operator))
        {
            if (config.Operator != Op::GraphLaplacian)
            {
                config.Domain = config.Positions.Domain = D::MeshVertex;
                config.LumpedMass = true;
            }
            config.SkipModes = config.Operator == Op::ThinShell ? 6u : 0u;
            if (config.Count <= config.SkipModes) config.Count = config.SkipModes + 10;
            changed = true;
        }
        if (config.Operator == Op::GraphLaplacian)
        {
            changed |= DrawSpecEnumCombo("Domain##Eigenbasis", fields, "domain", config.Domain, defaults.Domain);
        }
        changed |= DrawProcessingPropertyInput("Sample positions##Eigenbasis", model.PropertyCatalog, config.Positions,
            +[](const Runtime::GeometryPropertyRef& ref) { return ref.ValueKind == Geometry::PropertyValueKind::Vec3; });
        DrawSpecFieldHint(fields, "positions");
        if (config.Operator == Op::GraphLaplacian)
        {
            changed |= DrawSpecEnumCombo("Weights##Eigenbasis", fields, "weight", config.Weight, defaults.Weight);
            if (config.Weight != S::PropertyWeight::Cotangent && config.Weight != S::PropertyWeight::MeshUniform)
            {
                changed |= DrawSpecInputUInt("Neighbors##Eigenbasis", fields, "neighbors", config.Neighbors, defaults.Neighbors);
                if (config.Weight != S::PropertyWeight::Uniform)
                    changed |= DrawSpecInputDouble("Spatial sigma##Eigenbasis", fields, "spatial_sigma", config.SpatialSigma,
                                                   defaults.SpatialSigma);
            }
        }
        else if (config.Operator == Op::ThinShell)
        {
            changed |= DrawSpecInputDouble("Flexural weight##Eigenbasis", fields, "shell_flexural", config.ShellFlexural, defaults.ShellFlexural);
            changed |= DrawSpecInputDouble("Edge length weight##Eigenbasis", fields, "shell_length", config.ShellLength, defaults.ShellLength);
            changed |= DrawSpecInputDouble("Triangle area weight##Eigenbasis", fields, "shell_area", config.ShellArea, defaults.ShellArea);
        }
        DrawSpecCheckbox("Lumped vertex area mass (mesh vertices)", fields, "lumped_mass", config.LumpedMass, defaults.LumpedMass, changed);
        changed |= DrawSpecInputUInt("Eigenpairs", fields, "count", config.Count, defaults.Count);
        changed |= DrawProcessingPropertyName("Output prefix##Eigenbasis", config.OutputPrefix);
        DrawSpecFieldHint(fields, "output_prefix", defaults.OutputPrefix);
        changed |= DrawSpecInputUInt("Maximum iterations##Eigenbasis", fields, "max_iterations", config.MaxIterations, defaults.MaxIterations);
        changed |= DrawSpecInputDouble("Tolerance (backward error)##Eigenbasis", fields, "tolerance", config.Tolerance,
                                       defaults.Tolerance, "%.2e");
        if (ImGui::TreeNode("Modal signature and distance##Eigenbasis"))
        {
            changed |= DrawSpecInputUInt("Skipped leading modes", fields, "skip_modes", config.SkipModes, defaults.SkipModes);
            bool signature = !config.SignatureOutput.empty();
            if (ImGui::Checkbox("Publish signature S_t", &signature))
            { config.SignatureOutput = signature ? "modal_signature" : ""; changed = true; }
            if (signature)
            {
                changed |= DrawProcessingPropertyName("Signature output##Eigenbasis", config.SignatureOutput);
                const auto* scaleField = Runtime::FindConfigFieldSpec(fields, "signature_scale");
                float scale = float(config.SignatureScale);
                if (ImGui::SliderFloat("Scale (t_min .. t_max)##Eigenbasis", &scale,
                                       float(scaleField ? scaleField->Min.value_or(0.0) : 0.0),
                                       float(scaleField ? scaleField->Max.value_or(1.0) : 1.0)))
                { config.SignatureScale = scale; changed = true; }
                DrawConfigFieldHint(scaleField, std::format("{}", defaults.SignatureScale));
            }
            bool distance = config.DistanceSource >= 0;
            if (ImGui::Checkbox("Publish multi-scale distance", &distance))
            { config.DistanceSource = distance ? 0 : -1; changed = true; }
            if (distance)
            {
                changed |= ImGui::InputScalar("Source row##Eigenbasis", ImGuiDataType_S64, &config.DistanceSource);
                DrawSpecFieldHint(fields, "distance_source");
                changed |= DrawProcessingPropertyName("Distance output##Eigenbasis", config.DistanceOutput);
                DrawSpecFieldHint(fields, "distance_output", defaults.DistanceOutput);
                changed |= DrawSpecInputUInt("Log-scale samples##Eigenbasis", fields, "distance_samples", config.DistanceSamples,
                                             defaults.DistanceSamples);
            }
            ImGui::TreePop();
        }
        const auto apply = [&](const auto& c) { return Runtime::ApplyEditorLaplacianEigenbasisConfig(context.MeshFields.Commands, c); };
        if (changed)
        {
            Eigenbasis.LastResult.reset();
            Eigenbasis.ConfigDiagnostic = apply(config).Succeeded() ? "" : "Eigenbasis configuration was rejected.";
        }
        const auto readiness = Runtime::PreviewEditorLaplacianEigenbasisCommand(context.MeshFields.Commands, model.SelectedStableId, config);
        if (DrawProcessingActionButton("Compute eigenbasis", readiness))
            ApplyProcessingExecution(Eigenbasis, config, apply,
                [&] { return Runtime::ApplyEditorLaplacianEigenbasisCommand(context.MeshFields.Commands, model.SelectedStableId, config); },
                std::function<void(Runtime::EditorLaplacianEigenbasisResult)>{}, "Eigenbasis configuration was rejected.");
        ImGui::TextDisabled("CPU reference: shift-invert subspace iteration on the selected operator and mass.");
        if (Eigenbasis.LastResult && !Eigenbasis.LastResult->Eigenvalues.empty() && Eigenbasis.LastResult->Succeeded())
        {
            const auto& last = *Eigenbasis.LastResult;
            (void)Runtime::DrawEditorSpectrumBarWidget("EigenbasisSpectrum", last.Eigenvalues, SelectedEigenvector);
            const auto selectedIndex = std::size_t(std::max(SelectedEigenvector, 0));
            if (selectedIndex < last.Outputs.size())
            {
                const Runtime::GeometryPropertyRef selected{config.Domain, last.Outputs[selectedIndex],
                    config.Operator == Op::ThinShell ? Geometry::PropertyValueKind::Vec3 : Geometry::PropertyValueKind::Float};
                DrawProcessingPropertyShowButton(context, model.SelectedStableId, selected, Eigenbasis.VisualizationDiagnostic);
            }
            if (last.ScaleMax > 0.0)
                ImGui::Text("Scales t in [%.3g, %.3g]%s", last.ScaleMin, last.ScaleMax,
                            last.SignatureTime > 0.0 ? ("; signature at t = " + std::to_string(last.SignatureTime)).c_str() : "");
            for (const auto& name : {config.SignatureOutput, config.DistanceOutput})
                if (!name.empty() && std::ranges::find(last.Outputs, name) != last.Outputs.end())
                    DrawProcessingPropertyShowButton(context, model.SelectedStableId,
                        {config.Domain, name, Geometry::PropertyValueKind::Float}, Eigenbasis.VisualizationDiagnostic);
        }
        if (Eigenbasis.LastResult) ImGui::TextWrapped("%s", Eigenbasis.LastResult->Message.c_str());
        if (!Eigenbasis.ConfigDiagnostic.empty()) ImGui::TextWrapped("%s", Eigenbasis.ConfigDiagnostic.c_str());
        DrawProcessingDisplayDiagnostic(Eigenbasis.VisualizationDiagnostic);
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawHarmonicFieldWindow(bool& open, const SandboxEditorContext& context)
    {
        using K = Geometry::PropertyValueKind;
        namespace H = Geometry::HarmonicField;
        if (!ImGui::Begin("Harmonic Field", &open)) { ImGui::End(); return; }
        const auto previous = HarmonicEntity;
        DrawProcessingEntity("Entity##HarmonicField", context, HarmonicEntity, Harmonic.LastSelectedEntity);
        if (previous != HarmonicEntity) Harmonic.LastResult.reset();
        if (const auto active = Runtime::GetEditorHarmonicFieldConfig(context.MeshFields.Commands))
            Harmonic.Synchronize(*active, Runtime::SerializeHarmonicFieldConfig(*active));
        const auto& model = GetDomainWindowModel(context, Runtime::EditorDomainWindowKind::Mesh, HarmonicEntity);
        if (!model.HasSelectedEntity || Harmonic.LastApplied.empty())
        { ImGui::TextDisabled("Select a geometry entity to solve a harmonic field."); ImGui::End(); return; }
        auto& config = Harmonic.Draft;
        const auto fields = Runtime::HarmonicFieldConfigFieldSpecs();
        const Runtime::HarmonicFieldConfig defaults{};
        bool changed = false;
        if (DrawSpecEnumCombo("Mode", fields, "mode", config.Mode, defaults.Mode))
        {
            const auto kind = config.Mode == Runtime::HarmonicFieldMode::Labels ? K::Int32 : K::Double;
            config.Input.ValueKind = config.Output.ValueKind = kind;
            if (config.Mode == Runtime::HarmonicFieldMode::Labels)
            {
                config.HardMask.Name.clear(); config.SoftWeights.Name.clear(); config.Source.Name.clear(); config.PinBoundary = false;
                if (config.Field.Unconstrained == H::UnconstrainedPolicy::ZeroMean) config.Field.Unconstrained = H::UnconstrainedPolicy::Fail;
            }
            else { config.Confidence.Name.clear(); config.WeightsPrefix.clear(); }
            changed = true;
        }
        const bool labels = config.Mode == Runtime::HarmonicFieldMode::Labels;
        const auto floating = +[](const Runtime::GeometryPropertyRef& ref) {
            return ref.ValueKind == K::Float || ref.ValueKind == K::Double || ref.ValueKind == K::Vec2 ||
                   ref.ValueKind == K::Vec3 || ref.ValueKind == K::Vec4;
        };
        const auto int32 = +[](const Runtime::GeometryPropertyRef& ref) { return ref.ValueKind == K::Int32; };
        if (DrawProcessingPropertyInput(labels ? "Seed labels##Harmonic" : "Values##Harmonic", model.PropertyCatalog,
                                        config.Input, labels ? int32 : floating))
        {
            config.Output.Domain = config.HardMask.Domain = config.SoftWeights.Domain = config.Confidence.Domain =
                config.Source.Domain = config.Input.Domain;
            config.Output.ValueKind = config.Input.ValueKind;
            changed = true;
        }
        DrawSpecFieldHint(fields, "input");
        changed |= DrawProcessingPropertyInput("Neighborhood positions##Harmonic", model.PropertyCatalog, config.Positions,
            +[](const Runtime::GeometryPropertyRef& ref) { return ref.ValueKind == K::Vec3; });
        DrawSpecFieldHint(fields, "positions");
        changed |= DrawProcessingPropertyName(labels ? "Output labels##Harmonic" : "Output property##Harmonic", config.Output.Name);
        DrawSpecFieldHint(fields, "output");
        if (!labels && (config.Input.ValueKind == K::Float || config.Input.ValueKind == K::Double))
        {
            int kind = config.Output.ValueKind == K::Double ? 1 : 0;
            if (ImGui::Combo("Output storage", &kind, "float\0double\0"))
            { config.Output.ValueKind = kind ? K::Double : K::Float; changed = true; }
        }
        // An optional binding is enabled by a name; disabling clears it.
        const auto optionalProperty = [&](const char* toggle, const char* label, const std::string_view field,
                                          Runtime::GeometryPropertyRef& ref,
                                          bool (*accepts)(const Runtime::GeometryPropertyRef&), const char* defaultName) {
            bool enabled = !ref.Name.empty();
            if (ImGui::Checkbox(toggle, &enabled)) { ref.Name = enabled ? defaultName : ""; changed = true; }
            DrawSpecFieldHint(fields, field);
            if (enabled && DrawProcessingPropertyInput(label, model.PropertyCatalog, ref, accepts)) changed = true;
            ref.Domain = config.Input.Domain;
        };
        if (labels)
        {
            changed |= ImGui::InputInt("Unlabeled value", &config.Unlabeled);
            DrawSpecFieldHint(fields, "unlabeled", std::to_string(defaults.Unlabeled));
            optionalProperty("Write confidence##Harmonic", "Confidence property##Harmonic", "confidence", config.Confidence,
                +[](const Runtime::GeometryPropertyRef& ref) { return ref.ValueKind == K::Float || ref.ValueKind == K::Double; },
                "harmonic_confidence");
            bool weights = !config.WeightsPrefix.empty();
            if (ImGui::Checkbox("Write per-label weights##Harmonic", &weights))
            { config.WeightsPrefix = weights ? "harmonic_weight_" : ""; changed = true; }
            DrawSpecFieldHint(fields, "weights_prefix");
            if (weights)
            {
                changed |= DrawProcessingPropertyName("Weight prefix##Harmonic", config.WeightsPrefix);
                ImGui::TextDisabled("One float field per seed label: prefix + label.");
            }
        }
        else
        {
            ImGui::TextWrapped("Constrained rows keep (hard) or pull toward (soft) their input values; other input values are ignored.");
            optionalProperty("Hard constraint mask##Harmonic", "Hard mask (bool)##Harmonic", "hard_mask", config.HardMask,
                +[](const Runtime::GeometryPropertyRef& ref) { return ref.ValueKind == K::Bool; }, "harmonic_hard");
            optionalProperty("Soft constraint weights##Harmonic", "Soft weights##Harmonic", "soft_weights", config.SoftWeights,
                +[](const Runtime::GeometryPropertyRef& ref) { return ref.ValueKind == K::Float || ref.ValueKind == K::Double; },
                "harmonic_weight");
            DrawSpecCheckbox("Pin mesh boundary##Harmonic", fields, "pin_boundary", config.PinBoundary, defaults.PinBoundary, changed);
            optionalProperty("Source term (Poisson)##Harmonic", "Source density##Harmonic", "source", config.Source, floating,
                             "harmonic_source");
        }
        changed |= DrawSpecEnumCombo("Energy", fields, "order", config.Field.Order, defaults.Field.Order);
        const bool massMatters = config.Field.Order != H::FieldOrder::Harmonic || !config.Source.Name.empty();
        if (!massMatters && config.LumpedMass) { config.LumpedMass = false; changed = true; }
        if (massMatters)
            DrawSpecCheckbox("Lumped mesh area mass##Harmonic", fields, "lumped_mass", config.LumpedMass, defaults.LumpedMass, changed);
        changed |= DrawSpecEnumCombo("Weights##Harmonic", fields, "weight", config.Weight, defaults.Weight);
        if (config.Weight != Geometry::Smoothing::PropertyWeight::Cotangent && config.Weight != Geometry::Smoothing::PropertyWeight::MeshUniform)
        {
            changed |= DrawSpecInputUInt("Neighbors##Harmonic", fields, "neighbors", config.Neighbors, defaults.Neighbors);
            if (config.Weight != Geometry::Smoothing::PropertyWeight::Uniform)
                changed |= DrawSpecInputDouble("Spatial sigma##Harmonic", fields, "spatial_sigma", config.SpatialSigma, defaults.SpatialSigma);
        }
        // Label propagation has no pure-Neumann problem: only Fail and Keep input are offered.
        changed |= DrawSpecEnumCombo("Unconstrained components", fields, "unconstrained", config.Field.Unconstrained,
                                     defaults.Field.Unconstrained, labels ? 2 : -1);
        const auto apply = [&](const auto& c) { return Runtime::ApplyEditorHarmonicFieldConfig(context.MeshFields.Commands, c); };
        if (changed)
        {
            Harmonic.LastResult.reset();
            Harmonic.ConfigDiagnostic = apply(config).Succeeded() ? "" : "Harmonic field configuration was rejected.";
        }
        const auto readiness = Runtime::PreviewEditorHarmonicFieldCommand(context.MeshFields.Commands, model.SelectedStableId, config);
        if (DrawProcessingActionButton("Solve harmonic field", readiness))
            ApplyProcessingExecution(Harmonic, config, apply,
                [&] { return Runtime::ApplyEditorHarmonicFieldCommand(context.MeshFields.Commands, model.SelectedStableId, config); },
                std::function<void(Runtime::EditorHarmonicFieldResult)>{}, "Harmonic field configuration was rejected.");
        DrawProcessingPropertyShowButton(context, model.SelectedStableId, config.Output, Harmonic.VisualizationDiagnostic);
        ImGui::TextDisabled("CPU reference; one sparse Cholesky factorization serves every channel.");
        if (Harmonic.LastResult) ImGui::TextWrapped("%s", Harmonic.LastResult->Message.c_str());
        if (!Harmonic.ConfigDiagnostic.empty()) ImGui::TextWrapped("%s", Harmonic.ConfigDiagnostic.c_str());
        DrawProcessingDisplayDiagnostic(Harmonic.VisualizationDiagnostic);
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawGradientWindow(bool& open, const SandboxEditorContext& context)
    {
        if (!ImGui::Begin("Mesh / Processing / Faces / Scalar Field Gradient", &open))
        {
            ImGui::End();
            return;
        }
        const auto previous = GradientEntity;
        DrawProcessingEntity("Entity##ScalarGradient", context, GradientEntity,
                             Gradient.LastSelectedEntity, Runtime::EditorDomainWindowKind::Mesh);
        if (previous != GradientEntity)
        {
            Gradient.LastResult.reset();
            Gradient.VisualizationDiagnostic.clear();
        }
        if (const auto active = Runtime::GetEditorScalarGradientConfig(context.MeshFields.Commands))
            Gradient.Synchronize(*active, Runtime::SerializeScalarGradientConfig(*active));
        const auto& model = GetDomainWindowModel(context, Runtime::EditorDomainWindowKind::Mesh, GradientEntity);
        if (!model.DomainMatches || !model.Processing.HasSelectedEntity || Gradient.LastApplied.empty())
        {
            ImGui::TextDisabled("Choose a mesh entity with a scalar vertex property.");
            ImGui::End();
            return;
        }
        DrawProcessingCpuBackend();
        ImGui::TextWrapped("Compute a tangent gradient on each triangle from a vertex scalar field. "
                           "Degenerate triangles produce zero vectors. Polygon meshes require triangulation.");
        auto& config = Gradient.Draft;
        const auto vertex = +[](const Runtime::GeometryPropertyRef& ref) {
            return ref.Domain == Runtime::GeometryElementDomain::MeshVertex;
        };
        const auto fields = Runtime::ScalarGradientConfigFieldSpecs();
        bool changed = DrawProcessingPropertyInput("Scalar property##ScalarGradient", model.PropertyCatalog,
                                                   config.Scalar, vertex, 1u);
        DrawSpecFieldHint(fields, "scalar");
        changed |= DrawProcessingPropertyInput("Positions##ScalarGradient", model.PropertyCatalog,
                                               config.Positions, vertex, 3u);
        DrawSpecFieldHint(fields, "positions");
        changed |= DrawProcessingPropertyName("Output face property##ScalarGradient", config.Output.Name);
        DrawSpecFieldHint(fields, "output");
        const auto apply = [&](const auto& request) {
            return Runtime::ApplyEditorScalarGradientConfig(context.MeshFields.Commands, request);
        };
        if (changed)
        {
            Gradient.LastResult.reset();
            Gradient.ConfigDiagnostic = apply(config).Succeeded() ? "" : "Scalar gradient configuration was rejected.";
        }
        const auto readiness = Runtime::PreviewEditorScalarGradientCommand(context.MeshFields.Commands,
                                                                           model.SelectedStableId, config);
        if (DrawProcessingActionButton("Compute Gradient", readiness))
            ApplyProcessingExecution(Gradient, config, apply,
                [&] { return Runtime::ApplyEditorScalarGradientCommand(context.MeshFields.Commands,
                                                                      model.SelectedStableId, config); },
                std::function<void(Runtime::EditorScalarGradientResult)>{}, "Scalar gradient configuration was rejected.");
        const bool hasOutput = std::ranges::any_of(model.PropertyCatalog.Rows, [&](const auto& row) {
            return row.Domain == Runtime::EditorPropertyCatalogDomain::MeshFaces &&
                   row.Name == config.Output.Name && row.ValueKind == decltype(row.ValueKind)::Vec3 && row.Bindable;
        });
        ImGui::BeginDisabled(!hasOutput);
        if (ImGui::Button("Show Gradient Vectorfield"))
        {
            Runtime::EditorGeometryVectorFieldCommand command{
                .StableEntityId = model.SelectedStableId, .Layer = {.Vector = config.Output}};
            for (const auto& row : model.VectorFields.Layers)
                if (row.Layer.Vector == config.Output)
                {
                    command.Operation = Runtime::EditorVectorFieldOperation::Update;
                    command.Layer = row.Layer;
                    command.Layer.Enabled = true;
                    break;
                }
            const auto status = Runtime::ApplyEditorGeometryVectorFieldCommand(context.VisualizationCommands, command);
            Gradient.VisualizationDiagnostic = Runtime::DebugNameForEditorCommandStatus(status);
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Arrow scale, color and visibility: Appearance > Vector fields.");
        if (Gradient.LastResult) ImGui::TextWrapped("%s", Gradient.LastResult->Message.c_str());
        if (!Gradient.ConfigDiagnostic.empty()) ImGui::TextWrapped("%s", Gradient.ConfigDiagnostic.c_str());
        DrawProcessingDisplayDiagnostic(Gradient.VisualizationDiagnostic);
        ImGui::End();
    }

    void MeshProcessingPanels::Impl::DrawGeodesicsWindow(bool& open,
                                                         const SandboxEditorContext& context)
    {
        ImGui::SetNextWindowSize(ImVec2(460, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Mesh / Geodesics / Virtual Source Propagation", &open))
        {
            ImGui::End();
            return;
        }
        const bool initialized = !Geodesics.LastApplied.empty();
        const auto previousEntity = GeodesicsEntity;
        DrawProcessingEntity("Entity##Geodesics", context, GeodesicsEntity,
                             Geodesics.LastSelectedEntity, Runtime::EditorDomainWindowKind::Mesh);
        DrawProcessingCpuBackend();
        const auto& model = GetDomainWindowModel(context, Runtime::EditorDomainWindowKind::Mesh,
                                                GeodesicsEntity);
        if (initialized || (model.DomainMatches && model.Processing.HasSelectedEntity))
            if (const auto active = Runtime::GetEditorGeodesicsConfig(context.MeshFields.Commands))
                Geodesics.Synchronize(*active, Runtime::SerializeGeodesicsConfig(*active));
        if (initialized && previousEntity != GeodesicsEntity)
        {
            // Source indices belong to the previous mesh, even if the next
            // mesh has slots with the same indices. Publish deselection too.
            Geodesics.Draft.SourceVertices.clear();
            Geodesics.LastResult.reset();
            Geodesics.VisualizationDiagnostic.clear();
            GeodesicsSourceVertex = 0;
            Geodesics.ConfigDiagnostic = Runtime::ApplyEditorGeodesicsConfig(
                context.MeshFields.Commands, Geodesics.Draft).Succeeded()
                    ? "" : "Geodesics source reset was rejected.";
        }
        if (model.DomainMatches && model.Processing.HasSelectedEntity)
            DrawGeodesicsControls(model, context);
        else
            ImGui::TextDisabled("Choose a mesh entity to compute geodesic distances.");
        ImGui::End();
    }
    void MeshProcessingPanels::Impl::DrawGeodesicsControls(
        const Runtime::EditorDomainWindowModel& model, const SandboxEditorContext& context)
    {
        if (Geodesics.LastApplied.empty())
        {
            ImGui::TextDisabled("Geodesics configuration is unavailable.");
            return;
        }
        auto& config = Geodesics.Draft;
        const auto fields = Runtime::GeodesicsConfigFieldSpecs();
        const Runtime::GeodesicsConfig defaults{};
        bool changed = false;
        ImGui::TextWrapped(
            "Approximate surface distance from source vertices. Pick a vertex and add "
            "it, or enter its index below.");
        if (model.Primitive.HasVertexId && ImGui::Button("Add picked vertex"))
        {
            config.SourceVertices.push_back(model.Primitive.Primitive.VertexId);
            changed = true;
        }
        const auto selected = Runtime::ReadEditorPrimitiveSelection(
            context.Processing, model.SelectedStableId, Runtime::GeometryElementDomain::MeshVertex);
        ImGui::BeginDisabled(!selected.Usable() || selected.Indices.empty());
        if (ImGui::Button("Use selected vertices as sources"))
        {
            config.SourceVertices = selected.Indices;
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Select vertices in Mesh / Selection, then copy them here.");
        ImGui::InputInt("Source vertex", &GeodesicsSourceVertex);
        if (ImGui::Button("Add source") && GeodesicsSourceVertex >= 0)
        {
            config.SourceVertices.push_back(
                static_cast<std::uint32_t>(GeodesicsSourceVertex));
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear sources"))
        {
            config.SourceVertices.clear();
            changed = true;
        }
        if (changed)
        {
            auto& vertices = config.SourceVertices;
            std::sort(vertices.begin(), vertices.end());
            vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());
        }
        std::string sourceText = "Sources:";
        for (auto vertex : config.SourceVertices)
            sourceText += " " + std::to_string(vertex);
        ImGui::TextWrapped("%s", sourceText.c_str());
        const auto acceptsSourceProperty = [](const Runtime::GeometryPropertyRef& ref) {
            return ref.Domain == Runtime::GeometryElementDomain::MeshVertex;
        };
        auto sourceProperty = config.SourceVertexProperty;
        if (DrawProcessingPropertyInput("Source property##Geodesics", model.PropertyCatalog,
                                        sourceProperty, +acceptsSourceProperty, 1u))
        {
            config.SourceVertexProperty = sourceProperty;
            changed = true;
        }
        DrawSpecFieldHint(fields, "source_vertex_property");
        ImGui::SameLine();
        ImGui::BeginDisabled(config.SourceVertexProperty.Name.empty());
        if (ImGui::Button("Clear##GeodesicsSourceProperty"))
        {
            config.SourceVertexProperty.Name.clear();
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Nonzero values (e.g. v:feature from Scalar Ridges) add sources.");
        if (ImGui::BeginCombo("Position property", config.PositionProperty.Name.c_str()))
        {
            for (const auto& row : model.PropertyCatalog.Rows)
            {
                if (row.Domain != Runtime::EditorPropertyCatalogDomain::MeshVertices ||
                    row.ValueKind != decltype(row.ValueKind)::Vec3 || !row.Bindable)
                    continue;
                if (ImGui::Selectable(row.Name.c_str(),
                                      row.Name == config.PositionProperty.Name))
                {
                    config.PositionProperty.Name = row.Name;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        DrawSpecFieldHint(fields, "position_property");
        ImGui::SeparatorText("Output properties");
        changed |= DrawProcessingScalarOutput("Distance property", config.DistanceProperty);
        DrawSpecFieldHint(fields, "distance_property");
        changed |= DrawProcessingScalarOutput("Source mask property", config.SourceMaskProperty);
        DrawSpecFieldHint(fields, "source_mask_property");
        changed |= DrawSpecInputUInt("Expansion budget", fields, "max_halfedge_expansions",
                                     config.MaxHalfedgeExpansions, defaults.MaxHalfedgeExpansions);
        const auto apply = [&](const auto& request) {
            return Runtime::ApplyEditorGeodesicsConfig(context.MeshFields.Commands, request);
        };
        if (changed)
            Geodesics.ConfigDiagnostic = apply(config).Succeeded()
                ? "" : "Geodesics config was rejected; check property names and expansion budget.";
        const auto readiness = Runtime::ResolveEditorProcessingActionReadiness(
            context.MeshFields.Commands,
            Runtime::PreviewEditorGeodesicsCommand(context.MeshFields.Commands, config));
        if (DrawProcessingActionButton("Compute geodesics", readiness))
            ApplyProcessingExecution(Geodesics, config, apply,
                [&] { return Runtime::ApplyEditorConfiguredGeodesicsCommand(
                    context.MeshFields.Commands, model.SelectedStableId); },
                std::function<void(Runtime::EditorGeodesicsResult)>{}, "Geodesics configuration was rejected.");
        ImGui::SeparatorText("Display output properties");
        ImGui::TextDisabled("Unreachable distances are shown in gray.");
        DrawProcessingPropertyShowButton(context, model.SelectedStableId,
            config.DistanceProperty, Geodesics.VisualizationDiagnostic);
        DrawProcessingPropertyShowButton(context, model.SelectedStableId,
            config.SourceMaskProperty, Geodesics.VisualizationDiagnostic);
        if (!Geodesics.ConfigDiagnostic.empty()) ImGui::TextWrapped("%s", Geodesics.ConfigDiagnostic.c_str());
        DrawProcessingDisplayDiagnostic(Geodesics.VisualizationDiagnostic);
        if (Geodesics.LastResult)
        {
            ImGui::TextWrapped("%s", Geodesics.LastResult->Message.c_str());
            const auto& diagnostics = Geodesics.LastResult->Diagnostics;
            ImGui::Text("Sources: %zu | Expansions: %zu | Triangle updates: %zu",
                        diagnostics.SourceCount, diagnostics.HalfedgeExpansions,
                        diagnostics.TriangleUpdates);
            ImGui::Text("Unreachable vertices: %zu", diagnostics.UnreachableVertexCount);
        }
    }
}

namespace Extrinsic::Sandbox::Editor
{
    namespace
    {
        [[nodiscard]] bool AcceptsScalarRidgeInput(const Runtime::GeometryPropertyRef& ref)
        {
            return ref.Domain == Runtime::GeometryElementDomain::MeshVertex;
        }
    }
    void MeshProcessingPanels::Impl::DrawScalarRidgesWindow(bool& open,
                                                            const SandboxEditorContext& context)
    {
        ImGui::SetNextWindowSize(ImVec2(440, 420), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Mesh / Processing / Scalar Ridges", &open))
        {
            ImGui::End();
            return;
        }
        auto& command = ScalarRidges;
        const auto previousEntity = command.StableEntityId;
        DrawProcessingEntity("Entity##ScalarRidges", context, command.StableEntityId,
                             ScalarRidgesSelection, Runtime::EditorDomainWindowKind::Mesh);
        DrawProcessingCpuBackend();
        if (previousEntity != command.StableEntityId)
            ScalarRidgesResult.reset();
        const auto& model = GetDomainWindowModel(context, Runtime::EditorDomainWindowKind::Mesh,
                                                command.StableEntityId);
        if (!model.DomainMatches || !model.Processing.HasSelectedEntity)
        {
            ImGui::TextDisabled("Choose a mesh entity to extract ridges of a vertex property.");
            ImGui::End();
            return;
        }
        ImGui::TextWrapped(
            "Curves where a vertex scalar field is maximal (ridges) or minimal (valleys). "
            "Run Curvature first to use v:mean_curvature.");
        DrawProcessingPropertyInput("Scalar property##ScalarRidges", model.PropertyCatalog,
                                    command.Property, &AcceptsScalarRidgeInput, 1u);
        int method = static_cast<int>(command.Method);
        if (ImGui::Combo("Method##ScalarRidges", &method,
                         "Hessian ridges (height across dominant direction)\0"
                         "Watershed (gradient-flow basin boundaries)\0"))
            command.Method = static_cast<Runtime::EditorScalarExtremaMethod>(method);
        const bool watershed = command.Method == Runtime::EditorScalarExtremaMethod::Watershed;
        if (watershed)
        {
            float persistence = static_cast<float>(command.MinimumPersistence * 100.0);
            if (ImGui::SliderFloat("Min persistence (% of range)", &persistence, 0.0f, static_cast<float>(Runtime::kScalarRidgeMaxMinimumPersistence * 100.0), "%.2f"))
                command.MinimumPersistence = persistence / 100.0;
            ImGui::TextDisabled("Shallower basins merge into their older neighbor.");
        }
        else
        {
            float radiusPercent = static_cast<float>(command.RadiusRatio * 100.0);
            if (ImGui::SliderFloat("Fit radius (% of diagonal)", &radiusPercent, static_cast<float>(Runtime::kScalarRidgeMinRadiusRatio * 100.0), static_cast<float>(Runtime::kScalarRidgeMaxRadiusRatio * 100.0), "%.2f"))
                command.RadiusRatio = radiusPercent / 100.0;
            int scale = command.Scale;
            if (ImGui::Combo("Scale", &scale, "0.5x radius\0" "1x radius\0" "2x radius\0"))
                command.Scale = static_cast<std::uint8_t>(scale);
            ImGui::InputDouble("Min sharpness", &command.MinimumSharpness, 0.001, 0.01, "%.4f");
        }
        ImGui::InputDouble("Min strength", &command.MinimumStrength, 0.01, 0.1, "%.3f");
        ImGui::Checkbox("Ridges", &command.Ridges);
        ImGui::SameLine();
        ImGui::Checkbox("Valleys", &command.Valleys);
        if (!watershed)
            ImGui::Checkbox("Only curves found at another scale too", &command.RequirePersistence);
        ImGui::SeparatorText("Outputs");
        ImGui::Checkbox("Curve graph entity", &command.PublishGraph);
        ImGui::Checkbox("Mesh feature properties", &command.PublishMeshFeatures);
        if (command.PublishMeshFeatures)
        {
            DrawProcessingScalarOutput("Vertex features##ScalarRidges", command.VertexFeatures);
            DrawProcessingScalarOutput("Edge features##ScalarRidges", command.EdgeFeatures);
            if (watershed)
                DrawProcessingScalarOutput("Basin labels##ScalarRidges", command.BasinLabels);
            ImGui::TextDisabled("Curves snap to the nearest mesh vertices and edges; "
                                "vertex features can seed Geodesics.");
        }
        auto request = command;
        request.StableEntityId = model.SelectedStableId;
        const Runtime::ActionReadiness readiness = Runtime::PreviewEditorScalarRidgeCommand(context.Processing, request);
        if (DrawProcessingActionButton("Extract ridges", readiness))
        {
            ScalarRidgesResult = Runtime::ApplyEditorScalarRidgeCommand(context.Processing, request);
        }
        if (command.PublishGraph)
            ImGui::TextDisabled("The graph is a new entity; color its edges by "
                                "e:scalar_extremum (+1 ridge, -1 valley).");
        if (ScalarRidgesResult)
        {
            ImGui::TextWrapped("%s", ScalarRidgesResult->Message.c_str());
            if (ScalarRidgesResult->Succeeded() || ScalarRidgesResult->ComputeMilliseconds > 0.0)
                ImGui::Text("Ridge segments: %zu | Valley segments: %zu | %.1f ms",
                            ScalarRidgesResult->RidgeSegmentCount,
                            ScalarRidgesResult->ValleySegmentCount,
                            ScalarRidgesResult->ComputeMilliseconds);
            if (ScalarRidgesResult->FeatureVertexCount || ScalarRidgesResult->BasinCount)
                ImGui::Text("Feature vertices: %zu | Feature edges: %zu | Basins: %zu",
                            ScalarRidgesResult->FeatureVertexCount,
                            ScalarRidgesResult->FeatureEdgeCount,
                            ScalarRidgesResult->BasinCount);
            if (ScalarRidgesResult->Succeeded() && command.PublishMeshFeatures)
            {
                DrawProcessingPropertyShowButton(context, model.SelectedStableId,
                    command.VertexFeatures, ScalarRidgesVisualizationDiagnostic);
                if (watershed)
                    DrawProcessingPropertyShowButton(context, model.SelectedStableId,
                        command.BasinLabels, ScalarRidgesVisualizationDiagnostic);
                DrawProcessingDisplayDiagnostic(ScalarRidgesVisualizationDiagnostic);
            }
        }
        ImGui::End();
    }

    // RUNTIME-274: the standalone sampling window. Every Geometry.PointSampling method with its
    // own parameters; results go to rank/selection properties or a new point cloud.
    void MeshProcessingPanels::Impl::DrawPointSamplingWindow(bool& open, const SandboxEditorContext& context)
    {
        auto& state = PointSampling;
        const auto& commands = context.Registration.Commands;
        ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Point Sampling", &open))
        {
            ImGui::End();
            return;
        }
        ImGui::TextWrapped("Orders an entity's points so that every prefix is a well-spread subsample, with a choice "
                           "of method: exact farthest point (hole sieve), progressive Poisson disk, relaxed greedy "
                           "batches, sample elimination, random.");
        const auto active = Runtime::GetEditorPointSamplingConfig(commands).value_or(Runtime::PointSamplingOperationConfig{});
        const auto activeText = Runtime::SerializePointSamplingOperationConfig(active);
        if (activeText != state.LastApplied)
        {
            state.Draft = active;
            state.LastApplied = activeText;
            state.ConfigDiagnostic.clear();
            std::snprintf(state.WeightsName.data(), state.WeightsName.size(), "%s", active.WeightsName.c_str());
            std::snprintf(state.RankName.data(), state.RankName.size(), "%s", active.RankName.c_str());
            std::snprintf(state.SelectedName.data(), state.SelectedName.size(), "%s", active.SelectedName.c_str());
        }
        auto& config = state.Draft;
        const auto fields = Runtime::PointSamplingOperationFieldSpecs();
        const Runtime::PointSamplingOperationConfig defaults{};
        const auto hint = [&](std::string_view field) { DrawConfigFieldHint(Runtime::FindConfigFieldSpec(fields, field), {}); };
        bool changed = DrawProcessingEntity("Points##Sampling", context, config.SourceStableEntityId, state.LastSelected);
        hint("source");
        {
            const auto catalog = Runtime::GetEditorRegistrationInputCatalog(commands, config.SourceStableEntityId);
            const auto preview = std::string(Runtime::ToString(config.Positions.Domain)) + ": " + config.Positions.Name;
            if (ImGui::BeginCombo("Positions##Sampling", preview.c_str()))
            {
                for (const auto& row : catalog.Entries)
                {
                    const auto title = std::string(Runtime::ToString(row.Ref.Domain)) + ": " + row.Ref.Name + " (" +
                                       std::to_string(row.ElementCount) + ")";
                    if (ImGui::Selectable(title.c_str(), row.Ref == config.Positions)) { config.Positions = row.Ref; changed = true; }
                }
                ImGui::EndCombo();
            }
            hint("positions");
        }
        changed |= DrawSpecInputUInt("Samples##Sampling", fields, "count", config.Count, defaults.Count);
        ImGui::SeparatorText("Method");
        changed |= DrawPointSamplingControls("Sampling", fields, "", config.Sampling, defaults.Sampling);
        const bool weighted = config.Sampling.Method == Runtime::PointSamplingMethod::FarthestPoint ||
                              config.Sampling.Method == Runtime::PointSamplingMethod::CoupledSieve ||
                              (config.Sampling.Method == Runtime::PointSamplingMethod::ProgressivePoisson &&
                               config.Sampling.PoissonSelection == Runtime::PointSamplingPoissonSelection::FeaturePriority);
        if (weighted)
        {
            if (ImGui::InputText("Weights property##Sampling", state.WeightsName.data(), state.WeightsName.size()))
            {
                config.WeightsName = state.WeightsName.data();
                changed = true;
            }
            hint("weights");
        }
        ImGui::SeparatorText("Output");
        changed |= DrawSpecEnumCombo("Output##Sampling", fields, "output", config.Output, defaults.Output);
        changed |= DrawSpecEnumCombo("Backend##Sampling", fields, "backend", config.Backend, defaults.Backend);
        if (config.Output == Runtime::PointSamplingOutput::Properties)
        {
            if (ImGui::InputText("Rank property##Sampling", state.RankName.data(), state.RankName.size()))
            {
                config.RankName = state.RankName.data();
                changed = true;
            }
            hint("rank_name");
            if (ImGui::InputText("Selection property##Sampling", state.SelectedName.data(), state.SelectedName.size()))
            {
                config.SelectedName = state.SelectedName.data();
                changed = true;
            }
            hint("selected_name");
        }
        if (changed)
        {
            const auto applied = Runtime::ApplyEditorPointSamplingConfig(commands, config);
            state.ConfigDiagnostic = applied.Succeeded() ? std::string{}
                : Runtime::PreviewEditorPointSamplingCommand(commands, config).DisabledReason;
            if (!applied.Succeeded() && state.ConfigDiagnostic.empty()) state.ConfigDiagnostic = "Settings were rejected.";
        }
        if (!state.ConfigDiagnostic.empty()) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f), "%s", state.ConfigDiagnostic.c_str());
        const auto readiness = Runtime::ResolveEditorProcessingActionReadiness(
            commands, Runtime::PreviewEditorPointSamplingCommand(commands, config));
        if (DrawProcessingActionButton("Sample##Sampling", readiness))
        {
            auto slot = state.LastResult;
            *slot = Runtime::ApplyEditorPointSamplingCommand(commands, config,
                [slot](Runtime::EditorPointSamplingResult result) { *slot = std::move(result); });
            state.Run.WatchOutputIfQueued(*slot, config.SourceStableEntityId, config.RankName);
        }
        const Runtime::EditorOutputRef samplingDraft{config.SourceStableEntityId, config.RankName};
        state.Run.Draw(commands, config.SourceStableEntityId, "sampling_progress", &samplingDraft);
        if (!readiness.Enabled && !readiness.DisabledReason.empty()) ImGui::TextWrapped("%s", readiness.DisabledReason.c_str());
        if (*state.LastResult)
        {
            const auto& r = **state.LastResult;
            ImGui::Text("%s   %u of %u points   %.2f ms   backend: %s", r.Method.c_str(), r.SampleCount, r.InputCount,
                        r.Milliseconds, r.Backend.c_str());
            ImGui::Text("GPU input: %llu bytes uploaded, %llu cache hits; CPU stage: %llu bytes",
                        (unsigned long long)r.GpuInputUploadBytes, (unsigned long long)r.GpuInputCacheHits,
                        (unsigned long long)r.CpuStageReadbackBytes);
            if (!r.BackendDiagnostic.empty()) ImGui::TextWrapped("%s", r.BackendDiagnostic.c_str());
            if (!r.Message.empty()) ImGui::TextWrapped("%s", r.Message.c_str());
        }
        ImGui::End();
    }
}
