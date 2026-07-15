module;

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

import Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.SandboxEditorFacades;

namespace Extrinsic::Sandbox::Editor
{
    namespace
    {
        using ParameterizationPanelConfig =
            SandboxParameterizationPanelConfig;
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
        using ParameterizationSolverStatus = decltype(
            Runtime::SandboxEditorParameterizationResult{}
                .ParameterizationStatus);

        constexpr std::array<Runtime::SandboxEditorKMeansBackend, 2>
            kKMeansBackends{
                Runtime::SandboxEditorKMeansBackend::CpuReference,
                Runtime::SandboxEditorKMeansBackend::VulkanCompute,
            };
        constexpr std::array<Runtime::SandboxEditorProgressivePoissonChannel, 4>
            kProgressivePoissonChannels{
                Runtime::SandboxEditorProgressivePoissonChannel::Level,
                Runtime::SandboxEditorProgressivePoissonChannel::Phase,
                Runtime::SandboxEditorProgressivePoissonChannel::SplatRadius,
                Runtime::SandboxEditorProgressivePoissonChannel::PrefixVisible,
            };
        constexpr std::array<Runtime::SandboxEditorProgressivePoissonBackend, 2>
            kProgressivePoissonBackends{
                Runtime::SandboxEditorProgressivePoissonBackend::CpuReference,
                Runtime::SandboxEditorProgressivePoissonBackend::VulkanCompute,
            };

        [[nodiscard]] bool DomainWindowReady(
            const Runtime::SandboxEditorDomainWindowModel& model) noexcept
        {
            return model.HasSelectedEntity && model.DomainMatches;
        }

        void DrawDiagnostics(
            const std::vector<Runtime::SandboxEditorDiagnostic>& diagnostics)
        {
            for (const Runtime::SandboxEditorDiagnostic& diagnostic : diagnostics)
            {
                ImGui::TextDisabled(
                    "%s: %s",
                    Runtime::DebugNameForSandboxEditorDiagnosticCode(
                        diagnostic.Code),
                    diagnostic.Message.c_str());
            }
        }

        void DrawDomainWindowHeader(
            const Runtime::SandboxEditorDomainWindowModel& model)
        {
            ImGui::Text(
                "Expected domain: %s",
                Runtime::DebugNameForSandboxEditorGeometryDomain(
                    model.ExpectedDomain));
            if (model.HasSelectedEntity)
            {
                ImGui::Text(
                    "Selected: %s (%u)",
                    model.SelectedEntity.Name.c_str(),
                    model.SelectedStableId);
                ImGui::Text(
                    "Selected domain: %s",
                    Runtime::DebugNameForSandboxEditorGeometryDomain(
                        model.SelectedDomain));
            }
            else
            {
                ImGui::TextDisabled("Selected: none");
            }
            DrawDiagnostics(model.Diagnostics);
        }

        [[nodiscard]] bool ContainsKMeansDomain(
            const std::vector<Runtime::SandboxEditorGeometryProcessingDomain>&
                domains,
            const Runtime::SandboxEditorGeometryProcessingDomain domain)
        {
            return std::find(domains.begin(), domains.end(), domain) !=
                   domains.end();
        }

        [[nodiscard]] Runtime::SandboxEditorKMeansBackend KMeansBackendFromIndex(
            const std::int32_t index) noexcept
        {
            const std::int32_t clamped = std::clamp(
                index,
                0,
                static_cast<std::int32_t>(kKMeansBackends.size() - 1u));
            return kKMeansBackends[static_cast<std::size_t>(clamped)];
        }

        [[nodiscard]] Runtime::SandboxEditorProgressivePoissonChannel
        ProgressivePoissonChannelFromIndex(const std::int32_t index) noexcept
        {
            const std::int32_t clamped = std::clamp(
                index,
                0,
                static_cast<std::int32_t>(
                    kProgressivePoissonChannels.size() - 1u));
            return kProgressivePoissonChannels[
                static_cast<std::size_t>(clamped)];
        }

        [[nodiscard]] std::int32_t ProgressivePoissonChannelIndex(
            const Runtime::SandboxEditorProgressivePoissonChannel channel) noexcept
        {
            const auto found = std::find(
                kProgressivePoissonChannels.begin(),
                kProgressivePoissonChannels.end(),
                channel);
            return found == kProgressivePoissonChannels.end()
                ? 0
                : static_cast<std::int32_t>(
                      std::distance(kProgressivePoissonChannels.begin(), found));
        }

        [[nodiscard]] Runtime::SandboxEditorProgressivePoissonBackend
        ProgressivePoissonBackendFromIndex(const std::int32_t index) noexcept
        {
            const std::int32_t clamped = std::clamp(
                index,
                0,
                static_cast<std::int32_t>(
                    kProgressivePoissonBackends.size() - 1u));
            return kProgressivePoissonBackends[
                static_cast<std::size_t>(clamped)];
        }

        [[nodiscard]] std::int32_t ProgressivePoissonBackendIndex(
            const Runtime::SandboxEditorProgressivePoissonBackend backend) noexcept
        {
            const auto found = std::find(
                kProgressivePoissonBackends.begin(),
                kProgressivePoissonBackends.end(),
                backend);
            return found == kProgressivePoissonBackends.end()
                ? 0
                : static_cast<std::int32_t>(
                      std::distance(kProgressivePoissonBackends.begin(), found));
        }

        void DrawProgressivePoissonTooltip(const char* text)
        {
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", text);
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

        [[nodiscard]] bool IsFiniteVec2(const glm::vec2 value) noexcept
        {
            return std::isfinite(value.x) && std::isfinite(value.y);
        }

        [[nodiscard]] bool IsSupportedParameterizationStrategy(
            const Runtime::SandboxEditorParameterizationStrategy strategy) noexcept
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
    }

    std::array<SandboxParameterizationStrategyOption, 4u>
    SandboxParameterizationStrategyOptions() noexcept
    {
        using Strategy = Runtime::SandboxEditorParameterizationStrategy;
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
            Runtime::StableTokenForSandboxEditorParameterizationStrategy(
                config.Strategy).empty())
        {
            return std::nullopt;
        }
        return SandboxParameterizationPanelApplyRequest{
            .Config =
                Runtime::SandboxEditorParameterizationConfigCommand{
                    .Config = config,
                    .SourceId = "sandbox.parameterization.panel",
                },
            .Execute =
                Runtime::SandboxEditorConfiguredParameterizationCommand{
                    .StableEntityId = stableEntityId,
                },
        };
    }

    SandboxParameterizationPanelActionResult
    ApplySandboxParameterizationPanelAction(
        const Runtime::SandboxEditorContext& context,
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
                Runtime::SandboxEditorParameterizationConfigStatus::PreviewRejected;
            rejected.Config.Message =
                "Parameterization panel request is invalid or unsupported.";
            return rejected;
        }

        SandboxParameterizationPanelActionResult result{};
        result.Config = Runtime::ApplySandboxEditorParameterizationConfigCommand(
            context,
            request->Config);
        if (result.Config.Succeeded())
        {
            result.Execution =
                Runtime::ApplySandboxEditorConfiguredParameterizationCommand(
                    context,
                    request->Execute);
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
        const Runtime::SandboxEditorParameterizationViewModel& model,
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
        const Runtime::SandboxEditorParameterizationResult& result)
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
                Runtime::DebugNameForSandboxEditorCommandStatus(result.Status),
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

    struct MethodPanels::Impl
    {
        struct KMeansState
        {
            std::optional<Runtime::SandboxEditorKMeansResult> LastResult{};
            Runtime::SandboxEditorGeometryProcessingDomain Domain{
                Runtime::SandboxEditorGeometryProcessingDomain::None};
            std::int32_t Backend{0};
            std::int32_t ClusterCount{8};
            std::int32_t MaxIterations{32};
            std::int32_t Seed{42};
            bool UseHierarchicalInitialization{true};
        };

        struct ProgressivePoissonState
        {
            std::optional<Runtime::SandboxEditorProgressivePoissonResult>
                LastResult{};
            std::optional<Runtime::SandboxEditorProgressivePoissonConfigResult>
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
            std::int32_t MeshSurfaceSampleCount{4096};
            std::int32_t MeshSurfaceSampleSeed{1337};
            float MeshSurfaceMinTriangleArea{1.0e-14f};
            bool MeshSurfaceInterpolateNormals{true};
            bool AutoRunOnEdit{true};
            float DebounceSeconds{0.25f};
            bool AutoRunPending{false};
            double LastEditTime{0.0};
            std::uint32_t PendingStableEntityId{0u};
        };

        struct ParameterizationState
        {
            SandboxParameterizationPanelConfig Draft{};
            bool Initialized{false};
            bool Dirty{false};
            std::optional<Runtime::SandboxEditorParameterizationConfigResult>
                LastConfigResult{};
            std::optional<Runtime::SandboxEditorParameterizationResult>
                LastResult{};
            float SplitRatio{0.42f};
            float Zoom{1.0f};
            glm::vec2 Pan{0.0f};
            bool ShowGrid{true};
            bool ShowChecker{true};
        };

        EditorShell* Shell{nullptr};
        std::vector<Runtime::EditorWindowHandle> Handles{};
        int CachedModelFrame{-1};
        std::array<
            std::optional<Runtime::SandboxEditorDomainWindowModel>,
            3u>
            CachedDomainModels{};
        KMeansState KMeans{};
        ProgressivePoissonState ProgressivePoisson{};
        ParameterizationState Parameterization{};

        void Register(EditorShell& editorShell)
        {
            Unregister();
            Shell = &editorShell;

            RegisterKMeansWindow(
                Runtime::SandboxEditorDomainWindowKind::PointCloud,
                "pointcloud.processing.kmeans",
                {"PointCloud", "Processing"},
                "PointCloud / Processing / K-Means");
            RegisterKMeansWindow(
                Runtime::SandboxEditorDomainWindowKind::Graph,
                "graph.processing.kmeans",
                {"Graph", "Processing"},
                "Graph / Processing / K-Means");
            RegisterKMeansWindow(
                Runtime::SandboxEditorDomainWindowKind::Mesh,
                "mesh.processing.kmeans",
                {"Mesh", "Processing"},
                "Mesh / Processing / K-Means");
            RegisterProgressivePoissonWindow(
                Runtime::SandboxEditorDomainWindowKind::PointCloud,
                "pointcloud.processing.progressive_poisson",
                {"PointCloud", "Processing"},
                "PointCloud / Processing / Progressive Poisson");
            RegisterProgressivePoissonWindow(
                Runtime::SandboxEditorDomainWindowKind::Mesh,
                "mesh.processing.progressive_poisson",
                {"Mesh", "Processing"},
                "Mesh / Processing / Progressive Poisson");
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
            KMeans.LastResult.reset();
            ProgressivePoisson.LastResult.reset();
            ProgressivePoisson.LastConfigResult.reset();
            ProgressivePoisson.AutoRunPending = false;
            ProgressivePoisson.LastEditTime = 0.0;
            ProgressivePoisson.PendingStableEntityId = 0u;
            Parameterization = ParameterizationState{};
        }

        [[nodiscard]] const Runtime::SandboxEditorDomainWindowModel&
        GetDomainWindowModel(
            const Runtime::SandboxEditorContext& context,
            const Runtime::SandboxEditorDomainWindowKind kind)
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
                model = Runtime::BuildSandboxEditorDomainWindowModel(
                    context,
                    kind);
            }
            else if (context.ModelBuildStats != nullptr)
            {
                ++context.ModelBuildStats->DomainWindowModelCacheHits;
            }
            return *model;
        }

        void RegisterKMeansWindow(
            const Runtime::SandboxEditorDomainWindowKind kind,
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
                            const Runtime::SandboxEditorContext& context)
                        {
                            DrawKMeansWindow(open, context, kind, windowTitle);
                        },
                }));
        }

        void RegisterProgressivePoissonWindow(
            const Runtime::SandboxEditorDomainWindowKind kind,
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
                            const Runtime::SandboxEditorContext& context)
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
                            const Runtime::SandboxEditorContext& context)
                        {
                            DrawParameterizationWindow(open, context);
                        },
                }));
        }

        void DrawKMeansWindow(
            bool& open,
            const Runtime::SandboxEditorContext& context,
            const Runtime::SandboxEditorDomainWindowKind kind,
            const std::string& windowTitle)
        {
            ImGui::SetNextWindowSize(
                ImVec2(340.0f, 300.0f),
                ImGuiCond_FirstUseEver);
            if (ImGui::Begin(windowTitle.c_str(), &open))
            {
                const Runtime::SandboxEditorDomainWindowModel& model =
                    GetDomainWindowModel(context, kind);
                DrawDomainWindowHeader(model);
                DrawDiagnostics(model.Processing.Diagnostics);
                if (!DomainWindowReady(model) ||
                    !model.Processing.HasSelectedEntity)
                {
                    ImGui::TextDisabled(
                        "Select a matching domain entity to inspect processing affordances.");
                }
                else
                {
                    DrawKMeansControls(model, context);
                }
            }
            ImGui::End();
        }

        void DrawKMeansControls(
            const Runtime::SandboxEditorDomainWindowModel& model,
            const Runtime::SandboxEditorContext& context)
        {
            const Runtime::SandboxEditorGeometryProcessingModel& processing =
                model.Processing;
            ImGui::SeparatorText("K-Means execution");
            if (processing.KMeansDomains.empty())
            {
                ImGui::TextDisabled(
                    "K-Means is unavailable for this selection.");
                return;
            }

            if (context.LastKMeansResult != nullptr)
                KMeans.LastResult = *context.LastKMeansResult;
            if (!ContainsKMeansDomain(processing.KMeansDomains, KMeans.Domain))
                KMeans.Domain = processing.KMeansDomains.front();

            if (ImGui::BeginCombo(
                    "Domain##KMeans",
                    Runtime::DebugNameForSandboxEditorGeometryProcessingDomain(
                        KMeans.Domain)))
            {
                for (const Runtime::SandboxEditorGeometryProcessingDomain domain :
                     processing.KMeansDomains)
                {
                    const bool selected = KMeans.Domain == domain;
                    if (ImGui::Selectable(
                            Runtime::DebugNameForSandboxEditorGeometryProcessingDomain(
                                domain),
                            selected))
                    {
                        KMeans.Domain = domain;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            KMeans.Backend = std::clamp(
                KMeans.Backend,
                0,
                static_cast<std::int32_t>(kKMeansBackends.size() - 1u));
            const Runtime::SandboxEditorKMeansBackend backend =
                KMeansBackendFromIndex(KMeans.Backend);
            if (ImGui::BeginCombo(
                    "Backend##KMeans",
                    Runtime::DebugNameForSandboxEditorKMeansBackend(backend)))
            {
                for (std::size_t index = 0u;
                     index < kKMeansBackends.size();
                     ++index)
                {
                    const bool selected =
                        KMeans.Backend == static_cast<std::int32_t>(index);
                    if (ImGui::Selectable(
                            Runtime::DebugNameForSandboxEditorKMeansBackend(
                                kKMeansBackends[index]),
                            selected))
                    {
                        KMeans.Backend = static_cast<std::int32_t>(index);
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }

            ImGui::DragInt(
                "Clusters##KMeans",
                &KMeans.ClusterCount,
                1.0f,
                1,
                1024);
            ImGui::DragInt(
                "Max iterations##KMeans",
                &KMeans.MaxIterations,
                1.0f,
                1,
                4096);
            ImGui::DragInt(
                "Seed##KMeans",
                &KMeans.Seed,
                1.0f,
                0,
                1'000'000);
            KMeans.ClusterCount = std::clamp(KMeans.ClusterCount, 1, 1024);
            KMeans.MaxIterations =
                std::clamp(KMeans.MaxIterations, 1, 4096);
            KMeans.Seed = std::clamp(KMeans.Seed, 0, 1'000'000);
            ImGui::Checkbox(
                "Hierarchical initialization##KMeans",
                &KMeans.UseHierarchicalInitialization);

            if (ImGui::Button("Run K-Means##KMeans"))
            {
                Runtime::SandboxEditorKMeansResult result =
                    Runtime::ApplySandboxEditorKMeansCommand(
                        context,
                        Runtime::SandboxEditorKMeansCommand{
                            .StableEntityId = model.SelectedStableId,
                            .Domain = KMeans.Domain,
                            .ClusterCount = static_cast<std::uint32_t>(
                                KMeans.ClusterCount),
                            .MaxIterations = static_cast<std::uint32_t>(
                                KMeans.MaxIterations),
                            .Seed = static_cast<std::uint32_t>(KMeans.Seed),
                            .UseHierarchicalInitialization =
                                KMeans.UseHierarchicalInitialization,
                            .Backend =
                                KMeansBackendFromIndex(KMeans.Backend),
                        });
                KMeans.LastResult = result;
                if (context.MethodResultSinks.KMeans)
                    context.MethodResultSinks.KMeans(std::move(result));
            }

            const std::optional<Runtime::SandboxEditorKMeansResult>& result =
                KMeans.LastResult.has_value()
                    ? KMeans.LastResult
                    : processing.LastKMeansResult;
            DrawKMeansResultStatus(result);
        }

        static void DrawKMeansResultStatus(
            const std::optional<Runtime::SandboxEditorKMeansResult>& lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last K-Means run: none");
                return;
            }

            const Runtime::SandboxEditorKMeansResult& result = *lastResult;
            ImGui::Text(
                "Last K-Means run: %s",
                Runtime::DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text(
                "Domain: %s",
                Runtime::DebugNameForSandboxEditorGeometryProcessingDomain(
                    result.Domain));
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
                    "Labels: %u  clusters: %u  iterations: %u",
                    result.LabelCount,
                    result.ClusterCount,
                    result.Iterations);
                ImGui::Text(
                    "Converged: %s  inertia: %.6f",
                    result.Converged ? "yes" : "no",
                    static_cast<double>(result.Inertia));
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

        void DrawProgressivePoissonWindow(
            bool& open,
            const Runtime::SandboxEditorContext& context,
            const Runtime::SandboxEditorDomainWindowKind kind,
            const std::string& windowTitle)
        {
            ImGui::SetNextWindowSize(
                ImVec2(340.0f, 300.0f),
                ImGuiCond_FirstUseEver);
            if (ImGui::Begin(windowTitle.c_str(), &open))
            {
                const Runtime::SandboxEditorDomainWindowModel& model =
                    GetDomainWindowModel(context, kind);
                DrawDomainWindowHeader(model);
                DrawDiagnostics(model.Processing.Diagnostics);
                if (!DomainWindowReady(model) ||
                    !model.Processing.HasSelectedEntity)
                {
                    ImGui::TextDisabled(
                        "Select a matching domain entity to inspect processing affordances.");
                }
                else
                {
                    DrawProgressivePoissonControls(model, context);
                }
            }
            ImGui::End();
        }

        static void SyncProgressivePoissonState(
            ProgressivePoissonState& state,
            const Runtime::SandboxEditorProgressivePoissonConfig& config)
        {
            state.Dimension = static_cast<std::int32_t>(config.Dimension);
            state.GridWidth = static_cast<std::int32_t>(config.GridWidth);
            state.MaxLevels = static_cast<std::int32_t>(config.MaxLevels);
            state.HashLoadFactor = config.HashLoadFactor;
            state.RadiusAlpha = config.RadiusAlpha;
            state.RandomizeGridOrigin = config.RandomizeGridOrigin;
            state.GridOriginSeed =
                static_cast<std::int32_t>(config.GridOriginSeed);
            state.ShuffleWithinLevels = config.ShuffleWithinLevels;
            state.ShuffleSeed = static_cast<std::int32_t>(config.ShuffleSeed);
            state.PrefixCount = static_cast<std::int32_t>(config.PrefixCount);
            state.Channel = ProgressivePoissonChannelIndex(config.Channel);
            state.Backend = ProgressivePoissonBackendIndex(config.Backend);
            state.MeshSurfaceSampleCount =
                static_cast<std::int32_t>(config.MeshSurfaceSampleCount);
            state.MeshSurfaceSampleSeed =
                static_cast<std::int32_t>(config.MeshSurfaceSampleSeed);
            state.MeshSurfaceMinTriangleArea =
                static_cast<float>(config.MeshSurfaceMinTriangleArea);
            state.MeshSurfaceInterpolateNormals =
                config.MeshSurfaceInterpolateNormals;
            state.AutoRunOnEdit = config.AutoRunOnEdit;
            state.DebounceSeconds =
                static_cast<float>(config.DebounceSeconds);
        }

        [[nodiscard]] Runtime::SandboxEditorProgressivePoissonConfig
        BuildProgressivePoissonConfig() const
        {
            return Runtime::SandboxEditorProgressivePoissonConfig{
                .Dimension = static_cast<std::uint32_t>(
                    ProgressivePoisson.Dimension),
                .GridWidth = static_cast<std::uint32_t>(
                    ProgressivePoisson.GridWidth),
                .MaxLevels = static_cast<std::uint32_t>(
                    ProgressivePoisson.MaxLevels),
                .HashLoadFactor = ProgressivePoisson.HashLoadFactor,
                .RadiusAlpha = ProgressivePoisson.RadiusAlpha,
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
                .Channel = ProgressivePoissonChannelFromIndex(
                    ProgressivePoisson.Channel),
                .Backend = ProgressivePoissonBackendFromIndex(
                    ProgressivePoisson.Backend),
                .MeshSurfaceSampleCount = static_cast<std::uint32_t>(
                    ProgressivePoisson.MeshSurfaceSampleCount),
                .MeshSurfaceSampleSeed = static_cast<std::uint32_t>(
                    ProgressivePoisson.MeshSurfaceSampleSeed),
                .MeshSurfaceMinTriangleArea = static_cast<double>(
                    ProgressivePoisson.MeshSurfaceMinTriangleArea),
                .MeshSurfaceInterpolateNormals =
                    ProgressivePoisson.MeshSurfaceInterpolateNormals,
                .AutoRunOnEdit = ProgressivePoisson.AutoRunOnEdit,
                .DebounceSeconds = static_cast<double>(
                    ProgressivePoisson.DebounceSeconds),
            };
        }

        void DrawProgressivePoissonControls(
            const Runtime::SandboxEditorDomainWindowModel& model,
            const Runtime::SandboxEditorContext& context)
        {
            const Runtime::SandboxEditorGeometryProcessingModel& processing =
                model.Processing;
            ImGui::SeparatorText("Progressive Poisson");
            const bool meshInput =
                model.Kind == Runtime::SandboxEditorDomainWindowKind::Mesh;
            const bool available = meshInput
                ? processing.MeshProgressivePoissonAvailable
                : processing.PointCloudProgressivePoissonAvailable;
            if (!available)
            {
                ImGui::TextDisabled(
                    "Progressive Poisson is unavailable for this selection.");
                return;
            }

            const std::optional<Runtime::SandboxEditorProgressivePoissonConfig>
                activeConfig =
                    Runtime::GetSandboxEditorProgressivePoissonConfig(context);
            const bool configFacadeAvailable =
                activeConfig.has_value() &&
                context.PreviewEngineConfigDocument &&
                context.ApplyEngineConfigHotSubset &&
                context.EngineConfigCommandsAvailable;
            if (!configFacadeAvailable)
            {
                ImGui::TextDisabled(
                    "Progressive Poisson requires engine config-control.");
                return;
            }

            if (context.LastProgressivePoissonResult != nullptr)
            {
                ProgressivePoisson.LastResult =
                    *context.LastProgressivePoissonResult;
            }
            SyncProgressivePoissonState(ProgressivePoisson, *activeConfig);

            ProgressivePoisson.Dimension =
                ProgressivePoisson.Dimension <= 2 ? 2 : 3;
            bool configChanged = false;
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
                "Input dimensionality used by the reference sampler.");

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
            if (meshInput)
            {
                ProgressivePoisson.MeshSurfaceSampleCount = std::clamp(
                    ProgressivePoisson.MeshSurfaceSampleCount,
                    1,
                    10'000'000);
                ProgressivePoisson.MeshSurfaceSampleSeed = std::clamp(
                    ProgressivePoisson.MeshSurfaceSampleSeed,
                    0,
                    std::numeric_limits<std::int32_t>::max());
                if (!std::isfinite(
                        ProgressivePoisson.MeshSurfaceMinTriangleArea) ||
                    ProgressivePoisson.MeshSurfaceMinTriangleArea <= 0.0f)
                {
                    ProgressivePoisson.MeshSurfaceMinTriangleArea = 1.0e-14f;
                }
            }
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
                "Maximum progressive hierarchy levels to emit.");
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
                "Visible prefix count; zero shows all accepted points.");
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

            if (meshInput)
            {
                ImGui::SeparatorText("Surface input");
                configChanged |= ImGui::DragInt(
                    "Surface samples##ProgressivePoisson",
                    &ProgressivePoisson.MeshSurfaceSampleCount,
                    1.0f,
                    1,
                    10'000'000);
                DrawProgressivePoissonTooltip(
                    "Number of deterministic points sampled from the mesh surface.");
                configChanged |= ImGui::DragInt(
                    "Surface seed##ProgressivePoisson",
                    &ProgressivePoisson.MeshSurfaceSampleSeed,
                    1.0f,
                    0,
                    std::numeric_limits<std::int32_t>::max());
                DrawProgressivePoissonTooltip(
                    "Seed for deterministic mesh surface sampling.");
                configChanged |= ImGui::InputFloat(
                    "Min triangle area##ProgressivePoisson",
                    &ProgressivePoisson.MeshSurfaceMinTriangleArea,
                    0.0f,
                    0.0f,
                    "%.3e");
                DrawProgressivePoissonTooltip(
                    "Triangles below this area are rejected before sampling.");
                if (!std::isfinite(
                        ProgressivePoisson.MeshSurfaceMinTriangleArea) ||
                    ProgressivePoisson.MeshSurfaceMinTriangleArea <= 0.0f)
                {
                    ProgressivePoisson.MeshSurfaceMinTriangleArea = 1.0e-14f;
                }
                configChanged |= ImGui::Checkbox(
                    "Interpolate normals##ProgressivePoisson",
                    &ProgressivePoisson.MeshSurfaceInterpolateNormals);
                DrawProgressivePoissonTooltip(
                    "Interpolate vertex normals onto sampled surface points.");
            }

            const Runtime::SandboxEditorProgressivePoissonChannel channel =
                ProgressivePoissonChannelFromIndex(
                    ProgressivePoisson.Channel);
            if (ImGui::BeginCombo(
                    "Color channel##ProgressivePoisson",
                    Runtime::DebugNameForSandboxEditorProgressivePoissonChannel(
                        channel)))
            {
                for (std::size_t index = 0u;
                     index < kProgressivePoissonChannels.size();
                     ++index)
                {
                    const bool selected = ProgressivePoisson.Channel ==
                                          static_cast<std::int32_t>(index);
                    if (ImGui::Selectable(
                            Runtime::DebugNameForSandboxEditorProgressivePoissonChannel(
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
                "Scalar property used for point color visualization.");

            const Runtime::SandboxEditorProgressivePoissonBackend backend =
                ProgressivePoissonBackendFromIndex(
                    ProgressivePoisson.Backend);
            if (ImGui::BeginCombo(
                    "Backend##ProgressivePoisson",
                    Runtime::DebugNameForSandboxEditorProgressivePoissonBackend(
                        backend)))
            {
                for (std::size_t index = 0u;
                     index < kProgressivePoissonBackends.size();
                     ++index)
                {
                    const bool selected = ProgressivePoisson.Backend ==
                                          static_cast<std::int32_t>(index);
                    if (ImGui::Selectable(
                            Runtime::DebugNameForSandboxEditorProgressivePoissonBackend(
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
                "Requested method backend; the result reports the actual backend and any CPU fallback.");

            const auto applyConfig = [&]()
            {
                return Runtime::ApplySandboxEditorProgressivePoissonConfigCommand(
                    context,
                    Runtime::SandboxEditorProgressivePoissonConfigCommand{
                        .Config = BuildProgressivePoissonConfig(),
                        .SourceId = "sandbox.progressive_poisson",
                    });
            };
            const auto runSampler = [&]()
            {
                Runtime::SandboxEditorProgressivePoissonResult result =
                    Runtime::ApplySandboxEditorProgressivePoissonCommand(
                        context,
                        Runtime::SandboxEditorProgressivePoissonCommand{
                            .StableEntityId = model.SelectedStableId,
                            .Config = BuildProgressivePoissonConfig(),
                        });
                ProgressivePoisson.LastResult = result;
                if (context.MethodResultSinks.ProgressivePoisson)
                {
                    context.MethodResultSinks.ProgressivePoisson(
                        std::move(result));
                }
                ProgressivePoisson.AutoRunPending = false;
                ProgressivePoisson.PendingStableEntityId = 0u;
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

            if (ImGui::Button(
                    "Run Progressive Poisson##ProgressivePoisson"))
            {
                ProgressivePoisson.LastConfigResult = applyConfig();
                if (ProgressivePoisson.LastConfigResult->Succeeded())
                    runSampler();
            }

            if (ProgressivePoisson.AutoRunPending &&
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

            if (ProgressivePoisson.LastConfigResult.has_value() &&
                !ProgressivePoisson.LastConfigResult->Succeeded())
            {
                ImGui::TextWrapped(
                    "%s",
                    ProgressivePoisson.LastConfigResult->Message.c_str());
            }

            const std::optional<
                Runtime::SandboxEditorProgressivePoissonResult>& result =
                ProgressivePoisson.LastResult.has_value()
                    ? ProgressivePoisson.LastResult
                    : processing.LastProgressivePoissonResult;
            DrawProgressivePoissonResultStatus(result);
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

        static bool DrawParameterizationStrategy(
            SandboxParameterizationPanelConfig& config)
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
            SandboxParameterizationPanelConfig& config)
        {
            bool changed = DrawParameterizationStrategy(config);
            ImGui::SeparatorText("Strategy parameters");
            using Strategy = Runtime::SandboxEditorParameterizationStrategy;
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
            const std::optional<Runtime::SandboxEditorParameterizationResult>&
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
            if (!summary.Message.empty())
                ImGui::TextWrapped("%s", summary.Message.c_str());
        }

        void DrawParameterizationControlPane(
            const Runtime::SandboxEditorContext& context,
            const Runtime::SandboxEditorParameterizationViewModel& model)
        {
            if (!Parameterization.Initialized || !Parameterization.Dirty)
            {
                const auto active =
                    Runtime::GetSandboxEditorParameterizationConfig(context);
                if (active.has_value())
                {
                    Parameterization.Draft = *active;
                    Parameterization.Initialized = true;
                }
            }

            if (context.LastParameterizationResult != nullptr)
                Parameterization.LastResult = *context.LastParameterizationResult;

            Parameterization.Dirty |=
                DrawParameterizationConfigControls(Parameterization.Draft);
            if (Parameterization.Dirty)
                ImGui::TextDisabled("Draft has unapplied changes.");

            const bool configAvailable =
                context.EngineConfigControlState != nullptr &&
                context.EngineConfigCommandsAvailable;
            if (!configAvailable)
                ImGui::BeginDisabled();
            if (ImGui::Button("Apply configuration##Parameterization"))
            {
                Parameterization.LastConfigResult =
                    Runtime::ApplySandboxEditorParameterizationConfigCommand(
                        context,
                        Runtime::SandboxEditorParameterizationConfigCommand{
                            .Config = Parameterization.Draft,
                            .SourceId = "sandbox.parameterization.panel",
                        });
                if (Parameterization.LastConfigResult->Succeeded())
                    Parameterization.Dirty = false;
            }
            if (!configAvailable)
                ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Reload active##Parameterization"))
            {
                const auto active =
                    Runtime::GetSandboxEditorParameterizationConfig(context);
                if (active.has_value())
                {
                    Parameterization.Draft = *active;
                    Parameterization.Initialized = true;
                    Parameterization.Dirty = false;
                }
            }

            const bool canRun = configAvailable && model.HasSelectedEntity &&
                                model.SelectedEntityIsMesh;
            if (!canRun)
                ImGui::BeginDisabled();
            if (ImGui::Button("Parameterize selected mesh##Parameterization"))
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
            if (!canRun)
                ImGui::EndDisabled();

            const bool historyAvailable = context.CommandHistory != nullptr;
            const Runtime::EditorCommandHistorySnapshot history =
                historyAvailable
                    ? context.CommandHistory->Snapshot()
                    : Runtime::EditorCommandHistorySnapshot{};
            const bool canUndoUv =
                history.CanUndo && history.UndoLabel == "Parameterize mesh UVs";
            const bool canRedoUv =
                history.CanRedo && history.RedoLabel == "Parameterize mesh UVs";
            if (!canUndoUv)
                ImGui::BeginDisabled();
            if (ImGui::Button("Undo UV writeback##Parameterization") &&
                historyAvailable)
            {
                (void)context.CommandHistory->Undo();
            }
            if (!canUndoUv)
                ImGui::EndDisabled();
            ImGui::SameLine();
            if (!canRedoUv)
                ImGui::BeginDisabled();
            if (ImGui::Button("Redo UV writeback##Parameterization") &&
                historyAvailable)
            {
                (void)context.CommandHistory->Redo();
            }
            if (!canRedoUv)
                ImGui::EndDisabled();
            if (history.CanUndo && !canUndoUv)
            {
                ImGui::TextDisabled(
                    "Next global undo is '%s'; use File / Scene.",
                    history.UndoLabel.c_str());
            }

            if (Parameterization.LastConfigResult.has_value() &&
                !Parameterization.LastConfigResult->Message.empty())
            {
                ImGui::TextWrapped(
                    "%s",
                    Parameterization.LastConfigResult->Message.c_str());
            }
            DrawParameterizationResult(Parameterization.LastResult);
        }

        static ImVec2 ToImVec2(const glm::vec2 value) noexcept
        {
            return ImVec2{value.x, value.y};
        }

        void DrawParameterizationUvPane(
            const Runtime::SandboxEditorParameterizationViewModel& model)
        {
            ImGui::TextUnformatted("UV layout");
            ImGui::SameLine();
            if (ImGui::SmallButton("Fit##ParameterizationUv"))
            {
                Parameterization.Zoom = 1.0f;
                Parameterization.Pan = glm::vec2{0.0f};
            }
            ImGui::SameLine();
            ImGui::Checkbox("Grid##ParameterizationUv", &Parameterization.ShowGrid);
            ImGui::SameLine();
            ImGui::Checkbox(
                "Checker##ParameterizationUv",
                &Parameterization.ShowChecker);
            ImGui::SameLine();
            ImGui::TextDisabled("%.0f%%", Parameterization.Zoom * 100.0f);

            const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
            ImVec2 canvasSize = ImGui::GetContentRegionAvail();
            canvasSize.x = std::max(canvasSize.x, 80.0f);
            canvasSize.y = std::max(canvasSize.y, 80.0f);
            ImGui::InvisibleButton(
                "##ParameterizationUvCanvas",
                canvasSize);
            const bool hovered = ImGui::IsItemHovered();
            if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
            {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                Parameterization.Pan += glm::vec2{delta.x, delta.y};
            }
            if (hovered && ImGui::GetIO().MouseWheel != 0.0f)
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
            drawList->AddRect(
                canvasMin,
                canvasMax,
                IM_COL32(90, 96, 108, 255));

            const SandboxParameterizationUvProjection projection =
                BuildSandboxParameterizationUvProjection(
                    model,
                    SandboxParameterizationUvPane{
                        .Min = {canvasMin.x, canvasMin.y},
                        .Max = {canvasMax.x, canvasMax.y},
                        .Padding = 24.0f,
                        .Zoom = Parameterization.Zoom,
                        .Pan = Parameterization.Pan,
                    });
            drawList->PushClipRect(canvasMin, canvasMax, true);
            if (projection.Valid)
            {
                if (Parameterization.ShowChecker)
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
                if (Parameterization.ShowGrid)
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
                    const ImVec2 a = ToImVec2(projection.Vertices[triangle[0]]);
                    const ImVec2 b = ToImVec2(projection.Vertices[triangle[1]]);
                    const ImVec2 c = ToImVec2(projection.Vertices[triangle[2]]);
                    drawList->AddTriangleFilled(
                        a,
                        b,
                        c,
                        IM_COL32(66, 145, 214, 52));
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
            drawList->PopClipRect();
        }

        void DrawParameterizationWindow(
            bool& open,
            const Runtime::SandboxEditorContext& context)
        {
            ImGui::SetNextWindowSize(
                ImVec2(920.0f, 600.0f),
                ImGuiCond_FirstUseEver);
            if (ImGui::Begin(
                    "Mesh / Processing / Parameterize (UV)",
                    &open))
            {
                const Runtime::SandboxEditorParameterizationViewModel model =
                    Runtime::BuildSandboxEditorParameterizationViewModel(context);
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
                DrawParameterizationUvPane(model);
                ImGui::EndChild();
            }
            ImGui::End();
        }

        static void DrawProgressivePoissonResultStatus(
            const std::optional<
                Runtime::SandboxEditorProgressivePoissonResult>& lastResult)
        {
            if (!lastResult.has_value())
            {
                ImGui::TextDisabled("Last progressive Poisson run: none");
                return;
            }

            const Runtime::SandboxEditorProgressivePoissonResult& result =
                *lastResult;
            ImGui::Text(
                "Last progressive Poisson run: %s",
                Runtime::DebugNameForSandboxEditorCommandStatus(result.Status));
            ImGui::Text(
                "Channel: %s",
                Runtime::DebugNameForSandboxEditorProgressivePoissonChannel(
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
                if (result.MeshSurfaceSamplingUsed)
                {
                    ImGui::Text(
                        "Surface samples %u  triangles %u/%u  area %.6f",
                        result.MeshSurfaceSampleCount,
                        result.MeshSurfaceAcceptedTriangleCount,
                        result.MeshSurfaceTotalFaceCount,
                        result.MeshSurfaceArea);
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
