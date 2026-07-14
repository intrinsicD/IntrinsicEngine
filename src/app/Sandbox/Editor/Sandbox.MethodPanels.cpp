module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

module Extrinsic.Sandbox.Editor.MethodPanels;

import Extrinsic.Sandbox.Editor.Shell;

import Extrinsic.Runtime.EditorWindowRegistry;
import Extrinsic.Runtime.SandboxEditorFacades;

namespace Extrinsic::Sandbox::Editor
{
    namespace
    {
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

        EditorShell* Shell{nullptr};
        std::vector<Runtime::EditorWindowHandle> Handles{};
        int CachedModelFrame{-1};
        std::array<
            std::optional<Runtime::SandboxEditorDomainWindowModel>,
            3u>
            CachedDomainModels{};
        KMeansState KMeans{};
        ProgressivePoissonState ProgressivePoisson{};

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
