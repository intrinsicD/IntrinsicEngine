module;

#include <cstdio>
#include <cstddef>
#include <cstdint>
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
import Runtime.Selection;
import Runtime.SelectionModule;
import Runtime.SceneSerializer;

namespace Runtime::EditorUI
{
    using namespace Core::Hash;

    GeometryProcessingCapabilities GetGeometryProcessingCapabilities(const entt::registry& registry,
                                                                    entt::entity entity)
    {
        GeometryProcessingCapabilities capabilities{};
        if (entity == entt::null || !registry.valid(entity))
            return capabilities;

        if (registry.all_of<ECS::Surface::Component, ECS::MeshCollider::Component>(entity))
            capabilities.Domains |= GeometryProcessingDomain::SurfaceMesh;

        if (const auto* meshData = registry.try_get<ECS::Mesh::Data>(entity);
            meshData && meshData->MeshRef)
        {
            capabilities.Domains |= GeometryProcessingDomain::MeshVertices;
        }

        if (const auto* graphData = registry.try_get<ECS::Graph::Data>(entity);
            graphData && graphData->GraphRef)
        {
            capabilities.Domains |= GeometryProcessingDomain::GraphVertices;
        }

        if (const auto* pointCloudData = registry.try_get<ECS::PointCloud::Data>(entity);
            pointCloudData && pointCloudData->CloudRef && !pointCloudData->CloudRef->IsEmpty())
        {
            capabilities.Domains |= GeometryProcessingDomain::PointCloudPoints;
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
            return GeometryProcessingDomain::SurfaceMesh;
        default:
            return GeometryProcessingDomain::None;
        }
    }

    bool SupportsDomain(GeometryProcessingAlgorithm algorithm,
                        GeometryProcessingDomain domain) noexcept
    {
        return HasAnyDomain(GetSupportedDomains(algorithm), domain);
    }

    // File-local scene dirty tracker (one per process).
    static SceneDirtyTracker s_DirtyTracker;

    // Persistent UI state for editor panels.  Avoids function-local
    // static variables that retain stale content across panel hide/show.
    struct EditorPanelState
    {
        // Scene file dialogs
        char SavePath[512]   = "";
        char SaveAsPath[512] = "scene.json";
        char LoadPath[512]   = "scene.json";

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
            case 0: category = Core::FeatureCategory::RenderFeature; break;
            case 1: category = Core::FeatureCategory::System; break;
            case 2: category = Core::FeatureCategory::Panel; break;
            case 3: category = Core::FeatureCategory::GeometryOperator; break;
            default: break;
            }

            const auto list = reg.GetByCategory(category);

            if (ImGui::BeginTable("##features", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable))
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
        });

        // Add a menu entry to open it.
        Interface::GUI::RegisterMainMenuBar("Tools", []
        {
            if (ImGui::BeginMenu("Tools"))
            {
                if (ImGui::MenuItem("Features"))
                {
                    // Panel menu is global; toggling is handled there.
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
                const std::string label = "Layer " + std::to_string(li) + " (" + std::to_string(layer.size()) + " passes)";
                if (ImGui::TreeNode(label.c_str()))
                {
                    for (uint32_t passIndex : layer)
                    {
                        ImGui::BulletText("%s", std::string(fg.GetPassName(passIndex)).c_str());
                    }
                    ImGui::TreePop();
                }
            }
        });
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
                ImGui::RadioButton("Entity", &em, 0); ImGui::SameLine();
                ImGui::RadioButton("Vertex", &em, 1); ImGui::SameLine();
                ImGui::RadioButton("Edge",   &em, 2); ImGui::SameLine();
                ImGui::RadioButton("Face",   &em, 3);

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

                    const entt::entity selected = sel.GetSelectedEntity(engine.GetScene());
                    auto& reg = engine.GetScene().GetRegistry();

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
                cfg.Backend = backend == 1 ? Runtime::Selection::PickBackend::GPU : Runtime::Selection::PickBackend::CPU;

            const char* modes[] = {"Replace", "Add", "Toggle"};
            int mode = 0;
            switch (cfg.Mode)
            {
            case Runtime::Selection::PickMode::Replace: mode = 0; break;
            case Runtime::Selection::PickMode::Add: mode = 1; break;
            case Runtime::Selection::PickMode::Toggle: mode = 2; break;
            }
            if (ImGui::Combo("Default Mode (unused on click)", &mode, modes, IM_ARRAYSIZE(modes)))
            {
                cfg.Mode = (mode == 1) ? Runtime::Selection::PickMode::Add
                    : (mode == 2)       ? Runtime::Selection::PickMode::Toggle
                                        : Runtime::Selection::PickMode::Replace;
            }

            bool active = (cfg.Active == Runtime::SelectionModule::Activation::Enabled);
            if (ImGui::Checkbox("Active", &active))
                cfg.Active = active ? Runtime::SelectionModule::Activation::Enabled : Runtime::SelectionModule::Activation::Disabled;

            ImGui::SeparatorText("State");
            const entt::entity selected = sel.GetSelectedEntity(engine.GetScene());
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
                sel.ClearSelection(engine.GetScene());
        });
    }

    static void RegisterSceneFileMenu(Runtime::Engine& engine)
    {
        Interface::GUI::RegisterMainMenuBar("File", [&engine]
        {
            if (ImGui::BeginMenu("File"))
            {
                // ---- Save Scene ----
                if (ImGui::MenuItem("Save Scene"))
                {
                    ImGui::OpenPopup("SaveScenePopup");
                }

                // ---- Save Scene As... ----
                if (ImGui::MenuItem("Save Scene As..."))
                {
                    ImGui::OpenPopup("SaveSceneAsPopup");
                }

                // ---- Load Scene ----
                if (ImGui::MenuItem("Load Scene"))
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

    void RegisterDefaultPanels(Runtime::Engine& engine)
    {
        RegisterFeatureBrowserPanel(engine);
        RegisterFrameGraphInspectorPanel(engine);
        RegisterSelectionPanel(engine);
        RegisterSceneFileMenu(engine);
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
        auto& reg = engine.GetScene().GetRegistry();
        if (entity == entt::null || !reg.valid(entity))
            return;

        auto& dd = engine.GetRenderOrchestrator().GetDebugDraw();

        // Compute world transform for the entity.
        glm::mat4 world{1.0f};
        if (auto* xf = reg.try_get<ECS::Components::Transform::Component>(entity))
            world = ECS::Components::Transform::GetMatrix(*xf);

        // Sphere radius for vertex markers (world-space).
        constexpr float kVertexSphereRadius = 0.005f;
        constexpr uint32_t kVertexColor = Graphics::DebugDraw::PackColor(255, 40, 40); // Red
        constexpr uint32_t kGeodesicSourceColor = Graphics::DebugDraw::PackColor(40, 255, 120); // Green (geodesic source)
        constexpr uint32_t kEdgeColor = Graphics::DebugDraw::PackColor(255, 200, 40); // Yellow
        constexpr uint32_t kFaceColor = Graphics::DebugDraw::PackColor(40, 120, 255, 160); // Blue (semi-transparent)
        const bool geodesicActive = s_PanelState.GeodesicActive;

        // --- Mesh-based highlights ---
        if (auto* md = reg.try_get<ECS::Mesh::Data>(entity))
        {
            if (!md->MeshRef)
                return;

            auto& mesh = *md->MeshRef;

            // Vertex highlights: overlay spheres
            // In geodesic mode, source vertices are green; otherwise red.
            for (uint32_t vi : sub.SelectedVertices)
            {
                if (vi >= mesh.VerticesSize() || mesh.IsDeleted(Geometry::VertexHandle{vi}))
                    continue;
                const glm::vec3 localPos = mesh.Position(Geometry::VertexHandle{vi});
                const glm::vec3 worldPos = glm::vec3(world * glm::vec4(localPos, 1.0f));
                const uint32_t color = geodesicActive ? kGeodesicSourceColor : kVertexColor;
                dd.OverlaySphere(worldPos, kVertexSphereRadius, color);
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
                dd.OverlayLine(wA, wB, kEdgeColor);
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
                    dd.OverlayLine(faceVerts[i], faceVerts[j], kFaceColor);
                }

                // Draw filled triangles for face tinting (fan-triangulate)
                if (faceVerts.size() >= 3)
                {
                    glm::vec3 n = glm::normalize(glm::cross(faceVerts[1] - faceVerts[0],
                                                              faceVerts[2] - faceVerts[0]));
                    for (size_t i = 1; i + 1 < faceVerts.size(); ++i)
                        dd.Triangle(faceVerts[0], faceVerts[i], faceVerts[i + 1], n, kFaceColor);
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
                dd.OverlaySphere(worldPos, kVertexSphereRadius, kVertexColor);
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
                dd.OverlaySphere(worldPos, kVertexSphereRadius, kVertexColor);
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
                dd.OverlayLine(wA, wB, kEdgeColor);
            }
        }
    }
}
