module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
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
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.SelectionController;

namespace Extrinsic::Sandbox::Editor
{
    namespace
    {
        using MeshNormalWeighting = decltype(
            Runtime::EditorMeshVertexNormalsCommand{}.Weighting);
        using PointNormalOrientation = decltype(
            Runtime::EditorPointCloudVertexNormalsCommand{}.Orientation);
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
        constexpr std::array<Runtime::EditorICPVariant, 2>
            kIcpVariants{{
                Runtime::EditorICPVariant::PointToPoint,
                Runtime::EditorICPVariant::PointToPlane,
            }};
        constexpr std::array<MeshNormalWeighting, 4> kMeshNormalWeightings{{
            static_cast<MeshNormalWeighting>(0),
            static_cast<MeshNormalWeighting>(1),
            static_cast<MeshNormalWeighting>(2),
            static_cast<MeshNormalWeighting>(4),
        }};
        constexpr std::array<const char*, 5> kMeshNormalWeightingNames{{
            "UniformFace",
            "AreaWeighted",
            "AngleWeighted",
            "AreaAngleWeighted",
            "MaxWeighted",
        }};
        constexpr std::array<PointNormalOrientation, 2>
            kPointNormalOrientations{{
                static_cast<PointNormalOrientation>(0),
                static_cast<PointNormalOrientation>(1),
            }};
        constexpr std::array<const char*, 2> kPointNormalOrientationNames{{
            "None",
            "MinimumSpanningTree",
        }};
        constexpr std::array<const char*, 6> kDenoiseStatusNames{{
            "Success",
            "EmptyMesh",
            "NonManifoldInput",
            "DegenerateGeometry",
            "NonFiniteInput",
            "InvalidParams",
        }};
        constexpr std::array<const char*, 4> kMeshNormalStatusNames{{
            "Success",
            "EmptyMesh",
            "InvalidOutputProperty",
            "PropertyTypeConflict",
        }};
        constexpr std::array<const char*, 7> kGraphNormalStatusNames{{
            "Success",
            "EmptyGraph",
            "InvalidPositionProperty",
            "InvalidTopologyProperty",
            "InvalidOutputProperty",
            "PropertyTypeConflict",
            "CountMismatch",
        }};
        constexpr std::array<const char*, 9> kPointNormalStatusNames{{
            "Success",
            "EmptyInput",
            "TooFewFinitePoints",
            "InvalidPositionProperty",
            "InvalidOutputProperty",
            "PropertyTypeConflict",
            "CountMismatch",
            "SpatialIndexBuildFailed",
            "SpatialIndexQueryFailed",
        }};
        constexpr std::array<const char*, 3> kPointNormalBackendNames{{
            "KDTree",
            "SuppliedKDTree",
            "SuppliedOctree",
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

        // BUG-141: a stored outcome is superseded by the next run of its own
        // operation, but a user who is not going to run it again had no way to
        // clear the line. Dismissal drops both the panel's own copy and the
        // session slot the model is rebuilt from, so it does not reappear on
        // the next frame. Draw it last: it invalidates the result the caller
        // is rendering.
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

        struct MeshNormalsState
        {
            std::optional<Runtime::EditorMeshVertexNormalsResult>
                LastResult{};
            std::int32_t Weighting{1};
            glm::vec3 Fallback{0.0f, 1.0f, 0.0f};
        };

        struct GraphNormalsState
        {
            std::optional<Runtime::EditorGraphVertexNormalsResult>
                LastResult{};
            glm::vec3 Fallback{0.0f, 0.0f, 1.0f};
            bool OrientTowardFallback{true};
        };

        struct PointNormalsState
        {
            std::optional<Runtime::EditorPointCloudVertexNormalsResult>
                LastResult{};
            std::int32_t KNeighbors{15};
            std::int32_t MinimumNeighbors{2};
            bool UseRadius{false};
            float Radius{0.0f};
            std::int32_t Orientation{1};
            glm::vec3 Fallback{0.0f, 0.0f, 1.0f};
        };

        struct RegistrationState
        {
            std::optional<Runtime::EditorRegistrationResult> LastResult{};
            std::int32_t Variant{0};
            std::int32_t MaxIterations{50};
            float MaxCorrespondenceDistance{0.0f};
            float InlierRatio{0.9f};
            std::int32_t TrajectoryStep{0};
            bool SwapSourceTarget{false};
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
        RemeshState Remesh{};
        SubdivideState Subdivide{};
        SimplifyState Simplify{};
        MeshNormalsState MeshNormals{};
        GraphNormalsState GraphNormals{};
        PointNormalsState PointNormals{};
        RegistrationState Registration{};

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
        void DrawRemeshWindow(bool&, const SandboxEditorContext&);
        void DrawSubdivideWindow(bool&, const SandboxEditorContext&);
        void DrawSimplifyWindow(bool&, const SandboxEditorContext&);
        void DrawMeshNormalsWindow(bool&, const SandboxEditorContext&);
        void DrawGraphNormalsWindow(bool&, const SandboxEditorContext&);
        void DrawPointNormalsWindow(bool&, const SandboxEditorContext&);
        void DrawRegistrationWindow(bool&, const SandboxEditorContext&);

        void DrawDenoiseControls(
            const Runtime::EditorDomainWindowModel&,
            const SandboxEditorContext&);
        void DrawCurvatureControls(
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
        void DrawMeshNormalsControls(
            const Runtime::EditorDomainWindowModel&,
            const SandboxEditorContext&);
        void DrawGraphNormalsControls(
            const Runtime::EditorDomainWindowModel&,
            const SandboxEditorContext&);
        void DrawPointNormalsControls(
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
        RegisterWindow("mesh.processing.curvature", {"Mesh", "Processing"},
                       "Curvature", &Impl::DrawCurvatureWindow);
        RegisterWindow("mesh.processing.remesh", {"Mesh", "Processing"},
                       "Remesh", &Impl::DrawRemeshWindow);
        RegisterWindow("mesh.processing.subdivide", {"Mesh", "Processing"},
                       "Subdivide", &Impl::DrawSubdivideWindow);
        RegisterWindow("mesh.processing.simplify", {"Mesh", "Processing"},
                       "Simplify", &Impl::DrawSimplifyWindow);
        RegisterWindow(
            "mesh.processing.vertices.normals",
            {"Mesh", "Processing", "Vertices"},
            "Normals",
            &Impl::DrawMeshNormalsWindow);
        RegisterWindow(
            "graph.processing.vertices.normals",
            {"Graph", "Processing", "Vertices"},
            "Normals",
            &Impl::DrawGraphNormalsWindow);
        RegisterWindow(
            "pointcloud.processing.vertices.normals",
            {"PointCloud", "Processing", "Vertices"},
            "Normals",
            &Impl::DrawPointNormalsWindow);
        RegisterWindow("view.registration", {"View"},
                       "ICP Registration", &Impl::DrawRegistrationWindow);
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
        Remesh.LastResult.reset();
        Subdivide.LastResult.reset();
        Simplify.LastResult.reset();
        MeshNormals.LastResult.reset();
        GraphNormals.LastResult.reset();
        PointNormals.LastResult.reset();
        Registration.LastResult.reset();
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
            // BUG-141: `DrawDomainWindowHeader` already renders
            // `model.Diagnostics`, into which the runtime folds
            // `model.Processing.Diagnostics`. Rendering the processing list
            // again here printed every entry twice.
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

    void MeshProcessingPanels::Impl::DrawMeshNormalsWindow(
        bool& open, const SandboxEditorContext& context)
    {
        DrawDomainWindow(
            open, context, Runtime::EditorDomainWindowKind::Mesh,
            "Mesh / Processing / Vertices / Normals",
            &Impl::DrawMeshNormalsControls);
    }

    void MeshProcessingPanels::Impl::DrawGraphNormalsWindow(
        bool& open, const SandboxEditorContext& context)
    {
        DrawDomainWindow(
            open, context, Runtime::EditorDomainWindowKind::Graph,
            "Graph / Processing / Vertices / Normals",
            &Impl::DrawGraphNormalsControls);
    }

    void MeshProcessingPanels::Impl::DrawPointNormalsWindow(
        bool& open, const SandboxEditorContext& context)
    {
        DrawDomainWindow(
            open, context, Runtime::EditorDomainWindowKind::PointCloud,
            "PointCloud / Processing / Vertices / Normals",
            &Impl::DrawPointNormalsControls);
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
        // NoChange means the kernel ran and moved nothing, so its diagnostics
        // are exactly what explains why — keep showing them.
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

        if (ImGui::Button("Compute##MeshCurvature"))
        {
            PublishCommandResult(
                Curvature.LastResult,
                Runtime::ApplyEditorMeshCurvatureCommand(
                    context.GeometryCommands,
                    Runtime::EditorMeshCurvatureCommand{
                        .StableEntityId = model.SelectedStableId,
                        .Output = output,
                        .PublishPrincipalDirections =
                            Curvature.PublishPrincipalDirections,
                    }),
                context.MethodResultSinks.MeshCurvature);
        }
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
        ImGui::Text(
            "Output: %s",
            Runtime::DebugNameForEditorMeshCurvatureOutput(
                result->Output));
        // BUG-145: NoChange means the kernel ran and changed nothing, so its
        // counters are exactly what explains why — keep showing them.
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
        if (result->Succeeded())
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
        if (result->Succeeded())
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

    void MeshProcessingPanels::Impl::DrawMeshNormalsControls(
        const Runtime::EditorDomainWindowModel& model,
        const SandboxEditorContext& context)
    {
        const Runtime::EditorGeometryProcessingModel& processing =
            model.Processing;
        if (context.GeometryResults.LastMeshVertexNormalsResult.has_value())
            MeshNormals.LastResult = *context.GeometryResults.LastMeshVertexNormalsResult;
        ImGui::SeparatorText("Normals");
        if (!processing.MeshVertexNormalsAvailable)
        {
            ImGui::TextDisabled(
                "Mesh vertex normals are unavailable for this selection.");
            return;
        }

        MeshNormals.Weighting = std::clamp(
            MeshNormals.Weighting, 0,
            static_cast<std::int32_t>(kMeshNormalWeightings.size() - 1u));
        const MeshNormalWeighting weighting =
            FromIndex(kMeshNormalWeightings, MeshNormals.Weighting);
        if (ImGui::BeginCombo(
                "Weighting##MeshVertexNormals",
                IndexedName(weighting, kMeshNormalWeightingNames)))
        {
            for (std::size_t i = 0u; i < kMeshNormalWeightings.size(); ++i)
            {
                const bool selected =
                    MeshNormals.Weighting == static_cast<std::int32_t>(i);
                if (ImGui::Selectable(
                        IndexedName(
                            kMeshNormalWeightings[i],
                            kMeshNormalWeightingNames),
                        selected))
                {
                    MeshNormals.Weighting = static_cast<std::int32_t>(i);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::DragFloat3(
            "Fallback normal##MeshVertexNormals", &MeshNormals.Fallback.x,
            0.01f, -1.0f, 1.0f);
        if (ImGui::Button("Recompute##MeshVertexNormals"))
        {
            PublishCommandResult(
                MeshNormals.LastResult,
                Runtime::ApplyEditorMeshVertexNormalsCommand(
                    context.GeometryCommands,
                    Runtime::EditorMeshVertexNormalsCommand{
                        .StableEntityId = model.SelectedStableId,
                        .Weighting = weighting,
                        .FallbackNormal = MeshNormals.Fallback,
                    }),
                context.MethodResultSinks.MeshVertexNormals);
        }

        const auto& result = MeshNormals.LastResult.has_value()
            ? MeshNormals.LastResult
            : processing.LastMeshVertexNormalsResult;
        if (!result.has_value())
        {
            ImGui::TextDisabled("Last normals run: none");
            return;
        }
        ImGui::Text(
            "Last normals run: %s",
            Runtime::DebugNameForEditorCommandStatus(result->Status));
        ImGui::Text(
            "Geometry status: %s",
            IndexedName(result->NormalStatus, kMeshNormalStatusNames));
        ImGui::Text(
            "Weighting: %s",
            IndexedName(result->Weighting, kMeshNormalWeightingNames));
        // BUG-145: NoChange means the kernel ran and changed nothing, so its
        // counters are exactly what explains why — keep showing them.
        if (result->Succeeded() ||
            result->Status == Runtime::EditorCommandStatus::NoChange)
        {
            ImGui::Text(
                "Written: %zu / %zu  changed: %zu  valid: %zu  fallback: %zu",
                result->WrittenCount, result->VertexSlotCount,
                result->ChangedNormalCount,
                result->ValidNormalVertexCount, result->FallbackVertexCount);
            ImGui::Text(
                "Faces: processed=%zu  degenerate=%zu  nonfinite=%zu  invalid=%zu",
                result->ProcessedFaceCount, result->DegenerateFaceCount,
                result->NonFiniteFaceCount,
                result->InvalidTopologyFaceCount);
            ImGui::Text(
                "Corners: degenerate=%zu  deleted faces=%zu  deleted vertices=%zu",
                result->DegenerateCornerCount,
                result->SkippedDeletedFaceCount,
                result->SkippedDeletedVertexCount);
            ImGui::Text(
                "Fallback repaired: %s",
                result->FallbackNormalWasRepaired ? "yes" : "no");
        }
        if (!result->Message.empty())
            ImGui::TextWrapped("%s", result->Message.c_str());
        DrawDismissLastResultButton(
            "Dismiss##MeshNormals",
            MeshNormals.LastResult,
            Runtime::EditorGeometryProcessingResultSlot::MeshVertexNormals,
            context);
    }

    void MeshProcessingPanels::Impl::DrawGraphNormalsControls(
        const Runtime::EditorDomainWindowModel& model,
        const SandboxEditorContext& context)
    {
        const Runtime::EditorGeometryProcessingModel& processing =
            model.Processing;
        if (context.GeometryResults.LastGraphVertexNormalsResult.has_value())
            GraphNormals.LastResult = *context.GeometryResults.LastGraphVertexNormalsResult;
        ImGui::SeparatorText("Normals");
        if (!processing.GraphVertexNormalsAvailable)
        {
            ImGui::TextDisabled(
                "Graph vertex normals are unavailable for this selection.");
            return;
        }

        ImGui::DragFloat3(
            "Fallback normal##GraphVertexNormals", &GraphNormals.Fallback.x,
            0.01f, -1.0f, 1.0f);
        ImGui::Checkbox(
            "Orient toward fallback##GraphVertexNormals",
            &GraphNormals.OrientTowardFallback);
        if (ImGui::Button("Recompute##GraphVertexNormals"))
        {
            PublishCommandResult(
                GraphNormals.LastResult,
                Runtime::ApplyEditorGraphVertexNormalsCommand(
                    context.GeometryCommands,
                    Runtime::EditorGraphVertexNormalsCommand{
                        .StableEntityId = model.SelectedStableId,
                        .FallbackNormal = GraphNormals.Fallback,
                        .OrientTowardFallback =
                            GraphNormals.OrientTowardFallback,
                    }),
                context.MethodResultSinks.GraphVertexNormals);
        }

        const auto& result = GraphNormals.LastResult.has_value()
            ? GraphNormals.LastResult
            : processing.LastGraphVertexNormalsResult;
        if (!result.has_value())
        {
            ImGui::TextDisabled("Last normals run: none");
            return;
        }
        ImGui::Text(
            "Last normals run: %s",
            Runtime::DebugNameForEditorCommandStatus(result->Status));
        ImGui::Text(
            "Geometry status: %s",
            IndexedName(result->NormalStatus, kGraphNormalStatusNames));
        if (result->Succeeded() ||
            result->Status == Runtime::EditorCommandStatus::NoChange)
        {
            ImGui::Text(
                "Written: %zu / %zu  changed: %zu  valid: %zu  fallback: %zu",
                result->WrittenCount, result->VertexSlotCount,
                result->ChangedNormalCount,
                result->ValidNormalVertexCount, result->FallbackVertexCount);
            ImGui::Text(
                "Edges: %zu  invalid=%zu  deleted=%zu",
                result->EdgeSlotCount, result->InvalidEdgeCount,
                result->SkippedDeletedEdgeCount);
            ImGui::Text(
                "Neighborhoods: isolated=%zu  degree1=%zu  collinear=%zu",
                result->IsolatedVertexCount, result->DegreeOneVertexCount,
                result->CollinearNeighborhoodCount);
            ImGui::Text(
                "Positions: duplicate=%zu  nonfinite=%zu",
                result->DuplicatePositionCount,
                result->NonFinitePositionCount);
            ImGui::Text(
                "Fallback repaired: %s",
                result->FallbackNormalWasRepaired ? "yes" : "no");
        }
        if (!result->Message.empty())
            ImGui::TextWrapped("%s", result->Message.c_str());
        DrawDismissLastResultButton(
            "Dismiss##GraphNormals",
            GraphNormals.LastResult,
            Runtime::EditorGeometryProcessingResultSlot::GraphVertexNormals,
            context);
    }

    void MeshProcessingPanels::Impl::DrawPointNormalsControls(
        const Runtime::EditorDomainWindowModel& model,
        const SandboxEditorContext& context)
    {
        const Runtime::EditorGeometryProcessingModel& processing =
            model.Processing;
        if (context.GeometryResults.LastPointCloudVertexNormalsResult.has_value())
        {
            PointNormals.LastResult =
                *context.GeometryResults.LastPointCloudVertexNormalsResult;
        }
        ImGui::SeparatorText("Normals");
        if (!processing.PointCloudVertexNormalsAvailable)
        {
            ImGui::TextDisabled(
                "Point-cloud vertex normals are unavailable for this selection.");
            return;
        }

        PointNormals.KNeighbors =
            std::clamp(PointNormals.KNeighbors, 1, 512);
        PointNormals.MinimumNeighbors =
            std::clamp(PointNormals.MinimumNeighbors, 1, 512);
        PointNormals.Orientation = std::clamp(
            PointNormals.Orientation, 0,
            static_cast<std::int32_t>(kPointNormalOrientations.size() - 1u));
        ImGui::DragInt(
            "K##PointCloudVertexNormals", &PointNormals.KNeighbors,
            1.0f, 1, 512);
        ImGui::DragInt(
            "Minimum neighbors##PointCloudVertexNormals",
            &PointNormals.MinimumNeighbors, 1.0f, 1, 512);
        ImGui::Checkbox(
            "Use radius##PointCloudVertexNormals", &PointNormals.UseRadius);
        if (PointNormals.UseRadius)
        {
            PointNormals.Radius = std::max(PointNormals.Radius, 0.001f);
            ImGui::DragFloat(
                "Radius##PointCloudVertexNormals", &PointNormals.Radius,
                0.01f, 0.001f, 1000.0f);
        }

        const PointNormalOrientation orientation =
            FromIndex(kPointNormalOrientations, PointNormals.Orientation);
        if (ImGui::BeginCombo(
                "Orientation##PointCloudVertexNormals",
                IndexedName(orientation, kPointNormalOrientationNames)))
        {
            for (std::size_t i = 0u;
                 i < kPointNormalOrientations.size(); ++i)
            {
                const bool selected =
                    PointNormals.Orientation == static_cast<std::int32_t>(i);
                if (ImGui::Selectable(
                        IndexedName(
                            kPointNormalOrientations[i],
                            kPointNormalOrientationNames),
                        selected))
                {
                    PointNormals.Orientation = static_cast<std::int32_t>(i);
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::DragFloat3(
            "Fallback normal##PointCloudVertexNormals",
            &PointNormals.Fallback.x, 0.01f, -1.0f, 1.0f);
        if (ImGui::Button("Recompute##PointCloudVertexNormals"))
        {
            PublishCommandResult(
                PointNormals.LastResult,
                Runtime::ApplyEditorPointCloudVertexNormalsCommand(
                    context.GeometryCommands,
                    Runtime::EditorPointCloudVertexNormalsCommand{
                        .StableEntityId = model.SelectedStableId,
                        .KNeighbors = static_cast<std::uint32_t>(
                            PointNormals.KNeighbors),
                        .MinimumNeighbors = static_cast<std::uint32_t>(
                            PointNormals.MinimumNeighbors),
                        .UseRadiusSearch = PointNormals.UseRadius,
                        .Radius = PointNormals.Radius,
                        .Orientation = orientation,
                        .FallbackNormal = PointNormals.Fallback,
                    }),
                context.MethodResultSinks.PointCloudVertexNormals);
        }

        const auto& result = PointNormals.LastResult.has_value()
            ? PointNormals.LastResult
            : processing.LastPointCloudVertexNormalsResult;
        if (!result.has_value())
        {
            ImGui::TextDisabled("Last normals run: none");
            return;
        }
        ImGui::Text(
            "Last normals run: %s",
            Runtime::DebugNameForEditorCommandStatus(result->Status));
        ImGui::Text(
            "Geometry status: %s",
            IndexedName(result->NormalStatus, kPointNormalStatusNames));
        ImGui::Text(
            "Backend: %s  orientation: %s",
            IndexedName(result->Backend, kPointNormalBackendNames),
            IndexedName(result->Orientation, kPointNormalOrientationNames));
        if (result->Succeeded() ||
            result->Status == Runtime::EditorCommandStatus::NoChange)
        {
            ImGui::Text(
                "Written: %zu / %zu  changed: %zu  finite: %zu  valid: %zu",
                result->WrittenCount, result->PointSlotCount,
                result->ChangedNormalCount,
                result->FinitePointCount, result->ValidNormalPointCount);
            ImGui::Text(
                "Fallback: %zu  tooFew=%zu  degenerate=%zu  collinear=%zu",
                result->FallbackPointCount, result->TooFewNeighborCount,
                result->DegenerateNeighborhoodCount,
                result->CollinearNeighborhoodCount);
            ImGui::Text(
                "Positions: duplicate=%zu  nonfinite=%zu  deleted=%zu",
                result->DuplicatePositionCount,
                result->NonFinitePointCount,
                result->SkippedDeletedPointCount);
            ImGui::Text(
                "Queries: failures=%zu  visited=%zu  distances=%zu",
                result->SpatialQueryFailureCount,
                result->KNNVisitedNodeCount,
                result->KNNDistanceEvaluationCount);
            ImGui::Text(
                "Flipped: %zu  fallback repaired: %s",
                result->FlippedOrientationCount,
                result->FallbackNormalWasRepaired ? "yes" : "no");
        }
        if (!result->Message.empty())
            ImGui::TextWrapped("%s", result->Message.c_str());
        DrawDismissLastResultButton(
            "Dismiss##PointNormals",
            PointNormals.LastResult,
            Runtime::EditorGeometryProcessingResultSlot::PointCloudVertexNormals,
            context);
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

        ImGui::TextWrapped(
            "Aligns a source point cloud onto a target point cloud with ICP "
            "and drives the source entity Transform along the convergence "
            "trajectory.");
        std::vector<std::uint32_t> selected{};
        if (context.Selection != nullptr)
        {
            for (const std::uint32_t id :
                 context.Selection->SelectedStableIds)
            {
                selected.push_back(id);
            }
        }
        if (selected.size() < 2u)
        {
            ImGui::TextDisabled(
                "Select two point-cloud entities (source + target) to register.");
        }
        else
        {
            const std::uint32_t sourceId =
                selected[Registration.SwapSourceTarget ? 1u : 0u];
            const std::uint32_t targetId =
                selected[Registration.SwapSourceTarget ? 0u : 1u];
            ImGui::Text("Source entity: %u", sourceId);
            ImGui::Text("Target entity: %u", targetId);
            ImGui::Checkbox(
                "Swap source / target##ICP",
                &Registration.SwapSourceTarget);

            Registration.Variant = std::clamp(
                Registration.Variant, 0,
                static_cast<std::int32_t>(kIcpVariants.size() - 1u));
            const Runtime::EditorICPVariant variant =
                FromIndex(kIcpVariants, Registration.Variant);
            if (ImGui::BeginCombo(
                    "Variant##ICP",
                    Runtime::DebugNameForEditorICPVariant(variant)))
            {
                for (std::size_t i = 0u; i < kIcpVariants.size(); ++i)
                {
                    const bool selectedOption =
                        Registration.Variant == static_cast<std::int32_t>(i);
                    std::string label =
                        Runtime::DebugNameForEditorICPVariant(
                            kIcpVariants[i]);
                    label += "##ICPVariant" + std::to_string(i);
                    if (ImGui::Selectable(label.c_str(), selectedOption))
                        Registration.Variant = static_cast<std::int32_t>(i);
                    if (selectedOption)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            Registration.MaxIterations =
                std::clamp(Registration.MaxIterations, 1, 1000);
            ImGui::DragInt(
                "Max iterations##ICP", &Registration.MaxIterations,
                1.0f, 1, 1000);
            Registration.MaxCorrespondenceDistance =
                std::max(Registration.MaxCorrespondenceDistance, 0.0f);
            ImGui::DragFloat(
                "Max correspondence distance (0 = unlimited)##ICP",
                &Registration.MaxCorrespondenceDistance,
                0.01f, 0.0f, 1.0e6f, "%.4f");
            Registration.InlierRatio =
                std::clamp(Registration.InlierRatio, 0.01f, 1.0f);
            ImGui::DragFloat(
                "Inlier ratio##ICP", &Registration.InlierRatio,
                0.01f, 0.01f, 1.0f, "%.2f");

            const std::size_t trajectoryLength =
                Registration.LastResult.has_value()
                ? Registration.LastResult->TrajectoryLength
                : 0u;
            bool run = ImGui::Button("Run ICP##ICP");
            std::size_t requestedStep = static_cast<std::size_t>(
                Registration.MaxIterations);
            if (trajectoryLength > 0u)
            {
                Registration.TrajectoryStep = std::clamp(
                    Registration.TrajectoryStep, 0,
                    static_cast<std::int32_t>(trajectoryLength));
                ImGui::SliderInt(
                    "Trajectory step##ICP", &Registration.TrajectoryStep,
                    0, static_cast<int>(trajectoryLength));
                if (ImGui::IsItemDeactivatedAfterEdit())
                {
                    run = true;
                    requestedStep = static_cast<std::size_t>(
                        Registration.TrajectoryStep);
                }
            }
            if (run)
            {
                PublishCommandResult(
                    Registration.LastResult,
                    Runtime::ApplyEditorRegistrationCommand(
                        context.GeometryCommands,
                        Runtime::EditorRegistrationCommand{
                            .SourceStableEntityId = sourceId,
                            .TargetStableEntityId = targetId,
                            .Variant = variant,
                            .MaxIterations = static_cast<std::uint32_t>(
                                Registration.MaxIterations),
                            .MaxCorrespondenceDistance = static_cast<double>(
                                Registration.MaxCorrespondenceDistance),
                            .InlierRatio = static_cast<double>(
                                Registration.InlierRatio),
                            .TrajectoryStep = requestedStep,
                        }),
                    context.MethodResultSinks.Registration);
            }
        }

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
            if (result.Succeeded() && result.HasResult)
            {
                ImGui::Text(
                    "Variant: %s  points: %zu -> %zu",
                    Runtime::DebugNameForEditorICPVariant(
                        result.Variant),
                    result.SourcePointCount, result.TargetPointCount);
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

    // The app owns panel registration, ImGui state, and draw controllers.
    // Runtime retains snapshots, typed operations, undo, jobs, and results.
}
