module;

#include <algorithm>
#include <cstdio>
#include <cstddef>
#include <set>
#include <glm/glm.hpp>
#include <imgui.h>
#include <string>
#include <vector>
#include <entt/entity/entity.hpp>

module Runtime.EditorUI;

import Interface;

import Core.FeatureRegistry;
import Core.FrameGraph;
import Core.Hash;
import Core.Logging;
import Core.IOBackend;
import Core.Commands;

import Graphics.Components;

import Runtime.PointCloudKMeans;
import Runtime.Selection;
import Runtime.SelectionModule;
import Graphics.SubElementHighlightSettings;
import Runtime.SceneSerializer;

import Geometry.Geodesic;

namespace Runtime::EditorUI
{
    using namespace Core::Hash;

    namespace
    {
        constexpr GeometryProcessingDomain kMeshTopologyDomains =
            GeometryProcessingDomain::MeshVertices |
            GeometryProcessingDomain::MeshEdges |
            GeometryProcessingDomain::MeshHalfedges |
            GeometryProcessingDomain::MeshFaces;

        constexpr GeometryProcessingDomain kGraphTopologyDomains =
            GeometryProcessingDomain::GraphVertices |
            GeometryProcessingDomain::GraphEdges |
            GeometryProcessingDomain::GraphHalfedges;

        [[nodiscard]] constexpr bool IsSurfaceTopologyAlgorithm(GeometryProcessingAlgorithm algorithm) noexcept
        {
            switch (algorithm)
            {
            case GeometryProcessingAlgorithm::Remeshing:
            case GeometryProcessingAlgorithm::Simplification:
            case GeometryProcessingAlgorithm::Smoothing:
            case GeometryProcessingAlgorithm::Subdivision:
            case GeometryProcessingAlgorithm::Repair:
                return true;
            case GeometryProcessingAlgorithm::KMeans:
            case GeometryProcessingAlgorithm::NormalEstimation:
            case GeometryProcessingAlgorithm::ShortestPath:
            default:
                return false;
            }
        }
    }

    const char* GeometryDomainLabel(GeometryProcessingDomain domain) noexcept
    {
        switch (domain)
        {
        case GeometryProcessingDomain::MeshVertices: return "Mesh Vertices";
        case GeometryProcessingDomain::MeshEdges: return "Mesh Edges";
        case GeometryProcessingDomain::MeshHalfedges: return "Mesh Halfedges";
        case GeometryProcessingDomain::MeshFaces: return "Mesh Faces";
        case GeometryProcessingDomain::GraphVertices: return "Graph Nodes";
        case GeometryProcessingDomain::GraphEdges: return "Graph Edges";
        case GeometryProcessingDomain::GraphHalfedges: return "Graph Halfedges";
        case GeometryProcessingDomain::PointCloudPoints: return "Point Cloud Points";
        case GeometryProcessingDomain::None:
        default: return "None";
        }
    }

    const char* GeometryProcessingAlgorithmLabel(GeometryProcessingAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
        case GeometryProcessingAlgorithm::KMeans: return "K-Means";
        case GeometryProcessingAlgorithm::Remeshing: return "Remeshing";
        case GeometryProcessingAlgorithm::Simplification: return "Simplification";
        case GeometryProcessingAlgorithm::Smoothing: return "Smoothing";
        case GeometryProcessingAlgorithm::Subdivision: return "Subdivision";
        case GeometryProcessingAlgorithm::Repair: return "Repair";
        case GeometryProcessingAlgorithm::NormalEstimation: return "Normal Estimation";
        case GeometryProcessingAlgorithm::ShortestPath: return "Shortest Path";
        default: return "Unknown";
        }
    }

    GeometryProcessingCapabilities GetGeometryProcessingCapabilities(const entt::registry& registry,
                                                                     entt::entity entity)
    {
        GeometryProcessingCapabilities capabilities{};
        if (entity == entt::null || !registry.valid(entity))
            return capabilities;

        if (const auto* meshData = registry.try_get<ECS::Mesh::Data>(entity);
            meshData && meshData->MeshRef)
        {
            capabilities.Domains |= kMeshTopologyDomains;
        }
        else if (const auto* collider = registry.try_get<ECS::MeshCollider::Component>(entity);
                 collider && collider->CollisionRef && collider->CollisionRef->SourceMesh)
        {
            capabilities.Domains |= kMeshTopologyDomains;
        }

        if (const auto* graphData = registry.try_get<ECS::Graph::Data>(entity);
            graphData && graphData->GraphRef)
        {
            capabilities.Domains |= kGraphTopologyDomains;
        }

        if (const auto* pointCloudData = registry.try_get<ECS::PointCloud::Data>(entity);
            pointCloudData && ((pointCloudData->CloudRef && !pointCloudData->CloudRef->IsEmpty())
                            || pointCloudData->GpuPointCount > 0u))
        {
            capabilities.Domains |= GeometryProcessingDomain::PointCloudPoints;
        }

        if (const auto* collider = registry.try_get<ECS::MeshCollider::Component>(entity);
            registry.all_of<ECS::Surface::Component>(entity) && collider && collider->CollisionRef)
        {
            capabilities.HasEditableSurfaceMesh = true;
        }

        return capabilities;
    }

    GeometryProcessingDomain GetSupportedDomains(GeometryProcessingAlgorithm algorithm) noexcept
    {
        switch (algorithm)
        {
        case GeometryProcessingAlgorithm::KMeans:
            return GeometryProcessingDomain::MeshVertices
                | GeometryProcessingDomain::GraphVertices
                | GeometryProcessingDomain::PointCloudPoints;
        case GeometryProcessingAlgorithm::Remeshing:
        case GeometryProcessingAlgorithm::Simplification:
        case GeometryProcessingAlgorithm::Smoothing:
        case GeometryProcessingAlgorithm::Subdivision:
        case GeometryProcessingAlgorithm::Repair:
            return kMeshTopologyDomains;
        case GeometryProcessingAlgorithm::NormalEstimation:
            return GeometryProcessingDomain::PointCloudPoints;
        case GeometryProcessingAlgorithm::ShortestPath:
            return GeometryProcessingDomain::MeshVertices
                | GeometryProcessingDomain::GraphVertices;
        default:
            return GeometryProcessingDomain::None;
        }
    }

    bool SupportsDomain(GeometryProcessingAlgorithm algorithm,
                        GeometryProcessingDomain domain) noexcept
    {
        return HasAnyDomain(GetSupportedDomains(algorithm), domain);
    }

    std::vector<GeometryProcessingEntry> ResolveGeometryProcessingEntries(const entt::registry& registry,
                                                                          entt::entity entity)
    {
        const GeometryProcessingCapabilities capabilities = GetGeometryProcessingCapabilities(registry, entity);

        static constexpr std::array<GeometryProcessingAlgorithm, 8> kAlgorithmOrder = {
            GeometryProcessingAlgorithm::KMeans,
            GeometryProcessingAlgorithm::NormalEstimation,
            GeometryProcessingAlgorithm::ShortestPath,
            GeometryProcessingAlgorithm::Remeshing,
            GeometryProcessingAlgorithm::Simplification,
            GeometryProcessingAlgorithm::Smoothing,
            GeometryProcessingAlgorithm::Subdivision,
            GeometryProcessingAlgorithm::Repair,
        };

        std::vector<GeometryProcessingEntry> entries;
        entries.reserve(kAlgorithmOrder.size());
        for (const auto algorithm : kAlgorithmOrder)
        {
            if (IsSurfaceTopologyAlgorithm(algorithm) && !capabilities.HasEditableSurfaceMesh)
                continue;

            const GeometryProcessingDomain domains = capabilities.Domains & GetSupportedDomains(algorithm);
            if (domains == GeometryProcessingDomain::None)
                continue;
            entries.push_back(GeometryProcessingEntry{algorithm, domains});
        }
        return entries;
    }

    std::vector<Runtime::PointCloudKMeans::Domain> GetAvailableKMeansDomains(const entt::registry& registry,
                                                                             entt::entity entity)
    {
        const GeometryProcessingDomain available =
            GetGeometryProcessingCapabilities(registry, entity).Domains
            & GetSupportedDomains(GeometryProcessingAlgorithm::KMeans);

        std::vector<Runtime::PointCloudKMeans::Domain> domains;
        domains.reserve(3);

        if (HasAnyDomain(available, GeometryProcessingDomain::MeshVertices))
            domains.push_back(Runtime::PointCloudKMeans::Domain::MeshVertices);
        if (HasAnyDomain(available, GeometryProcessingDomain::GraphVertices))
            domains.push_back(Runtime::PointCloudKMeans::Domain::GraphVertices);
        if (HasAnyDomain(available, GeometryProcessingDomain::PointCloudPoints))
            domains.push_back(Runtime::PointCloudKMeans::Domain::PointCloudPoints);

        return domains;
    }

    // File-local scene dirty tracker (one per process).
    static SceneDirtyTracker s_DirtyTracker;

    // Persistent UI state for editor panels.  Avoids function-local
    // static variables that retain stale content across panel hide/show.
    struct EditorPanelState
    {
        // Scene file dialogs
        char SavePath[512] = "";
        char SaveAsPath[512] = "scene.json";
        char LoadPath[512] = "scene.json";
        char BenchmarkOutputPath[512] = "benchmark.json";
        int BenchmarkFrameCount = 300;
        int BenchmarkWarmupFrames = 30;

        // Feature browser
        int FeatureCategory = 0;

        // Geodesic distance
        bool GeodesicActive = false;
        entt::entity GeodesicEntity = entt::null;
        std::set<uint32_t> GeodesicSourceVertices;
        bool GeodesicDirty = false;
    };

    static EditorPanelState s_PanelState;

    SceneDirtyTracker& GetSceneDirtyTracker()
    {
        return s_DirtyTracker;
    }

    static void RegisterFeatureBrowserPanel(Runtime::Engine& engine)
    {
        Interface::GUI::RegisterPanel("Features", [&engine]()
        {
            auto& reg = engine.GetFeatureRegistry();

            int& cat = s_PanelState.FeatureCategory;
            const char* cats[] = {"RenderFeature", "System", "Panel", "GeometryOperator"};

            ImGui::TextDisabled("Feature Registry");
            ImGui::Separator();
            ImGui::Combo("Category", &cat, cats, IM_ARRAYSIZE(cats));

            Core::FeatureCategory category = Core::FeatureCategory::RenderFeature;
            switch (cat)
            {
            case 0: category = Core::FeatureCategory::RenderFeature;
                break;
            case 1: category = Core::FeatureCategory::System;
                break;
            case 2: category = Core::FeatureCategory::Panel;
                break;
            case 3: category = Core::FeatureCategory::GeometryOperator;
                break;
            default: break;
            }

            const auto list = reg.GetByCategory(category);

            if (ImGui::BeginTable("##features", 3,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
            {
                ImGui::TableSetupColumn("Enabled", ImGuiTableColumnFlags_WidthFixed, 70.0f);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const auto* info : list)
                {
                    if (!info) continue;

                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    bool enabled = reg.IsEnabled(info->Id);
                    const std::string chkId = std::string("##en_") + info->Name;
                    if (ImGui::Checkbox(chkId.c_str(), &enabled))
                        reg.SetEnabled(info->Id, enabled);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(info->Name.c_str());

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", info->Description.c_str());
                }

                ImGui::EndTable();
            }
        }, true, 0, false);

        // Add a menu entry to open it.
        Interface::GUI::RegisterMainMenuBar("Tools", []
        {
            if (ImGui::BeginMenu("Tools"))
            {
                if (ImGui::MenuItem("Features", "F12"))
                {
                    Interface::GUI::OpenPanel("Features");
                }
                ImGui::EndMenu();
            }
        });
    }

    static void RegisterFrameGraphInspectorPanel(Runtime::Engine& engine)
    {
        Interface::GUI::RegisterPanel("Frame Graph", [&engine]()
        {
            const auto& fg = engine.GetRenderOrchestrator().GetFrameGraph();
            const auto& layers = fg.GetExecutionLayers();

            ImGui::Text("Passes: %u", fg.GetPassCount());
            ImGui::Text("Layers: %u", (std::uint32_t)layers.size());
            ImGui::Separator();

            for (uint32_t li = 0; li < (uint32_t)layers.size(); ++li)
            {
                const auto& layer = layers[li];
                const std::string label = "Layer " + std::to_string(li) + " (" + std::to_string(layer.size()) +
                    " passes)";
                if (ImGui::TreeNode(label.c_str()))
                {
                    for (uint32_t passIndex : layer)
                    {
                        ImGui::BulletText("%s", std::string(fg.GetPassName(passIndex)).c_str());
                    }
                    ImGui::TreePop();
                }
            }
        }, true, 0, false);
    }

    static void RegisterSelectionPanel(Runtime::Engine& engine)
    {
        Interface::GUI::RegisterPanel("Selection", [&engine]()
        {
            auto& sel = engine.GetSelection();
            auto& cfg = sel.GetConfig();

            // --- Element Selection Mode (radio buttons) ---
            ImGui::SeparatorText("Selection Mode");
            {
                int em = static_cast<int>(cfg.ElementMode);
                ImGui::RadioButton("Entity", &em, 0);
                ImGui::SameLine();
                ImGui::RadioButton("Vertex", &em, 1);
                ImGui::SameLine();
                ImGui::RadioButton("Edge", &em, 2);
                ImGui::SameLine();
                ImGui::RadioButton("Face", &em, 3);

                const auto newMode = static_cast<Runtime::Selection::ElementMode>(em);
                if (newMode != cfg.ElementMode)
                {
                    cfg.ElementMode = newMode;
                    sel.ClearSubElementSelection();
                }
            }

            const auto& sub = sel.GetSubElementSelection();

            if (cfg.ElementMode != Runtime::Selection::ElementMode::Entity)
            {
                ImGui::TextDisabled("Hold Shift to add/toggle sub-elements.");
                if (!sub.SelectedVertices.empty())
                    ImGui::Text("Selected vertices: %zu", sub.SelectedVertices.size());
                if (!sub.SelectedEdges.empty())
                    ImGui::Text("Selected edges: %zu", sub.SelectedEdges.size());
                if (!sub.SelectedFaces.empty())
                    ImGui::Text("Selected faces: %zu", sub.SelectedFaces.size());

                if (!sub.Empty() && ImGui::Button("Clear Sub-Elements"))
                    sel.ClearSubElementSelection();
            }

            // --- Geodesic Distance Tool ---
            if (cfg.ElementMode == Runtime::Selection::ElementMode::Vertex)
            {
                ImGui::SeparatorText("Geodesic Distance");
                ImGui::Checkbox("Geodesic Mode", &s_PanelState.GeodesicActive);

                if (s_PanelState.GeodesicActive)
                {
                    ImGui::TextDisabled("Select source vertices, then click Compute.");
                    ImGui::Text("Source vertices: %zu", sub.SelectedVertices.size());

                    const entt::entity selected = sel.GetSelectedEntity(engine.GetSceneManager().GetScene());
                    auto& reg = engine.GetSceneManager().GetScene().GetRegistry();

                    bool canCompute = selected != entt::null
                        && !sub.SelectedVertices.empty()
                        && reg.valid(selected)
                        && reg.all_of<ECS::Mesh::Data>(selected);

                    if (!canCompute)
                        ImGui::BeginDisabled();

                    if (ImGui::Button("Compute Geodesic"))
                    {
                        auto* md = reg.try_get<ECS::Mesh::Data>(selected);
                        if (md && md->MeshRef)
                        {
                            std::vector<std::size_t> sources(sub.SelectedVertices.begin(),
                                                             sub.SelectedVertices.end());

                            auto result = Geometry::Geodesic::ComputeDistance(*md->MeshRef, sources);
                            if (result && result->Converged)
                            {
                                s_PanelState.GeodesicEntity = selected;
                                s_PanelState.GeodesicSourceVertices = sub.SelectedVertices;
                                md->AttributesDirty = true;
                                reg.emplace_or_replace<ECS::DirtyTag::VertexAttributes>(selected);
                                Core::Log::Info("Geodesic distance computed: heat={} iters, poisson={} iters",
                                                result->HeatSolveIterations, result->PoissonSolveIterations);
                            }
                            else
                            {
                                Core::Log::Warn("Geodesic computation failed or did not converge.");
                            }
                        }
                    }

                    if (!canCompute)
                        ImGui::EndDisabled();

                    if (s_PanelState.GeodesicEntity != entt::null)
                    {
                        ImGui::TextDisabled("Result stored as 'v:geodesic_distance' property.");
                        ImGui::TextDisabled("Use Vertex Color Source to visualize.");
                    }
                }
            }

            ImGui::SeparatorText("Config");
            ImGui::SliderInt("Mouse Button", &cfg.MouseButton, 0, 2);
            ImGui::SliderFloat("Pick Radius (px)", &cfg.PickRadiusPixels, 1.0f, 32.0f, "%.1f");

            const char* backends[] = {"CPU", "GPU"};
            int backend = (cfg.Backend == Runtime::Selection::PickBackend::GPU) ? 1 : 0;
            if (ImGui::Combo("Pick Backend", &backend, backends, IM_ARRAYSIZE(backends)))
                cfg.Backend = backend == 1
                                  ? Runtime::Selection::PickBackend::GPU
                                  : Runtime::Selection::PickBackend::CPU;

            const char* modes[] = {"Replace", "Add", "Toggle"};
            int mode = 0;
            switch (cfg.Mode)
            {
            case Runtime::Selection::PickMode::Replace: mode = 0;
                break;
            case Runtime::Selection::PickMode::Add: mode = 1;
                break;
            case Runtime::Selection::PickMode::Toggle: mode = 2;
                break;
            }
            if (ImGui::Combo("Default Mode (unused on click)", &mode, modes, IM_ARRAYSIZE(modes)))
            {
                cfg.Mode = (mode == 1)
                               ? Runtime::Selection::PickMode::Add
                               : (mode == 2)
                               ? Runtime::Selection::PickMode::Toggle
                               : Runtime::Selection::PickMode::Replace;
            }

            bool active = (cfg.Active == Runtime::SelectionModule::Activation::Enabled);
            if (ImGui::Checkbox("Active", &active))
                cfg.Active = active
                                 ? Runtime::SelectionModule::Activation::Enabled
                                 : Runtime::SelectionModule::Activation::Disabled;

            // --- Highlight Appearance ---
            ImGui::SeparatorText("Highlight Appearance");
            {
                auto& hl = sel.GetHighlightSettings();

                if (ImGui::TreeNodeEx("Vertex##hl", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ColorEdit4("Color##hl_vtx", hl.VertexColor);
                    ColorEdit4("Geodesic Color##hl_vtx", hl.VertexGeodesicColor);
                    ImGui::SliderFloat("Sphere Radius##hl", &hl.VertexSphereRadius, 0.001f, 0.05f, "%.4f");
                    int seg = static_cast<int>(hl.VertexSphereSegments);
                    if (ImGui::SliderInt("Sphere Segments##hl", &seg, 6, 48))
                        hl.VertexSphereSegments = static_cast<uint32_t>(seg);
                    ImGui::TreePop();
                }

                if (ImGui::TreeNodeEx("Edge##hl", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ColorEdit4("Color##hl_edge", hl.EdgeColor);
                    ImGui::TreePop();
                }

                if (ImGui::TreeNodeEx("Face##hl", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    ColorEdit4("Outline Color##hl_face", hl.FaceOutlineColor);
                    ColorEdit4("Fill Color##hl_face", hl.FaceFillColor);
                    ImGui::TreePop();
                }
            }

            ImGui::SeparatorText("State");
            const entt::entity selected = sel.GetSelectedEntity(engine.GetSceneManager().GetScene());
            ImGui::Text("Selected entity: %u", (uint32_t)selected);

            const auto& picked = sel.GetPicked();
            ImGui::Text("Picked entity: %u (%s)",
                        static_cast<uint32_t>(picked.entity.id),
                        picked.entity ? "hit" : "background");
            ImGui::Text("vertex=%u edge=%u face=%u radius=%.5f",
                        picked.entity.vertex_idx,
                        picked.entity.edge_idx,
                        picked.entity.face_idx,
                        picked.entity.pick_radius);
            ImGui::Text("world=(%.3f, %.3f, %.3f)",
                        picked.spaces.World.x,
                        picked.spaces.World.y,
                        picked.spaces.World.z);

            if (ImGui::Button("Clear Selection"))
                sel.ClearSelection(engine.GetSceneManager().GetScene());
        }, true, 0, false);
    }

    static void RegisterBenchmarkPanel(Runtime::Engine& engine)
    {
        const Runtime::EngineConfig& cfg = engine.GetEngineConfig();
        s_PanelState.BenchmarkFrameCount = static_cast<int>(std::max(1u, cfg.BenchmarkFrames));
        s_PanelState.BenchmarkWarmupFrames = static_cast<int>(cfg.BenchmarkWarmupFrames);
        std::snprintf(s_PanelState.BenchmarkOutputPath,
                      sizeof(s_PanelState.BenchmarkOutputPath),
                      "%s",
                      cfg.BenchmarkOutputPath.c_str());

        Interface::GUI::RegisterPanel("Benchmark", [&engine]()
        {
            auto& runner = engine.GetBenchmarkRunner();

            ImGui::TextDisabled("Capture telemetry stats to JSON.");
            ImGui::Separator();

            ImGui::InputInt("Frame Count", &s_PanelState.BenchmarkFrameCount);
            ImGui::InputInt("Warmup Frames", &s_PanelState.BenchmarkWarmupFrames);
            ImGui::InputText("Output Path",
                             s_PanelState.BenchmarkOutputPath,
                             sizeof(s_PanelState.BenchmarkOutputPath));

            s_PanelState.BenchmarkFrameCount = std::max(1, s_PanelState.BenchmarkFrameCount);
            s_PanelState.BenchmarkWarmupFrames = std::max(0, s_PanelState.BenchmarkWarmupFrames);

            if (ImGui::Button("Run Benchmark"))
            {
                Core::Benchmark::BenchmarkConfig runCfg{};
                runCfg.FrameCount = static_cast<uint32_t>(s_PanelState.BenchmarkFrameCount);
                runCfg.WarmupFrames = static_cast<uint32_t>(s_PanelState.BenchmarkWarmupFrames);
                runCfg.OutputPath = s_PanelState.BenchmarkOutputPath;
                runner.Configure(runCfg);
                runner.Start();
                Core::Log::Info("Benchmark panel: started run (frames={}, warmup={}, output='{}').",
                                runCfg.FrameCount,
                                runCfg.WarmupFrames,
                                runCfg.OutputPath);
            }

            ImGui::SameLine();
            ImGui::TextUnformatted(runner.IsRunning() ? "Status: Running" : "Status: Idle");
            ImGui::Text("Recorded Frames: %u", runner.FramesRecorded());
            ImGui::Text("Warmup: %s", runner.IsWarmingUp() ? "Yes" : "No");

            if (!runner.IsRunning() && runner.FramesRecorded() > 0)
            {
                const auto stats = runner.ComputeStats();
                ImGui::SeparatorText("Summary");
                ImGui::Text("Frames: %u", stats.TotalFrames);
                ImGui::Text("Avg / Min / Max (ms): %.3f / %.3f / %.3f",
                            stats.AvgFrameTimeMs,
                            stats.MinFrameTimeMs,
                            stats.MaxFrameTimeMs);
                ImGui::Text("p95 / p99 (ms): %.3f / %.3f", stats.P95FrameTimeMs, stats.P99FrameTimeMs);
                ImGui::Text("Avg CPU / GPU (ms): %.3f / %.3f", stats.AvgCpuTimeMs, stats.AvgGpuTimeMs);
                ImGui::Text("Avg FPS: %.2f", stats.AvgFPS);

                if (ImGui::CollapsingHeader("Per-Pass Averages", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::BeginTable("##benchmark_passes", 3,
                                          ImGuiTableFlags_Borders |
                                              ImGuiTableFlags_RowBg |
                                              ImGuiTableFlags_Resizable |
                                              ImGuiTableFlags_SizingStretchSame))
                    {
                        ImGui::TableSetupColumn("Pass");
                        ImGui::TableSetupColumn("GPU (ms)");
                        ImGui::TableSetupColumn("CPU (ms)");
                        ImGui::TableHeadersRow();

                        for (const auto& pass : stats.PassAverages)
                        {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(pass.Name.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%.3f", pass.AvgGpuMs);
                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("%.3f", pass.AvgCpuMs);
                        }
                        ImGui::EndTable();
                    }
                }
            }
        }, true, 0, false);
    }

    static void RegisterSceneFileMenu(Runtime::Engine& engine)
    {
        Interface::GUI::RegisterMainMenuBar("File", [&engine]
        {
            if (ImGui::BeginMenu("File"))
            {
                // ---- Save Scene ----
                if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
                {
                    ImGui::OpenPopup("SaveScenePopup");
                }

                // ---- Save Scene As... ----
                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S"))
                {
                    ImGui::OpenPopup("SaveSceneAsPopup");
                }

                // ---- Load Scene ----
                if (ImGui::MenuItem("Load Scene", "Ctrl+O"))
                {
                    ImGui::OpenPopup("LoadScenePopup");
                }

                ImGui::EndMenu();
            }

            // ---- Save Scene popup (use current path or ask) ----
            if (ImGui::BeginPopupModal("SaveScenePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                auto& tracker = GetSceneDirtyTracker();
                auto& savePath = s_PanelState.SavePath;

                // Pre-fill with current path if empty
                if (savePath[0] == '\0' && !tracker.GetCurrentPath().empty())
                {
                    std::snprintf(savePath, sizeof(savePath), "%s", tracker.GetCurrentPath().c_str());
                }

                if (savePath[0] == '\0')
                {
                    std::snprintf(savePath, sizeof(savePath), "scene.json");
                }

                ImGui::Text("Save scene to:");
                ImGui::InputText("##savepath", savePath, sizeof(savePath));

                if (ImGui::Button("Save", ImVec2(120, 0)))
                {
                    auto result = SaveScene(engine, std::string(savePath), engine.GetIOBackend());
                    if (result)
                    {
                        tracker.SetCurrentPath(savePath);
                        tracker.ClearDirty();
                        Core::Log::Info("Scene saved to: {}", savePath);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            // ---- Save As popup ----
            if (ImGui::BeginPopupModal("SaveSceneAsPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                auto& saveAsPath = s_PanelState.SaveAsPath;

                ImGui::Text("Save scene to:");
                ImGui::InputText("##saveaspath", saveAsPath, sizeof(saveAsPath));

                if (ImGui::Button("Save", ImVec2(120, 0)))
                {
                    auto result = SaveScene(engine, std::string(saveAsPath), engine.GetIOBackend());
                    if (result)
                    {
                        auto& tracker = GetSceneDirtyTracker();
                        tracker.SetCurrentPath(saveAsPath);
                        tracker.ClearDirty();
                        Core::Log::Info("Scene saved to: {}", saveAsPath);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            // ---- Load Scene popup ----
            if (ImGui::BeginPopupModal("LoadScenePopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                auto& loadPath = s_PanelState.LoadPath;

                // Dirty-state warning
                auto& tracker = GetSceneDirtyTracker();
                if (tracker.IsDirty())
                {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f),
                                       "Warning: unsaved changes will be lost.");
                    ImGui::Separator();
                }

                ImGui::Text("Load scene from:");
                ImGui::InputText("##loadpath", loadPath, sizeof(loadPath));

                if (ImGui::Button("Load", ImVec2(120, 0)))
                {
                    auto result = LoadScene(engine, std::string(loadPath), engine.GetIOBackend());
                    if (result)
                    {
                        tracker.SetCurrentPath(loadPath);
                        tracker.ClearDirty();
                        Core::Log::Info("Scene loaded: {} entities, {} assets, {} failed",
                                        result->EntitiesLoaded, result->AssetsLoaded, result->AssetsFailed);
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        });
    }

    static void RegisterEditMenu(Runtime::Engine& engine)
    {
        Interface::GUI::RegisterMainMenuBar("Edit", [&engine]
        {
            Core::CommandHistory& history = engine.GetCommandHistory();
            const bool canUndo = history.CanUndo();
            const bool canRedo = history.CanRedo();

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo))
                    (void)history.Undo();

                if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, canRedo))
                    (void)history.Redo();

                ImGui::EndMenu();
            }

            const ImGuiIO& io = ImGui::GetIO();
            if (!io.WantTextInput && !ImGui::IsAnyItemActive())
            {
                if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false) && canRedo)
                    (void)history.Redo();
                else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false) && canRedo)
                    (void)history.Redo();
                else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false) && canUndo)
                    (void)history.Undo();
            }
        });
    }

    void RegisterDefaultPanels(Runtime::Engine& engine)
    {
        RegisterFeatureBrowserPanel(engine);
        RegisterFrameGraphInspectorPanel(engine);
        RegisterSelectionPanel(engine);
        RegisterBenchmarkPanel(engine);
        RegisterSceneFileMenu(engine);
        RegisterEditMenu(engine);
    }

    // =========================================================================
    // Sub-element highlight rendering via DebugDraw
    // =========================================================================
    void DrawSubElementHighlights(Runtime::Engine& engine)
    {
        const auto& sel = engine.GetSelection();
        const auto& sub = sel.GetSubElementSelection();
        const auto elemMode = sel.GetConfig().ElementMode;

        if (elemMode == Runtime::Selection::ElementMode::Entity || sub.Empty())
            return;

        const entt::entity entity = sub.Entity;
        auto& reg = engine.GetSceneManager().GetScene().GetRegistry();
        if (entity == entt::null || !reg.valid(entity))
            return;

        auto& dd = engine.GetRenderOrchestrator().GetDebugDraw();
        const auto& hl = sel.GetHighlightSettings();

        // Pack glm::vec4 RGBA to uint32 ABGR for DebugDraw.
        auto pack = [](const glm::vec4& c) {
            return Graphics::DebugDraw::PackColorF(c.r, c.g, c.b, c.a);
        };

        // Compute world transform for the entity.
        glm::mat4 world{1.0f};
        if (auto* xf = reg.try_get<ECS::Components::Transform::Component>(entity))
            world = ECS::Components::Transform::GetMatrix(*xf);

        const float vertexRadius    = std::max(hl.VertexSphereRadius, 0.0001f);
        const uint32_t vertexSegs   = std::clamp(hl.VertexSphereSegments, 6u, 128u);
        const uint32_t vertexColor  = pack(hl.VertexColor);
        const uint32_t geodesicColor = pack(hl.VertexGeodesicColor);
        const uint32_t edgeColor    = pack(hl.EdgeColor);
        const uint32_t faceOutline  = pack(hl.FaceOutlineColor);
        const uint32_t faceFill     = pack(hl.FaceFillColor);
        const bool geodesicActive   = s_PanelState.GeodesicActive;

        // --- Mesh-based highlights ---
        if (auto* md = reg.try_get<ECS::Mesh::Data>(entity))
        {
            if (!md->MeshRef)
                return;

            auto& mesh = *md->MeshRef;

            // Vertex highlights: overlay spheres
            // In geodesic mode, source vertices are green; otherwise configurable color.
            for (uint32_t vi : sub.SelectedVertices)
            {
                if (vi >= mesh.VerticesSize() || mesh.IsDeleted(Geometry::VertexHandle{vi}))
                    continue;
                const glm::vec3 localPos = mesh.Position(Geometry::VertexHandle{vi});
                const glm::vec3 worldPos = glm::vec3(world * glm::vec4(localPos, 1.0f));
                const uint32_t color = geodesicActive ? geodesicColor : vertexColor;
                dd.OverlaySphere(worldPos, vertexRadius, color, vertexSegs);
            }

            // Edge highlights: overlay lines
            for (uint32_t ei : sub.SelectedEdges)
            {
                if (ei >= mesh.EdgesSize() || mesh.IsDeleted(Geometry::EdgeHandle{ei}))
                    continue;
                const auto h = mesh.Halfedge(Geometry::EdgeHandle{ei}, 0);
                const auto v0 = mesh.FromVertex(h);
                const auto v1 = mesh.ToVertex(h);
                const glm::vec3 wA = glm::vec3(world * glm::vec4(mesh.Position(v0), 1.0f));
                const glm::vec3 wB = glm::vec3(world * glm::vec4(mesh.Position(v1), 1.0f));
                dd.OverlayLine(wA, wB, edgeColor);
            }

            // Face highlights: overlay triangle edges (thick outline)
            for (uint32_t fi : sub.SelectedFaces)
            {
                if (fi >= mesh.FacesSize() || mesh.IsDeleted(Geometry::FaceHandle{fi}))
                    continue;

                std::vector<glm::vec3> faceVerts;
                for (auto vh : mesh.VerticesAroundFace(Geometry::FaceHandle{fi}))
                    faceVerts.push_back(glm::vec3(world * glm::vec4(mesh.Position(vh), 1.0f)));

                // Draw face outline as overlay lines
                for (size_t i = 0; i < faceVerts.size(); ++i)
                {
                    const size_t j = (i + 1) % faceVerts.size();
                    dd.OverlayLine(faceVerts[i], faceVerts[j], faceOutline);
                }

                // Draw filled triangles for face tinting (fan-triangulate)
                if (faceVerts.size() >= 3)
                {
                    glm::vec3 n = glm::normalize(glm::cross(faceVerts[1] - faceVerts[0],
                                                            faceVerts[2] - faceVerts[0]));
                    for (size_t i = 1; i + 1 < faceVerts.size(); ++i)
                        dd.Triangle(faceVerts[0], faceVerts[i], faceVerts[i + 1], n, faceFill);
                }
            }

            return;
        }

        // --- Point cloud highlights (vertex mode only) ---
        if (auto* pcd = reg.try_get<ECS::PointCloud::Data>(entity))
        {
            if (!pcd->CloudRef || pcd->CloudRef->IsEmpty())
                return;

            auto positions = pcd->CloudRef->Positions();
            for (uint32_t vi : sub.SelectedVertices)
            {
                if (vi >= positions.size())
                    continue;
                const glm::vec3 worldPos = glm::vec3(world * glm::vec4(positions[vi], 1.0f));
                dd.OverlaySphere(worldPos, vertexRadius, vertexColor, vertexSegs);
            }
            return;
        }

        // --- Graph highlights ---
        if (auto* gd = reg.try_get<ECS::Graph::Data>(entity))
        {
            if (!gd->GraphRef)
                return;

            auto& graph = *gd->GraphRef;

            // Node highlights (vertex mode)
            for (uint32_t vi : sub.SelectedVertices)
            {
                if (vi >= graph.VerticesSize())
                    continue;
                const glm::vec3 localPos = graph.VertexPosition(Geometry::VertexHandle{vi});
                const glm::vec3 worldPos = glm::vec3(world * glm::vec4(localPos, 1.0f));
                dd.OverlaySphere(worldPos, vertexRadius, vertexColor, vertexSegs);
            }

            // Edge highlights
            for (uint32_t ei : sub.SelectedEdges)
            {
                if (ei >= graph.EdgesSize())
                    continue;
                const auto h = graph.Halfedge(Geometry::EdgeHandle{ei}, 0);
                const auto v0 = graph.ToVertex(h);
                const auto v1 = graph.ToVertex(graph.Halfedge(Geometry::EdgeHandle{ei}, 1));
                const glm::vec3 wA = glm::vec3(world * glm::vec4(graph.VertexPosition(v0), 1.0f));
                const glm::vec3 wB = glm::vec3(world * glm::vec4(graph.VertexPosition(v1), 1.0f));
                dd.OverlayLine(wA, wB, edgeColor);
            }
        }
    }
}
