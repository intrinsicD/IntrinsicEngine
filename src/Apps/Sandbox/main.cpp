// Sandbox Application — main entry point and SandboxApp orchestration class.
//
// The three controller subsystems (SpatialDebugController, GeometryWorkflowController,
// InspectorController) live in the Runtime.EditorUI module. main.cpp contains only
// bootstrap (main), asset setup (OnStart), and per-frame orchestration (OnUpdate).

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <functional>
#include <memory>
#include <filesystem>
#include <algorithm>
#include <charconv>
#include <set>
#include <span>
#include <cmath>
#include <optional>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <imgui.h>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

import Runtime.Engine;
import Runtime.GraphicsBackend;
import Runtime.AssetPipeline;
import Runtime.RenderOrchestrator;
import Runtime.SelectionModule;
import Runtime.Selection;
import Runtime.EditorUI;

import Core.Logging;
import Core.Commands;
import Core.Filesystem;
import Core.Assets;
import Core.FrameGraph;
import Core.FeatureRegistry;
import Core.Hash;
import Core.Input;
import Graphics.Camera;
import Graphics.Components;
import Graphics.AssetErrors;
import Graphics.FeatureCatalog;
import Graphics.Material;
import Graphics.Model;
import Graphics.ModelLoader;
import Graphics.Passes.SelectionOutline;
import Graphics.Passes.SelectionOutlineSettings;
import Graphics.Passes.PostProcessSettings;
import Graphics.Pipelines;
import Graphics.RenderDriver;
import Graphics.RenderPipeline;
import Graphics.TextureLoader;
import Graphics.TransformGizmo;
import Graphics.Geometry;
import Graphics.OverlayEntityFactory;
import Geometry;
import ECS;
import RHI.Device;
import RHI.Transfer;
import RHI.Texture;
import Interface;
import Core.Telemetry;


using namespace Core;
using namespace Core::Hash;
using namespace Runtime;

// =============================================================================
// SandboxApp — Application class. Owns subsystem controllers, asset handles,
// and orchestrates the per-frame update pipeline.
// =============================================================================
class SandboxApp : public Engine
{
public:
    static Runtime::EngineConfig MakeDefaultConfig()
    {
        Runtime::EngineConfig config{};
        config.AppName = "Sandbox";
        config.Width = 1600;
        config.Height = 900;
        return config;
    }

    explicit SandboxApp(const Runtime::EngineConfig& config = MakeDefaultConfig())
        : Engine(config)
    {
    }

    // -------------------------------------------------------------------------
    // OnStart — subsystem wiring, asset loading, panel registration
    // -------------------------------------------------------------------------
    void OnStart() override
    {
        Log::Info("Sandbox Started!");

        auto& gfx = GetGraphicsBackend();

        // Camera
        auto &sceneManager = GetSceneManager();
        auto &scene = sceneManager.GetScene();
        auto &registry = scene.GetRegistry();
        m_CameraEntity = scene.CreateEntity("Main Camera");
        m_Camera = registry.emplace<Graphics::CameraComponent>(m_CameraEntity);
        registry.emplace<Graphics::OrbitControlComponent>(m_CameraEntity);

        // Cache selected entity via dispatcher sink instead of polling every frame.
        scene.GetDispatcher().sink<ECS::Events::SelectionChanged>().connect<
            [](entt::entity& cached, const ECS::Events::SelectionChanged& evt)
            {
                cached = evt.Entity;
            }>(m_CachedSelectedEntity);

        // Connect SelectionModule to the scene dispatcher so GPU pick results
        // arrive via GpuPickCompleted event instead of per-frame polling.
        GetSelection().ConnectToScene(scene);

        // --- Asset loading ---
        LoadDuckAssets(gfx);

        // --- Register client features in the central FeatureRegistry ---
        RegisterFeatures();

        // --- Initialize extracted controller subsystems ---
        m_GeometryWorkflow.Init(*this, m_CachedSelectedEntity);
        m_Inspector.Init(*this, m_CachedSelectedEntity, &m_GeometryWorkflow);

        // --- Register panels ---
        RegisterPanels();
    }

    // -------------------------------------------------------------------------
    // OnUpdate — ordered orchestration pipeline
    // -------------------------------------------------------------------------
    void OnUpdate(float dt) override
    {
        auto& sceneManager = GetSceneManager();
        auto& scene = sceneManager.GetScene();
        auto& registry = scene.GetRegistry();

        if (auto completed = m_Gizmo.ConsumeCompletedInteraction(); completed && !completed->Changes.empty())
            CommitGizmoCommand(registry, std::move(*completed));

        GetAssetPipeline().GetAssetManager().Update();

        bool uiCapturesMouse = Interface::GUI::WantCaptureMouse();
        bool uiCapturesKeyboard = Interface::GUI::WantCaptureKeyboard();
        bool inputCaptured = uiCapturesMouse || uiCapturesKeyboard;

        float aspectRatio = 1.0f;
        if (m_Window->GetWindowHeight() > 0)
        {
            aspectRatio = (float)m_Window->GetWindowWidth() / (float)m_Window->GetWindowHeight();
        }

        // 1. Camera update
        Graphics::CameraComponent* cameraComponent = nullptr;
        if (registry.valid(m_CameraEntity))
        {
            cameraComponent = registry.try_get<Graphics::CameraComponent>(m_CameraEntity);
            if (auto* orbit = registry.try_get<Graphics::OrbitControlComponent>(m_CameraEntity))
            {
                Graphics::OnUpdate(*cameraComponent, *orbit, m_Window->GetInput(), dt, inputCaptured);
            }
            else if (auto* fly = registry.try_get<Graphics::FlyControlComponent>(m_CameraEntity))
            {
                Graphics::OnUpdate(*cameraComponent, *fly, m_Window->GetInput(), dt, inputCaptured);
            }

            if (m_Window->GetWindowWidth() != 0 && m_Window->GetWindowHeight() != 0)
            {
                Graphics::OnResize(*cameraComponent, m_Window->GetWindowWidth(), m_Window->GetWindowHeight());
            }

            // 2. Camera keyboard shortcuts (F/C/Q)
            HandleCameraShortcuts(uiCapturesKeyboard, cameraComponent);

            // 3. Gizmo mode shortcuts (W/E/R/X)
            if (!uiCapturesKeyboard && !m_Gizmo.IsActive())
            {
                if (m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::W))
                    m_Gizmo.SetMode(Graphics::GizmoMode::Translate);
                if (m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::E))
                    m_Gizmo.SetMode(Graphics::GizmoMode::Rotate);
                if (m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::R))
                    m_Gizmo.SetMode(Graphics::GizmoMode::Scale);
                if (m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::X))
                {
                    m_Gizmo.GetConfig().Space =
                        (m_Gizmo.GetConfig().Space == Graphics::GizmoSpace::World)
                            ? Graphics::GizmoSpace::Local
                            : Graphics::GizmoSpace::World;
                }
            }
        }

        // 4. Update camera matrices
        {
            auto view = registry.view<Graphics::CameraComponent>();
            for (auto [entity, cam] : view.each())
            {
                Graphics::UpdateMatrices(cam, aspectRatio);
            }
        }

        // 5. Spawn duck model once loaded
        if (!m_IsEntitySpawned)
        {
            if (GetAssetPipeline().GetAssetManager().GetState(m_DuckModel) == Assets::LoadState::Ready)
            {
                m_DuckEntity = SpawnModel(m_DuckModel, m_DuckMaterialHandle, glm::vec3(0.0f), glm::vec3(0.01f));
                if (m_DuckEntity != entt::null)
                    GetSelection().SetSelectedEntity(scene, m_DuckEntity);

                m_IsEntitySpawned = true;
                Log::Info("Duck Entity Spawned.");
            }
        }

        // 6. Update world OBBs
        {
            auto view = registry.view<
                ECS::Components::Transform::Component, ECS::MeshCollider::Component>();
            for (auto [entity, transform, collider] : view.each())
            {
                glm::vec3 localCenter = collider.CollisionRef->LocalAABB.GetCenter();
                collider.WorldOBB.Center = glm::vec3(GetMatrix(transform) * glm::vec4(localCenter, 1.0f));

                glm::vec3 localExtents = collider.CollisionRef->LocalAABB.GetExtents();
                collider.WorldOBB.Extents = localExtents * glm::abs(transform.Scale);
                collider.WorldOBB.Rotation = transform.Rotation;
            }
        }

        // 7. Transform gizmo (before selection so gizmo can block scene clicks)
        bool gizmoConsumedMouse = false;
        if (cameraComponent != nullptr)
        {
            gizmoConsumedMouse = m_Gizmo.Update(
                registry,
                *cameraComponent,
                m_Window->GetInput(),
                static_cast<uint32_t>(m_Window->GetWindowWidth()),
                static_cast<uint32_t>(m_Window->GetWindowHeight()),
                uiCapturesMouse);
        }

        // 8. Spatial debug visualization (before render system)
        m_SpatialDebug.Update(*this, m_CachedSelectedEntity);

        // 8b. Sub-element selection highlights (vertex spheres, edge lines, face tint)
        EditorUI::DrawSubElementHighlights(*this);

        // 9. Selection
        if (cameraComponent != nullptr)
        {
            auto& renderSys = GetRenderOrchestrator().GetRenderDriver();

            GetSelection().GetConfig().MouseButton = m_SelectMouseButton;

            GetSelection().Update(
                scene,
                renderSys,
                cameraComponent,
                *m_Window,
                uiCapturesMouse || gizmoConsumedMouse);
        }
    }

    void OnRegisterSystems(Core::FrameGraph& graph, float deltaTime) override
    {
        using namespace Core::Hash;
        auto& sceneManager = GetSceneManager();
        auto& scene = sceneManager.GetScene();
        auto& registry = scene.GetRegistry();

        if (GetFeatureRegistry().IsEnabled("AxisRotator"_id))
            ECS::Systems::AxisRotator::RegisterSystem(graph, registry, deltaTime);
    }

private:
    // --- Resources ---
    Assets::AssetHandle m_DuckModel{};
    Assets::AssetHandle m_DuckTexture{};
    Assets::AssetHandle m_DuckMaterialHandle{};

    // --- State ---
    bool m_IsEntitySpawned = false;
    entt::entity m_DuckEntity = entt::null;

    // --- Camera ---
    entt::entity m_CameraEntity = entt::null;
    Graphics::CameraComponent m_Camera{};

    // --- Editor / Selection ---
    entt::entity m_CachedSelectedEntity = entt::null;
    entt::entity m_SelectionAnchor = entt::null;  // Anchor for Shift+click range selection in hierarchy.
    int m_SelectMouseButton = 0;

    // --- Hierarchy panel state ---
    entt::entity m_RenameEntity = entt::null;
    char m_RenameBuffer[256] = {};
    int m_HierarchyCollapseRequest = 0; // +1 = expand all, -1 = collapse all, 0 = none

    // --- Extracted subsystem controllers ---
    EditorUI::SpatialDebugController m_SpatialDebug{};
    EditorUI::GeometryWorkflowController m_GeometryWorkflow{};
    EditorUI::InspectorController m_Inspector{};

    // --- Transform Gizmo ---
    Graphics::TransformGizmo m_Gizmo{};

    // =========================================================================
    // Asset loading helpers
    // =========================================================================
    void CommitGizmoCommand(entt::registry& registry, Graphics::TransformGizmo::CompletedInteraction interaction)
    {
        auto toLabel = [](Graphics::GizmoMode mode) -> const char*
        {
            switch (mode)
            {
            case Graphics::GizmoMode::Translate: return "Move";
            case Graphics::GizmoMode::Rotate: return "Rotate";
            case Graphics::GizmoMode::Scale: return "Scale";
            default: return "Transform";
            }
        };

        Core::EditorCommand command{};
        command.name = std::string(toLabel(interaction.Mode));
        command.redo = [&registry, changes = interaction.Changes]()
        {
            for (const auto& change : changes)
            {
                if (!registry.valid(change.Entity))
                    continue;
                registry.emplace_or_replace<ECS::Components::Transform::Component>(change.Entity, change.After);
                registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(change.Entity);
            }
        };
        command.undo = [&registry, changes = std::move(interaction.Changes)]()
        {
            for (const auto& change : changes)
            {
                if (!registry.valid(change.Entity))
                    continue;
                registry.emplace_or_replace<ECS::Components::Transform::Component>(change.Entity, change.Before);
                registry.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(change.Entity);
            }
        };

        (void)GetCommandHistory().Execute(std::move(command));
    }

    void LoadDuckAssets(GraphicsBackend& gfx)
    {
        auto textureLoader = [this, &gfx](const std::filesystem::path& path, Core::Assets::AssetHandle handle)
            -> std::shared_ptr<RHI::Texture>
        {
            auto result = Graphics::TextureLoader::LoadAsync(path, GetGraphicsBackend().GetDevice(),
                                                             gfx.GetTransferManager(), gfx.GetTextureManager());

            if (result)
            {
                GetAssetPipeline().RegisterAssetLoad(handle, result->Token, [this, texHandle = result->TextureHandle]()
                {
                    auto& g = GetGraphicsBackend();
                    if (const auto* data = g.GetTextureManager().Get(texHandle))
                    {
                        g.GetBindlessSystem().EnqueueUpdate(data->BindlessSlot, data->Image->GetView(), data->Sampler);
                    }
                });

                GetAssetPipeline().GetAssetManager().MoveToProcessing(handle);
                return std::move(result->Texture);
            }

            Log::Warn("Texture load failed: {} ({})", path.string(), Graphics::AssetErrorToString(result.error()));
            return {};
        };
        m_DuckTexture = GetAssetPipeline().GetAssetManager().Load<RHI::Texture>(Filesystem::GetAssetPath("textures/DuckCM.png"),
                                                             textureLoader);

        auto modelLoader = [&](const std::string& path, Assets::AssetHandle handle)
            -> std::unique_ptr<Graphics::Model>
        {
            auto result = Graphics::ModelLoader::LoadAsync(
                GetGraphicsBackend().GetDeviceShared(), gfx.GetTransferManager(), GetRenderOrchestrator().GetGeometryStorage(), path,
                GetIORegistry(), GetIOBackend());

            if (result)
            {
                GetAssetPipeline().RegisterAssetLoad(handle, result->Token);
                GetAssetPipeline().GetAssetManager().MoveToProcessing(handle);
                return std::move(result->ModelData);
            }

            Log::Warn("Model load failed: {} ({})", path, Graphics::AssetErrorToString(result.error()));
            return nullptr;
        };

        m_DuckModel = GetAssetPipeline().GetAssetManager().Load<Graphics::Model>(
            Filesystem::GetAssetPath("models/Duck.glb"),
            modelLoader
        );

        Graphics::MaterialData matData;
        matData.AlbedoID = gfx.GetDefaultTextureIndex();
        matData.RoughnessFactor = 1.0f;
        matData.MetallicFactor = 0.0f;

        auto DuckMaterial = std::make_unique<Graphics::Material>(
            GetRenderOrchestrator().GetMaterialRegistry(),
            matData
        );

        DuckMaterial->SetAlbedoTexture(m_DuckTexture);

        m_DuckMaterialHandle = GetAssetPipeline().GetAssetManager().Create("DuckMaterial", std::move(DuckMaterial));
        GetAssetPipeline().TrackMaterial(m_DuckMaterialHandle);

        Log::Info("Asset Load Requested. Waiting for background thread...");
    }

    // =========================================================================
    // Feature registration
    // =========================================================================
    void RegisterFeatures()
    {
        auto& features = GetFeatureRegistry();
        using Cat = Core::FeatureCategory;

        // Client ECS system
        {
            Core::FeatureInfo info{};
            info.Name = "AxisRotator";
            info.Id = Core::Hash::StringID(Core::Hash::HashString(info.Name));
            info.Category = Cat::System;
            info.Description = "Continuous rotation animation for tagged entities";
            info.Enabled = true;
            features.Register(std::move(info), []() -> void* { return nullptr; }, [](void*)
            {
            });
        }

        // UI panels
        auto registerPanelFeature = [&features](const std::string& name, const std::string& desc)
        {
            Core::FeatureInfo info{};
            info.Name = name;
            info.Id = Core::Hash::StringID(Core::Hash::HashString(info.Name));
            info.Category = Cat::Panel;
            info.Description = desc;
            info.Enabled = true;
            features.Register(std::move(info), []() -> void* { return nullptr; }, [](void*)
            {
            });
        };
        registerPanelFeature("Hierarchy", "Scene entity hierarchy browser");
        registerPanelFeature("Inspector", "Component property editor");
        registerPanelFeature("Assets", "Asset manager browser");
        registerPanelFeature("Stats", "Performance statistics and debug controls");
        registerPanelFeature("View Settings", "Selection outline and viewport display settings");
        registerPanelFeature("Render Target Viewer", "Render target debug visualization");
        registerPanelFeature("Geometry Workflow", "Workflow hub for geometry processing tools");
        registerPanelFeature("Geometry - Remeshing", "Remeshing operators: isotropic and adaptive workflows");
        registerPanelFeature("Geometry - Simplification", "Mesh simplification operators");
        registerPanelFeature("Geometry - Smoothing", "Surface smoothing operators grouped by approach");
        registerPanelFeature("Geometry - Subdivision", "Subdivision operators for surface refinement");
        registerPanelFeature("Geometry - Repair", "Mesh cleanup and repair operators");
        registerPanelFeature("Status Bar",
                             "Bottom-of-viewport frame summary (frame time, entity count, active renderer)");
        registerPanelFeature("Viewport Toolbar", "Transform gizmo mode switching and snap controls");

        Log::Info("FeatureRegistry: {} total features after client registration", features.Count());
    }

    // =========================================================================
    // Panel registration
    // =========================================================================
    void RegisterPanels()
    {
        Interface::GUI::RegisterPanel("Hierarchy", [this]() { DrawHierarchyPanel(); }, true, 0, false);
        Interface::GUI::RegisterPanel("Inspector", [this]() { m_Inspector.Draw(); }, true, 0, false);
        Interface::GUI::RegisterPanel("Assets", [this]() { GetAssetPipeline().GetAssetManager().AssetsUiPanel(); }, true, 0, false);

        // Geometry workflow panels and menu bar (delegated to controller).
        m_GeometryWorkflow.RegisterPanelsAndMenu();

        // Register shared editor-facing panels (Feature browser, FrameGraph inspector, Selection config).
        Runtime::EditorUI::RegisterDefaultPanels(*this);

        // Stats panel
        Interface::GUI::RegisterPanel("Stats", [this]()
        {
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Entities: %d", (int)GetSceneManager().GetScene().Size());

            ImGui::SeparatorText("Render Pipeline");
            {
                if (ImGui::Button("Hot-swap: DefaultPipeline"))
                {
                    auto pipeline = std::make_unique<Graphics::DefaultPipeline>();
                    pipeline->SetFeatureRegistry(&GetFeatureRegistry());
                    GetRenderOrchestrator().GetRenderDriver().RequestPipelineSwap(std::move(pipeline));
                }
            }

            ImGui::Separator();
            ImGui::Text("Select Mouse Button: %d", m_SelectMouseButton);

            const entt::entity selected = m_CachedSelectedEntity;
            const bool selectedValid = (selected != entt::null) && GetSceneManager().GetScene().GetRegistry().valid(selected);

            ImGui::Text("Selected: %u (%s)",
                        static_cast<uint32_t>(static_cast<entt::id_type>(selected)),
                        selectedValid ? "valid" : "invalid");

            if (selectedValid)
            {
                const auto& reg = GetSceneManager().GetScene().GetRegistry();
                const bool hasSelectedTag = reg.all_of<ECS::Components::Selection::SelectedTag>(selected);
                const bool hasSelectableTag = reg.all_of<ECS::Components::Selection::SelectableTag>(selected);
                const bool hasSurface = reg.all_of<ECS::Surface::Component>(selected);
                const bool hasMeshCollider = reg.all_of<ECS::MeshCollider::Component>(selected);
                const bool hasGraph = reg.all_of<ECS::Graph::Data>(selected);
                const bool hasPointCloud = reg.all_of<ECS::PointCloud::Data>(selected);

                ImGui::Text("Tags: Selectable=%d Selected=%d", (int)hasSelectableTag, (int)hasSelectedTag);
                ImGui::Text("Components: Surface=%d MeshCollider=%d Graph=%d PointCloud=%d",
                            (int)hasSurface, (int)hasMeshCollider, (int)hasGraph, (int)hasPointCloud);

                uint32_t selfPickId = 0u;
                if (const auto* pid = reg.try_get<ECS::Components::Selection::PickID>(selected))
                    selfPickId = pid->Value;

                uint32_t outlineIds[Graphics::Passes::SelectionOutlinePass::kMaxSelectedIds] = {};
                const uint32_t outlineCount = Graphics::Passes::AppendOutlineRenderablePickIds(
                    reg, selected, outlineIds);
                ImGui::Text("PickIDs: Self=%u OutlineResolved=%u", selfPickId, outlineCount);
                for (uint32_t i = 0; i < outlineCount; ++i)
                    ImGui::BulletText("Outline PickID[%u] = %u", i, outlineIds[i]);

                if (outlineCount == 0u)
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                       "No renderable outline PickIDs resolved for this selection.");

                const auto gpuPick = GetRenderOrchestrator().GetRenderDriver().GetLastPickResult();
                ImGui::Text("Last GPU Pick: Hit=%d EntityID=%u", (int)gpuPick.HasHit, gpuPick.EntityID);

                const auto& picked = GetSelection().GetPicked();
                constexpr uint32_t Invalid = Runtime::Selection::Picked::Entity::InvalidIndex;
                ImGui::Text("Picked IDs: V=%s E=%s F=%s",
                            picked.entity.vertex_idx != Invalid ? std::to_string(picked.entity.vertex_idx).c_str() : "-",
                            picked.entity.edge_idx != Invalid ? std::to_string(picked.entity.edge_idx).c_str() : "-",
                            picked.entity.face_idx != Invalid ? std::to_string(picked.entity.face_idx).c_str() : "-");
                ImGui::Text("Picked Local: (%.3f, %.3f, %.3f)",
                            picked.spaces.Local.x, picked.spaces.Local.y, picked.spaces.Local.z);
                ImGui::Text("Picked World: (%.3f, %.3f, %.3f)",
                            picked.spaces.World.x, picked.spaces.World.y, picked.spaces.World.z);
            }
        }, true, 0, false);

        // Status bar — enriched overlay (not a dockable panel)
        Interface::GUI::RegisterOverlay("Status Bar", [this]()
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float statusBarHeight = ImGui::GetFrameHeight() + 10.0f;

            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x,
                                           viewport->WorkPos.y + viewport->WorkSize.y - statusBarHeight));
            ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, statusBarHeight));

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoBringToFrontOnFocus |
                ImGuiWindowFlags_NoNavFocus;

            const float fps = ImGui::GetIO().Framerate;
            const float frameMs = fps > 0.0f ? (1000.0f / fps) : 0.0f;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
            auto VerticalSeparator = []()
            {
                ImGui::SameLine();
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const float h = ImGui::GetFrameHeight();
                ImGui::GetWindowDrawList()->AddLine(
                    ImVec2(pos.x, pos.y),
                    ImVec2(pos.x, pos.y + h),
                    ImGui::GetColorU32(ImGuiCol_Separator));
                ImGui::Dummy(ImVec2(1.0f, h));
                ImGui::SameLine();
            };

            if (ImGui::Begin("##IntrinsicStatusBar", nullptr, flags))
            {
                // Frame timing
                ImGui::Text("%.2f ms (%.0f FPS)", frameMs, fps);
                VerticalSeparator();

                // Entity count
                ImGui::Text("Entities: %d", static_cast<int>(GetSceneManager().GetScene().Size()));
                VerticalSeparator();

                // Selection mode
                const auto& selConfig = GetSelection().GetConfig();
                constexpr const char* modeNames[] = {"Entity", "Vertex", "Edge", "Face"};
                const int modeIdx = std::clamp(static_cast<int>(selConfig.ElementMode), 0,
                                               static_cast<int>(std::size(modeNames) - 1));
                ImGui::Text("Mode: %s", modeNames[modeIdx]);
                VerticalSeparator();

                // Selected entity / sub-element counts
                {
                    const entt::entity sel = m_CachedSelectedEntity;
                    const bool hasSel = (sel != entt::null) &&
                                        GetSceneManager().GetScene().GetRegistry().valid(sel);

                    if (hasSel)
                    {
                        const auto& subSel = GetSelection().GetSubElementSelection();
                        const size_t vCount = subSel.SelectedVertices.size();
                        const size_t eCount = subSel.SelectedEdges.size();
                        const size_t fCount = subSel.SelectedFaces.size();
                        const size_t totalSub = vCount + eCount + fCount;

                        if (totalSub > 0)
                            ImGui::Text("Sel: 1 entity, %zu sub", totalSub);
                        else
                            ImGui::Text("Sel: 1 entity");
                    }
                    else
                    {
                        ImGui::TextDisabled("Sel: none");
                    }
                }
                VerticalSeparator();

                // Active gizmo tool
                {
                    constexpr const char* toolNames[] = {"Translate", "Rotate", "Scale"};
                    const int toolIdx = std::clamp(static_cast<int>(m_Gizmo.GetConfig().Mode), 0,
                                                   static_cast<int>(std::size(toolNames) - 1));
                    ImGui::Text("Tool: %s", toolNames[toolIdx]);
                }
                VerticalSeparator();

                // Lighting path
                {
                    using namespace Core::Hash;
                    const bool deferred = GetFeatureRegistry().IsEnabled("DeferredLighting"_id);
                    ImGui::Text("Lighting: %s", deferred ? "Deferred" : "Forward");
                }
                VerticalSeparator();

                // Undo/redo depth
                {
                    const Core::CommandHistory& history = GetCommandHistory();
                    ImGui::Text("Undo: %zu  Redo: %zu", history.UndoCount(), history.RedoCount());
                }
                VerticalSeparator();

                // GPU memory usage (device-local summary)
                {
                    const auto& mem = Core::Telemetry::TelemetrySystem::Get().GetGpuMemorySnapshot();
                    uint64_t totalUsed = 0;
                    uint64_t totalBudget = 0;
                    for (uint32_t i = 0; i < mem.HeapCount; ++i)
                    {
                        if (mem.Heaps[i].Flags & Core::Telemetry::kHeapFlagDeviceLocal)
                        {
                            totalUsed += mem.Heaps[i].UsageBytes;
                            totalBudget += mem.Heaps[i].BudgetBytes;
                        }
                    }

                    if (totalBudget > 0)
                    {
                        const float usedMB = static_cast<float>(totalUsed) / (1024.0f * 1024.0f);
                        const float budgetMB = static_cast<float>(totalBudget) / (1024.0f * 1024.0f);
                        const float pct = static_cast<float>(static_cast<double>(totalUsed) / static_cast<double>(totalBudget)) * 100.0f;

                        // Color-code: green < 70%, yellow < 85%, red >= 85%.
                        ImVec4 color;
                        if (pct < 70.0f)      color = ImVec4(0.20f, 0.75f, 0.30f, 1.0f);
                        else if (pct < 85.0f) color = ImVec4(0.90f, 0.75f, 0.15f, 1.0f);
                        else                  color = ImVec4(0.95f, 0.25f, 0.20f, 1.0f);

                        ImGui::TextColored(color, "VRAM: %.0f/%.0f MB", usedMB, budgetMB);
                    }
                    else
                    {
                        ImGui::TextDisabled("VRAM: N/A");
                    }
                }
            }
            ImGui::End();
            ImGui::PopStyleVar();
        });

        // Viewport toolbar (gizmo)
        Interface::GUI::RegisterPanel("Viewport Toolbar", [this]()
        {
            auto& cfg = m_Gizmo.GetConfig();

            ImGui::SeparatorText("Gizmo Mode");
            int mode = static_cast<int>(cfg.Mode);
            if (ImGui::RadioButton("Translate (W)", mode == 0)) m_Gizmo.SetMode(Graphics::GizmoMode::Translate);
            Interface::GUI::ItemTooltip("Move selected entities along axes. Shortcut: W");
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate (E)", mode == 1)) m_Gizmo.SetMode(Graphics::GizmoMode::Rotate);
            Interface::GUI::ItemTooltip("Rotate selected entities around axes. Shortcut: E");
            ImGui::SameLine();
            if (ImGui::RadioButton("Scale (R)", mode == 2)) m_Gizmo.SetMode(Graphics::GizmoMode::Scale);
            Interface::GUI::ItemTooltip("Scale selected entities along axes. Shortcut: R");

            ImGui::SeparatorText("Transform Space");
            int space = static_cast<int>(cfg.Space);
            if (ImGui::RadioButton("World", space == 0)) cfg.Space = Graphics::GizmoSpace::World;
            Interface::GUI::ItemTooltip("Gizmo axes aligned to world coordinate system.");
            ImGui::SameLine();
            if (ImGui::RadioButton("Local (X)", space == 1)) cfg.Space = Graphics::GizmoSpace::Local;
            Interface::GUI::ItemTooltip("Gizmo axes aligned to entity's local rotation. Toggle: X");

            ImGui::SeparatorText("Pivot");
            int pivot = static_cast<int>(cfg.Pivot);
            if (ImGui::RadioButton("Centroid", pivot == 0)) cfg.Pivot = Graphics::GizmoPivot::Centroid;
            Interface::GUI::ItemTooltip("Gizmo placed at the average position of all selected entities.");
            ImGui::SameLine();
            if (ImGui::RadioButton("First Selected", pivot == 1)) cfg.Pivot = Graphics::GizmoPivot::FirstSelected;
            Interface::GUI::ItemTooltip("Gizmo placed at the first entity that was selected.");

            ImGui::SeparatorText("Snapping");
            ImGui::Checkbox("Enable Snap", &cfg.SnapEnabled);
            Interface::GUI::ItemTooltip("When enabled, transform values snap to discrete increments.");
            if (cfg.SnapEnabled)
            {
                ImGui::DragFloat("Translate Snap", &cfg.TranslateSnap, 0.05f, 0.01f, 10.0f, "%.2f");
                Interface::GUI::ItemTooltip("Snap increment for translation in world units.");
                ImGui::DragFloat("Rotate Snap (deg)", &cfg.RotateSnap, 1.0f, 1.0f, 90.0f, "%.1f");
                Interface::GUI::ItemTooltip("Snap increment for rotation in degrees.");
                ImGui::DragFloat("Scale Snap", &cfg.ScaleSnap, 0.01f, 0.01f, 1.0f, "%.2f");
                Interface::GUI::ItemTooltip("Snap increment for scale factor.");
            }

            ImGui::SeparatorText("Appearance");
            ImGui::SliderFloat("Handle Size", &cfg.HandleLength, 0.3f, 3.0f, "%.1f");
            Interface::GUI::ItemTooltip("Visual length of the gizmo handles.");
            ImGui::SliderFloat("Handle Thickness", &cfg.HandleThickness, 1.0f, 8.0f, "%.1f px");
            Interface::GUI::ItemTooltip("Pixel thickness of the gizmo handle lines.");

            ImGui::Separator();
            const char* stateNames[] = {"Idle", "Hovered", "Active"};
            ImGui::Text("State: %s", stateNames[static_cast<int>(m_Gizmo.GetState())]);
        }, true, 0, false);

        Interface::GUI::RegisterOverlay("Transform Gizmo", [this]()
        {
            m_Gizmo.DrawImGui();
        });

        // Viewport context menu overlay (right-click on 3D viewport)
        Interface::GUI::RegisterOverlay("Viewport Context Menu", [this]()
        {
            DrawViewportContextMenu();
        });

        // View Settings panel
        Interface::GUI::RegisterPanel("View Settings", [this]()
        {
            DrawViewSettingsPanel();
        }, true, 0, false);
    }

    // =========================================================================
    // View Settings panel (post-process + selection outline + spatial debug)
    // =========================================================================
    void DrawViewSettingsPanel()
    {
        // --- Camera ---
        {
            auto& registry = GetSceneManager().GetScene().GetRegistry();
            if (registry.valid(m_CameraEntity))
            {
                if (auto* camera = registry.try_get<Graphics::CameraComponent>(m_CameraEntity))
                {
                    ImGui::SeparatorText("Camera");

                    constexpr const char* projectionTypes[] = {"Perspective", "Orthographic"};
                    int projectionType = static_cast<int>(camera->ProjectionType);
                    if (ImGui::Combo("Projection", &projectionType, projectionTypes, IM_ARRAYSIZE(projectionTypes)))
                    {
                        camera->ProjectionType = static_cast<Graphics::CameraProjectionType>(projectionType);
                    }
                    Interface::GUI::ItemTooltip("Projection model used by the active camera.");

                    ImGui::DragFloat("Near Clip", &camera->Near, 0.01f, 1e-4f, 1000.0f, "%.4f",
                                     ImGuiSliderFlags_Logarithmic);
                    Interface::GUI::ItemTooltip("Near clipping plane distance in world units.");
                    ImGui::DragFloat("Far Clip", &camera->Far, 1.0f, 0.01f, 100000.0f, "%.2f",
                                     ImGuiSliderFlags_Logarithmic);
                    Interface::GUI::ItemTooltip("Far clipping plane distance in world units.");
                    if (camera->Far <= camera->Near)
                        camera->Far = camera->Near + 0.001f;

                    if (camera->ProjectionType == Graphics::CameraProjectionType::Perspective)
                    {
                        ImGui::SliderFloat("Field of View (deg)", &camera->Fov, 10.0f, 140.0f, "%.1f");
                        Interface::GUI::ItemTooltip("Vertical field of view in degrees for perspective projection.");
                    }
                    else
                    {
                        ImGui::DragFloat("Ortho Height", &camera->OrthographicHeight, 0.05f, 0.01f, 10000.0f, "%.3f",
                                         ImGuiSliderFlags_Logarithmic);
                        Interface::GUI::ItemTooltip("Vertical world-space span of the orthographic frustum.");
                        camera->OrthographicHeight = std::max(camera->OrthographicHeight, 0.01f);
                    }

                    const glm::vec3 forward = camera->GetForward();
                    ImGui::Text("Eye: (%.3f, %.3f, %.3f)", camera->Position.x, camera->Position.y, camera->Position.z);
                    ImGui::Text("Forward: (%.3f, %.3f, %.3f)", forward.x, forward.y, forward.z);
                }
            }
        }

        // --- Rendering ---
        {
            auto& renderDriver = GetRenderOrchestrator().GetRenderDriver();
            ImGui::SeparatorText("Rendering");

            constexpr const char* renderModes[] = {
                "None",
                "Shaded",
                "Wireframe",
                "Wireframe + Shaded",
                "Points",
                "Flat"
            };

            int renderMode = static_cast<int>(renderDriver.GetGlobalRenderModeOverride());
            if (ImGui::Combo("Render Mode Override", &renderMode, renderModes, IM_ARRAYSIZE(renderModes)))
            {
                renderDriver.SetGlobalRenderModeOverride(
                    static_cast<Graphics::GlobalRenderModeOverride>(renderMode));
            }
            Interface::GUI::ItemTooltip("Viewport-wide rendering override. None keeps per-entity Surface/Line/Point visibility. Flat currently maps to the shaded surface path.");
        }

        // --- Lighting ---
        {
            auto& lighting = GetRenderOrchestrator().GetRenderDriver().GetLightEnvironment();
            auto& features = GetFeatureRegistry();

            ImGui::SeparatorText("Lighting");

            // Promote lighting-path selection into View Settings for quick access.
            {
                constexpr const char* lightingPaths[] = {"Forward", "Deferred"};
                int lightingPath = features.IsEnabled("DeferredLighting"_id) ? 1 : 0;
                if (ImGui::Combo("Lighting Path", &lightingPath, lightingPaths, IM_ARRAYSIZE(lightingPaths)))
                {
                    const bool deferredEnabled = (lightingPath == 1);
                    features.SetEnabled("DeferredLighting"_id, deferredEnabled);
                }
                Interface::GUI::ItemTooltip("Select the active frame lighting path. Forward renders directly to HDR; Deferred uses G-buffer + composition.");
            }

            // Directional light direction — spherical angles for intuitive control.
            glm::vec3 dir = glm::normalize(lighting.LightDirection);
            float elevation = glm::degrees(std::asin(glm::clamp(dir.y, -1.0f, 1.0f)));
            float azimuth = glm::degrees(std::atan2(dir.x, dir.z));

            bool dirChanged = false;
            dirChanged |= ImGui::SliderFloat("Azimuth##Light", &azimuth, -180.0f, 180.0f, "%.1f deg");
            Interface::GUI::ItemTooltip("Horizontal rotation of the directional light source (-180 to 180 degrees).");
            dirChanged |= ImGui::SliderFloat("Elevation##Light", &elevation, -89.0f, 89.0f, "%.1f deg");
            Interface::GUI::ItemTooltip("Vertical angle of the directional light. Higher values move the sun upward.");

            if (dirChanged)
            {
                float elevRad = glm::radians(elevation);
                float azimRad = glm::radians(azimuth);
                lighting.LightDirection = glm::vec3(
                    std::cos(elevRad) * std::sin(azimRad),
                    std::sin(elevRad),
                    std::cos(elevRad) * std::cos(azimRad));
            }

            ImGui::SliderFloat("Intensity##Light", &lighting.LightIntensity, 0.0f, 5.0f, "%.2f");
            Interface::GUI::ItemTooltip("Brightness multiplier for the directional light (0 = off, 5 = very bright).");

            float lightCol[3] = { lighting.LightColor.r, lighting.LightColor.g, lighting.LightColor.b };
            if (ImGui::ColorEdit3("Color##Light", lightCol))
                lighting.LightColor = glm::vec3(lightCol[0], lightCol[1], lightCol[2]);
            Interface::GUI::ItemTooltip("RGB color of the directional light.");

            ImGui::SeparatorText("Ambient");

            float ambCol[3] = { lighting.AmbientColor.r, lighting.AmbientColor.g, lighting.AmbientColor.b };
            if (ImGui::ColorEdit3("Color##Ambient", ambCol))
                lighting.AmbientColor = glm::vec3(ambCol[0], ambCol[1], ambCol[2]);
            Interface::GUI::ItemTooltip("RGB color of the ambient (fill) light that illuminates all surfaces equally.");

            ImGui::SliderFloat("Intensity##Ambient", &lighting.AmbientIntensity, 0.0f, 1.0f, "%.2f");
            Interface::GUI::ItemTooltip("Strength of the ambient light. 0 = no ambient, 1 = full ambient fill.");

            ImGui::Spacing();
            if (ImGui::Button("Reset Lighting"))
                lighting = Graphics::LightEnvironmentPacket{};
            Interface::GUI::ItemTooltip("Reset all lighting parameters to their default values.");
        }

        // --- Post-Processing ---
        auto* postSettings = GetRenderOrchestrator().GetRenderDriver().GetPostProcessSettings();
        if (postSettings)
        {
            ImGui::SeparatorText("Post Processing");

            const char* toneMapOps[] = {"ACES", "Reinhard", "Uncharted 2"};
            int toneOp = static_cast<int>(postSettings->ToneOperator);
            if (ImGui::Combo("Tone Mapping", &toneOp, toneMapOps, 3))
                postSettings->ToneOperator = static_cast<Graphics::Passes::ToneMapOperator>(toneOp);
            Interface::GUI::ItemTooltip("Tone mapping operator that maps HDR scene colors to displayable LDR range.\nACES: filmic, industry standard.\nReinhard: simple, soft rolloff.\nUncharted 2: Hable filmic curve.");

            ImGui::SliderFloat("Exposure", &postSettings->Exposure, 0.1f, 10.0f, "%.2f");
            Interface::GUI::ItemTooltip("Global exposure multiplier applied before tone mapping. Higher = brighter scene.");

            // Bloom
            ImGui::Spacing();
            ImGui::Checkbox("Bloom", &postSettings->BloomEnabled);
            Interface::GUI::ItemTooltip("Enable bloom glow effect on bright areas of the image.");
            if (postSettings->BloomEnabled)
            {
                ImGui::SliderFloat("Bloom Threshold", &postSettings->BloomThreshold, 0.0f, 5.0f, "%.2f");
                Interface::GUI::ItemTooltip("Minimum HDR brightness for a pixel to contribute to bloom. Lower = more glow.");
                ImGui::SliderFloat("Bloom Intensity", &postSettings->BloomIntensity, 0.0f, 1.0f, "%.3f");
                Interface::GUI::ItemTooltip("Strength of the bloom effect blended into the final image.");
                ImGui::SliderFloat("Bloom Radius", &postSettings->BloomFilterRadius, 0.5f, 3.0f, "%.2f");
                Interface::GUI::ItemTooltip("Filter radius of the bloom blur kernel. Larger = wider glow spread.");
            }

            // Anti-Aliasing
            ImGui::Spacing();
            {
                const char* aaModeNames[] = {"None", "FXAA", "SMAA"};
                int aaIdx = static_cast<int>(postSettings->AntiAliasingMode);
                if (ImGui::Combo("Anti-Aliasing", &aaIdx, aaModeNames, 3))
                    postSettings->AntiAliasingMode = static_cast<Graphics::Passes::AAMode>(aaIdx);
                Interface::GUI::ItemTooltip("Anti-aliasing method.\nNone: no AA.\nFXAA: fast approximate AA.\nSMAA: enhanced morphological AA (default, higher quality).");
            }
            if (postSettings->AntiAliasingMode == Graphics::Passes::AAMode::FXAA)
            {
                ImGui::SliderFloat("FXAA Contrast", &postSettings->FXAAContrastThreshold, 0.01f, 0.1f, "%.4f");
                Interface::GUI::ItemTooltip("Minimum contrast to trigger edge detection. Lower = more edges detected.");
                ImGui::SliderFloat("FXAA Relative", &postSettings->FXAARelativeThreshold, 0.01f, 0.2f, "%.4f");
                Interface::GUI::ItemTooltip("Relative threshold against the local maximum luma. Lower = more aggressive.");
                ImGui::SliderFloat("FXAA Subpixel", &postSettings->FXAASubpixelBlending, 0.0f, 1.0f, "%.2f");
                Interface::GUI::ItemTooltip("Subpixel blending amount. Higher = smoother but potentially blurrier.");
            }
            if (postSettings->AntiAliasingMode == Graphics::Passes::AAMode::SMAA)
            {
                ImGui::SliderFloat("SMAA Edge Threshold", &postSettings->SMAAEdgeThreshold, 0.01f, 0.5f, "%.3f");
                Interface::GUI::ItemTooltip("Luma threshold for edge detection. Lower = more edges, higher quality but slower.");
                ImGui::SliderInt("SMAA Search Steps", &postSettings->SMAAMaxSearchSteps, 4, 32);
                Interface::GUI::ItemTooltip("Maximum search distance for edge endpoints. Higher = better long-edge AA.");
                ImGui::SliderInt("SMAA Diag Steps", &postSettings->SMAAMaxSearchStepsDiag, 0, 16);
                Interface::GUI::ItemTooltip("Maximum diagonal search steps. 0 disables diagonal edge detection.");
            }

            // Luminance Histogram
            ImGui::Spacing();
            ImGui::Checkbox("Exposure Histogram", &postSettings->HistogramEnabled);
            Interface::GUI::ItemTooltip("Display a luminance histogram of the rendered scene.");
            if (postSettings->HistogramEnabled)
            {
                ImGui::SliderFloat("Min EV", &postSettings->HistogramMinEV, -20.0f, 0.0f, "%.1f");
                Interface::GUI::ItemTooltip("Minimum exposure value for histogram range.");
                ImGui::SliderFloat("Max EV", &postSettings->HistogramMaxEV, 0.0f, 20.0f, "%.1f");
                Interface::GUI::ItemTooltip("Maximum exposure value for histogram range.");

                const auto* histo = GetRenderOrchestrator().GetRenderDriver().GetHistogramReadback();
                if (histo && histo->Valid)
                {
                    uint32_t maxBin = 1;
                    for (uint32_t i = 0; i < Graphics::Passes::kHistogramBinCount; ++i)
                        maxBin = std::max(maxBin, histo->Bins[i]);

                    float plotData[Graphics::Passes::kHistogramBinCount];
                    for (uint32_t i = 0; i < Graphics::Passes::kHistogramBinCount; ++i)
                        plotData[i] = static_cast<float>(histo->Bins[i]) / static_cast<float>(maxBin);

                    ImGui::PlotHistogram("##LumHist", plotData,
                                         static_cast<int>(Graphics::Passes::kHistogramBinCount),
                                         0, nullptr, 0.0f, 1.0f, ImVec2(0, 80));

                    float avgEV = (histo->AverageLuminance > 1e-6f)
                                      ? std::log2(histo->AverageLuminance)
                                      : postSettings->HistogramMinEV;
                    ImGui::Text("Avg Luminance: %.4f  (%.1f EV)", histo->AverageLuminance, avgEV);
                }
                else
                {
                    ImGui::TextDisabled("Histogram data not available yet.");
                }
            }

            // Color Grading
            ImGui::Spacing();
            ImGui::Checkbox("Color Grading", &postSettings->ColorGradingEnabled);
            Interface::GUI::ItemTooltip("Enable lift/gamma/gain color correction in linear space.");
            if (postSettings->ColorGradingEnabled)
            {
                ImGui::SliderFloat("Saturation", &postSettings->Saturation, 0.0f, 2.0f, "%.2f");
                Interface::GUI::ItemTooltip("Color saturation. 0 = grayscale, 1 = normal, 2 = oversaturated.");
                ImGui::SliderFloat("Contrast##CG", &postSettings->Contrast, 0.5f, 2.0f, "%.2f");
                Interface::GUI::ItemTooltip("Contrast around mid-gray (0.18). Higher = more punch.");
                ImGui::SliderFloat("Temperature", &postSettings->ColorTempOffset, -1.0f, 1.0f, "%.2f");
                Interface::GUI::ItemTooltip("White balance shift. Negative = cooler (blue), positive = warmer (orange).");
                ImGui::SliderFloat("Tint", &postSettings->TintOffset, -1.0f, 1.0f, "%.2f");
                Interface::GUI::ItemTooltip("Green-magenta tint correction. Negative = green, positive = magenta.");

                ImGui::Spacing();
                ImGui::TextDisabled("Lift (Shadows)");
                ImGui::SliderFloat3("Lift", &postSettings->Lift.x, -0.5f, 0.5f, "%.3f");
                Interface::GUI::ItemTooltip("Per-channel shadow offset (R, G, B). Shifts dark tones.");

                ImGui::TextDisabled("Gamma (Midtones)");
                ImGui::SliderFloat3("Gamma", &postSettings->Gamma.x, 0.2f, 3.0f, "%.2f");
                Interface::GUI::ItemTooltip("Per-channel midtone power curve (R, G, B). 1.0 = neutral.");

                ImGui::TextDisabled("Gain (Highlights)");
                ImGui::SliderFloat3("Gain", &postSettings->Gain.x, 0.0f, 3.0f, "%.2f");
                Interface::GUI::ItemTooltip("Per-channel highlight multiplier (R, G, B). 1.0 = neutral.");

                if (ImGui::Button("Reset Color Grading"))
                {
                    postSettings->Saturation = 1.0f;
                    postSettings->Contrast = 1.0f;
                    postSettings->ColorTempOffset = 0.0f;
                    postSettings->TintOffset = 0.0f;
                    postSettings->Lift  = glm::vec3(0.0f);
                    postSettings->Gamma = glm::vec3(1.0f);
                    postSettings->Gain  = glm::vec3(1.0f);
                }
            }
        }

        // --- Selection Outline ---
        auto* outlineSettings = GetRenderOrchestrator().GetRenderDriver().GetSelectionOutlineSettings();
        if (!outlineSettings)
        {
            ImGui::TextDisabled("Selection outline settings not available.");
            return;
        }

        ImGui::SeparatorText("Selection Outline");

        {
            const char* modeNames[] = {"Solid", "Pulse", "Glow"};
            int currentMode = static_cast<int>(outlineSettings->Mode);
            if (ImGui::Combo("Outline Mode", &currentMode, modeNames, 3))
                outlineSettings->Mode = static_cast<Graphics::Passes::OutlineMode>(currentMode);
            Interface::GUI::ItemTooltip("Selection outline rendering style.\nSolid: constant opacity.\nPulse: alpha oscillates over time.\nGlow: radial falloff around silhouette.");
        }

        float selColor[4] = {
            outlineSettings->SelectionColor.r,
            outlineSettings->SelectionColor.g,
            outlineSettings->SelectionColor.b,
            outlineSettings->SelectionColor.a
        };
        if (ImGui::ColorEdit4("Selection Color", selColor))
        {
            outlineSettings->SelectionColor = glm::vec4(selColor[0], selColor[1], selColor[2], selColor[3]);
        }
        Interface::GUI::ItemTooltip("Color and opacity of the selection outline.");

        float hoverColor[4] = {
            outlineSettings->HoverColor.r,
            outlineSettings->HoverColor.g,
            outlineSettings->HoverColor.b,
            outlineSettings->HoverColor.a
        };
        if (ImGui::ColorEdit4("Hover Color", hoverColor))
        {
            outlineSettings->HoverColor = glm::vec4(hoverColor[0], hoverColor[1], hoverColor[2], hoverColor[3]);
        }
        Interface::GUI::ItemTooltip("Color and opacity of the hover highlight outline.");

        ImGui::SliderFloat("Outline Width", &outlineSettings->OutlineWidth, 1.0f, 10.0f, "%.1f px");
        Interface::GUI::ItemTooltip("Pixel width of the selection/hover outline.");
        ImGui::SliderFloat("Selection Fill", &outlineSettings->SelectionFillAlpha, 0.0f, 0.5f, "%.2f");
        Interface::GUI::ItemTooltip("Interior fill opacity for selected entities. 0 = outline only.");
        ImGui::SliderFloat("Hover Fill", &outlineSettings->HoverFillAlpha, 0.0f, 0.5f, "%.2f");
        Interface::GUI::ItemTooltip("Interior fill opacity for hovered entities. 0 = outline only.");

        if (outlineSettings->Mode == Graphics::Passes::OutlineMode::Pulse)
        {
            ImGui::SliderFloat("Pulse Speed", &outlineSettings->PulseSpeed, 0.5f, 10.0f, "%.1f");
            Interface::GUI::ItemTooltip("Oscillation speed of the pulsing outline (cycles per second).");
            ImGui::SliderFloat("Pulse Min Alpha", &outlineSettings->PulseMin, 0.0f, 1.0f, "%.2f");
            Interface::GUI::ItemTooltip("Minimum alpha during pulse cycle.");
            ImGui::SliderFloat("Pulse Max Alpha", &outlineSettings->PulseMax, 0.0f, 1.0f, "%.2f");
            Interface::GUI::ItemTooltip("Maximum alpha during pulse cycle.");
        }
        else if (outlineSettings->Mode == Graphics::Passes::OutlineMode::Glow)
        {
            ImGui::SliderFloat("Glow Falloff", &outlineSettings->GlowFalloff, 0.5f, 8.0f, "%.1f");
            Interface::GUI::ItemTooltip("Exponential falloff rate for glow distance from silhouette edge.");
        }

        if (ImGui::Button("Reset Outline Defaults"))
        {
            *outlineSettings = Graphics::Passes::SelectionOutlineSettings{};
        }

        // Spatial structure debug visualization (delegated to controller).
        m_SpatialDebug.DrawUI(*this);
    }

    // =========================================================================
    // Hierarchy panel — tree-nested with entity type icons
    // =========================================================================

    // Returns a short text icon prefix based on the entity's primary component type.
    static const char* EntityTypeIcon(const entt::registry& reg, entt::entity e)
    {
        if (reg.all_of<ECS::DataAuthority::MeshTag>(e))         return "[M] ";  // Mesh
        if (reg.all_of<ECS::DataAuthority::GraphTag>(e))        return "[G] ";  // Graph
        if (reg.all_of<ECS::DataAuthority::PointCloudTag>(e))   return "[P] ";  // Point Cloud
        if (reg.all_of<ECS::Components::Hierarchy::Component>(e))
        {
            const auto& h = reg.get<ECS::Components::Hierarchy::Component>(e);
            if (h.FirstChild != entt::null)                     return "[+] ";  // Group/parent
        }
        return "[o] ";  // Empty/other
    }

    // Toggle the Visible flag on all render components of an entity.
    static void ToggleEntityVisibility(entt::registry& reg, entt::entity e)
    {
        // Determine current state: visible if any render component is visible.
        bool isVisible = false;
        if (const auto* sc = reg.try_get<ECS::Surface::Component>(e))
            isVisible |= sc->Visible;
        if (const auto* gd = reg.try_get<ECS::Graph::Data>(e))
            isVisible |= gd->Visible;
        if (const auto* pcd = reg.try_get<ECS::PointCloud::Data>(e))
            isVisible |= pcd->Visible;

        const bool newVisible = !isVisible;
        if (auto* sc = reg.try_get<ECS::Surface::Component>(e))
            sc->Visible = newVisible;
        if (auto* gd = reg.try_get<ECS::Graph::Data>(e))
            gd->Visible = newVisible;
        if (auto* pcd = reg.try_get<ECS::PointCloud::Data>(e))
            pcd->Visible = newVisible;
    }

    // Check if an entity has any visible render components.
    static bool IsEntityVisible(const entt::registry& reg, entt::entity e)
    {
        if (const auto* sc = reg.try_get<ECS::Surface::Component>(e))
            if (sc->Visible) return true;
        if (const auto* gd = reg.try_get<ECS::Graph::Data>(e))
            if (gd->Visible) return true;
        if (const auto* pcd = reg.try_get<ECS::PointCloud::Data>(e))
            if (pcd->Visible) return true;
        return false;
    }

    // Collect entities in the same depth-first order they appear in the hierarchy panel.
    // Used for Shift+click range selection.
    void CollectVisibleHierarchyOrder(std::vector<entt::entity>& out)
    {
        auto& reg = GetSceneManager().GetScene().GetRegistry();
        reg.view<entt::entity>().each([&](auto entityID)
        {
            if (reg.all_of<Runtime::EditorUI::HiddenEditorEntityTag>(entityID))
                return;
            if (const auto* hierarchy = reg.try_get<ECS::Components::Hierarchy::Component>(entityID))
            {
                if (hierarchy->Parent != entt::null)
                    return;
            }
            CollectEntitySubtree(reg, entityID, out);
        });
    }

    static void CollectEntitySubtree(entt::registry& reg, entt::entity entityID,
                                     std::vector<entt::entity>& out)
    {
        if (!reg.valid(entityID)) return;
        if (reg.all_of<Runtime::EditorUI::HiddenEditorEntityTag>(entityID)) return;

        out.push_back(entityID);

        if (const auto* hierarchy = reg.try_get<ECS::Components::Hierarchy::Component>(entityID))
        {
            entt::entity child = hierarchy->FirstChild;
            uint32_t safety = 0;
            while (child != entt::null && reg.valid(child) && safety < 1000)
            {
                CollectEntitySubtree(reg, child, out);
                if (const auto* childH = reg.try_get<ECS::Components::Hierarchy::Component>(child))
                    child = childH->NextSibling;
                else
                    break;
                ++safety;
            }
        }
    }

    // Select all entities in the hierarchy between anchor and clicked entity (inclusive).
    void SelectRangeInHierarchy(entt::entity clickedEntity)
    {
        std::vector<entt::entity> order;
        CollectVisibleHierarchyOrder(order);

        // Find anchor and clicked indices.
        std::size_t anchorIdx = order.size();
        std::size_t clickIdx = order.size();
        for (std::size_t i = 0; i < order.size(); ++i)
        {
            if (order[i] == m_SelectionAnchor) anchorIdx = i;
            if (order[i] == clickedEntity)     clickIdx = i;
        }

        // Fallback to single Replace selection if anchor not found.
        if (anchorIdx >= order.size() || clickIdx >= order.size())
        {
            GetSelection().SetSelectedEntity(GetSceneManager().GetScene(), clickedEntity);
            m_SelectionAnchor = clickedEntity;
            return;
        }

        auto& scene = GetSceneManager().GetScene();
        auto& reg = scene.GetRegistry();
        const std::size_t lo = std::min(anchorIdx, clickIdx);
        const std::size_t hi = std::max(anchorIdx, clickIdx);

        // Clear existing selection.
        auto selectedView = reg.view<ECS::Components::Selection::SelectedTag>();
        for (auto e : selectedView)
            reg.remove<ECS::Components::Selection::SelectedTag>(e);

        // Add range in bulk without firing per-entity events.
        for (std::size_t i = lo; i <= hi; ++i)
        {
            if (reg.valid(order[i])
                && reg.all_of<ECS::Components::Selection::SelectableTag>(order[i])
                && !reg.all_of<ECS::Components::Selection::SelectedTag>(order[i]))
                reg.emplace<ECS::Components::Selection::SelectedTag>(order[i]);
        }

        // Fire a single SelectionChanged event with the clicked entity as primary.
        scene.GetDispatcher().enqueue<ECS::Events::SelectionChanged>({clickedEntity});
    }

    // Recursively draw one entity and its hierarchy children.
    void DrawEntityNode(entt::entity entityID)
    {
        auto& reg = GetSceneManager().GetScene().GetRegistry();
        if (!reg.valid(entityID))
            return;
        if (reg.all_of<Runtime::EditorUI::HiddenEditorEntityTag>(entityID))
            return;

        // Entity display name
        std::string name = "Entity";
        if (const auto* nameTag = reg.try_get<ECS::Components::NameTag::Component>(entityID))
            name = nameTag->Name;

        const char* icon = EntityTypeIcon(reg, entityID);
        std::string displayName = std::string(icon) + name;

        // Determine if this entity has children
        bool hasChildren = false;
        if (const auto* hierarchy = reg.try_get<ECS::Components::Hierarchy::Component>(entityID))
            hasChildren = (hierarchy->FirstChild != entt::null);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (reg.all_of<ECS::Components::Selection::SelectedTag>(entityID))
            flags |= ImGuiTreeNodeFlags_Selected;
        if (!hasChildren)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        // Expand/collapse all request
        if (m_HierarchyCollapseRequest != 0)
            ImGui::SetNextItemOpen(m_HierarchyCollapseRequest > 0);

        bool opened = ImGui::TreeNodeEx(
            reinterpret_cast<void*>(static_cast<entt::id_type>(entityID)),
            flags, "%s", displayName.c_str());

        // --- Click to select (supports Ctrl+click toggle, Shift+click range) ---
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        {
            const bool ctrl  = ImGui::GetIO().KeyCtrl;
            const bool shift = ImGui::GetIO().KeyShift;

            // Multi-select only applies in Entity selection mode.
            // In sub-element modes, hierarchy clicks always do single Replace.
            const bool entityMode = GetSelection().GetConfig().ElementMode
                                    == Runtime::Selection::ElementMode::Entity;

            if (entityMode && shift && m_SelectionAnchor != entt::null)
            {
                SelectRangeInHierarchy(entityID);
                // Do not update anchor on shift-click (standard behavior).
            }
            else if (entityMode && ctrl)
            {
                auto& scene = GetSceneManager().GetScene();
                Runtime::Selection::ApplySelection(scene, entityID, Runtime::Selection::PickMode::Toggle);
                m_SelectionAnchor = entityID;
            }
            else
            {
                GetSelection().SetSelectedEntity(GetSceneManager().GetScene(), entityID);
                m_SelectionAnchor = entityID;
            }
        }

        // --- Drag source for reparenting ---
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
        {
            ImGui::SetDragDropPayload("HIERARCHY_ENTITY", &entityID, sizeof(entt::entity));
            ImGui::Text("%s", displayName.c_str());
            ImGui::EndDragDropSource();
        }

        // --- Drop target for reparenting ---
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY"))
            {
                entt::entity droppedEntity = *static_cast<const entt::entity*>(payload->Data);
                if (droppedEntity != entityID && reg.valid(droppedEntity))
                {
                    (void)GetCommandHistory().Execute(
                        Runtime::EditorUI::MakeReparentEntityCommand(*this, droppedEntity, entityID));
                }
            }
            ImGui::EndDragDropTarget();
        }

        // --- Context menu per entity ---
        bool wantRename = false;
        if (ImGui::BeginPopupContextItem())
        {
            if (ImGui::MenuItem("Focus Camera"))
            {
                GetSelection().SetSelectedEntity(GetSceneManager().GetScene(), entityID);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Duplicate"))
            {
                (void)GetCommandHistory().Execute(
                    Runtime::EditorUI::MakeDuplicateEntityCommand(*this, entityID));
            }
            if (ImGui::MenuItem("Rename"))
            {
                m_RenameEntity = entityID;
                std::strncpy(m_RenameBuffer, name.c_str(), sizeof(m_RenameBuffer) - 1);
                m_RenameBuffer[sizeof(m_RenameBuffer) - 1] = '\0';
                wantRename = true;
            }
            if (ImGui::MenuItem("Create Child"))
            {
                (void)GetCommandHistory().Execute(
                    Runtime::EditorUI::MakeCreateChildEntityCommand(*this, "Child Entity", entityID));
            }
            ImGui::Separator();
            {
                const bool visible = IsEntityVisible(reg, entityID);
                if (ImGui::MenuItem(visible ? "Hide" : "Show"))
                {
                    ToggleEntityVisibility(reg, entityID);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Delete"))
            {
                (void)GetCommandHistory().Execute(
                    Runtime::EditorUI::MakeDeleteEntityCommand(*this, entityID));
                ImGui::EndPopup();
                if (opened && hasChildren)
                    ImGui::TreePop();
                return;
            }
            ImGui::EndPopup();
        }

        // Open rename popup outside the context menu so the ID stack matches.
        if (wantRename)
            ImGui::OpenPopup("RenameEntityPopup");

        // --- Inline rename popup ---
        if (m_RenameEntity == entityID)
        {
            if (ImGui::BeginPopup("RenameEntityPopup"))
            {
                ImGui::Text("Rename:");
                if (ImGui::InputText("##rename", m_RenameBuffer, sizeof(m_RenameBuffer),
                                     ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll))
                {
                    std::string newName(m_RenameBuffer);
                    if (!newName.empty() && newName != name)
                    {
                        (void)GetCommandHistory().Execute(
                            Runtime::EditorUI::MakeRenameEntityCommand(*this, entityID, newName));
                    }
                    m_RenameEntity = entt::null;
                    ImGui::CloseCurrentPopup();
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    m_RenameEntity = entt::null;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            else
            {
                // Popup was closed externally
                m_RenameEntity = entt::null;
            }
        }

        if (opened && hasChildren)
        {
            // Walk child linked list
            if (const auto* hierarchy = reg.try_get<ECS::Components::Hierarchy::Component>(entityID))
            {
                entt::entity child = hierarchy->FirstChild;
                uint32_t safety = 0;
                while (child != entt::null && reg.valid(child) && safety < 1000)
                {
                    DrawEntityNode(child);
                    if (const auto* childH = reg.try_get<ECS::Components::Hierarchy::Component>(child))
                        child = childH->NextSibling;
                    else
                        break;
                    ++safety;
                }
            }
            ImGui::TreePop();
        }
    }

    void DrawHierarchyPanel()
    {
        auto& reg = GetSceneManager().GetScene().GetRegistry();
        const entt::entity selected = m_CachedSelectedEntity;

        // --- Toolbar: Expand All / Collapse All ---
        if (ImGui::SmallButton("Expand All"))
            m_HierarchyCollapseRequest = +1;
        ImGui::SameLine();
        if (ImGui::SmallButton("Collapse All"))
            m_HierarchyCollapseRequest = -1;
        ImGui::Separator();

        // Collect root entities (no parent, or no Hierarchy component).
        reg.view<entt::entity>().each([&](auto entityID)
        {
            if (reg.all_of<Runtime::EditorUI::HiddenEditorEntityTag>(entityID))
                return;

            // Skip entities that have a parent (they'll be drawn as children).
            if (const auto* hierarchy = reg.try_get<ECS::Components::Hierarchy::Component>(entityID))
            {
                if (hierarchy->Parent != entt::null)
                    return;
            }

            DrawEntityNode(entityID);
        });

        // Clear the expand/collapse request after one frame.
        m_HierarchyCollapseRequest = 0;

        // --- Drop on empty space = reparent to root ---
        // Use an invisible button spanning remaining space as a proper drop target.
        {
            const float remainingHeight = ImGui::GetContentRegionAvail().y;
            if (remainingHeight > 1.0f)
            {
                ImGui::InvisibleButton("##hierarchy_drop_area",
                                       ImVec2(ImGui::GetContentRegionAvail().x,
                                              remainingHeight));
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("HIERARCHY_ENTITY"))
                    {
                        entt::entity droppedEntity = *static_cast<const entt::entity*>(payload->Data);
                        if (reg.valid(droppedEntity))
                        {
                            (void)GetCommandHistory().Execute(
                                Runtime::EditorUI::MakeReparentEntityCommand(*this, droppedEntity, entt::null));
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }
        }

        // Click on empty space to deselect
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        {
            GetSelection().ClearSelection(GetSceneManager().GetScene());
            m_SelectionAnchor = entt::null;
        }

        // Right-click context menu on empty space
        if (ImGui::BeginPopupContextWindow(nullptr, ImGuiPopupFlags_NoOpenOverExistingPopup | ImGuiPopupFlags_MouseButtonRight))
        {
            if (ImGui::MenuItem("Create Empty Entity"))
            {
                (void)GetCommandHistory().Execute(
                    Runtime::EditorUI::MakeCreateEntityCommand(*this, "Empty Entity"));
            }
            if (ImGui::MenuItem("Create Demo Point Cloud"))
            {
                SpawnDemoPointCloud();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Remove Selected", nullptr, false,
                                selected != entt::null && reg.valid(selected)))
            {
                (void)GetCommandHistory().Execute(
                    Runtime::EditorUI::MakeDeleteEntityCommand(*this, selected));
            }
            ImGui::EndPopup();
        }
    }

    // =========================================================================
    // Camera helpers (Focus, Center, Reset)
    // =========================================================================

    // Compute entity world-space center and bounding radius.
    // Returns false if the entity has no computable bounds.
    bool ComputeEntityBounds(entt::entity entity, glm::vec3& outCenter, float& outRadius)
    {
        auto& reg = GetSceneManager().GetScene().GetRegistry();
        if (entity == entt::null || !reg.valid(entity)) return false;

        // 1) Mesh entity: use world OBB.
        if (auto* collider = reg.try_get<ECS::MeshCollider::Component>(entity))
        {
            if (collider->CollisionRef)
            {
                outCenter = collider->WorldOBB.Center;
                outRadius = glm::length(collider->WorldOBB.Extents);
                return true;
            }
        }

        // 2) Point cloud entity: compute AABB from cloud positions.
        if (auto* pcd = reg.try_get<ECS::PointCloud::Data>(entity))
        {
            if (pcd->CloudRef && !pcd->CloudRef->IsEmpty())
            {
                auto positions = pcd->CloudRef->Positions();
                Geometry::AABB aabb;
                for (const auto& p : positions)
                {
                    aabb.Min = glm::min(aabb.Min, p);
                    aabb.Max = glm::max(aabb.Max, p);
                }
                if (auto* xf = reg.try_get<ECS::Components::Transform::Component>(entity))
                {
                    glm::mat4 world = GetMatrix(*xf);
                    glm::vec3 wLo, wHi;
                    Runtime::EditorUI::TransformAABB(aabb.Min, aabb.Max, world, wLo, wHi);
                    outCenter = (wLo + wHi) * 0.5f;
                    outRadius = glm::length((wHi - wLo) * 0.5f);
                }
                else
                {
                    outCenter = aabb.GetCenter();
                    outRadius = glm::length(aabb.GetExtents());
                }
                return true;
            }
        }

        // 3) Graph entity: compute AABB from node positions.
        if (auto* gd = reg.try_get<ECS::Graph::Data>(entity))
        {
            if (gd->GraphRef && gd->GraphRef->VertexCount() > 0)
            {
                Geometry::AABB aabb;
                for (std::size_t i = 0; i < gd->GraphRef->VerticesSize(); ++i)
                {
                    Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
                    if (gd->GraphRef->IsValid(v))
                    {
                        glm::vec3 p = gd->GraphRef->VertexPosition(v);
                        aabb.Min = glm::min(aabb.Min, p);
                        aabb.Max = glm::max(aabb.Max, p);
                    }
                }
                if (auto* xf = reg.try_get<ECS::Components::Transform::Component>(entity))
                {
                    glm::mat4 world = GetMatrix(*xf);
                    glm::vec3 wLo, wHi;
                    Runtime::EditorUI::TransformAABB(aabb.Min, aabb.Max, world, wLo, wHi);
                    outCenter = (wLo + wHi) * 0.5f;
                    outRadius = glm::length((wHi - wLo) * 0.5f);
                }
                else
                {
                    outCenter = aabb.GetCenter();
                    outRadius = glm::length(aabb.GetExtents());
                }
                return true;
            }
        }

        return false;
    }

    void FocusCameraOnEntity(entt::entity entity)
    {
        auto* cam = GetSceneManager().GetScene().GetRegistry().try_get<Graphics::CameraComponent>(m_CameraEntity);
        auto* orbit = GetSceneManager().GetScene().GetRegistry().try_get<Graphics::OrbitControlComponent>(m_CameraEntity);
        if (!cam || !orbit) return;

        glm::vec3 center;
        float radius;
        if (!ComputeEntityBounds(entity, center, radius)) return;
        if (radius < 0.001f) radius = 1.0f;

        orbit->Target = center;
        float halfFov = glm::radians(cam->Fov) * 0.5f;
        float fitDistance = radius / glm::tan(halfFov);
        orbit->Distance = fitDistance * 1.5f;

        glm::vec3 viewDir = glm::normalize(cam->Position - orbit->Target);
        cam->Position = orbit->Target + viewDir * orbit->Distance;
    }

    // =========================================================================
    // Camera shortcuts (F=Focus, C=Center, Q=Reset)
    // =========================================================================
    void HandleCameraShortcuts(bool uiCapturesKeyboard, Graphics::CameraComponent* cameraComponent)
    {
        if (!cameraComponent) return;

        const bool fPressed = !uiCapturesKeyboard && m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::F);
        const bool cPressed = !uiCapturesKeyboard && m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::C);
        if (fPressed || cPressed)
        {
            auto* orbit = GetSceneManager().GetScene().GetRegistry().try_get<Graphics::OrbitControlComponent>(m_CameraEntity);
            if (!orbit) return;

            glm::vec3 center;
            float radius;
            if (!ComputeEntityBounds(m_CachedSelectedEntity, center, radius)) return;
            if (radius < 0.001f) radius = 1.0f;

            orbit->Target = center;

            if (fPressed)
            {
                float halfFov = glm::radians(cameraComponent->Fov) * 0.5f;
                float fitDistance = radius / glm::tan(halfFov);
                orbit->Distance = fitDistance * 1.5f;
            }

            glm::vec3 viewDir = glm::normalize(cameraComponent->Position - orbit->Target);
            cameraComponent->Position = orbit->Target + viewDir * orbit->Distance;
        }

        // Q Key: Reset camera to defaults
        if (!uiCapturesKeyboard && m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::Q))
        {
            ResetCamera();
        }
    }

    // =========================================================================
    // Viewport Context Menu — right-click on 3D viewport
    // =========================================================================
    // State for distinguishing right-click from right-drag (orbit camera).
    bool m_RightMousePressedInViewport = false;
    glm::vec2 m_RightMousePressPos{0.0f};

    void DrawViewportContextMenu()
    {
        // Track right-click press in viewport.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
            !ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow) &&
            !Interface::GUI::WantCaptureMouse())
        {
            m_RightMousePressedInViewport = true;
            m_RightMousePressPos = {ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y};
        }

        // Open context menu only on release, if no significant drag occurred.
        if (m_RightMousePressedInViewport && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            m_RightMousePressedInViewport = false;
            const glm::vec2 releasePos{ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y};
            const float dragDist = glm::length(releasePos - m_RightMousePressPos);
            constexpr float kDragThreshold = 5.0f; // pixels
            if (dragDist < kDragThreshold)
                ImGui::OpenPopup("##ViewportContextMenu");
        }

        if (!ImGui::BeginPopup("##ViewportContextMenu"))
            return;

        auto& scene = GetSceneManager().GetScene();
        auto& reg = scene.GetRegistry();
        const auto& selConfig = GetSelection().GetConfig();
        const bool isEntityMode = selConfig.ElementMode == Runtime::Selection::ElementMode::Entity;

        // Gather all entities currently carrying SelectedTag.
        const auto selectedEntities = GetSelection().GetSelectedEntities(scene);
        const entt::entity primary = m_CachedSelectedEntity;
        const bool hasPrimary = (primary != entt::null) && reg.valid(primary);

        if (isEntityMode)
        {
            DrawEntityContextMenuItems(reg, scene, selectedEntities, primary, hasPrimary);
        }
        else
        {
            DrawSubElementContextMenuItems(reg);
        }

        ImGui::EndPopup();
    }

    void DrawEntityContextMenuItems(entt::registry& reg, ECS::Scene& scene,
                                    const std::vector<entt::entity>& selectedEntities,
                                    entt::entity primary, bool hasPrimary)
    {
        // --- Entity actions (enabled when something is selected) ---
        if (ImGui::MenuItem("Focus Camera", "F", false, hasPrimary))
        {
            FocusCameraOnEntity(primary);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, hasPrimary))
        {
            for (auto e : selectedEntities)
            {
                if (reg.valid(e))
                    (void)GetCommandHistory().Execute(
                        Runtime::EditorUI::MakeDuplicateEntityCommand(*this, e));
            }
        }

        if (ImGui::MenuItem("Select Children", nullptr, false, hasPrimary))
        {
            SelectAllChildren(reg, scene, primary);
        }

        ImGui::Separator();

        {
            const bool anyVisible = std::any_of(selectedEntities.begin(), selectedEntities.end(),
                [&reg](entt::entity e) { return IsEntityVisible(reg, e); });
            if (ImGui::MenuItem(anyVisible ? "Hide" : "Show", "H", false, hasPrimary))
            {
                for (auto e : selectedEntities)
                {
                    if (reg.valid(e))
                        ToggleEntityVisibility(reg, e);
                }
            }
        }

        if (ImGui::MenuItem("Isolate", nullptr, false, hasPrimary))
        {
            IsolateEntities(reg, selectedEntities);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Delete", "Del", false, hasPrimary))
        {
            // Delete all selected entities (in reverse to avoid hierarchy issues)
            for (auto it = selectedEntities.rbegin(); it != selectedEntities.rend(); ++it)
            {
                if (reg.valid(*it))
                    (void)GetCommandHistory().Execute(
                        Runtime::EditorUI::MakeDeleteEntityCommand(*this, *it));
            }
        }

        ImGui::Separator();
        DrawCreatePrimitiveMenu(scene);

        ImGui::Separator();
        if (ImGui::MenuItem("Reset Camera", "Q"))
        {
            ResetCamera();
        }
    }

    void DrawSubElementContextMenuItems(entt::registry& reg)
    {
        const entt::entity entity = m_CachedSelectedEntity;
        const bool hasMesh = (entity != entt::null) && reg.valid(entity) &&
                             reg.all_of<ECS::Mesh::Data>(entity);
        const auto& selConfig = GetSelection().GetConfig();

        if (ImGui::MenuItem("Select Connected", nullptr, false, hasMesh))
        {
            GrowSelectionToConnected(reg, entity, selConfig.ElementMode);
        }
        if (ImGui::MenuItem("Grow Selection", "+", false, hasMesh))
        {
            GrowSelection(reg, entity, selConfig.ElementMode);
        }
        if (ImGui::MenuItem("Shrink Selection", "-", false, hasMesh))
        {
            ShrinkSelection(reg, entity, selConfig.ElementMode);
        }

        // Edge Loop / Edge Ring — only available in Edge mode with at least one edge selected
        const bool edgeModeWithSelection =
            hasMesh &&
            selConfig.ElementMode == Runtime::Selection::ElementMode::Edge &&
            !GetSelection().GetSubElementSelection().SelectedEdges.empty();
        if (ImGui::MenuItem("Select Edge Loop", "L", false, edgeModeWithSelection))
        {
            SelectEdgeLoop(reg, entity);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Extend selection along edge loops through vertices.\n"
                              "Best results on quad meshes (clean rows/columns).\n"
                              "On mixed tri/quad topology, uses angle-based\n"
                              "continuation for deterministic results.");
        if (ImGui::MenuItem("Select Edge Ring", nullptr, false, edgeModeWithSelection))
        {
            SelectEdgeRing(reg, entity);
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Extend selection across parallel edges in face strips.\n"
                              "Best results on quad meshes. Stops at boundaries\n"
                              "and non-tri/quad faces.");
        ImGui::Separator();
        if (ImGui::MenuItem("Clear Sub-Element Selection"))
        {
            GetSelection().ClearSubElementSelection();
        }
    }

    void DrawCreatePrimitiveMenu(ECS::Scene& /*scene*/)
    {
        if (ImGui::BeginMenu("Create Primitive"))
        {
            if (ImGui::MenuItem("Cube"))
                SpawnPrimitiveMesh("Cube", Geometry::AABB{glm::vec3(-0.5f), glm::vec3(0.5f)});
            if (ImGui::MenuItem("Sphere"))
                SpawnPrimitiveMesh("Sphere", Geometry::Sphere{glm::vec3(0.0f), 0.5f});
            if (ImGui::MenuItem("Cylinder"))
                SpawnPrimitiveMesh("Cylinder", Geometry::Cylinder{glm::vec3(0.0f, -0.5f, 0.0f), glm::vec3(0.0f, 0.5f, 0.0f), 0.5f});
            if (ImGui::MenuItem("Plane"))
            {
                // Thin box (non-degenerate) approximating a plane
                SpawnPrimitiveMesh("Plane", Geometry::AABB{glm::vec3(-1.0f, -0.001f, -1.0f), glm::vec3(1.0f, 0.001f, 1.0f)});
            }
            if (ImGui::MenuItem("Icosahedron"))
                SpawnPrimitiveMeshFromHalfedge("Icosahedron", Geometry::Halfedge::MakeMeshIcosahedron());
            if (ImGui::MenuItem("Octahedron"))
                SpawnPrimitiveMeshFromHalfedge("Octahedron", Geometry::Halfedge::MakeMeshOctahedron());
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Create Empty Entity"))
        {
            (void)GetCommandHistory().Execute(
                Runtime::EditorUI::MakeCreateEntityCommand(*this, "Empty Entity"));
        }
    }

    // =========================================================================
    // Primitive mesh spawning
    // =========================================================================
    template <typename PrimitiveT>
    void SpawnPrimitiveMesh(const char* name, const PrimitiveT& primitive)
    {
        auto meshOpt = Geometry::Halfedge::MakeMesh(primitive);
        if (!meshOpt)
        {
            Log::Warn("Failed to create {} primitive mesh.", name);
            return;
        }
        SpawnPrimitiveMeshFromHalfedge(name, std::move(*meshOpt));
    }

    void SpawnPrimitiveMeshFromHalfedge(const char* name, Geometry::Halfedge::Mesh mesh)
    {
        auto meshShared = std::make_shared<Geometry::Halfedge::Mesh>(std::move(mesh));

        // Shared handle between redo/undo lambdas (avoids dangling reference).
        auto entityHandle = std::make_shared<entt::entity>(entt::null);

        Core::EditorCommand command{};
        command.name = std::string("Create ") + name;

        command.redo = [this, meshShared, entityHandle, entityName = std::string(name)]()
        {
            auto& sc = GetSceneManager().GetScene();
            auto& r = sc.GetRegistry();
            auto& g = GetGraphicsBackend();

            entt::entity entity = sc.CreateEntity(entityName);
            r.emplace<ECS::Components::Selection::SelectableTag>(entity);
            r.emplace<ECS::DataAuthority::MeshTag>(entity);

            auto& md = r.emplace<ECS::Mesh::Data>(entity);
            md.MeshRef = meshShared;
            md.AttributesDirty = true;

            auto& surface = r.emplace<ECS::Surface::Component>(entity);

            std::vector<glm::vec3> positions;
            std::vector<uint32_t> indices;
            std::vector<glm::vec4> aux;
            Geometry::MeshUtils::ExtractIndexedTriangles(*meshShared, positions, indices, &aux);

            if (!positions.empty() && !indices.empty())
            {
                std::vector<glm::vec3> normals(positions.size(), glm::vec3(0.0f, 1.0f, 0.0f));
                Geometry::MeshUtils::CalculateNormals(positions, indices, normals);

                Graphics::GeometryUploadRequest upload{};
                upload.Positions = positions;
                upload.Normals = normals;
                upload.Aux = aux;
                upload.Indices = indices;
                upload.Topology = Graphics::PrimitiveTopology::Triangles;
                upload.UploadMode = Graphics::GeometryUploadMode::Staged;

                auto& geomStorage = GetRenderOrchestrator().GetGeometryStorage();
                auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(
                    g.GetDeviceShared(), g.GetTransferManager(), upload, &geomStorage);
                (void)token;

                if (gpuData && gpuData->GetVertexBuffer() && gpuData->GetIndexBuffer())
                    surface.Geometry = geomStorage.Add(std::move(gpuData));
            }

            auto collisionData = std::make_shared<Graphics::GeometryCollisionData>();
            collisionData->SourceMesh = meshShared;
            collisionData->Positions = positions;
            collisionData->Indices = indices;
            collisionData->LocalAABB = Geometry::Union(Geometry::ToAABB(
                std::span<const glm::vec3>(positions)));

            // Build octree for CPU raycast picking
            if (indices.size() >= 3)
            {
                std::vector<Geometry::AABB> primBounds;
                primBounds.reserve(indices.size() / 3);
                for (size_t ti = 0; ti + 2 < indices.size(); ti += 3)
                {
                    auto triAabb = Geometry::AABB{positions[indices[ti]], positions[indices[ti]]};
                    triAabb = Geometry::Union(triAabb, positions[indices[ti + 1]]);
                    triAabb = Geometry::Union(triAabb, positions[indices[ti + 2]]);
                    primBounds.push_back(triAabb);
                }
                if (!primBounds.empty())
                    static_cast<void>(collisionData->LocalOctree.Build(
                        primBounds, Geometry::Octree::SplitPolicy{}, 16, 8));
            }

            // Build vertex lookup KD-tree for sub-element picking
            collisionData->LocalVertexLookupPoints.reserve(meshShared->VertexCount());
            collisionData->LocalVertexLookupIndices.reserve(meshShared->VertexCount());
            for (std::size_t vi = 0; vi < meshShared->VerticesSize(); ++vi)
            {
                Geometry::VertexHandle vh{static_cast<Geometry::PropertyIndex>(vi)};
                if (!meshShared->IsValid(vh) || meshShared->IsDeleted(vh)) continue;
                collisionData->LocalVertexLookupPoints.push_back(meshShared->Position(vh));
                collisionData->LocalVertexLookupIndices.push_back(static_cast<uint32_t>(vh.Index));
            }
            if (!collisionData->LocalVertexLookupPoints.empty())
                static_cast<void>(collisionData->LocalVertexKdTree.BuildFromPoints(
                    collisionData->LocalVertexLookupPoints));

            auto& collider = r.emplace<ECS::MeshCollider::Component>(entity);
            collider.CollisionRef = collisionData;
            collider.WorldOBB.Center = collisionData->LocalAABB.GetCenter();
            collider.WorldOBB.Extents = collisionData->LocalAABB.GetExtents();

            GetSelection().SetSelectedEntity(sc, entity);
            *entityHandle = entity;
        };

        command.undo = [this, entityHandle]()
        {
            auto& sc = GetSceneManager().GetScene();
            auto& r = sc.GetRegistry();
            if (*entityHandle != entt::null && r.valid(*entityHandle))
            {
                // Detach from hierarchy before destroying (prevents dangling sibling refs).
                ECS::Components::Hierarchy::Detach(r, *entityHandle);
                r.destroy(*entityHandle);
                *entityHandle = entt::null;
                GetSelection().ClearSelection(sc);
            }
        };

        (void)GetCommandHistory().Execute(std::move(command));
    }

    // =========================================================================
    // Entity selection helpers
    // =========================================================================
    void SelectAllChildren(entt::registry& reg, ECS::Scene& scene, entt::entity parent)
    {
        if (parent == entt::null || !reg.valid(parent)) return;

        auto* hierarchy = reg.try_get<ECS::Components::Hierarchy::Component>(parent);
        if (!hierarchy) return;

        entt::entity child = hierarchy->FirstChild;
        uint32_t safety = 0;
        while (child != entt::null && reg.valid(child) && safety < 10000)
        {
            Runtime::Selection::ApplySelection(scene, child, Runtime::Selection::PickMode::Add);

            // Recursively select grandchildren and advance to next sibling.
            // Cache NextSibling before recursion in case the child list is modified.
            entt::entity nextSibling = entt::null;
            if (auto* childH = reg.try_get<ECS::Components::Hierarchy::Component>(child))
            {
                nextSibling = childH->NextSibling;
                SelectAllChildren(reg, scene, child);
            }
            child = nextSibling;
            ++safety;
        }
    }

    void IsolateEntities(entt::registry& reg, const std::vector<entt::entity>& keepVisible)
    {
        // Hide all entities, then show only the selected ones.
        reg.view<entt::entity>().each([&](auto entityID)
        {
            if (reg.all_of<Runtime::EditorUI::HiddenEditorEntityTag>(entityID))
                return;

            // Check if this entity is in the keep-visible list
            bool keep = false;
            for (auto e : keepVisible)
            {
                if (e == entityID)
                {
                    keep = true;
                    break;
                }
            }

            if (keep)
            {
                // Ensure visible
                if (auto* sc = reg.try_get<ECS::Surface::Component>(entityID))
                    sc->Visible = true;
                if (auto* gd = reg.try_get<ECS::Graph::Data>(entityID))
                    gd->Visible = true;
                if (auto* pcd = reg.try_get<ECS::PointCloud::Data>(entityID))
                    pcd->Visible = true;
            }
            else
            {
                // Hide
                if (auto* sc = reg.try_get<ECS::Surface::Component>(entityID))
                    sc->Visible = false;
                if (auto* gd = reg.try_get<ECS::Graph::Data>(entityID))
                    gd->Visible = false;
                if (auto* pcd = reg.try_get<ECS::PointCloud::Data>(entityID))
                    pcd->Visible = false;
            }
        });
    }

    void ResetCamera()
    {
        auto& reg = GetSceneManager().GetScene().GetRegistry();
        auto* cam = reg.try_get<Graphics::CameraComponent>(m_CameraEntity);
        auto* orbit = reg.try_get<Graphics::OrbitControlComponent>(m_CameraEntity);
        if (cam && orbit)
        {
            orbit->Target = glm::vec3(0.0f);
            orbit->Distance = 5.0f;
            cam->Position = glm::vec3(0.0f, 0.0f, 4.0f);
            cam->Orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    // =========================================================================
    // Sub-element selection helpers (Grow / Shrink / Select Connected)
    // =========================================================================
    void GrowSelectionToConnected(entt::registry& reg, entt::entity entity,
                                  Runtime::Selection::ElementMode mode)
    {
        auto* meshData = reg.try_get<ECS::Mesh::Data>(entity);
        if (!meshData || !meshData->MeshRef) return;
        const auto& mesh = *meshData->MeshRef;
        auto& subSel = GetSelection().GetSubElementSelection();

        if (mode == Runtime::Selection::ElementMode::Vertex)
        {
            // Flood fill from selected vertices through edge connectivity
            const uint32_t safetyLimit = static_cast<uint32_t>(mesh.VertexCount()) * 2 + 1;
            uint32_t iterations = 0;
            std::vector<uint32_t> frontier(subSel.SelectedVertices.begin(), subSel.SelectedVertices.end());
            while (!frontier.empty() && iterations++ < safetyLimit)
            {
                std::vector<uint32_t> next;
                for (uint32_t vi : frontier)
                {
                    auto vh = Geometry::VertexHandle(vi);
                    if (!mesh.IsValid(vh)) continue;
                    for (auto he : mesh.HalfedgesAroundVertex(vh))
                    {
                        auto target = mesh.ToVertex(he);
                        uint32_t ti = target.IsValid() ? target.Index : UINT32_MAX;
                        if (ti != UINT32_MAX && subSel.SelectedVertices.find(ti) == subSel.SelectedVertices.end())
                        {
                            subSel.SelectedVertices.insert(ti);
                            next.push_back(ti);
                        }
                    }
                }
                frontier = std::move(next);
            }
        }
        else if (mode == Runtime::Selection::ElementMode::Face)
        {
            // Flood fill from selected faces through shared edges
            const uint32_t safetyLimit = static_cast<uint32_t>(mesh.FaceCount()) * 2 + 1;
            uint32_t iterations = 0;
            std::vector<uint32_t> frontier(subSel.SelectedFaces.begin(), subSel.SelectedFaces.end());
            while (!frontier.empty() && iterations++ < safetyLimit)
            {
                std::vector<uint32_t> next;
                for (uint32_t fi : frontier)
                {
                    auto fh = Geometry::FaceHandle(fi);
                    if (!mesh.IsValid(fh)) continue;
                    for (auto he : mesh.HalfedgesAroundFace(fh))
                    {
                        auto opp = mesh.OppositeHalfedge(he);
                        if (!opp.IsValid()) continue;
                        auto adjFace = mesh.Face(opp);
                        if (!adjFace.IsValid()) continue;
                        uint32_t ai = adjFace.Index;
                        if (subSel.SelectedFaces.find(ai) == subSel.SelectedFaces.end())
                        {
                            subSel.SelectedFaces.insert(ai);
                            next.push_back(ai);
                        }
                    }
                }
                frontier = std::move(next);
            }
        }
        else if (mode == Runtime::Selection::ElementMode::Edge)
        {
            // Flood fill edges through shared vertices
            const uint32_t safetyLimit = static_cast<uint32_t>(mesh.EdgeCount()) * 2 + 1;
            uint32_t iterations = 0;
            std::vector<uint32_t> frontier(subSel.SelectedEdges.begin(), subSel.SelectedEdges.end());
            while (!frontier.empty() && iterations++ < safetyLimit)
            {
                std::vector<uint32_t> next;
                for (uint32_t ei : frontier)
                {
                    auto eh = Geometry::EdgeHandle(ei);
                    if (!mesh.IsValid(eh)) continue;
                    auto he0 = mesh.Halfedge(eh, 0);
                    if (!he0.IsValid()) continue;
                    auto he1 = mesh.OppositeHalfedge(he0);

                    // Get both endpoint vertices and find adjacent edges
                    for (auto endpointHe : {he0, he1})
                    {
                        if (!endpointHe.IsValid()) continue;
                        auto v = mesh.ToVertex(endpointHe);
                        if (!v.IsValid()) continue;
                        for (auto vhe : mesh.HalfedgesAroundVertex(v))
                        {
                            auto adjEdge = mesh.Edge(vhe);
                            if (!adjEdge.IsValid()) continue;
                            uint32_t ai = adjEdge.Index;
                            if (subSel.SelectedEdges.find(ai) == subSel.SelectedEdges.end())
                            {
                                subSel.SelectedEdges.insert(ai);
                                next.push_back(ai);
                            }
                        }
                    }
                }
                frontier = std::move(next);
            }
        }
    }

    void GrowSelection(entt::registry& reg, entt::entity entity,
                        Runtime::Selection::ElementMode mode)
    {
        auto* meshData = reg.try_get<ECS::Mesh::Data>(entity);
        if (!meshData || !meshData->MeshRef) return;
        const auto& mesh = *meshData->MeshRef;
        auto& subSel = GetSelection().GetSubElementSelection();

        if (mode == Runtime::Selection::ElementMode::Vertex)
        {
            std::set<uint32_t> toAdd;
            for (uint32_t vi : subSel.SelectedVertices)
            {
                auto vh = Geometry::VertexHandle(vi);
                if (!mesh.IsValid(vh)) continue;
                for (auto he : mesh.HalfedgesAroundVertex(vh))
                {
                    auto target = mesh.ToVertex(he);
                    if (target.IsValid())
                        toAdd.insert(target.Index);
                }
            }
            subSel.SelectedVertices.insert(toAdd.begin(), toAdd.end());
        }
        else if (mode == Runtime::Selection::ElementMode::Face)
        {
            std::set<uint32_t> toAdd;
            for (uint32_t fi : subSel.SelectedFaces)
            {
                auto fh = Geometry::FaceHandle(fi);
                if (!mesh.IsValid(fh)) continue;
                for (auto he : mesh.HalfedgesAroundFace(fh))
                {
                    auto opp = mesh.OppositeHalfedge(he);
                    if (!opp.IsValid()) continue;
                    auto adjFace = mesh.Face(opp);
                    if (adjFace.IsValid())
                        toAdd.insert(adjFace.Index);
                }
            }
            subSel.SelectedFaces.insert(toAdd.begin(), toAdd.end());
        }
        else if (mode == Runtime::Selection::ElementMode::Edge)
        {
            std::set<uint32_t> toAdd;
            for (uint32_t ei : subSel.SelectedEdges)
            {
                auto eh = Geometry::EdgeHandle(ei);
                if (!mesh.IsValid(eh)) continue;
                auto he0 = mesh.Halfedge(eh, 0);
                if (!he0.IsValid()) continue;
                auto he1 = mesh.OppositeHalfedge(he0);
                for (auto endpointHe : {he0, he1})
                {
                    if (!endpointHe.IsValid()) continue;
                    auto v = mesh.ToVertex(endpointHe);
                    if (!v.IsValid()) continue;
                    for (auto vhe : mesh.HalfedgesAroundVertex(v))
                    {
                        auto adjEdge = mesh.Edge(vhe);
                        if (adjEdge.IsValid())
                            toAdd.insert(adjEdge.Index);
                    }
                }
            }
            subSel.SelectedEdges.insert(toAdd.begin(), toAdd.end());
        }
    }

    void ShrinkSelection(entt::registry& reg, entt::entity entity,
                          Runtime::Selection::ElementMode mode)
    {
        auto* meshData = reg.try_get<ECS::Mesh::Data>(entity);
        if (!meshData || !meshData->MeshRef) return;
        const auto& mesh = *meshData->MeshRef;
        auto& subSel = GetSelection().GetSubElementSelection();

        if (mode == Runtime::Selection::ElementMode::Vertex)
        {
            // Remove vertices that have any unselected neighbor
            std::set<uint32_t> toRemove;
            for (uint32_t vi : subSel.SelectedVertices)
            {
                auto vh = Geometry::VertexHandle(vi);
                if (!mesh.IsValid(vh)) { toRemove.insert(vi); continue; }
                for (auto he : mesh.HalfedgesAroundVertex(vh))
                {
                    auto target = mesh.ToVertex(he);
                    if (target.IsValid() &&
                        subSel.SelectedVertices.find(target.Index) == subSel.SelectedVertices.end())
                    {
                        toRemove.insert(vi);
                        break;
                    }
                }
            }
            for (uint32_t v : toRemove)
                subSel.SelectedVertices.erase(v);
        }
        else if (mode == Runtime::Selection::ElementMode::Face)
        {
            std::set<uint32_t> toRemove;
            for (uint32_t fi : subSel.SelectedFaces)
            {
                auto fh = Geometry::FaceHandle(fi);
                if (!mesh.IsValid(fh)) { toRemove.insert(fi); continue; }
                for (auto he : mesh.HalfedgesAroundFace(fh))
                {
                    auto opp = mesh.OppositeHalfedge(he);
                    if (!opp.IsValid()) { toRemove.insert(fi); break; }
                    auto adjFace = mesh.Face(opp);
                    if (!adjFace.IsValid() ||
                        subSel.SelectedFaces.find(adjFace.Index) == subSel.SelectedFaces.end())
                    {
                        toRemove.insert(fi);
                        break;
                    }
                }
            }
            for (uint32_t f : toRemove)
                subSel.SelectedFaces.erase(f);
        }
        else if (mode == Runtime::Selection::ElementMode::Edge)
        {
            std::set<uint32_t> toRemove;
            for (uint32_t ei : subSel.SelectedEdges)
            {
                auto eh = Geometry::EdgeHandle(ei);
                if (!mesh.IsValid(eh)) { toRemove.insert(ei); continue; }
                auto he0 = mesh.Halfedge(eh, 0);
                if (!he0.IsValid()) { toRemove.insert(ei); continue; }
                auto he1 = mesh.OppositeHalfedge(he0);
                bool boundary = false;
                for (auto endpointHe : {he0, he1})
                {
                    if (!endpointHe.IsValid()) continue;
                    auto v = mesh.ToVertex(endpointHe);
                    if (!v.IsValid()) continue;
                    for (auto vhe : mesh.HalfedgesAroundVertex(v))
                    {
                        auto adjEdge = mesh.Edge(vhe);
                        if (adjEdge.IsValid() &&
                            subSel.SelectedEdges.find(adjEdge.Index) == subSel.SelectedEdges.end())
                        {
                            boundary = true;
                            break;
                        }
                    }
                    if (boundary) break;
                }
                if (boundary)
                    toRemove.insert(ei);
            }
            for (uint32_t e : toRemove)
                subSel.SelectedEdges.erase(e);
        }
    }

    // =========================================================================
    // Edge Loop / Edge Ring selection
    // =========================================================================
    void SelectEdgeLoop(entt::registry& reg, entt::entity entity)
    {
        auto* meshData = reg.try_get<ECS::Mesh::Data>(entity);
        if (!meshData || !meshData->MeshRef) return;
        const auto& mesh = *meshData->MeshRef;
        auto& subSel = GetSelection().GetSubElementSelection();

        // Collect loops from all currently selected edges
        std::set<uint32_t> seedEdges(subSel.SelectedEdges);
        for (uint32_t ei : seedEdges)
        {
            auto eh = Geometry::EdgeHandle(ei);
            if (!mesh.IsValid(eh) || mesh.IsDeleted(eh)) continue;
            auto loop = Geometry::MeshUtils::CollectEdgeLoop(mesh, eh);
            subSel.SelectedEdges.insert(loop.begin(), loop.end());
        }
    }

    void SelectEdgeRing(entt::registry& reg, entt::entity entity)
    {
        auto* meshData = reg.try_get<ECS::Mesh::Data>(entity);
        if (!meshData || !meshData->MeshRef) return;
        const auto& mesh = *meshData->MeshRef;
        auto& subSel = GetSelection().GetSubElementSelection();

        // Collect rings from all currently selected edges
        std::set<uint32_t> seedEdges(subSel.SelectedEdges);
        for (uint32_t ei : seedEdges)
        {
            auto eh = Geometry::EdgeHandle(ei);
            if (!mesh.IsValid(eh) || mesh.IsDeleted(eh)) continue;
            auto ring = Geometry::MeshUtils::CollectEdgeRing(mesh, eh);
            subSel.SelectedEdges.insert(ring.begin(), ring.end());
        }
    }

    // =========================================================================
    // SpawnDemoPointCloud — hemisphere point cloud for testing
    // =========================================================================
    void SpawnDemoPointCloud()
    {
        constexpr std::size_t N = 500;
        constexpr float radius = 1.0f;

        Geometry::PointCloud::Cloud cloud;
        cloud.Reserve(N);
        cloud.EnableNormals();
        cloud.EnableColors();

        const float goldenRatio = (1.0f + std::sqrt(5.0f)) * 0.5f;
        const float goldenAngle = 2.0f * glm::pi<float>() / (goldenRatio * goldenRatio);

        for (std::size_t i = 0; i < N; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(N - 1);
            const float phi = std::acos(1.0f - 2.0f * t);
            const float theta = goldenAngle * static_cast<float>(i);

            const float y = std::cos(phi);
            if (y < -0.05f) continue;

            const float sinPhi = std::sin(phi);
            const float x = sinPhi * std::cos(theta);
            const float z = sinPhi * std::sin(theta);

            const glm::vec3 pos = glm::vec3(x, y, z) * radius;
            const glm::vec3 normal = glm::normalize(pos);

            const float h = (y + 0.05f) / 1.05f;
            const glm::vec4 color = glm::mix(
                glm::vec4(0.2f, 0.4f, 0.9f, 1.0f),
                glm::vec4(1.0f, 0.6f, 0.1f, 1.0f),
                h);

            auto ph = cloud.AddPoint(pos);
            cloud.Normal(ph) = normal;
            cloud.Color(ph) = color;
        }

        if (cloud.IsEmpty())
        {
            Log::Warn("SpawnDemoPointCloud: no points generated.");
            return;
        }

        Geometry::PointCloud::RadiusEstimationParams radiiParams;
        radiiParams.KNeighbors = 6;
        radiiParams.ScaleFactor = 1.2f;
        auto radiiResult = Geometry::PointCloud::EstimateRadii(cloud, radiiParams);

        auto& scene = GetSceneManager().GetScene();
        entt::entity entity = scene.CreateEntity("Demo Point Cloud");

        scene.GetRegistry().emplace<ECS::DataAuthority::PointCloudTag>(entity);
        auto& pcd = scene.GetRegistry().emplace<ECS::PointCloud::Data>(entity);
        pcd.CloudRef = std::make_shared<Geometry::PointCloud::Cloud>(std::move(cloud));
        pcd.GpuDirty = true;
        pcd.RenderMode = Geometry::PointCloud::RenderMode::FlatDisc;
        pcd.DefaultRadius = 0.02f;
        pcd.SizeMultiplier = 1.0f;

        scene.GetRegistry().emplace<ECS::Components::Selection::SelectableTag>(entity);
        static uint32_t s_PointCloudPickId = 10000u;
        scene.GetRegistry().emplace<ECS::Components::Selection::PickID>(entity, s_PointCloudPickId++);

        Log::Info("Spawned demo point cloud: {} points, normals={}, radii={}",
                  pcd.PointCount(), pcd.HasNormals() ? "yes" : "no", pcd.HasRadii() ? "yes" : "no");
    }
};

// =============================================================================
// main() — CLI parsing and app bootstrap only
// =============================================================================
int main(int argc, char* argv[])
{
    const auto printUsage = []()
    {
        Log::Info("Sandbox CLI options:");
        Log::Info("  --benchmark <frames>      Run benchmark mode for N frames.");
        Log::Info("  --warmup <frames>         Benchmark warmup frame count.");
        Log::Info("  --out <path>              Benchmark output JSON path.");
        Log::Info("  --fixed-hz <hz>           Fixed simulation rate (e.g. 60, 120).");
        Log::Info("  --max-frame-dt <sec>      Clamp frame delta in seconds.");
        Log::Info("  --max-substeps <count>    Maximum fixed ticks per frame.");
        Log::Info("  --max-active-fps <fps>    Active FPS cap (0 = VSync only).");
        Log::Info("  --idle-fps <fps>          Idle FPS cap.");
        Log::Info("  --idle-timeout <sec>      Seconds before idle throttling.");
        Log::Info("  --no-idle-throttle        Disable activity-aware idle pacing.");
        Log::Info("  --help                    Print this help.");
    };

    const auto parseUInt = [](std::string_view text) -> std::optional<uint32_t>
    {
        uint32_t value = 0;
        const auto* first = text.data();
        const auto* last = first + text.size();
        const auto [ptr, ec] = std::from_chars(first, last, value);
        if (ec != std::errc{} || ptr != last)
            return std::nullopt;
        return value;
    };

    const auto parseInt = [](std::string_view text) -> std::optional<int>
    {
        int value = 0;
        const auto* first = text.data();
        const auto* last = first + text.size();
        const auto [ptr, ec] = std::from_chars(first, last, value);
        if (ec != std::errc{} || ptr != last)
            return std::nullopt;
        return value;
    };

    const auto parseDouble = [](std::string_view text) -> std::optional<double>
    {
        double value = 0.0;
        const auto* first = text.data();
        const auto* last = first + text.size();
        const auto [ptr, ec] = std::from_chars(first, last, value);
        if (ec != std::errc{} || ptr != last)
            return std::nullopt;
        return value;
    };

    Runtime::EngineConfig config{};
    config.AppName = "Sandbox";
    config.Width = 1600;
    config.Height = 900;
    // Editor-oriented defaults: VSync handles primary pacing; the activity
    // tracker drops to 15 fps after 2 seconds of idle to conserve CPU/GPU.
    config.FramePacing = {
        .ActiveFps = 0.0,             // 0 = VSync-limited (no additional sleep cap)
        .IdleFps = 15.0,              // Idle rate when no user interaction
        .IdleTimeoutSeconds = 2.0,    // Seconds before entering idle pacing
        .Enabled = true,
    };

    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg = argv[i];
        const auto requireValue = [&](std::string_view flag) -> std::optional<std::string_view>
        {
            if (i + 1 >= argc)
            {
                Log::Error("Missing value for {}", flag);
                Log::Error("Fix: provide {} <value>.", flag);
                return std::nullopt;
            }
            return std::string_view(argv[++i]);
        };

        if (arg == "--benchmark")
        {
            config.BenchmarkMode = true;
            const auto value = requireValue("--benchmark");
            if (!value) return 2;
            const auto parsed = parseUInt(*value);
            if (!parsed)
            {
                Log::Error("Invalid integer for --benchmark: '{}'", *value);
                Log::Error("Fix: use a positive integer frame count, e.g. --benchmark 300.");
                return 2;
            }
            config.BenchmarkFrames = *parsed;
        }
        else if (arg == "--warmup")
        {
            const auto value = requireValue("--warmup");
            if (!value) return 2;
            const auto parsed = parseUInt(*value);
            if (!parsed)
            {
                Log::Error("Invalid integer for --warmup: '{}'", *value);
                Log::Error("Fix: use a non-negative integer frame count, e.g. --warmup 30.");
                return 2;
            }
            config.BenchmarkWarmupFrames = *parsed;
        }
        else if (arg == "--out")
        {
            const auto value = requireValue("--out");
            if (!value) return 2;
            config.BenchmarkOutputPath = std::string(*value);
        }
        else if (arg == "--fixed-hz")
        {
            const auto value = requireValue("--fixed-hz");
            if (!value) return 2;
            const auto parsed = parseDouble(*value);
            if (!parsed)
            {
                Log::Error("Invalid floating-point value for --fixed-hz: '{}'", *value);
                Log::Error("Fix: use a finite numeric value, e.g. --fixed-hz 60.");
                return 2;
            }
            config.FixedStepHz = *parsed;
        }
        else if (arg == "--max-frame-dt")
        {
            const auto value = requireValue("--max-frame-dt");
            if (!value) return 2;
            const auto parsed = parseDouble(*value);
            if (!parsed)
            {
                Log::Error("Invalid floating-point value for --max-frame-dt: '{}'", *value);
                Log::Error("Fix: use a positive value in seconds, e.g. --max-frame-dt 0.25.");
                return 2;
            }
            config.MaxFrameDeltaSeconds = *parsed;
        }
        else if (arg == "--max-substeps")
        {
            const auto value = requireValue("--max-substeps");
            if (!value) return 2;
            const auto parsed = parseInt(*value);
            if (!parsed)
            {
                Log::Error("Invalid integer for --max-substeps: '{}'", *value);
                Log::Error("Fix: use a positive integer, e.g. --max-substeps 8.");
                return 2;
            }
            config.MaxSubstepsPerFrame = *parsed;
        }
        else if (arg == "--max-active-fps")
        {
            const auto value = requireValue("--max-active-fps");
            if (!value) return 2;
            const auto parsed = parseDouble(*value);
            if (!parsed)
            {
                Log::Error("Invalid floating-point value for --max-active-fps: '{}'", *value);
                Log::Error("Fix: use 0 or a positive value, e.g. --max-active-fps 144.");
                return 2;
            }
            config.FramePacing.ActiveFps = *parsed;
        }
        else if (arg == "--idle-fps")
        {
            const auto value = requireValue("--idle-fps");
            if (!value) return 2;
            const auto parsed = parseDouble(*value);
            if (!parsed)
            {
                Log::Error("Invalid floating-point value for --idle-fps: '{}'", *value);
                Log::Error("Fix: use 0 or a positive value, e.g. --idle-fps 15.");
                return 2;
            }
            config.FramePacing.IdleFps = *parsed;
        }
        else if (arg == "--idle-timeout")
        {
            const auto value = requireValue("--idle-timeout");
            if (!value) return 2;
            const auto parsed = parseDouble(*value);
            if (!parsed)
            {
                Log::Error("Invalid floating-point value for --idle-timeout: '{}'", *value);
                Log::Error("Fix: use a non-negative timeout in seconds, e.g. --idle-timeout 2.0.");
                return 2;
            }
            config.FramePacing.IdleTimeoutSeconds = *parsed;
        }
        else if (arg == "--no-idle-throttle")
        {
            config.FramePacing.Enabled = false;
        }
        else if (arg == "--help")
        {
            printUsage();
            return 0;
        }
        else
        {
            Log::Error("Unknown CLI option '{}'.", arg);
            printUsage();
            return 2;
        }
    }

    const Runtime::EngineConfigValidationResult configValidation = Runtime::ValidateEngineConfig(config);
    if (configValidation.HasErrors())
    {
        for (const auto& issue : configValidation.Issues)
        {
            if (issue.Severity == Runtime::EngineConfigIssueSeverity::Error)
            {
                Log::Error("Invalid config [{}]: {} | Fix: {}", issue.Field, issue.Message, issue.Remediation);
            }
        }
        Log::Error("Sandbox startup aborted due to invalid configuration.");
        return 2;
    }

    SandboxApp app(configValidation.Sanitized);
    app.Run();
    return 0;
}
