module;
#include <functional>
#include <span>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>
#include <imgui.h>

module Extrinsic.Sandbox.Editor.MethodPanels;

import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.ClusteringTypes;
import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.PointCloudConsolidationTypes;
import Extrinsic.Runtime.ProgressivePoissonConfig;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.TextureBakeModule;
import Extrinsic.Runtime.RenderRecipeEditingOperations;

#include "Sandbox.PanelSupport.hpp"
#include "Sandbox.PointCloudConsolidationPanel.hpp"

namespace Extrinsic::Sandbox::Editor
{
    namespace
    {
        const Runtime::EditorPointCloudServicePreparedFrame& PointCloudServiceFrame(
            const SandboxEditorContext& context)
        {
            // Preserve the empty, unavailable state of a default context.
            static const Runtime::EditorPointCloudServicePreparedFrame unavailable{};
            return context.PointCloudService ? *context.PointCloudService : unavailable;
        }
    }
}

extern "C++"
{
namespace Extrinsic::Sandbox::Editor
{
    std::array<SandboxPointCloudConsolidationStrategyOption, 4u>
    SandboxPointCloudConsolidationStrategyOptions() noexcept
    {
        using Strategy = Runtime::PointCloudConsolidationStrategy;
        return {
            SandboxPointCloudConsolidationStrategyOption{
                .Strategy = Strategy::Lop,
                .Label = "LOP",
                .StableToken = Runtime::StableToken(Strategy::Lop),
                .Available = true,
            },
            SandboxPointCloudConsolidationStrategyOption{
                .Strategy = Strategy::Wlop,
                .Label = "WLOP",
                .StableToken = Runtime::StableToken(Strategy::Wlop),
                .Available = true,
            },
            SandboxPointCloudConsolidationStrategyOption{
                .Strategy = Strategy::Clop,
                .Label = "CLOP",
                .StableToken = Runtime::StableToken(Strategy::Clop),
                .Available = true,
            },
            SandboxPointCloudConsolidationStrategyOption{
                .Strategy = Strategy::Ear,
                .Label = "EAR",
                .StableToken = Runtime::StableToken(Strategy::Ear),
                .Available = true,
            },
        };
    }

    std::optional<SandboxPointCloudConsolidationPanelApplyRequest>
    BuildSandboxPointCloudConsolidationPanelApplyRequest(
        const std::uint32_t stableEntityId,
        const Runtime::PointCloudConsolidationPropertyRefs& properties,
        const SandboxPointCloudConsolidationPanelConfig& config)
    {
        if (stableEntityId == 0u ||
            !Runtime::IsValidPointCloudConsolidationPropertyRefs(properties))
        {
            return std::nullopt;
        }

        if (!Runtime::IsValidEditorPointCloudConsolidationConfig(config))
            return std::nullopt;

        return SandboxPointCloudConsolidationPanelApplyRequest{
            .Config = config,
            .Execute = Runtime::PointCloudConsolidationRequest{
                .StableEntityId = stableEntityId,
                .Properties = properties,
                .Config = config,
            },
        };
    }

    SandboxPointCloudConsolidationPanelActionResult
    ApplySandboxPointCloudConsolidationPanelAction(
        const SandboxEditorContext& context,
        const std::uint32_t stableEntityId,
        const Runtime::PointCloudConsolidationPropertyRefs& properties,
        const SandboxPointCloudConsolidationPanelConfig& config)
    {
        const auto& service = PointCloudServiceFrame(context);
        SandboxPointCloudConsolidationPanelActionResult result{};
        result.Config.Status =
            Runtime::RuntimeEngineConfigApplyStatus::Rejected;
        result.Config.Source = Runtime::RuntimeConfigControlSource::Editor;
        const auto request =
            BuildSandboxPointCloudConsolidationPanelApplyRequest(
                stableEntityId, properties, config);
        if (!request.has_value())
            return result;

        result.Config = Runtime::ApplyEditorPointCloudConsolidationConfig(
            service.Commands,
            request->Config,
            request->SourceId);
        if (result.Config.Succeeded())
        {
            result.Submission =
                Runtime::SubmitEditorPointCloudConsolidation(
                    service.Commands,
                    service.PointCloudConsolidation,
                    request->Execute);
        }
        return result;
    }

    SandboxPointCloudConsolidationResultSummary
    BuildSandboxPointCloudConsolidationResultSummary(
        const Runtime::PointCloudConsolidationResult& result)
    {
        return SandboxPointCloudConsolidationResultSummary{
            .Succeeded = result.Succeeded(),
            .Queued = result.Status ==
                Runtime::PointCloudConsolidationRunStatus::Queued,
            .Status = std::string{Runtime::ToString(result.Status)},
            .ImplementationId = result.ImplementationId,
            .StrategyToken = result.StrategyToken,
            .RequestedBackend = std::string{
                Runtime::StableToken(result.RequestedBackend)},
            .ActualBackend = std::string{
                Runtime::StableToken(result.ActualBackend)},
            .FellBackToCpu = result.FellBackToCpu,
            .BackendDiagnostic = result.BackendDiagnostic,
            .SupportRadiusAnalysisStatus =
                result.SupportRadiusAnalysisStatus,
            .SupportRadiusSource = result.SupportRadiusSource,
            .SupportRadiusQuantile = result.SupportRadiusQuantile,
            .Message = result.Message,
            .SupportRadiusEstimatorVersion =
                result.SupportRadiusEstimatorVersion,
            .SupportRadiusProfileSampleCount =
                result.SupportRadiusProfileSampleCount,
            .SupportRadiusRequestedNeighborRank =
                result.SupportRadiusRequestedNeighborRank,
            .SupportRadiusNeighborRank =
                result.SupportRadiusNeighborRank,
            .SupportRadiusWorkloadAdjusted =
                result.SupportRadiusWorkloadAdjusted,
            .SupportRadiusNeighborDistance =
                result.SupportRadiusNeighborDistance,
            .ResolvedSupportRadius = result.ResolvedSupportRadius,
            .SupportRadiusBoundingBoxDiagonal =
                result.SupportRadiusBoundingBoxDiagonal,
            .SupportNeighborsP50 = result.SupportNeighborsP50,
            .SupportNeighborsP95 = result.SupportNeighborsP95,
            .SupportNeighborsMax = result.SupportNeighborsMax,
            .PredictedSupportQueryCount =
                result.PredictedSupportQueryCount,
            .PredictedContributionCount =
                result.PredictedContributionCount,
            .InputPointCount = result.InputPointCount,
            .OutputPointCount = result.OutputPointCount,
            .Iterations = result.Iterations,
            .Converged = result.Converged,
            .AverageDisplacement = result.AverageDisplacement,
            .MaxDisplacement = result.MaxDisplacement,
            .UsedAuthoredNormals = result.UsedAuthoredNormals,
            .EstimatedNormals = result.EstimatedNormals,
            .NormalRefinementIterations =
                result.NormalRefinementIterations,
            .InsertedPointCount = result.InsertedPointCount,
        };
    }

}
}

namespace Extrinsic::Sandbox::Editor
{
    namespace
    {
        using ParameterizationPanelConfig =
            Runtime::ParameterizationConfig;
        using ParameterizationUvConfig =
            decltype(ParameterizationPanelConfig{}.Lscm.PinUv0);
        using ParameterizationLscmConfig =
            decltype(ParameterizationPanelConfig{}.Lscm);
        using ParameterizationHarmonicConfig =
            decltype(ParameterizationPanelConfig{}.Harmonic);
        using ParameterizationBffConfig =
            decltype(ParameterizationPanelConfig{}.Bff);
        using ParameterizationBoundaryPolicy =
            decltype(ParameterizationPanelConfig{}.Harmonic.Boundary);
        using ParameterizationBffBoundaryMode =
            decltype(ParameterizationPanelConfig{}.Bff.Mode);
        using ParameterizationUvRenderMode =
            decltype(ParameterizationPanelConfig{}.View.RenderMode);
        using ParameterizationUvBackgroundMode =
            decltype(ParameterizationPanelConfig{}.View.BackgroundMode);
        using ParameterizationSolverStatus = decltype(
            Runtime::EditorParameterizationResult{}
                .ParameterizationStatus);

        constexpr std::array<Runtime::ClusteringBackend, 2>
            kKMeansBackends{
                Runtime::ClusteringBackend::CpuReference,
                Runtime::ClusteringBackend::VulkanCompute,
            };
        constexpr std::array<Runtime::ProgressivePoissonPlaygroundChannel, 4>
            kProgressivePoissonChannels{
                Runtime::ProgressivePoissonPlaygroundChannel::Level,
                Runtime::ProgressivePoissonPlaygroundChannel::Rank,
                Runtime::ProgressivePoissonPlaygroundChannel::SplatRadius,
                Runtime::ProgressivePoissonPlaygroundChannel::PrefixVisible,
            };
        constexpr std::array<Runtime::ProgressivePoissonPlaygroundBackend, 2>
            kProgressivePoissonBackends{
                Runtime::ProgressivePoissonPlaygroundBackend::CpuReference,
                Runtime::ProgressivePoissonPlaygroundBackend::VulkanCompute,
            };

        [[nodiscard]] bool IsPointSetVec3Property(
            const Runtime::EditorPropertyCatalogRow& row) noexcept
        {
            return row.Bindable && row.ElementCount != 0u &&
                   row.Descriptor.Domain !=
                       Runtime::GeometryElementDomain::Unknown &&
                   row.ValueKind == Geometry::PropertyValueKind::Vec3;
        }

        [[nodiscard]] int PointSetPositionPreference(
            const Runtime::EditorPropertyCatalogRow& row) noexcept
        {
            if (row.Name == "v:position" || row.Name == "v:point" ||
                row.Name == "p:position")
            {
                return 0;
            }
            if (row.Name.find("position") != std::string::npos)
                return 1;
            if (row.Name == "f:centroid" ||
                row.Name.find("centroid") != std::string::npos ||
                row.Name.find("center") != std::string::npos)
            {
                return 2;
            }
            if (row.Name.find("normal") == std::string::npos)
                return 3;
            return 4;
        }

        [[nodiscard]] const Runtime::EditorPropertyCatalogRow*
        FindPreferredPointSetPosition(
            const Runtime::EditorPropertyCatalogModel& catalog)
        {
            const Runtime::EditorPropertyCatalogRow* preferred = nullptr;
            int preferredRank = std::numeric_limits<int>::max();
            for (const Runtime::EditorPropertyCatalogRow& row : catalog.Rows)
            {
                if (!IsPointSetVec3Property(row))
                    continue;
                const int rank = PointSetPositionPreference(row);
                if (preferred == nullptr || rank < preferredRank)
                {
                    preferred = &row;
                    preferredRank = rank;
                }
            }
            return preferred;
        }

        [[nodiscard]] const Runtime::EditorPropertyCatalogRow*
        FindPointSetProperty(
            const Runtime::EditorPropertyCatalogModel& catalog,
            const Runtime::GeometryPropertyRef& property)
        {
            const auto found = std::find_if(
                catalog.Rows.begin(),
                catalog.Rows.end(),
                [&property](const Runtime::EditorPropertyCatalogRow& row)
                {
                    return IsPointSetVec3Property(row) &&
                           row.Descriptor == property;
                });
            return found != catalog.Rows.end() ? &*found : nullptr;
        }

        [[nodiscard]] bool IsCompatiblePointSetNormal(
            const Runtime::EditorPropertyCatalogRow& row,
            const Runtime::EditorPropertyCatalogRow& positions) noexcept
        {
            return IsPointSetVec3Property(row) &&
                   row.Descriptor.Domain == positions.Descriptor.Domain &&
                   row.ElementCount == positions.ElementCount &&
                   row.Name != positions.Name;
        }

        [[nodiscard]] const Runtime::EditorPropertyCatalogRow*
        FindPreferredPointSetNormal(
            const Runtime::EditorPropertyCatalogModel& catalog,
            const Runtime::EditorPropertyCatalogRow& positions)
        {
            for (const Runtime::EditorPropertyCatalogRow& row : catalog.Rows)
            {
                if (!IsCompatiblePointSetNormal(row, positions))
                    continue;
                if (row.Name == "v:normal" || row.Name == "p:normal" ||
                    row.Name == "f:normal" ||
                    row.Name.find("normal") != std::string::npos)
                {
                    return &row;
                }
            }
            return nullptr;
        }

        [[nodiscard]] std::string PointSetPropertyLabel(
            const Runtime::EditorPropertyCatalogRow& row)
        {
            std::string label =
                Runtime::DebugNameForEditorPropertyCatalogDomain(row.Domain);
            label += " / ";
            label += row.Name;
            label += " (";
            label += std::to_string(row.ElementCount);
            label += ")";
            return label;
        }

        [[nodiscard]] const Runtime::EditorPropertyCatalogRow*
        DrawPointSetPositionInput(
            const char* label,
            const Runtime::EditorPropertyCatalogModel& catalog,
            const Runtime::GeometryPropertyRef& current)
        {
            const Runtime::EditorPropertyCatalogRow* picked = nullptr;
            if (ImGui::BeginCombo(label, current.Name.c_str()))
            {
                const auto* selected = &current;
                for (const auto& row : catalog.Rows)
                {
                    if (!IsPointSetVec3Property(row)) continue;
                    const auto rowLabel = PointSetPropertyLabel(row);
                    if (ImGui::Selectable(rowLabel.c_str(), row.Descriptor == *selected))
                    {
                        picked = &row;
                        selected = &row.Descriptor;
                    }
                }
                ImGui::EndCombo();
            }
            return picked;
        }

        template <std::size_t Size>
        void SetPropertyNameBuffer(
            std::array<char, Size>& buffer,
            const std::string_view name)
        {
            buffer.fill('\0');
            const std::size_t count =
                std::min(name.size(), buffer.size() - 1u);
            std::copy_n(name.data(), count, buffer.data());
        }

        [[nodiscard]] bool IsConsolidationHistoryLabel(
            const std::string_view label) noexcept
        {
            return label == "Consolidate point set" ||
                   label == "Consolidate point cloud";
        }



        template <typename T, std::size_t Size>
        [[nodiscard]] T OptionFromIndex(
            const std::array<T, Size>& options,
            const std::int32_t index) noexcept
        {
            static_assert(Size > 0u);
            const std::int32_t clamped = std::clamp(
                index, 0, static_cast<std::int32_t>(Size - 1u));
            return options[static_cast<std::size_t>(clamped)];
        }

        template <typename T, std::size_t Size>
        [[nodiscard]] std::int32_t IndexOfOption(
            const std::array<T, Size>& options,
            const T value) noexcept
        {
            static_assert(Size > 0u);
            const auto found = std::find(options.begin(), options.end(), value);
            return found == options.end()
                ? 0
                : static_cast<std::int32_t>(std::distance(options.begin(), found));
        }

        void DrawProgressivePoissonTooltip(const char* text)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", text);
        }

        void DrawProgressivePoissonDisabledRun(
            const std::string_view disabledReason)
        {
            (void)DrawProcessingActionButton("Run Progressive Poisson##ProgressivePoisson",
                {false, std::string{disabledReason}});
            if (!disabledReason.empty())
                ImGui::TextDisabled("%.*s",
                                    static_cast<int>(disabledReason.size()),
                                    disabledReason.data());
        }

        [[nodiscard]] std::string FormatLevelCounts(
            const std::vector<std::uint32_t>& counts)
        {
            if (counts.empty())
                return "none";

            std::string text{};
            for (std::size_t index = 0u; index < counts.size(); ++index)
            {
                if (index != 0u)
                    text += ", ";
                text += std::to_string(index);
                text += ":";
                text += std::to_string(counts[index]);
            }
            return text;
        }


    }

    struct MethodPanels::Impl
    {
        struct KMeansState
        {
            ProcessingEntityInput Input{};
            std::optional<Runtime::KMeansRunCompleted> LastResult{};
            std::optional<Runtime::RuntimeEngineConfigApplyResult>
                LastConfigApply{};
            Runtime::KMeansPropertyRefs Properties{};
            std::uint32_t Entity{};
            std::string VisualizationDiagnostic{};
            std::int32_t Backend{0};
            std::int32_t ClusterCount{8};
            std::int32_t MaxIterations{32};
            std::uint32_t Seed{42u};
            bool UseHierarchicalInitialization{true};
            bool Initialized{false};
            bool Dirty{false};
        };

        struct ProgressivePoissonState : ProcessingDraftState<
            Runtime::ProgressivePoissonPlaygroundConfig, Runtime::EditorProgressivePoissonResult>
        {
            ProcessingEntityInput Input{};
            std::optional<Runtime::RuntimeEngineConfigApplyResult>
                LastConfigResult{};
            std::int32_t Dimension{3};
            std::int32_t GridWidth{4};
            std::int32_t MaxLevels{16};
            float HashLoadFactor{0.25f};
            float RadiusAlpha{-1.0f};
            bool RandomizeGridOrigin{true};
            std::int32_t GridOriginSeed{1337};
            bool ShuffleWithinLevels{true};
            std::int32_t ShuffleSeed{0x51ed270b};
            std::int32_t PrefixCount{0};
            std::int32_t Channel{0};
            std::int32_t Backend{0};
            bool AutoRunOnEdit{true};
            float DebounceSeconds{0.25f};
            bool AutoRunPending{false};
            double LastEditTime{0.0};
            std::uint32_t PendingStableEntityId{0u};
        };

        struct PointCloudConsolidationState
        {
            ProcessingEntityInput Input{};
            SandboxPointCloudConsolidationPanelConfig Draft{};
            Runtime::PointCloudConsolidationPropertyRefs Properties{};
            std::array<char, 128u> OutputPositionName{};
            std::array<char, 128u> OutputNormalName{};
            std::uint32_t BoundStableEntityId{0u};
            bool BindingsInitialized{false};
            bool PublishNormals{false};
            bool Initialized{false};
            bool Dirty{false};
            std::optional<Runtime::RuntimeEngineConfigApplyResult>
                LastConfigApply{};
            std::optional<Runtime::PointCloudConsolidationResult>
                LastResult{};
        };

        struct ParameterizationState
        {
            ProcessingEntityInput Input{};
            Runtime::ParameterizationConfig Draft{};
            bool Initialized{false};
            bool Dirty{false};
            std::optional<Runtime::RuntimeEngineConfigApplyResult>
                LastConfigResult{};
            std::optional<Runtime::EditorParameterizationResult>
                LastResult{};
            std::string VisualizationDiagnostic{};
            float SplitRatio{0.42f};
            float Zoom{1.0f};
            glm::vec2 Pan{0.0f};
            std::optional<
                Runtime::EditorParameterizationUvViewState>
                LastUvViewState{};
        };

        EditorShell* Shell{nullptr};
        std::vector<Runtime::EditorWindowHandle> Handles{};
        int CachedModelFrame{-1};
        std::array<
            std::optional<Runtime::EditorDomainWindowModel>,
            3u>
            CachedDomainModels{};
        std::optional<Runtime::EditorInspectorModel> CachedInspectorModel{};
        KMeansState KMeans{};
        ProgressivePoissonState ProgressivePoisson{};
        PointCloudConsolidationState PointCloudConsolidation{};
        ParameterizationState Parameterization{};

        void Register(EditorShell& editorShell)
        {
            Unregister();
            Shell = &editorShell;

            RegisterKMeansWindow(
                Runtime::EditorDomainWindowKind::PointCloud,
                "pointcloud.processing.kmeans",
                {"PointCloud", "Processing"},
                "PointCloud / Processing / K-Means");
            RegisterKMeansWindow(
                Runtime::EditorDomainWindowKind::Graph,
                "graph.processing.kmeans",
                {"Graph", "Processing"},
                "Graph / Processing / K-Means");
            RegisterKMeansWindow(
                Runtime::EditorDomainWindowKind::Mesh,
                "mesh.processing.kmeans",
                {"Mesh", "Processing"},
                "Mesh / Processing / K-Means");
            RegisterProgressivePoissonWindow(
                Runtime::EditorDomainWindowKind::PointCloud,
                "pointcloud.processing.progressive_poisson",
                {"PointCloud", "Processing"},
                "PointCloud / Processing / Progressive Poisson");
            RegisterProgressivePoissonWindow(
                Runtime::EditorDomainWindowKind::Graph,
                "graph.processing.progressive_poisson",
                {"Graph", "Processing"},
                "Graph / Processing / Progressive Poisson");
            RegisterProgressivePoissonWindow(
                Runtime::EditorDomainWindowKind::Mesh,
                "mesh.processing.progressive_poisson",
                {"Mesh", "Processing"},
                "Mesh / Processing / Progressive Poisson");
            RegisterPointSetConsolidationWindow(
                "pointcloud.processing.consolidation",
                {"PointCloud", "Processing"},
                "PointCloud / Processing / Consolidate (LOP/WLOP/CLOP/EAR)");
            RegisterPointSetConsolidationWindow(
                "graph.processing.consolidation",
                {"Graph", "Processing"},
                "Graph / Processing / Consolidate (LOP/WLOP/CLOP/EAR)");
            RegisterPointSetConsolidationWindow(
                "mesh.processing.consolidation",
                {"Mesh", "Processing"},
                "Mesh / Processing / Consolidate (LOP/WLOP/CLOP/EAR)");
            RegisterParameterizationWindow();
        }

        void Unregister()
        {
            if (Shell != nullptr)
            {
                for (const Runtime::EditorWindowHandle handle : Handles)
                    (void)Shell->UnregisterEditorWindow(handle);
            }
            Handles.clear();
            Shell = nullptr;
            CachedModelFrame = -1;
            for (auto& model : CachedDomainModels)
                model.reset();
            CachedInspectorModel.reset();
            KMeans = KMeansState{};
            ProgressivePoisson.Input = {};
            ProgressivePoisson.LastResult.reset();
            ProgressivePoisson.LastConfigResult.reset();
            ProgressivePoisson.AutoRunPending = false;
            ProgressivePoisson.LastEditTime = 0.0;
            ProgressivePoisson.PendingStableEntityId = 0u;
            PointCloudConsolidation =
                PointCloudConsolidationState{};
            Parameterization = ParameterizationState{};
        }

        [[nodiscard]] const Runtime::EditorDomainWindowModel&
        GetDomainWindowModel(
            const SandboxEditorContext& context,
            const Runtime::EditorDomainWindowKind kind, std::optional<std::uint32_t> entity = std::nullopt)
        {
            const int frame = ImGui::GetFrameCount();
            if (CachedModelFrame != frame)
            {
                CachedModelFrame = frame;
                for (auto& model : CachedDomainModels)
                    model.reset();
                CachedInspectorModel.reset();
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

        [[nodiscard]] const Runtime::EditorInspectorModel& GetInspectorModel(
            const SandboxEditorContext& context, std::optional<std::uint32_t> entity = std::nullopt)
        {
            const int frame = ImGui::GetFrameCount();
            if (CachedModelFrame != frame)
            {
                CachedModelFrame = frame;
                for (auto& model : CachedDomainModels)
                    model.reset();
                CachedInspectorModel.reset();
            }

            if (!CachedInspectorModel.has_value() || (entity && CachedInspectorModel->Entity.StableEntityId != *entity))
            {
                CachedInspectorModel = Runtime::BuildEditorInspectorModel(
                    context.SnapshotQueries,
                    context.ModelBuildStats, entity);
            }
            return *CachedInspectorModel;
        }

        void RegisterKMeansWindow(
            const Runtime::EditorDomainWindowKind kind,
            std::string id,
            std::vector<std::string> menuPath,
            std::string windowTitle)
        {
            const std::string callbackTitle = windowTitle;
            Handles.push_back(Shell->RegisterEditorWindow(
                EditorWindowDescriptor{
                    .Id = std::move(id),
                    .MenuPath = std::move(menuPath),
                    .Title = "K-Means",
                    .OpenByDefault = false,
                    .Draw =
                        [this, kind, windowTitle = callbackTitle](
                            bool& open,
                            const SandboxEditorContext& context)
                        {
                            DrawKMeansWindow(open, context, kind, windowTitle);
                        },
                }));
        }

        void RegisterProgressivePoissonWindow(
            const Runtime::EditorDomainWindowKind kind,
            std::string id,
            std::vector<std::string> menuPath,
            std::string windowTitle)
        {
            const std::string callbackTitle = windowTitle;
            Handles.push_back(Shell->RegisterEditorWindow(
                EditorWindowDescriptor{
                    .Id = std::move(id),
                    .MenuPath = std::move(menuPath),
                    .Title = "Progressive Poisson",
                    .OpenByDefault = false,
                    .Draw =
                        [this, kind, windowTitle = callbackTitle](
                            bool& open,
                            const SandboxEditorContext& context)
                        {
                            DrawProgressivePoissonWindow(
                                open,
                                context,
                                kind,
                                windowTitle);
                        },
                }));
        }

        void RegisterParameterizationWindow()
        {
            Handles.push_back(Shell->RegisterEditorWindow(
                EditorWindowDescriptor{
                    .Id = "mesh.processing.parameterize_uv",
                    .MenuPath = {"Mesh", "Processing"},
                    .Title = "Parameterize (UV)",
                    .OpenByDefault = false,
                    .Draw =
                        [this](
                            bool& open,
                            const SandboxEditorContext& context)
                        {
                            DrawParameterizationWindow(open, context);
                        },
                }));
        }

        void RegisterPointSetConsolidationWindow(
            std::string id,
            std::vector<std::string> menuPath,
            std::string windowTitle)
        {
            const std::string callbackTitle = windowTitle;
            Handles.push_back(Shell->RegisterEditorWindow(
                EditorWindowDescriptor{
                    .Id = std::move(id),
                    .MenuPath = std::move(menuPath),
                    .Title = "Consolidate (LOP/WLOP/CLOP/EAR)",
                    .OpenByDefault = false,
                    .Draw =
                        [this, windowTitle = callbackTitle](
                            bool& open,
                            const SandboxEditorContext& context)
                        {
                            DrawPointCloudConsolidationWindow(
                                open,
                                context,
                                windowTitle);
                        },
                }));
        }

        static bool DrawPointCloudConsolidationStrategy(
            SandboxPointCloudConsolidationPanelConfig& config)
        {
            const auto options =
                SandboxPointCloudConsolidationStrategyOptions();
            const auto current = std::find_if(
                options.begin(),
                options.end(),
                [&config](
                    const SandboxPointCloudConsolidationStrategyOption& option)
                {
                    return option.Strategy == config.Strategy;
                });
            const char* preview = current != options.end()
                ? current->Label.data()
                : "Unsupported";
            bool changed = false;
            if (ImGui::BeginCombo(
                    "Strategy##PointCloudConsolidation", preview))
            {
                for (const auto& option : options)
                {
                    const bool selected = option.Strategy == config.Strategy;
                    if (!option.Available)
                        ImGui::BeginDisabled();
                    if (ImGui::Selectable(
                            option.Label.data(), selected) &&
                        option.Available)
                    {
                        config.Strategy = option.Strategy;
                        changed = true;
                    }
                    if (!option.Available)
                    {
                        ImGui::SameLine();
                        ImGui::TextDisabled("unavailable");
                        ImGui::EndDisabled();
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        static bool DrawPointCloudConsolidationNormalControls(
            SandboxPointCloudConsolidationPanelConfig& config)
        {
            bool changed = false;
            const bool requireAuthored =
                config.NormalSource ==
                Runtime::PointCloudConsolidationNormalSource::
                    RequireAuthored;
            if (ImGui::BeginCombo(
                    "Normal source##PointCloudConsolidation",
                    requireAuthored
                        ? "Require authored"
                        : "Authored or estimate"))
            {
                if (ImGui::Selectable(
                        "Authored or estimate##PointCloudConsolidation",
                        !requireAuthored))
                {
                    config.NormalSource =
                        Runtime::PointCloudConsolidationNormalSource::
                            AuthoredOrEstimate;
                    changed = true;
                }
                if (!requireAuthored)
                    ImGui::SetItemDefaultFocus();
                if (ImGui::Selectable(
                        "Require authored##PointCloudConsolidation",
                        requireAuthored))
                {
                    config.NormalSource =
                        Runtime::PointCloudConsolidationNormalSource::
                            RequireAuthored;
                    changed = true;
                }
                if (requireAuthored)
                    ImGui::SetItemDefaultFocus();
                ImGui::EndCombo();
            }
            changed |= ImGui::InputDouble(
                "Normal angle (radians)##PointCloudConsolidation",
                &config.NormalAngleRadians,
                0.0,
                0.0,
                "%.6g");
            changed |= ImGui::InputScalar(
                "Normal refinement rounds##PointCloudConsolidation",
                ImGuiDataType_U32,
                &config.NormalRefinementRounds);
            return changed;
        }

        static bool DrawPointCloudConsolidationControls(
            SandboxPointCloudConsolidationPanelConfig& config)
        {
            bool changed = false;
            using Backend = Runtime::PointCloudConsolidationBackend;
            int compute = config.Backend == Backend::VulkanCompute ? 1 : 0;
            if (ImGui::Combo("Backend##PointCloudConsolidation", &compute, "CPU\0Vulkan\0"))
            {
                config.Backend = compute ? Backend::VulkanCompute : Backend::CpuReference;
                changed = true;
            }
            changed |= DrawPointCloudConsolidationStrategy(config);
            if (compute == 0)
            {
                if (config.Backend == Backend::CpuLBVH && config.Strategy != Runtime::PointCloudConsolidationStrategy::Lop)
                { config.Backend = Backend::CpuReference; changed = true; }
                const auto label = [](Backend backend) {
                    if (backend == Backend::CpuLBVH) return "CPU LBVH (cached)";
                    if (backend == Backend::VulkanLBVH) return "Vulkan LBVH";
                    return "CPU reference neighborhoods";
                };
                if (ImGui::BeginCombo("Acceleration##PointCloudConsolidation", label(config.Backend)))
                {
                    for (auto backend : {Backend::CpuReference, Backend::CpuLBVH, Backend::VulkanLBVH})
                    {
                        if (backend == Backend::CpuLBVH && config.Strategy != Runtime::PointCloudConsolidationStrategy::Lop) continue;
                        if (ImGui::Selectable(label(backend), config.Backend == backend))
                        { config.Backend = backend; changed = true; }
                    }
                    ImGui::EndCombo();
                }
            }
            if (config.Backend == Runtime::PointCloudConsolidationBackend::VulkanLBVH)
            {
                changed |= ImGui::InputScalar("GPU query batch size##LOP",ImGuiDataType_U32,&config.GpuQueryBatchSize);
                changed |= ImGui::InputScalar("GPU radius capacity##LOP",ImGuiDataType_U32,&config.GpuRadiusCapacity);
            }
            ImGui::SeparatorText("Shared parameters");
            const bool manualRadius = config.SupportRadiusMode ==
                Runtime::PointCloudConsolidationSupportRadiusMode::Manual;
            if (ImGui::BeginCombo(
                    "Support radius mode##PointCloudConsolidation",
                    manualRadius ? "Manual" : "Auto"))
            {
                if (ImGui::Selectable(
                        "Auto##PointCloudConsolidation",
                        !manualRadius))
                {
                    config.SupportRadiusMode = Runtime::
                        PointCloudConsolidationSupportRadiusMode::Auto;
                    changed = true;
                }
                if (!manualRadius)
                    ImGui::SetItemDefaultFocus();
                if (ImGui::Selectable(
                        "Manual##PointCloudConsolidation",
                        manualRadius))
                {
                    config.SupportRadiusMode = Runtime::
                        PointCloudConsolidationSupportRadiusMode::Manual;
                    changed = true;
                }
                if (manualRadius)
                    ImGui::SetItemDefaultFocus();
                ImGui::EndCombo();
            }
            ImGui::BeginDisabled(!manualRadius);
            changed |= ImGui::InputDouble(
                "Support radius (h)##PointCloudConsolidation",
                &config.SupportRadius,
                0.0,
                0.0,
                "%.6g");
            ImGui::EndDisabled();
            if (!manualRadius)
            {
                ImGui::TextDisabled(
                    "Auto profiles the selected position property on the worker.");
            }
            changed |= ImGui::InputScalar(
                "Maximum sampled support##PointCloudConsolidation",
                ImGuiDataType_U32,
                &config.MaxSupportNeighbors);
            changed |= ImGui::InputScalar(
                "Maximum predicted contributions##PointCloudConsolidation",
                ImGuiDataType_U64,
                &config.MaxPredictedContributions);
            changed |= ImGui::InputDouble(
                "Repulsion weight (mu)##PointCloudConsolidation",
                &config.RepulsionWeight,
                0.0,
                0.0,
                "%.6g");
            changed |= ImGui::InputScalar(
                "Max iterations##PointCloudConsolidation",
                ImGuiDataType_U32,
                &config.MaxIterations);
            changed |= ImGui::InputDouble(
                "Convergence tolerance##PointCloudConsolidation",
                &config.ConvergenceTolerance,
                0.0,
                0.0,
                "%.3e");
            changed |= ImGui::InputScalar(
                "Target point count (0 keeps input)##PointCloudConsolidation",
                ImGuiDataType_U32,
                &config.TargetPointCount);
            changed |= ImGui::InputScalar(
                "Seed##PointCloudConsolidation",
                ImGuiDataType_U32,
                &config.Seed);

            ImGui::SeparatorText("Strategy parameters");
            using Strategy = Runtime::PointCloudConsolidationStrategy;
            switch (config.Strategy)
            {
            case Strategy::Lop:
                ImGui::TextDisabled(
                    "LOP uses unit density weights and the shared parameters.");
                break;
            case Strategy::Wlop:
                changed |= ImGui::Checkbox(
                    "Anisotropic weighting##PointCloudConsolidation",
                    &config.WlopAnisotropic);
                if (config.WlopAnisotropic)
                {
                    changed |=
                        DrawPointCloudConsolidationNormalControls(config);
                }
                break;
            case Strategy::Clop:
                changed |= ImGui::InputScalar(
                    "Mixture components##PointCloudConsolidation",
                    ImGuiDataType_U32,
                    &config.ClopMixtureComponentCount);
                changed |= ImGui::InputScalar(
                    "Mixture max iterations##PointCloudConsolidation",
                    ImGuiDataType_U32,
                    &config.ClopMixtureMaxIterations);
                changed |= ImGui::InputDouble(
                    "Mixture relative tolerance##PointCloudConsolidation",
                    &config.ClopMixtureRelativeTolerance,
                    0.0,
                    0.0,
                    "%.3e");
                changed |= ImGui::InputDouble(
                    "Covariance floor##PointCloudConsolidation",
                    &config.ClopCovarianceFloor,
                    0.0,
                    0.0,
                    "%.3e");
                break;
            case Strategy::Ear:
                changed |=
                    DrawPointCloudConsolidationNormalControls(config);
                changed |= ImGui::InputDouble(
                    "Edge sensitivity##PointCloudConsolidation",
                    &config.EarEdgeSensitivity,
                    0.0,
                    0.0,
                    "%.6g");
                break;
            }
            return changed;
        }

        static void DrawPointCloudConsolidationResult(
            const std::optional<Runtime::PointCloudConsolidationResult>& result)
        {
            ImGui::SeparatorText("Last run diagnostics");
            if (!result.has_value())
            {
                ImGui::TextDisabled(
                    "No point-cloud consolidation has run this session.");
                return;
            }

            const SandboxPointCloudConsolidationResultSummary summary =
                BuildSandboxPointCloudConsolidationResultSummary(*result);
            ImGui::Text("Status: %s", summary.Status.c_str());
            ImGui::Text(
                "Strategy: %s  implementation: %s",
                summary.StrategyToken.c_str(),
                summary.ImplementationId.c_str());
            ImGui::Text(
                "Backend: requested %s  actual %s",
                summary.RequestedBackend.c_str(),
                summary.ActualBackend.c_str());
            if (!summary.BackendDiagnostic.empty())
            {
                ImGui::TextWrapped(
                    summary.FellBackToCpu
                        ? "Backend fallback: %s"
                        : "Backend diagnostic: %s",
                    summary.BackendDiagnostic.c_str());
            }
            if (summary.SupportRadiusAnalysisStatus != "not_run" &&
                !summary.SupportRadiusAnalysisStatus.empty())
            {
                ImGui::Text(
                    "Support radius: %.6g  source: %s  status: %s",
                    summary.ResolvedSupportRadius,
                    summary.SupportRadiusSource.c_str(),
                    summary.SupportRadiusAnalysisStatus.c_str());
                if (summary.SupportRadiusSource == "recommended")
                {
                    if (summary.SupportRadiusWorkloadAdjusted)
                    {
                        ImGui::Text(
                            "Profile v%u: %u samples  requested rank %u -> selected %u (%s)",
                            summary.SupportRadiusEstimatorVersion,
                            summary.SupportRadiusProfileSampleCount,
                            summary.SupportRadiusRequestedNeighborRank,
                            summary.SupportRadiusNeighborRank,
                            "work-budget backoff");
                        ImGui::Text(
                            "%s distance %.6g  bbox %.6g",
                            summary.SupportRadiusQuantile.c_str(),
                            summary.SupportRadiusNeighborDistance,
                            summary.SupportRadiusBoundingBoxDiagonal);
                    }
                    else
                    {
                        ImGui::Text(
                            "Profile v%u: %u samples  rank %u %s distance %.6g  bbox %.6g",
                            summary.SupportRadiusEstimatorVersion,
                            summary.SupportRadiusProfileSampleCount,
                            summary.SupportRadiusNeighborRank,
                            summary.SupportRadiusQuantile.c_str(),
                            summary.SupportRadiusNeighborDistance,
                            summary.SupportRadiusBoundingBoxDiagonal);
                    }
                }
                else
                {
                    ImGui::Text(
                        "Profile v%u: %u samples  bbox %.6g",
                        summary.SupportRadiusEstimatorVersion,
                        summary.SupportRadiusProfileSampleCount,
                        summary.SupportRadiusBoundingBoxDiagonal);
                }
                ImGui::Text(
                    "Sampled support p50 %.1f  p95 %.1f  max %u",
                    summary.SupportNeighborsP50,
                    summary.SupportNeighborsP95,
                    summary.SupportNeighborsMax);
                ImGui::Text(
                    "Predicted queries %llu  contributions %llu",
                    static_cast<unsigned long long>(
                        summary.PredictedSupportQueryCount),
                    static_cast<unsigned long long>(
                        summary.PredictedContributionCount));
            }
            if (!summary.Queued)
            {
                ImGui::Text(
                    "Points: %u -> %u  iterations: %u",
                    summary.InputPointCount,
                    summary.OutputPointCount,
                    summary.Iterations);
                ImGui::Text(
                    "Converged: %s  displacement avg %.6g  max %.6g",
                    summary.Converged ? "yes" : "no",
                    summary.AverageDisplacement,
                    summary.MaxDisplacement);
                if (summary.UsedAuthoredNormals ||
                    summary.EstimatedNormals ||
                    summary.NormalRefinementIterations != 0u)
                {
                    const char* normalSource =
                        summary.UsedAuthoredNormals && summary.EstimatedNormals
                        ? "authored + estimated"
                        : summary.UsedAuthoredNormals
                            ? "authored"
                            : summary.EstimatedNormals
                                ? "estimated"
                                : "none";
                    ImGui::Text(
                        "Normals: %s  refinement rounds: %u",
                        normalSource,
                        summary.NormalRefinementIterations);
                }
                if (summary.InsertedPointCount != 0u)
                {
                    ImGui::Text(
                        "Inserted points: %u",
                        summary.InsertedPointCount);
                }
            }
            else
            {
                ImGui::TextDisabled(
                    "In progress: radius analysis -> backend execution -> publication");
            }
            if (!summary.Message.empty())
                ImGui::TextWrapped("%s", summary.Message.c_str());
        }

        static void ClearPointCloudConsolidationBindings(
            PointCloudConsolidationState& state,
            const std::uint32_t stableEntityId)
        {
            state.Properties.InputPositions = {};
            state.Properties.InputNormals.reset();
            state.Properties.OutputPositions = {};
            state.Properties.OutputNormals.reset();
            state.OutputPositionName.fill('\0');
            state.OutputNormalName.fill('\0');
            state.BoundStableEntityId = stableEntityId;
            state.BindingsInitialized = true;
            state.PublishNormals = false;
        }

        static void BindPointCloudConsolidationPosition(
            PointCloudConsolidationState& state,
            const Runtime::EditorPropertyCatalogModel& catalog,
            const Runtime::EditorPropertyCatalogRow& positions)
        {
            state.Properties.InputPositions = positions.Descriptor;
            state.Properties.OutputPositions = positions.Descriptor;
            SetPropertyNameBuffer(
                state.OutputPositionName,
                positions.Descriptor.Name);

            const Runtime::EditorPropertyCatalogRow* normals =
                FindPreferredPointSetNormal(catalog, positions);
            if (normals != nullptr)
            {
                state.Properties.InputNormals = normals->Descriptor;
                state.Properties.OutputNormals = normals->Descriptor;
                SetPropertyNameBuffer(
                    state.OutputNormalName,
                    normals->Descriptor.Name);
                state.PublishNormals = true;
            }
            else
            {
                state.Properties.InputNormals.reset();
                state.Properties.OutputNormals.reset();
                state.OutputNormalName.fill('\0');
                state.PublishNormals = false;
            }
        }

        static void EnsurePointCloudConsolidationBindings(
            PointCloudConsolidationState& state,
            const Runtime::EditorInspectorModel& inspector)
        {
            const std::uint32_t stableEntityId = inspector.HasEntity
                ? inspector.Entity.StableEntityId
                : 0u;
            if (stableEntityId == 0u)
            {
                if (!state.BindingsInitialized ||
                    state.BoundStableEntityId != 0u)
                {
                    ClearPointCloudConsolidationBindings(state, 0u);
                }
                return;
            }

            const Runtime::EditorPropertyCatalogModel& catalog =
                inspector.PropertyCatalog;
            const Runtime::EditorPropertyCatalogRow* positions =
                FindPointSetProperty(
                    catalog,
                    state.Properties.InputPositions);
            if (!state.BindingsInitialized ||
                state.BoundStableEntityId != stableEntityId ||
                positions == nullptr)
            {
                ClearPointCloudConsolidationBindings(state, stableEntityId);
                positions = FindPreferredPointSetPosition(catalog);
                if (positions != nullptr)
                {
                    BindPointCloudConsolidationPosition(
                        state,
                        catalog,
                        *positions);
                }
                return;
            }

            state.Properties.OutputPositions.Domain =
                positions->Descriptor.Domain;
            state.Properties.OutputPositions.ValueKind =
                Geometry::PropertyValueKind::Vec3;
            if (state.Properties.InputNormals.has_value())
            {
                const Runtime::EditorPropertyCatalogRow* normals =
                    FindPointSetProperty(
                        catalog,
                        *state.Properties.InputNormals);
                if (normals == nullptr ||
                    !IsCompatiblePointSetNormal(*normals, *positions))
                {
                    state.Properties.InputNormals.reset();
                }
            }
        }

        static void DrawPointCloudConsolidationSourceHeader(
            const Runtime::EditorInspectorModel& inspector)
        {
            ImGui::TextDisabled(
                "Minimum contract: one finite vec3 Position property "
                "on any element domain.");
            if (inspector.HasEntity)
            {
                ImGui::Text(
                    "Selected: %s (%u)",
                    inspector.Entity.Name.c_str(),
                    inspector.Entity.StableEntityId);
                ImGui::Text(
                    "Geometry provenance: %s",
                    Runtime::DebugNameForEditorGeometryDomain(
                        inspector.Geometry.Domain));
            }
            else
            {
                ImGui::TextDisabled("Selected: none");
            }
            DrawDiagnostics(inspector.Diagnostics);
            DrawDiagnostics(inspector.PropertyCatalog.Diagnostics);
        }

        static void DrawPointCloudConsolidationPropertySlots(
            PointCloudConsolidationState& state,
            const Runtime::EditorPropertyCatalogModel& catalog)
        {
            ImGui::SeparatorText("Property slots");
            const Runtime::EditorPropertyCatalogRow* positions =
                FindPointSetProperty(
                    catalog,
                    state.Properties.InputPositions);
            const std::string positionPreview = positions != nullptr
                ? PointSetPropertyLabel(*positions)
                : std::string{"Unbound"};
            if (ImGui::BeginCombo(
                    "Position##PointCloudConsolidation",
                    positionPreview.c_str()))
            {
                for (const Runtime::EditorPropertyCatalogRow& row :
                     catalog.Rows)
                {
                    if (!IsPointSetVec3Property(row))
                        continue;
                    const bool selected = positions == &row;
                    const std::string label = PointSetPropertyLabel(row);
                    if (ImGui::Selectable(label.c_str(), selected))
                    {
                        BindPointCloudConsolidationPosition(
                            state,
                            catalog,
                            row);
                        positions = &row;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (positions == nullptr)
            {
                ImGui::TextDisabled(
                    "The selected entity exposes no bindable vec3 property.");
                return;
            }

            const Runtime::EditorPropertyCatalogRow* normals = nullptr;
            if (state.Properties.InputNormals.has_value())
            {
                normals = FindPointSetProperty(
                    catalog,
                    *state.Properties.InputNormals);
                if (normals != nullptr &&
                    !IsCompatiblePointSetNormal(*normals, *positions))
                {
                    normals = nullptr;
                }
            }
            const std::string normalPreview = normals != nullptr
                ? PointSetPropertyLabel(*normals)
                : std::string{"None (estimate when needed)"};
            if (ImGui::BeginCombo(
                    "Normal (optional)##PointCloudConsolidation",
                    normalPreview.c_str()))
            {
                if (ImGui::Selectable(
                        "None (estimate when needed)",
                        normals == nullptr))
                {
                    state.Properties.InputNormals.reset();
                    normals = nullptr;
                }
                for (const Runtime::EditorPropertyCatalogRow& row :
                     catalog.Rows)
                {
                    if (!IsCompatiblePointSetNormal(row, *positions))
                        continue;
                    const bool selected = normals == &row;
                    const std::string label = PointSetPropertyLabel(row);
                    if (ImGui::Selectable(label.c_str(), selected))
                    {
                        state.Properties.InputNormals = row.Descriptor;
                        normals = &row;
                        if (state.OutputNormalName.front() == '\0')
                        {
                            SetPropertyNameBuffer(
                                state.OutputNormalName,
                                row.Descriptor.Name);
                        }
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::TextDisabled(
                "Outputs stay on %s; topology-bearing domains require "
                "a count-preserving target.",
                Runtime::ToString(positions->Descriptor.Domain).data());
            (void)ImGui::InputText(
                "Output Position##PointCloudConsolidation",
                state.OutputPositionName.data(),
                state.OutputPositionName.size());
            state.Properties.OutputPositions = Runtime::GeometryPropertyRef{
                .Domain = positions->Descriptor.Domain,
                .Name = std::string{state.OutputPositionName.data()},
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            };

            if (ImGui::Checkbox(
                    "Publish normals##PointCloudConsolidation",
                    &state.PublishNormals) &&
                state.PublishNormals &&
                state.OutputNormalName.front() == '\0')
            {
                SetPropertyNameBuffer(
                    state.OutputNormalName,
                    normals != nullptr
                        ? std::string_view{normals->Name}
                        : std::string_view{"lop:normal"});
            }
            if (state.PublishNormals)
            {
                (void)ImGui::InputText(
                    "Output Normal##PointCloudConsolidation",
                    state.OutputNormalName.data(),
                    state.OutputNormalName.size());
                state.Properties.OutputNormals = Runtime::GeometryPropertyRef{
                    .Domain = positions->Descriptor.Domain,
                    .Name = std::string{state.OutputNormalName.data()},
                    .ValueKind = Geometry::PropertyValueKind::Vec3,
                };
            }
            else
            {
                state.Properties.OutputNormals.reset();
            }
        }

        void DrawPointCloudConsolidationWindow(
            bool& open,
            const SandboxEditorContext& context,
            const std::string& windowTitle)
        {
            const auto& service = PointCloudServiceFrame(context);
            ImGui::SetNextWindowSize(
                ImVec2(440.0f, 660.0f),
                ImGuiCond_FirstUseEver);
            if (ImGui::Begin(
                    windowTitle.c_str(),
                    &open))
            {
                DrawProcessingEntity("Entity##Processing", context, PointCloudConsolidation.Input.Entity,
                    PointCloudConsolidation.Input.PreviousSelection);
                const auto& inspector = GetInspectorModel(context, PointCloudConsolidation.Input.Entity);
                DrawPointCloudConsolidationSourceHeader(inspector);
                EnsurePointCloudConsolidationBindings(
                    PointCloudConsolidation,
                    inspector);
                DrawPointCloudConsolidationPropertySlots(
                    PointCloudConsolidation,
                    inspector.PropertyCatalog);

                if (!PointCloudConsolidation.Initialized ||
                    !PointCloudConsolidation.Dirty)
                {
                    const auto active =
                        Runtime::GetEditorPointCloudConsolidationConfig(
                            service.Commands);
                    if (active.has_value())
                    {
                        PointCloudConsolidation.Draft = *active;
                        PointCloudConsolidation.Initialized = true;
                    }
                }
                if (service.Results
                        .LastPointCloudConsolidationResult.has_value())
                {
                    PointCloudConsolidation.LastResult =
                        *service.Results
                             .LastPointCloudConsolidationResult;
                }

                PointCloudConsolidation.Dirty |=
                    DrawPointCloudConsolidationControls(
                        PointCloudConsolidation.Draft);
                const std::uint32_t stableEntityId = inspector.HasEntity
                    ? inspector.Entity.StableEntityId
                    : 0u;
                const bool configValid =
                    Runtime::IsValidEditorPointCloudConsolidationConfig(
                        PointCloudConsolidation.Draft);
                const auto request =
                    BuildSandboxPointCloudConsolidationPanelApplyRequest(
                        stableEntityId,
                        PointCloudConsolidation.Properties,
                        PointCloudConsolidation.Draft);
                Runtime::PointCloudConsolidationAvailability availability{};
                if (request.has_value())
                {
                    availability =
                        Runtime::ResolveEditorPointCloudConsolidationAvailability(
                            service.Commands,
                            service.PointCloudConsolidation,
                            request->Execute);
                }
                else
                {
                    availability.Message = stableEntityId == 0u
                        ? "Select a geometry entity to bind point-set properties."
                        : "Bind valid Position/output slots on one element domain.";
                }
                if (!configValid)
                    ImGui::TextDisabled(
                        "Draft contains an unsupported or out-of-range value.");
                else if (PointCloudConsolidation.Dirty)
                    ImGui::TextDisabled("Draft has unapplied changes.");

                const bool configAvailable =
                    context.ProcessingConfigCommandsAvailable;
                ImGui::BeginDisabled(!configAvailable || !configValid);
                if (ImGui::Button(
                        "Apply configuration##PointCloudConsolidation"))
                {
                    PointCloudConsolidation.LastConfigApply =
                        Runtime::ApplyEditorPointCloudConsolidationConfig(
                            service.Commands,
                            PointCloudConsolidation.Draft,
                            "sandbox.point_cloud_consolidation.panel");
                    if (PointCloudConsolidation.LastConfigApply->Succeeded())
                        PointCloudConsolidation.Dirty = false;
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(!configAvailable);
                if (ImGui::Button(
                        "Reload active##PointCloudConsolidation"))
                {
                    PointCloudConsolidation.Initialized = false;
                    PointCloudConsolidation.Dirty = false;
                }
                ImGui::EndDisabled();

                const bool canRun = configAvailable && request.has_value() &&
                    availability.Available;
                ImGui::BeginDisabled(!canRun);
                if (ImGui::Button(
                        "Consolidate selected property set##PointCloudConsolidation"))
                {
                    SandboxPointCloudConsolidationPanelActionResult action =
                        ApplySandboxPointCloudConsolidationPanelAction(
                            context,
                            stableEntityId,
                            PointCloudConsolidation.Properties,
                            PointCloudConsolidation.Draft);
                    PointCloudConsolidation.LastConfigApply =
                        std::move(action.Config);
                    if (action.Submission.has_value())
                    {
                        PointCloudConsolidation.LastResult =
                            std::move(action.Submission);
                    }
                    if (PointCloudConsolidation.LastConfigApply->Succeeded())
                        PointCloudConsolidation.Dirty = false;
                }
                ImGui::EndDisabled();
                if (!configAvailable)
                {
                    ImGui::TextDisabled(
                        "Point-cloud consolidation config control is unavailable.");
                }
                if (!availability.Available && !availability.Message.empty())
                {
                    ImGui::TextDisabled("%s", availability.Message.c_str());
                }

                std::string displayDiagnostic;
                DrawProcessingPropertyShowButton(context, stableEntityId,
                    PointCloudConsolidation.Properties.OutputPositions, displayDiagnostic);
                if (PointCloudConsolidation.Properties.OutputNormals)
                    DrawProcessingPropertyShowButton(context, stableEntityId,
                        *PointCloudConsolidation.Properties.OutputNormals, displayDiagnostic);
                if (!displayDiagnostic.empty()) ImGui::Text("Display: %s", displayDiagnostic.c_str());

                const Runtime::EditorDocumentModel history =
                    context.Document != nullptr
                    ? *context.Document
                    : Runtime::EditorDocumentModel{};
                const bool historyAvailable =
                    context.DocumentCommands.Available();
                const bool canUndo = history.CanUndo &&
                    IsConsolidationHistoryLabel(history.UndoLabel);
                const bool canRedo = history.CanRedo &&
                    IsConsolidationHistoryLabel(history.RedoLabel);
                ImGui::BeginDisabled(!canUndo || !historyAvailable);
                if (ImGui::Button(
                        "Undo consolidation##PointCloudConsolidation"))
                {
                    (void)context.DocumentCommands.Undo();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(!canRedo || !historyAvailable);
                if (ImGui::Button(
                        "Redo consolidation##PointCloudConsolidation"))
                {
                    (void)context.DocumentCommands.Redo();
                }
                ImGui::EndDisabled();

                if (PointCloudConsolidation.LastConfigApply.has_value() &&
                    !PointCloudConsolidation.LastConfigApply->Succeeded())
                {
                    ImGui::TextDisabled(
                        "Configuration preview/apply was rejected.");
                }
                DrawPointCloudConsolidationResult(
                    PointCloudConsolidation.LastResult);
            }
            ImGui::End();
        }

        void DrawKMeansWindow(
            bool& open,
            const SandboxEditorContext& context,
            const Runtime::EditorDomainWindowKind kind,
            const std::string& windowTitle)
        {
            ImGui::SetNextWindowSize(
                ImVec2(340.0f, 300.0f),
                ImGuiCond_FirstUseEver);
            if (ImGui::Begin(windowTitle.c_str(), &open))
            {
                DrawProcessingEntity("Entity##Processing", context, KMeans.Input.Entity,
                    KMeans.Input.PreviousSelection, kind);
                const auto& model = GetDomainWindowModel(context, kind, KMeans.Input.Entity);
                // The header already includes processing diagnostics; render
                // them only once.
                DrawDomainWindowHeader(model);
                DrawKMeansControls(model, context);
            }
            ImGui::End();
        }

        void DrawKMeansControls(
            const Runtime::EditorDomainWindowModel& model,
            const SandboxEditorContext& context)
        {
            const auto& service = PointCloudServiceFrame(context);
            ImGui::SeparatorText("K-Means execution");

            if (service.Results.LastKMeansResult.has_value())
                KMeans.LastResult = *service.Results.LastKMeansResult;
            if (!KMeans.Initialized || !KMeans.Dirty)
            {
                const std::optional<Runtime::ClusteringConfig> active =
                    Runtime::GetEditorClusteringConfig(service.Commands);
                if (active.has_value())
                {
                    KMeans.Backend = IndexOfOption(kKMeansBackends, active->Backend);
                    KMeans.ClusterCount = static_cast<std::int32_t>(
                        active->Parameters.ClusterCount);
                    KMeans.MaxIterations = static_cast<std::int32_t>(
                        active->Parameters.MaxIterations);
                    KMeans.Seed = active->Parameters.Seed;
                    KMeans.UseHierarchicalInitialization =
                        active->Parameters.Initialization ==
                        Runtime::KMeansInitialization::Hierarchical;
                    if (active->Properties) KMeans.Properties = *active->Properties;
                    KMeans.Initialized = true;
                }
            }
            if (KMeans.Entity != model.SelectedStableId)
            {
                KMeans.Entity = model.SelectedStableId;
                if (!FindPointSetProperty(model.PropertyCatalog, KMeans.Properties.InputPositions))
                {
                    if (const auto* preferred = FindPreferredPointSetPosition(model.PropertyCatalog))
                    {
                        KMeans.Properties = Runtime::MakeKMeansPropertyRefs(preferred->Descriptor.Domain);
                        KMeans.Properties.InputPositions = preferred->Descriptor;
                        KMeans.Dirty = true;
                    }
                }
            }
            ImGui::SeparatorText("Input properties");
            if (const auto* row = DrawPointSetPositionInput(
                    "Positions##KMeans", model.PropertyCatalog, KMeans.Properties.InputPositions))
            {
                if (row->Descriptor.Domain != KMeans.Properties.InputPositions.Domain)
                    KMeans.Properties = Runtime::MakeKMeansPropertyRefs(row->Descriptor.Domain);
                KMeans.Properties.InputPositions = row->Descriptor;
                KMeans.Dirty = true;
            }
            ImGui::SeparatorText("Output properties");
            KMeans.Dirty |= DrawProcessingPropertyName("Labels##KMeans", KMeans.Properties.OutputLabels.Name);
            KMeans.Dirty |= DrawProcessingPropertyName("Colors##KMeans", KMeans.Properties.OutputColors.Name);
            bool scalarLabels = KMeans.Properties.OutputScalarLabels.has_value();
            if (ImGui::Checkbox("Publish scalar labels##KMeans", &scalarLabels))
            {
                if (scalarLabels) KMeans.Properties.OutputScalarLabels = Runtime::GeometryPropertyRef{
                    KMeans.Properties.InputPositions.Domain, "v:kmeans_scalar_label", Geometry::PropertyValueKind::Float};
                else KMeans.Properties.OutputScalarLabels.reset();
                KMeans.Dirty = true;
            }
            if (scalarLabels) KMeans.Dirty |= DrawProcessingPropertyName("Scalar labels##KMeans", KMeans.Properties.OutputScalarLabels->Name);

            KMeans.Backend = std::clamp(
                KMeans.Backend,
                0,
                static_cast<std::int32_t>(kKMeansBackends.size() - 1u));
            const Runtime::ClusteringBackend previewBackend =
                OptionFromIndex(kKMeansBackends, KMeans.Backend);
            if (ImGui::BeginCombo(
                    "Backend##KMeans",
                    Runtime::ToString(previewBackend).data()))
            {
                for (std::size_t index = 0u;
                     index < kKMeansBackends.size();
                     ++index)
                {
                    const bool selected =
                        KMeans.Backend == static_cast<std::int32_t>(index);
                    if (ImGui::Selectable(
                            Runtime::ToString(kKMeansBackends[index]).data(),
                            selected))
                    {
                        KMeans.Backend = static_cast<std::int32_t>(index);
                        KMeans.Dirty = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            bool configChanged = ImGui::DragInt(
                "Clusters##KMeans",
                &KMeans.ClusterCount,
                1.0f,
                1,
                1024);
            configChanged |= ImGui::DragInt(
                "Max iterations##KMeans",
                &KMeans.MaxIterations,
                1.0f,
                1,
                4096);
            configChanged |= ImGui::InputScalar(
                "Seed##KMeans",
                ImGuiDataType_U32,
                &KMeans.Seed);
            KMeans.ClusterCount = std::clamp(KMeans.ClusterCount, 1, 1024);
            KMeans.MaxIterations =
                std::clamp(KMeans.MaxIterations, 1, 4096);
            configChanged |= ImGui::Checkbox(
                "Hierarchical initialization##KMeans",
                &KMeans.UseHierarchicalInitialization);
            KMeans.Dirty |= configChanged;

            const Runtime::ClusteringBackend backend =
                OptionFromIndex(kKMeansBackends, KMeans.Backend);
            const Runtime::ClusteringConfig clusteringConfig{
                .Parameters = Runtime::KMeansParameters{
                    .ClusterCount = static_cast<std::uint32_t>(
                        KMeans.ClusterCount),
                    .MaxIterations = static_cast<std::uint32_t>(
                        KMeans.MaxIterations),
                    .Seed = KMeans.Seed,
                    .Initialization = KMeans.UseHierarchicalInitialization
                        ? Runtime::KMeansInitialization::Hierarchical
                        : Runtime::KMeansInitialization::Random,
                },
                .Backend = backend,
                .Properties = KMeans.Properties,
            };

            const bool configAvailable =
                context.ProcessingConfigCommandsAvailable;
            ImGui::BeginDisabled(!configAvailable || !KMeans.Dirty);
            if (ImGui::Button("Apply configuration##KMeans"))
            {
                KMeans.LastConfigApply =
                    Runtime::ApplyEditorClusteringConfig(
                        service.Commands,
                        clusteringConfig,
                        "sandbox.clustering.panel");
                if (KMeans.LastConfigApply->Succeeded())
                    KMeans.Dirty = false;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!configAvailable);
            if (ImGui::Button("Reload active##KMeans"))
            {
                KMeans.Dirty = false;
                KMeans.Initialized = false;
            }
            ImGui::EndDisabled();

            const bool clusteringAvailable = service.ClusteringAvailable;
            const Runtime::RunKMeans request = Runtime::MakeConfiguredKMeansRequest(
                model.SelectedStableId, KMeans.Properties, clusteringConfig);
            const auto readiness = Runtime::ResolveEditorProcessingActionReadiness(
                service.Commands,
                Runtime::PreviewEditorKMeansRun(service.Commands, service.Clustering, request));
            if (DrawProcessingActionButton("Run K-Means##KMeans", readiness))
            {
                KMeans.LastConfigApply =
                    Runtime::ApplyEditorClusteringConfig(
                        service.Commands,
                        clusteringConfig,
                        "sandbox.clustering.panel.run");
                if (KMeans.LastConfigApply->Succeeded())
                {
                    KMeans.Dirty = false;
                    KMeans.LastResult = Runtime::SubmitKMeansRun(
                        service.Commands,
                        service.Clustering,
                        request);
                }
            }
            ImGui::SeparatorText("Display output properties");
            DrawProcessingPropertyShowButton(context, model.SelectedStableId, KMeans.Properties.OutputLabels, KMeans.VisualizationDiagnostic);
            DrawProcessingPropertyShowButton(context, model.SelectedStableId, KMeans.Properties.OutputColors, KMeans.VisualizationDiagnostic);
            if (KMeans.Properties.OutputScalarLabels)
                DrawProcessingPropertyShowButton(context, model.SelectedStableId, *KMeans.Properties.OutputScalarLabels, KMeans.VisualizationDiagnostic);
            if (!KMeans.VisualizationDiagnostic.empty()) ImGui::Text("Display: %s", KMeans.VisualizationDiagnostic.c_str());
            if (!clusteringAvailable)
                ImGui::TextDisabled("ClusteringService is unavailable.");
            if (!configAvailable)
                ImGui::TextDisabled(
                    "Clustering config control is unavailable.");
            if (KMeans.LastConfigApply.has_value() &&
                !KMeans.LastConfigApply->Succeeded())
            {
                ImGui::TextDisabled(
                    "Clustering config preview/apply was rejected.");
            }

            const std::optional<Runtime::KMeansRunCompleted>& result = KMeans.LastResult;
            const bool hasResult = result.has_value();
            DrawKMeansResultStatus(result);
            if (hasResult)
            {
                DrawDismissLastResultButton("Dismiss##KMeans", KMeans.LastResult, Runtime::EditorPointCloudServiceResultSlot::KMeans, service.ResultSinks.DismissResult);
            }
        }

        static void DrawKMeansResultStatus(
            const std::optional<Runtime::KMeansRunCompleted>& lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last K-Means run: none");
                return;
            }

            const Runtime::KMeansRunCompleted& result = *lastResult;
            ImGui::Text(
                "Last K-Means run: %s",
                Runtime::ToString(result.Status).data());
            ImGui::Text(
                "Domain: %s",
                Runtime::ToString(
                    result.Properties.InputPositions.Domain).data());
            ImGui::Text(
                "Backend: requested %s, actual %s",
                Runtime::ToString(result.RequestedBackend).data(),
                Runtime::ToString(result.ActualBackend).data());
            if (result.Succeeded())
            {
                ImGui::Text(
                    "Labels: %u  clusters: %u  iterations: %u",
                    result.LabelCount,
                    result.ClusterCount,
                    result.Iterations);
                ImGui::Text(
                    "Converged: %s  inertia: %.6f",
                    result.Converged ? "yes" : "no",
                    static_cast<double>(result.Inertia));
            }
            if (!result.BackendDiagnostic.empty())
            {
                ImGui::TextWrapped(
                    result.FellBackToCpu ? "Backend fallback: %s" : "Backend diagnostic: %s",
                    result.BackendDiagnostic.c_str());
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
        }

        void DrawProgressivePoissonWindow(
            bool& open,
            const SandboxEditorContext& context,
            const Runtime::EditorDomainWindowKind kind,
            const std::string& windowTitle)
        {
            ImGui::SetNextWindowSize(
                ImVec2(340.0f, 300.0f),
                ImGuiCond_FirstUseEver);
            if (ImGui::Begin(windowTitle.c_str(), &open))
            {
                DrawProcessingEntity("Entity##Processing", context, ProgressivePoisson.Input.Entity,
                    ProgressivePoisson.Input.PreviousSelection, kind);
                const auto& model = GetDomainWindowModel(context, kind, ProgressivePoisson.Input.Entity);
                // The header already includes processing diagnostics; render
                // them only once.
                DrawDomainWindowHeader(model);
                DrawProgressivePoissonControls(model, context);
            }
            ImGui::End();
        }

        static void SyncProgressivePoissonState(
            ProgressivePoissonState& state,
            const Runtime::ProgressivePoissonPlaygroundConfig& config)
        {
            state.Dimension = static_cast<std::int32_t>(config.Dimension);
            state.GridWidth = static_cast<std::int32_t>(config.GridWidth);
            state.MaxLevels = static_cast<std::int32_t>(config.MaxLevels);
            state.HashLoadFactor = static_cast<float>(config.HashLoadFactor);
            state.RadiusAlpha = static_cast<float>(config.RadiusAlpha);
            state.RandomizeGridOrigin = config.RandomizeGridOrigin;
            state.GridOriginSeed =
                static_cast<std::int32_t>(config.GridOriginSeed);
            state.ShuffleWithinLevels = config.ShuffleWithinLevels;
            state.ShuffleSeed = static_cast<std::int32_t>(config.ShuffleSeed);
            state.PrefixCount = static_cast<std::int32_t>(config.PrefixCount);
            state.Channel = IndexOfOption(kProgressivePoissonChannels, config.Channel);
            state.Backend = IndexOfOption(kProgressivePoissonBackends, config.Backend);
            state.AutoRunOnEdit = config.AutoRunOnEdit;
            state.DebounceSeconds =
                static_cast<float>(config.DebounceSeconds);
        }

        // The panel edits `float`/`int` widgets; the serialized config keeps
        // `double` knobs, so the widget values widen here and the execution
        // boundary narrows them back exactly as before.
        [[nodiscard]] Runtime::ProgressivePoissonPlaygroundConfig
        BuildProgressivePoissonConfig() const
        {
            return Runtime::ProgressivePoissonPlaygroundConfig{
                .Dimension = static_cast<std::uint32_t>(
                    ProgressivePoisson.Dimension),
                .GridWidth = static_cast<std::uint32_t>(
                    ProgressivePoisson.GridWidth),
                .MaxLevels = static_cast<std::uint32_t>(
                    ProgressivePoisson.MaxLevels),
                .HashLoadFactor = static_cast<double>(ProgressivePoisson.HashLoadFactor),
                .RadiusAlpha = static_cast<double>(ProgressivePoisson.RadiusAlpha),
                .RandomizeGridOrigin =
                    ProgressivePoisson.RandomizeGridOrigin,
                .GridOriginSeed = static_cast<std::uint32_t>(
                    ProgressivePoisson.GridOriginSeed),
                .ShuffleWithinLevels =
                    ProgressivePoisson.ShuffleWithinLevels,
                .ShuffleSeed = static_cast<std::uint32_t>(
                    ProgressivePoisson.ShuffleSeed),
                .PrefixCount = static_cast<std::uint32_t>(
                    ProgressivePoisson.PrefixCount),
                .Channel = OptionFromIndex(kProgressivePoissonChannels,
                    ProgressivePoisson.Channel),
                .Backend = OptionFromIndex(kProgressivePoissonBackends,
                    ProgressivePoisson.Backend),
                .AutoRunOnEdit = ProgressivePoisson.AutoRunOnEdit,
                .DebounceSeconds = static_cast<double>(
                    ProgressivePoisson.DebounceSeconds),
                .Positions = ProgressivePoisson.Draft.Positions,
                .Level = ProgressivePoisson.Draft.Level,
                .Rank = ProgressivePoisson.Draft.Rank,
                .SplatRadius = ProgressivePoisson.Draft.SplatRadius,
                .PrefixVisible = ProgressivePoisson.Draft.PrefixVisible,
            };
        }

        void DrawProgressivePoissonControls(
            const Runtime::EditorDomainWindowModel& model,
            const SandboxEditorContext& context)
        {
            ImGui::SeparatorText("Progressive Poisson");
            ImGui::TextWrapped(
                "Orders the selected position property's finite samples; "
                "source topology and cardinality are preserved.");
            const std::optional<Runtime::ProgressivePoissonPlaygroundConfig>
                activeConfig =
                    Runtime::GetEditorProgressivePoissonConfig(context.PointSet.Commands);
            const bool configControlAvailable =
                activeConfig.has_value() &&
                context.ProcessingConfigCommandsAvailable;
            if (!configControlAvailable)
            {
                DrawProgressivePoissonDisabledRun(
                    "Progressive Poisson requires engine config-control.");
                return;
            }

            if (context.PointSet.Results.LastProgressivePoissonResult.has_value())
            {
                ProgressivePoisson.LastResult =
                    *context.PointSet.Results.LastProgressivePoissonResult;
            }
            if (ProgressivePoisson.Synchronize(*activeConfig,
                    Runtime::SerializeProgressivePoissonPlaygroundConfig(*activeConfig)))
                SyncProgressivePoissonState(ProgressivePoisson, *activeConfig);

            ProgressivePoisson.Dimension =
                ProgressivePoisson.Dimension <= 2 ? 2 : 3;
            bool configChanged = false;
            ImGui::SeparatorText("Input properties");
            if (const auto* row = DrawPointSetPositionInput(
                    "Positions##ProgressivePoisson", model.PropertyCatalog, ProgressivePoisson.Draft.Positions))
            {
                ProgressivePoisson.Draft.Positions = row->Descriptor;
                for (auto* output : {&ProgressivePoisson.Draft.Level, &ProgressivePoisson.Draft.Rank,
                                     &ProgressivePoisson.Draft.SplatRadius, &ProgressivePoisson.Draft.PrefixVisible})
                    output->Domain = row->Descriptor.Domain;
                configChanged = true;
            }
            ImGui::SeparatorText("Output properties");
            configChanged |= DrawProcessingPropertyName("Level##ProgressivePoisson", ProgressivePoisson.Draft.Level.Name);
            configChanged |= DrawProcessingPropertyName("Rank##ProgressivePoisson", ProgressivePoisson.Draft.Rank.Name);
            configChanged |= DrawProcessingPropertyName("SplatRadius##ProgressivePoisson", ProgressivePoisson.Draft.SplatRadius.Name);
            configChanged |= DrawProcessingPropertyName("PrefixVisible##ProgressivePoisson", ProgressivePoisson.Draft.PrefixVisible.Name);

            if (ImGui::BeginCombo(
                    "Dimension##ProgressivePoisson",
                    ProgressivePoisson.Dimension == 2 ? "2D" : "3D"))
            {
                if (ImGui::Selectable(
                        "2D##ProgressivePoisson",
                        ProgressivePoisson.Dimension == 2))
                {
                    ProgressivePoisson.Dimension = 2;
                    configChanged = true;
                }
                if (ProgressivePoisson.Dimension == 2)
                    ImGui::SetItemDefaultFocus();
                if (ImGui::Selectable(
                        "3D##ProgressivePoisson",
                        ProgressivePoisson.Dimension == 3))
                {
                    ProgressivePoisson.Dimension = 3;
                    configChanged = true;
                }
                if (ProgressivePoisson.Dimension == 3)
                    ImGui::SetItemDefaultFocus();
                ImGui::EndCombo();
            }
            DrawProgressivePoissonTooltip(
                "Interpret the existing vertex positions in 2D (XY) or 3D.");

            ProgressivePoisson.GridWidth =
                std::clamp(ProgressivePoisson.GridWidth, 1, 4096);
            ProgressivePoisson.MaxLevels =
                std::clamp(ProgressivePoisson.MaxLevels, 1, 32);
            ProgressivePoisson.HashLoadFactor = std::clamp(
                ProgressivePoisson.HashLoadFactor,
                0.01f,
                16.0f);
            if (!std::isfinite(ProgressivePoisson.RadiusAlpha))
                ProgressivePoisson.RadiusAlpha = -1.0f;
            ProgressivePoisson.GridOriginSeed = std::clamp(
                ProgressivePoisson.GridOriginSeed,
                0,
                std::numeric_limits<std::int32_t>::max());
            ProgressivePoisson.ShuffleSeed = std::clamp(
                ProgressivePoisson.ShuffleSeed,
                0,
                std::numeric_limits<std::int32_t>::max());
            ProgressivePoisson.PrefixCount = std::clamp(
                ProgressivePoisson.PrefixCount,
                0,
                10'000'000);
            ProgressivePoisson.Channel = std::clamp(
                ProgressivePoisson.Channel,
                0,
                static_cast<std::int32_t>(
                    kProgressivePoissonChannels.size() - 1u));
            ProgressivePoisson.Backend = std::clamp(
                ProgressivePoisson.Backend,
                0,
                static_cast<std::int32_t>(
                    kProgressivePoissonBackends.size() - 1u));
            ProgressivePoisson.DebounceSeconds = std::clamp(
                ProgressivePoisson.DebounceSeconds,
                0.0f,
                10.0f);

            configChanged |= ImGui::DragInt(
                "Grid width##ProgressivePoisson",
                &ProgressivePoisson.GridWidth,
                1.0f,
                1,
                4096);
            DrawProgressivePoissonTooltip(
                "Spatial hash grid width used before method-side clamping.");
            configChanged |= ImGui::DragInt(
                "Max levels##ProgressivePoisson",
                &ProgressivePoisson.MaxLevels,
                1.0f,
                1,
                32);
            DrawProgressivePoissonTooltip(
                "Maximum hierarchy depth for ordering the existing finite input set.");
            configChanged |= ImGui::DragFloat(
                "Hash load##ProgressivePoisson",
                &ProgressivePoisson.HashLoadFactor,
                0.01f,
                0.01f,
                16.0f);
            DrawProgressivePoissonTooltip(
                "Target hash load factor used by the CPU reference backend.");
            configChanged |= ImGui::DragFloat(
                "Radius alpha##ProgressivePoisson",
                &ProgressivePoisson.RadiusAlpha,
                0.01f,
                -1.0f,
                0.999f);
            DrawProgressivePoissonTooltip(
                "Negative values keep the reference backend's default radius alpha.");
            configChanged |= ImGui::Checkbox(
                "Randomize grid origin##ProgressivePoisson",
                &ProgressivePoisson.RandomizeGridOrigin);
            DrawProgressivePoissonTooltip(
                "Jitter the grid origin with the configured seed.");
            configChanged |= ImGui::DragInt(
                "Grid seed##ProgressivePoisson",
                &ProgressivePoisson.GridOriginSeed,
                1.0f,
                0,
                std::numeric_limits<std::int32_t>::max());
            DrawProgressivePoissonTooltip(
                "Seed for grid-origin randomization.");
            configChanged |= ImGui::Checkbox(
                "Shuffle within levels##ProgressivePoisson",
                &ProgressivePoisson.ShuffleWithinLevels);
            DrawProgressivePoissonTooltip(
                "Shuffle accepted samples inside each progressive level.");
            configChanged |= ImGui::DragInt(
                "Shuffle seed##ProgressivePoisson",
                &ProgressivePoisson.ShuffleSeed,
                1.0f,
                0,
                std::numeric_limits<std::int32_t>::max());
            DrawProgressivePoissonTooltip(
                "Seed for deterministic level-local shuffling.");
            configChanged |= ImGui::DragInt(
                "Prefix count##ProgressivePoisson",
                &ProgressivePoisson.PrefixCount,
                1.0f,
                0,
                10'000'000);
            DrawProgressivePoissonTooltip(
                "Leading accepted input vertices to display; zero displays every accepted vertex.");
            configChanged |= ImGui::Checkbox(
                "Auto run on edit##ProgressivePoisson",
                &ProgressivePoisson.AutoRunOnEdit);
            DrawProgressivePoissonTooltip(
                "Rerun the sampler after knob edits settle.");
            configChanged |= ImGui::DragFloat(
                "Debounce seconds##ProgressivePoisson",
                &ProgressivePoisson.DebounceSeconds,
                0.01f,
                0.0f,
                10.0f);
            DrawProgressivePoissonTooltip(
                "Delay after the last edit before auto-running.");

            const Runtime::ProgressivePoissonPlaygroundChannel channel =
                OptionFromIndex(kProgressivePoissonChannels,
                    ProgressivePoisson.Channel);
            if (ImGui::BeginCombo(
                    "Color channel##ProgressivePoisson",
                    Runtime::DebugNameForProgressivePoissonChannel(
                        channel)))
            {
                for (std::size_t index = 0u;
                     index < kProgressivePoissonChannels.size();
                     ++index)
                {
                    const bool selected = ProgressivePoisson.Channel ==
                                          static_cast<std::int32_t>(index);
                    if (ImGui::Selectable(
                            Runtime::DebugNameForProgressivePoissonChannel(
                                kProgressivePoissonChannels[index]),
                            selected))
                    {
                        ProgressivePoisson.Channel =
                            static_cast<std::int32_t>(index);
                        configChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            DrawProgressivePoissonTooltip(
                "Published source-cardinality level, rank, introduction-radius, or prefix-visibility scalar; rejected inputs retain documented sentinels.");

            const Runtime::ProgressivePoissonPlaygroundBackend backend =
                OptionFromIndex(kProgressivePoissonBackends,
                    ProgressivePoisson.Backend);
            if (ImGui::BeginCombo(
                    "Backend##ProgressivePoisson",
                    Runtime::DebugNameForProgressivePoissonBackend(
                        backend)))
            {
                for (std::size_t index = 0u;
                     index < kProgressivePoissonBackends.size();
                     ++index)
                {
                    const bool selected = ProgressivePoisson.Backend ==
                                          static_cast<std::int32_t>(index);
                    if (ImGui::Selectable(
                            Runtime::DebugNameForProgressivePoissonBackend(
                                kProgressivePoissonBackends[index]),
                            selected))
                    {
                        ProgressivePoisson.Backend =
                            static_cast<std::int32_t>(index);
                        configChanged = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            DrawProgressivePoissonTooltip(
                "Request CPU reference or Vulkan compute; result status reports the actual backend and any CPU fallback reason.");

            const Runtime::EditorProgressivePoissonCommand command{
                .StableEntityId = model.SelectedStableId,
                .Config = BuildProgressivePoissonConfig(),
            };
            const auto applyConfig = [&]()
            {
                return Runtime::ApplyEditorProgressivePoissonConfig(
                    context.PointSet.Commands, command.Config);
            };
            const auto runSampler = [&]()
            {
                ProgressivePoisson.AutoRunPending = false;
                ProgressivePoisson.PendingStableEntityId = 0u;
                ProgressivePoisson.LastConfigResult = applyConfig();
                if (!ProgressivePoisson.LastConfigResult->Succeeded()) return;
                Runtime::EditorProgressivePoissonResult result =
                    Runtime::ApplyEditorProgressivePoissonCommand(
                        context.PointSet.Commands,
                        command,
                        context.PointSet.ResultSinks.ProgressivePoisson);
                ProgressivePoisson.LastResult = result;
                if (context.PointSet.ResultSinks.ProgressivePoisson)
                {
                    context.PointSet.ResultSinks.ProgressivePoisson(
                        std::move(result));
                }
            };

            if (configChanged)
            {
                ProgressivePoisson.LastConfigResult = applyConfig();
                if (ProgressivePoisson.LastConfigResult->Succeeded() &&
                    ProgressivePoisson.AutoRunOnEdit)
                {
                    ProgressivePoisson.AutoRunPending = true;
                    ProgressivePoisson.PendingStableEntityId =
                        model.SelectedStableId;
                    ProgressivePoisson.LastEditTime = ImGui::GetTime();
                }
                else
                {
                    ProgressivePoisson.AutoRunPending = false;
                    ProgressivePoisson.PendingStableEntityId = 0u;
                }
            }

            const auto readiness = Runtime::ResolveEditorProcessingActionReadiness(
                context.PointSet.Commands,
                Runtime::PreviewEditorProgressivePoissonCommand(context.PointSet.Commands, command));
            if (DrawProcessingActionButton("Run Progressive Poisson##ProgressivePoisson", readiness))
                runSampler();

            if (ProgressivePoisson.AutoRunPending && readiness.Enabled &&
                ProgressivePoisson.PendingStableEntityId ==
                    model.SelectedStableId)
            {
                const double elapsed =
                    ImGui::GetTime() - ProgressivePoisson.LastEditTime;
                if (ProgressivePoisson.AutoRunOnEdit &&
                    elapsed >= static_cast<double>(
                                   ProgressivePoisson.DebounceSeconds))
                {
                    runSampler();
                }
            }
            else if (ProgressivePoisson.AutoRunPending)
            {
                ProgressivePoisson.AutoRunPending = false;
                ProgressivePoisson.PendingStableEntityId = 0u;
            }

            ImGui::SeparatorText("Display output properties");
            for (const auto* output : {&ProgressivePoisson.Draft.Level, &ProgressivePoisson.Draft.Rank,
                                       &ProgressivePoisson.Draft.SplatRadius, &ProgressivePoisson.Draft.PrefixVisible})
                DrawProcessingPropertyShowButton(context, model.SelectedStableId, *output, ProgressivePoisson.VisualizationDiagnostic);
            if (!ProgressivePoisson.VisualizationDiagnostic.empty()) ImGui::Text("Display: %s", ProgressivePoisson.VisualizationDiagnostic.c_str());
            if (ProgressivePoisson.LastConfigResult.has_value() &&
                !ProgressivePoisson.LastConfigResult->Succeeded())
            {
                ImGui::TextWrapped("Progressive Poisson config was rejected.");
                for (const auto& diagnostic : ProgressivePoisson.LastConfigResult->LoadResult.Diagnostics)
                    ImGui::TextWrapped("%s", diagnostic.Message.c_str());
            }

            const std::optional<Runtime::EditorProgressivePoissonResult>& result =
                ProgressivePoisson.LastResult;
            const bool hasResult = result.has_value();
            DrawProgressivePoissonResultStatus(result);
            if (hasResult)
            {
                DrawDismissLastResultButton("Dismiss##ProgressivePoisson", ProgressivePoisson.LastResult,
                    Runtime::EditorPointSetResultSlot::ProgressivePoisson,
                    context.PointSet.ResultSinks.DismissResult);
            }
        }

        static bool DrawParameterizationU32(
            const char* label,
            std::uint32_t& value)
        {
            return ImGui::InputScalar(
                label,
                ImGuiDataType_U32,
                &value);
        }

        static bool DrawParameterizationUvValue(
            ParameterizationUvConfig& uv)
        {
            bool changed = false;
            changed |= ImGui::InputDouble("U", &uv.U, 0.0, 0.0, "%.8g");
            ImGui::SameLine();
            changed |= ImGui::InputDouble("V", &uv.V, 0.0, 0.0, "%.8g");
            return changed;
        }

        static const char* ParameterizationBoundaryLabel(
            const ParameterizationBoundaryPolicy boundary) noexcept
        {
            using Boundary = ParameterizationBoundaryPolicy;
            switch (boundary)
            {
            case Boundary::Circle:
                return "Circle";
            case Boundary::Square:
                return "Square";
            case Boundary::Custom:
                return "Custom pins";
            }
            return "Unsupported";
        }

        static const char* ParameterizationBffModeLabel(
            const ParameterizationBffBoundaryMode mode) noexcept
        {
            using Mode = ParameterizationBffBoundaryMode;
            switch (mode)
            {
            case Mode::AutomaticConformal:
                return "Automatic conformal";
            case Mode::TargetLengths:
                return "Target boundary lengths";
            case Mode::TargetAngles:
                return "Target boundary angles";
            }
            return "Unsupported";
        }

        static const char* ParameterizationUvRenderModeLabel(
            const ParameterizationUvRenderMode mode) noexcept
        {
            using Mode = ParameterizationUvRenderMode;
            switch (mode)
            {
            case Mode::CpuLayout:
                return "CPU layout";
            case Mode::GpuShaded:
                return "GPU shaded";
            }
            return "Unsupported";
        }

        static const char* ParameterizationUvBackgroundModeLabel(
            const ParameterizationUvBackgroundMode mode) noexcept
        {
            using Mode = ParameterizationUvBackgroundMode;
            switch (mode)
            {
            case Mode::Grid:
                return "Grid";
            case Mode::Checker:
                return "Checker";
            case Mode::TexelDensity:
                return "Texel density";
            case Mode::Texture:
                return "Selected albedo texture";
            }
            return "Unsupported";
        }

        static bool DrawParameterizationUvViewControls(
            decltype(ParameterizationPanelConfig{}.View)& config)
        {
            using RenderMode = ParameterizationUvRenderMode;
            using BackgroundMode = ParameterizationUvBackgroundMode;
            constexpr std::array<RenderMode, 2u> renderModes{
                RenderMode::CpuLayout,
                RenderMode::GpuShaded,
            };
            constexpr std::array<BackgroundMode, 4u> backgroundModes{
                BackgroundMode::Grid,
                BackgroundMode::Checker,
                BackgroundMode::TexelDensity,
                BackgroundMode::Texture,
            };

            bool changed = false;
            if (ImGui::BeginCombo(
                    "Render mode##ParameterizationUvView",
                    ParameterizationUvRenderModeLabel(config.RenderMode)))
            {
                for (const RenderMode mode : renderModes)
                {
                    const bool selected = config.RenderMode == mode;
                    if (ImGui::Selectable(
                            ParameterizationUvRenderModeLabel(mode),
                            selected))
                    {
                        config.RenderMode = mode;
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            if (ImGui::BeginCombo(
                    "Background##ParameterizationUvView",
                    ParameterizationUvBackgroundModeLabel(
                        config.BackgroundMode)))
            {
                for (const BackgroundMode mode : backgroundModes)
                {
                    const bool selected = config.BackgroundMode == mode;
                    if (ImGui::Selectable(
                            ParameterizationUvBackgroundModeLabel(mode),
                            selected))
                    {
                        config.BackgroundMode = mode;
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            changed |= ImGui::Checkbox(
                "Distortion heatmap##ParameterizationUvView",
                &config.ShowDistortionHeatmap);
            return changed;
        }

        static bool DrawParameterizationStrategy(
            Runtime::ParameterizationConfig& config)
        {
            const auto options = SandboxParameterizationStrategyOptions();
            const auto selected = std::find_if(
                options.begin(),
                options.end(),
                [&config](const SandboxParameterizationStrategyOption& option)
                {
                    return option.Strategy == config.Strategy;
                });
            const char* preview = selected == options.end()
                ? "Unsupported"
                : selected->Label.data();
            bool changed = false;
            if (ImGui::BeginCombo("Strategy##Parameterization", preview))
            {
                for (const SandboxParameterizationStrategyOption& option :
                     options)
                {
                    const bool isSelected = option.Strategy == config.Strategy;
                    if (ImGui::Selectable(option.Label.data(), isSelected))
                    {
                        config.Strategy = option.Strategy;
                        changed = true;
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        static bool DrawParameterizationLscmControls(
            ParameterizationLscmConfig& config)
        {
            bool changed = false;
            changed |= ImGui::Checkbox(
                "Choose pins automatically##Parameterization",
                &config.AutoPins);
            if (!config.AutoPins)
            {
                changed |= DrawParameterizationU32(
                    "First pin vertex##Parameterization",
                    config.PinVertex0);
                changed |= DrawParameterizationU32(
                    "Second pin vertex##Parameterization",
                    config.PinVertex1);
            }
            ImGui::TextUnformatted("First pin UV");
            ImGui::PushID("FirstPinUv");
            changed |= DrawParameterizationUvValue(config.PinUv0);
            ImGui::PopID();
            ImGui::TextUnformatted("Second pin UV");
            ImGui::PushID("SecondPinUv");
            changed |= DrawParameterizationUvValue(config.PinUv1);
            ImGui::PopID();
            changed |= ImGui::InputDouble(
                "Solver tolerance##Parameterization",
                &config.SolverTolerance,
                0.0,
                0.0,
                "%.3e");
            changed |= DrawParameterizationU32(
                "Maximum iterations##Parameterization",
                config.MaxSolverIterations);
            return changed;
        }

        static bool DrawParameterizationHarmonicControls(
            ParameterizationHarmonicConfig& config)
        {
            using Boundary = ParameterizationBoundaryPolicy;
            constexpr std::array<Boundary, 3u> boundaries{
                Boundary::Circle,
                Boundary::Square,
                Boundary::Custom,
            };

            bool changed = false;
            if (ImGui::BeginCombo(
                    "Boundary##Parameterization",
                    ParameterizationBoundaryLabel(config.Boundary)))
            {
                for (const Boundary boundary : boundaries)
                {
                    const bool selected = config.Boundary == boundary;
                    if (ImGui::Selectable(
                            ParameterizationBoundaryLabel(boundary),
                            selected))
                    {
                        config.Boundary = boundary;
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            changed |= ImGui::Checkbox(
                "Arc-length boundary spacing##Parameterization",
                &config.ArcLengthSpacing);
            changed |= ImGui::Checkbox(
                "Clamp non-convex weights##Parameterization",
                &config.ClampNonConvexWeights);

            ImGui::SeparatorText("Pinned boundary vertices");
            if (config.PinnedVertices.size() != config.PinnedUvs.size())
            {
                ImGui::TextDisabled(
                    "Pin vertex and UV counts must match before Apply.");
            }
            const std::size_t pairCount = std::min(
                config.PinnedVertices.size(),
                config.PinnedUvs.size());
            std::optional<std::size_t> removeIndex{};
            for (std::size_t index = 0u; index < pairCount; ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                ImGui::Text("Pin %zu", index + 1u);
                changed |= DrawParameterizationU32(
                    "Vertex##ParameterizationPin",
                    config.PinnedVertices[index]);
                changed |= DrawParameterizationUvValue(
                    config.PinnedUvs[index]);
                if (ImGui::Button("Remove##ParameterizationPin"))
                    removeIndex = index;
                ImGui::PopID();
            }
            if (removeIndex.has_value())
            {
                config.PinnedVertices.erase(
                    config.PinnedVertices.begin() +
                    static_cast<std::ptrdiff_t>(*removeIndex));
                config.PinnedUvs.erase(
                    config.PinnedUvs.begin() +
                    static_cast<std::ptrdiff_t>(*removeIndex));
                changed = true;
            }
            if (ImGui::Button("Add pin##Parameterization"))
            {
                config.PinnedVertices.push_back(0u);
                config.PinnedUvs.emplace_back();
                changed = true;
            }
            return changed;
        }

        static bool DrawParameterizationBffControls(
            ParameterizationBffConfig& config)
        {
            using Mode = ParameterizationBffBoundaryMode;
            constexpr std::array<Mode, 3u> modes{
                Mode::AutomaticConformal,
                Mode::TargetLengths,
                Mode::TargetAngles,
            };
            bool changed = false;
            if (ImGui::BeginCombo(
                    "Boundary mode##Parameterization",
                    ParameterizationBffModeLabel(config.Mode)))
            {
                for (const Mode mode : modes)
                {
                    const bool selected = config.Mode == mode;
                    if (ImGui::Selectable(
                            ParameterizationBffModeLabel(mode),
                            selected))
                    {
                        config.Mode = mode;
                        if (mode == Mode::AutomaticConformal)
                            config.BoundaryData.clear();
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            if (config.Mode != Mode::AutomaticConformal)
            {
                ImGui::SeparatorText(
                    config.Mode == Mode::TargetLengths
                        ? "Boundary target lengths"
                        : "Boundary target angles");
                std::optional<std::size_t> removeIndex{};
                for (std::size_t index = 0u;
                     index < config.BoundaryData.size();
                     ++index)
                {
                    ImGui::PushID(static_cast<int>(index));
                    changed |= ImGui::InputDouble(
                        "Value##ParameterizationBffBoundary",
                        &config.BoundaryData[index],
                        0.0,
                        0.0,
                        "%.8g");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove##ParameterizationBffBoundary"))
                        removeIndex = index;
                    ImGui::PopID();
                }
                if (removeIndex.has_value())
                {
                    config.BoundaryData.erase(
                        config.BoundaryData.begin() +
                        static_cast<std::ptrdiff_t>(*removeIndex));
                    changed = true;
                }
                if (ImGui::Button("Add boundary value##Parameterization"))
                {
                    config.BoundaryData.push_back(
                        config.Mode == Mode::TargetLengths ? 1.0 : 0.0);
                    changed = true;
                }
            }
            changed |= ImGui::InputDouble(
                "Angle-sum tolerance##Parameterization",
                &config.AngleSumTolerance,
                0.0,
                0.0,
                "%.3e");
            changed |= ImGui::InputDouble(
                "Degeneracy tolerance##Parameterization",
                &config.DegeneracyTolerance,
                0.0,
                0.0,
                "%.3e");
            return changed;
        }

        static bool DrawParameterizationConfigControls(
            Runtime::ParameterizationConfig& config)
        {
            ImGui::SeparatorText("UV view");
            bool changed = DrawParameterizationUvViewControls(config.View);
            ImGui::SeparatorText("Parameterization method");
            changed |= DrawParameterizationStrategy(config);
            ImGui::SeparatorText("Strategy parameters");
            using Strategy = Runtime::EditorParameterizationStrategy;
            switch (config.Strategy)
            {
            case Strategy::Lscm:
                changed |= DrawParameterizationLscmControls(config.Lscm);
                break;
            case Strategy::HarmonicCotangent:
            case Strategy::TutteUniform:
                changed |= DrawParameterizationHarmonicControls(
                    config.Harmonic);
                break;
            case Strategy::Bff:
                changed |= DrawParameterizationBffControls(config.Bff);
                break;
            }
            return changed;
        }

        static void DrawParameterizationResult(
            const std::optional<Runtime::EditorParameterizationResult>&
                result)
        {
            ImGui::SeparatorText("Last run diagnostics");
            if (!result.has_value())
            {
                ImGui::TextDisabled("No parameterization has run this session.");
                return;
            }
            const SandboxParameterizationResultSummary summary =
                BuildSandboxParameterizationResultSummary(*result);
            ImGui::Text(
                "Status: %s (%s)",
                summary.CommandStatus.c_str(),
                summary.SolverStatus.c_str());
            ImGui::Text("Strategy token: %s", summary.StrategyToken.c_str());
            if (summary.HasDiagnostics)
            {
                ImGui::Text(
                    "Faces: %zu evaluated, %zu skipped, %zu flipped",
                    summary.EvaluatedFaceCount,
                    summary.SkippedFaceCount,
                    summary.FlippedElementCount);
                ImGui::Text(
                    "Boundary edges: %zu",
                    summary.BoundaryEdgeCount);
                ImGui::Text(
                    "Mean conformal %.6g  area %.6g  stretch %.6g",
                    summary.MeanConformalDistortion,
                    summary.MeanAreaDistortion,
                    summary.MeanStretch);
            }
            // Rejected solves expose both topology preconditions so the user
            // can identify the unsupported mesh structure.
            if (!result->Succeeded() && result->Rejection.Evaluated)
            {
                const std::size_t components =
                    result->Rejection.ConnectedComponentCount;
                const std::size_t loops = result->Rejection.BoundaryLoopCount;
                ImGui::Text(
                    "Rejected mesh: %zu connected %s, %zu boundary %s",
                    components, components == 1u ? "component" : "components",
                    loops, loops == 1u ? "loop" : "loops");
            }
            if (!summary.Message.empty())
                ImGui::TextWrapped("%s", summary.Message.c_str());
        }

        void DrawParameterizationControlPane(
            const SandboxEditorContext& context,
            const Runtime::EditorParameterizationViewModel& model)
        {
            if (!Parameterization.Initialized || !Parameterization.Dirty)
            {
                const auto active =
                    Runtime::GetEditorParameterizationConfig(context.Parameterization.Commands);
                if (active.has_value())
                {
                    Parameterization.Draft = *active;
                    Parameterization.Initialized = true;
                }
            }

            if (context.Parameterization.Results.LastParameterizationResult.has_value())
                Parameterization.LastResult =
                    *context.Parameterization.Results.LastParameterizationResult;

            const auto selectedPins = Runtime::ReadEditorPrimitiveSelection(
                context.Processing, model.SelectedStableEntityId,
                Runtime::GeometryElementDomain::MeshVertex);
            if (Parameterization.Draft.Strategy == Runtime::EditorParameterizationStrategy::Lscm)
            {
                ImGui::BeginDisabled(!selectedPins.Usable() || selectedPins.Indices.size() != 2);
                if (ImGui::Button("Use two selected vertices as LSCM pins"))
                {
                    auto& pins = Parameterization.Draft.Lscm;
                    pins.AutoPins = false;
                    pins.PinVertex0 = selectedPins.Indices[0];
                    pins.PinVertex1 = selectedPins.Indices[1];
                    Parameterization.Dirty = true;
                }
                ImGui::EndDisabled();
            }
            else if (Parameterization.Draft.Strategy ==
                         Runtime::EditorParameterizationStrategy::HarmonicCotangent ||
                     Parameterization.Draft.Strategy ==
                         Runtime::EditorParameterizationStrategy::TutteUniform)
            {
                ImGui::BeginDisabled(!selectedPins.Usable() || selectedPins.Indices.empty());
                if (ImGui::Button("Use selected vertices as boundary pins"))
                {
                    auto& pins = Parameterization.Draft.Harmonic;
                    pins.PinnedVertices = selectedPins.Indices;
                    pins.PinnedUvs.clear();
                    for (auto index : selectedPins.Indices)
                    {
                        const auto uv =
                            index < model.UVs.size() ? model.UVs[index] : glm::vec2(0.f);
                        pins.PinnedUvs.push_back({uv.x, uv.y});
                    }
                    Parameterization.Dirty = true;
                }
                ImGui::EndDisabled();
                ImGui::TextWrapped("Pin UVs start from the current UV map (zero if absent); edit "
                                   "them below. The method validates boundary eligibility.");
            }
            const auto& domainModel = GetDomainWindowModel(context, Runtime::EditorDomainWindowKind::Mesh, model.SelectedStableEntityId);
            ImGui::SeparatorText("Input properties");
            Parameterization.Dirty |= DrawProcessingPropertyInput("Positions##Parameterization", domainModel.PropertyCatalog, Parameterization.Draft.Positions);
            ImGui::SeparatorText("Output properties");
            Parameterization.Dirty |= DrawProcessingPropertyName("Texture coordinates##Parameterization", Parameterization.Draft.Texcoords.Name);
            Parameterization.Dirty |=
                DrawParameterizationConfigControls(Parameterization.Draft);
            if (Parameterization.Dirty)
                ImGui::TextDisabled("Draft has unapplied changes.");

            const auto configReadiness = Runtime::ResolveEditorProcessingActionReadiness(
                context.Parameterization.Commands, {true, {}});
            if (DrawProcessingActionButton("Apply configuration##Parameterization", configReadiness))
            {
                Parameterization.LastConfigResult =
                    Runtime::ApplyEditorParameterizationConfig(
                        context.Parameterization.Commands, Parameterization.Draft,
                        "sandbox.parameterization.panel");
                if (Parameterization.LastConfigResult->Succeeded())
                    Parameterization.Dirty = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload active##Parameterization"))
            {
                const auto active =
                    Runtime::GetEditorParameterizationConfig(context.Parameterization.Commands);
                if (active.has_value())
                {
                    Parameterization.Draft = *active;
                    Parameterization.Initialized = true;
                    Parameterization.Dirty = false;
                }
            }

            const auto readiness = Runtime::ResolveEditorProcessingActionReadiness(
                context.Parameterization.Commands,
                {model.HasSelectedEntity && model.SelectedEntityIsMesh, model.Message});
            if (DrawProcessingActionButton("Parameterize selected mesh##Parameterization", readiness))
            {
                SandboxParameterizationPanelActionResult action =
                    ApplySandboxParameterizationPanelAction(
                        context,
                        model.SelectedStableEntityId,
                        Parameterization.Draft);
                Parameterization.LastConfigResult = std::move(action.Config);
                if (action.Execution.has_value())
                    Parameterization.LastResult = std::move(action.Execution);
                if (Parameterization.LastConfigResult->Succeeded())
                    Parameterization.Dirty = false;
            }

            DrawProcessingPropertyShowButton(context, model.SelectedStableEntityId,
                Parameterization.Draft.Texcoords, Parameterization.VisualizationDiagnostic);
            if (!Parameterization.VisualizationDiagnostic.empty()) ImGui::Text("Display: %s", Parameterization.VisualizationDiagnostic.c_str());
            const bool historyAvailable =
                context.Document != nullptr &&
                context.DocumentCommands.Available();
            const Runtime::EditorDocumentModel history =
                context.Document != nullptr
                    ? *context.Document
                    : Runtime::EditorDocumentModel{};
            const bool canUndoUv =
                history.CanUndo && history.UndoLabel == "Parameterize mesh UVs";
            const bool canRedoUv =
                history.CanRedo && history.RedoLabel == "Parameterize mesh UVs";
            if (!canUndoUv)
                ImGui::BeginDisabled();
            if (ImGui::Button("Undo UV writeback##Parameterization") &&
                historyAvailable)
            {
                (void)context.DocumentCommands.Undo();
            }
            if (!canUndoUv)
                ImGui::EndDisabled();
            ImGui::SameLine();
            if (!canRedoUv)
                ImGui::BeginDisabled();
            if (ImGui::Button("Redo UV writeback##Parameterization") &&
                historyAvailable)
            {
                (void)context.DocumentCommands.Redo();
            }
            if (!canRedoUv)
                ImGui::EndDisabled();
            if (history.CanUndo && !canUndoUv)
            {
                ImGui::TextDisabled(
                    "Next global undo is '%s'; use File / Scene.",
                    history.UndoLabel.c_str());
            }

            if (Parameterization.LastConfigResult.has_value())
            {
                const auto& applied = *Parameterization.LastConfigResult;
                if (applied.Succeeded())
                    ImGui::TextUnformatted(applied.Status == Runtime::RuntimeEngineConfigApplyStatus::NoChange
                        ? "Parameterization config unchanged." : "Parameterization config applied.");
                else
                {
                    ImGui::TextWrapped("Parameterization config was rejected.");
                    for (const auto& diagnostic : applied.LoadResult.Diagnostics)
                        ImGui::TextWrapped("%s", diagnostic.Message.c_str());
                }
            }
            DrawParameterizationResult(Parameterization.LastResult);
            if (Parameterization.LastResult.has_value())
            {
                if (DrawDismissLastResultButton("Dismiss##Parameterization"))
                {
                    Parameterization.LastResult.reset();
                    if (context.Parameterization.ResultSinks.DismissResult)
                        context.Parameterization.ResultSinks.DismissResult();
                }
            }
        }

        static ImVec2 ToImVec2(const glm::vec2 value) noexcept
        {
            return ImVec2{value.x, value.y};
        }

        void DrawParameterizationUvPane(
            const SandboxEditorContext& context,
            const Runtime::EditorParameterizationViewModel& model)
        {
            ImGui::TextUnformatted("UV layout");
            ImGui::SameLine();
            if (ImGui::SmallButton("Fit##ParameterizationUv"))
            {
                Parameterization.Zoom = 1.0f;
                Parameterization.Pan = glm::vec2{0.0f};
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%.0f%%", Parameterization.Zoom * 100.0f);
            if (Parameterization.LastUvViewState.has_value())
            {
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "%s / %s%s",
                    Runtime::DebugNameForEditorParameterizationUvViewStatus(
                        Parameterization.LastUvViewState->Status),
                    ParameterizationUvBackgroundModeLabel(
                        Parameterization.LastUvViewState->ActiveBackground),
                    Parameterization.LastUvViewState->HeatmapActive
                        ? " / heatmap"
                        : "");
                if (ImGui::IsItemHovered() &&
                    !Parameterization.LastUvViewState->Message.empty())
                {
                    ImGui::SetTooltip(
                        "%s",
                        Parameterization.LastUvViewState->Message.c_str());
                }
            }

            const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
            ImVec2 canvasSize = ImGui::GetContentRegionAvail();
            canvasSize.x = std::max(canvasSize.x, 80.0f);
            canvasSize.y = std::max(canvasSize.y, 80.0f);
            Runtime::EditorParameterizationUvViewState uvView =
                Runtime::SubmitEditorParameterizationUvView(
                    context.Parameterization.UvViewCommands,
                    model,
                    static_cast<std::uint32_t>(canvasSize.x),
                    static_cast<std::uint32_t>(canvasSize.y));
            Parameterization.LastUvViewState = uvView;
            ImGui::InvisibleButton(
                "##ParameterizationUvCanvas",
                canvasSize);
            const bool hovered = ImGui::IsItemHovered();
            if (!uvView.GpuReady && hovered &&
                ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                Parameterization.Pan += glm::vec2{delta.x, delta.y};
            }
            if (!uvView.GpuReady && hovered &&
                ImGui::GetIO().MouseWheel != 0.0f)
            {
                const float oldZoom = Parameterization.Zoom;
                const float factor = std::pow(
                    1.15f,
                    ImGui::GetIO().MouseWheel);
                Parameterization.Zoom = std::clamp(
                    oldZoom * factor,
                    0.1f,
                    20.0f);
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const glm::vec2 paneCenter{
                    canvasMin.x + canvasSize.x * 0.5f,
                    canvasMin.y + canvasSize.y * 0.5f,
                };
                const glm::vec2 cursorOffset =
                    glm::vec2{mouse.x, mouse.y} - paneCenter -
                    Parameterization.Pan;
                Parameterization.Pan +=
                    cursorOffset *
                    (1.0f - Parameterization.Zoom / oldZoom);
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 canvasMax{
                canvasMin.x + canvasSize.x,
                canvasMin.y + canvasSize.y,
            };
            drawList->AddRectFilled(
                canvasMin,
                canvasMax,
                IM_COL32(24, 27, 32, 255));
            drawList->PushClipRect(canvasMin, canvasMax, true);
            if (uvView.GpuReady)
            {
                drawList->AddImage(
                    static_cast<ImTextureID>(uvView.BindlessIndex),
                    canvasMin,
                    canvasMax);
            }
            else
            {
                const bool showGrid =
                    uvView.ActiveBackground ==
                    ParameterizationUvBackgroundMode::Grid;
                const bool showChecker = !showGrid;
                const SandboxParameterizationUvProjection projection =
                    BuildSandboxParameterizationUvProjection(
                        model,
                        SandboxParameterizationUvPane{
                            .Min = {canvasMin.x, canvasMin.y},
                            .Max = {canvasMax.x, canvasMax.y},
                            .Padding = 24.0f,
                            .Zoom = Parameterization.Zoom,
                            .Pan = Parameterization.Pan,
                            .IncludeUnitSquare = true,
                        });
                if (projection.Valid)
                {
                    if (showChecker)
                    {
                        constexpr std::uint32_t kCheckerCount = 10u;
                        for (std::uint32_t y = 0u; y < kCheckerCount; ++y)
                        {
                            for (std::uint32_t x = 0u; x < kCheckerCount; ++x)
                            {
                                const glm::vec2 uv0{
                                    static_cast<float>(x) /
                                        static_cast<float>(kCheckerCount),
                                    static_cast<float>(y) /
                                        static_cast<float>(kCheckerCount),
                                };
                                const glm::vec2 uv1{
                                    static_cast<float>(x + 1u) /
                                        static_cast<float>(kCheckerCount),
                                    static_cast<float>(y + 1u) /
                                        static_cast<float>(kCheckerCount),
                                };
                                const glm::vec2 p0 =
                                    ProjectSandboxParameterizationUvPoint(
                                        projection,
                                        uv0);
                                const glm::vec2 p1 =
                                    ProjectSandboxParameterizationUvPoint(
                                        projection,
                                        uv1);
                                if (!IsFiniteVec2(p0) || !IsFiniteVec2(p1))
                                    continue;
                                drawList->AddRectFilled(
                                    ImVec2{
                                        std::min(p0.x, p1.x),
                                        std::min(p0.y, p1.y),
                                    },
                                    ImVec2{
                                        std::max(p0.x, p1.x),
                                        std::max(p0.y, p1.y),
                                    },
                                    ((x + y) & 1u) == 0u
                                        ? IM_COL32(53, 57, 66, 255)
                                        : IM_COL32(36, 40, 47, 255));
                            }
                        }
                    }
                    if (showGrid)
                    {
                        for (std::uint32_t index = 0u; index <= 10u; ++index)
                        {
                            const float t = static_cast<float>(index) / 10.0f;
                            const glm::vec2 vertical0 =
                                ProjectSandboxParameterizationUvPoint(
                                    projection,
                                    {t, 0.0f});
                            const glm::vec2 vertical1 =
                                ProjectSandboxParameterizationUvPoint(
                                    projection,
                                    {t, 1.0f});
                            const glm::vec2 horizontal0 =
                                ProjectSandboxParameterizationUvPoint(
                                    projection,
                                    {0.0f, t});
                            const glm::vec2 horizontal1 =
                                ProjectSandboxParameterizationUvPoint(
                                    projection,
                                    {1.0f, t});
                            if (!IsFiniteVec2(vertical0) ||
                                !IsFiniteVec2(vertical1) ||
                                !IsFiniteVec2(horizontal0) ||
                                !IsFiniteVec2(horizontal1))
                            {
                                continue;
                            }
                            const ImU32 color = index == 0u || index == 10u
                                ? IM_COL32(121, 129, 146, 190)
                                : IM_COL32(91, 98, 112, 100);
                            drawList->AddLine(
                                ToImVec2(vertical0),
                                ToImVec2(vertical1),
                                color);
                            drawList->AddLine(
                                ToImVec2(horizontal0),
                                ToImVec2(horizontal1),
                                color);
                        }
                    }

                    for (const auto& triangle : projection.Triangles)
                    {
                        const ImVec2 a =
                            ToImVec2(projection.Vertices[triangle[0]]);
                        const ImVec2 b =
                            ToImVec2(projection.Vertices[triangle[1]]);
                        const ImVec2 c =
                            ToImVec2(projection.Vertices[triangle[2]]);
                        drawList->AddTriangleFilled(
                            a, b, c, IM_COL32(66, 145, 214, 52));
                        drawList->AddTriangle(
                            a,
                            b,
                            c,
                            IM_COL32(113, 190, 255, 220),
                            1.25f);
                    }
                    for (const glm::vec2 vertex : projection.Vertices)
                    {
                        drawList->AddCircleFilled(
                            ToImVec2(vertex),
                            2.0f,
                            IM_COL32(225, 240, 255, 235));
                    }
                }
                else
                {
                    const std::string& message = projection.Message.empty()
                        ? model.Message
                        : projection.Message;
                    drawList->AddText(
                        ImVec2{canvasMin.x + 12.0f, canvasMin.y + 12.0f},
                        IM_COL32(170, 176, 188, 255),
                        message.empty()
                            ? "Parameterize the selected mesh to populate UVs."
                            : message.c_str());
                }
            }
            drawList->PopClipRect();
            drawList->AddRect(
                canvasMin,
                canvasMax,
                IM_COL32(90, 96, 108, 255));
        }

        void DrawParameterizationWindow(
            bool& open,
            const SandboxEditorContext& context)
        {
            ImGui::SetNextWindowSize(
                ImVec2(920.0f, 600.0f),
                ImGuiCond_FirstUseEver);
            const bool contentsVisible = ImGui::Begin(
                    "Mesh / Processing / Parameterize (UV)",
                    &open);
            if (contentsVisible)
            {
                DrawProcessingEntity("Entity##Processing", context, Parameterization.Input.Entity,
                    Parameterization.Input.PreviousSelection, Runtime::EditorDomainWindowKind::Mesh);
                DrawProcessingCpuBackend();
                Runtime::EditorParameterizationViewModel model =
                    Runtime::BuildEditorParameterizationViewModel(
                        context.Parameterization.Commands, context.Parameterization.Results,
                        Parameterization.Input.Entity);
                if (model.HasSelectedEntity)
                {
                    ImGui::Text(
                        "Selected entity: %u%s",
                        model.SelectedStableEntityId,
                        model.SelectedEntityIsMesh ? " (mesh)" : "");
                }
                else
                {
                    ImGui::TextDisabled("Selected entity: none");
                }
                if (!model.Message.empty())
                    ImGui::TextWrapped("%s", model.Message.c_str());

                constexpr float splitterWidth = 6.0f;
                const ImVec2 available = ImGui::GetContentRegionAvail();
                const float usableWidth =
                    std::max(available.x - splitterWidth, 2.0f);
                Parameterization.SplitRatio = std::clamp(
                    Parameterization.SplitRatio,
                    0.28f,
                    0.72f);
                const float controlWidth =
                    usableWidth * Parameterization.SplitRatio;

                ImGui::BeginChild(
                    "##ParameterizationControls",
                    ImVec2(controlWidth, available.y),
                    true);
                DrawParameterizationControlPane(context, model);
                ImGui::EndChild();
                if (const auto active =
                        Runtime::GetEditorParameterizationConfig(context.Parameterization.Commands);
                    active.has_value())
                {
                    model.View = active->View;
                }
                ImGui::SameLine(0.0f, 0.0f);

                ImGui::InvisibleButton(
                    "##ParameterizationSplitter",
                    ImVec2(splitterWidth, available.y));
                if (ImGui::IsItemActive())
                {
                    Parameterization.SplitRatio = std::clamp(
                        Parameterization.SplitRatio +
                            ImGui::GetIO().MouseDelta.x / usableWidth,
                        0.28f,
                        0.72f);
                }
                const ImVec2 splitterMin = ImGui::GetItemRectMin();
                const ImVec2 splitterMax = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(
                    splitterMin,
                    splitterMax,
                    ImGui::IsItemHovered() || ImGui::IsItemActive()
                        ? IM_COL32(94, 155, 214, 210)
                        : IM_COL32(69, 75, 86, 180));
                ImGui::SameLine(0.0f, 0.0f);

                ImGui::BeginChild(
                    "##ParameterizationUv",
                    ImVec2(0.0f, available.y),
                    true);
                DrawParameterizationUvPane(context, model);
                ImGui::EndChild();
            }
            ImGui::End();
            if (!open || !contentsVisible)
            {
                Runtime::DisableEditorParameterizationUvView(context.Parameterization.UvViewCommands);
                Parameterization.LastUvViewState.reset();
            }
        }

        static void DrawProgressivePoissonResultStatus(
            const std::optional<
                Runtime::EditorProgressivePoissonResult>& lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last progressive Poisson run: none");
                return;
            }

            const Runtime::EditorProgressivePoissonResult& result =
                *lastResult;
            ImGui::Text(
                "Last progressive Poisson run: %s",
                Runtime::DebugNameForEditorCommandStatus(result.Status));
            ImGui::Text(
                "Channel: %s",
                Runtime::DebugNameForProgressivePoissonChannel(
                    result.Channel));
            if (!result.BackendId.empty())
            {
                if (!result.BackendDisplayName.empty())
                {
                    ImGui::Text(
                        "Backend: %s (%s)",
                        result.BackendDisplayName.c_str(),
                        result.BackendId.c_str());
                }
                else
                {
                    ImGui::Text("Backend: %s", result.BackendId.c_str());
                }
            }
            if (!result.RequestedBackendId.empty() &&
                result.RequestedBackendId != result.BackendId)
            {
                if (!result.RequestedBackendDisplayName.empty())
                {
                    ImGui::Text(
                        "Requested backend: %s (%s)",
                        result.RequestedBackendDisplayName.c_str(),
                        result.RequestedBackendId.c_str());
                }
                else
                {
                    ImGui::Text(
                        "Requested backend: %s",
                        result.RequestedBackendId.c_str());
                }
            }
            if (result.Succeeded())
            {
                ImGui::Text(
                    "Accepted %u / %u  prefix %u  levels %u",
                    result.AcceptedCount,
                    result.InputCount,
                    result.PrefixCount,
                    result.LevelCount);
                ImGui::Text(
                    "Base radius %.6f  alpha %.6f",
                    static_cast<double>(result.BaseRadius),
                    static_cast<double>(result.UsedAlpha));
                ImGui::TextWrapped(
                    "Level counts: %s",
                    FormatLevelCounts(result.LevelAcceptedCounts).c_str());
                if (result.AlphaDefaulted ||
                    result.ClampedGridWidth ||
                    result.ClampedMaxLevels)
                {
                    ImGui::Text(
                        "Defaults: alpha=%s grid=%s levels=%s",
                        result.AlphaDefaulted ? "yes" : "no",
                        result.ClampedGridWidth ? "yes" : "no",
                        result.ClampedMaxLevels ? "yes" : "no");
                }
            }
            if (!result.BackendFallbackReason.empty())
            {
                ImGui::TextWrapped(
                    "Backend fallback: %s",
                    result.BackendFallbackReason.c_str());
            }
            if (!result.Message.empty())
                ImGui::TextWrapped("%s", result.Message.c_str());
        }
    };

    MethodPanels::MethodPanels()
        : m_Impl(std::make_unique<Impl>())
    {
    }

    MethodPanels::~MethodPanels()
    {
        m_Impl->Unregister();
    }

    void MethodPanels::Register(EditorShell& editorShell)
    {
        m_Impl->Register(editorShell);
    }

    void MethodPanels::Unregister()
    {
        m_Impl->Unregister();
    }
}
