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
#include <cmath>
#include <string>
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
import Graphics.TextureLoader;
import Graphics.TransformGizmo;
import Geometry;
import ECS;
import RHI.Texture;
import Interface;


using namespace Core;
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
            auto& renderSys = GetRenderOrchestrator().GetRenderSystem();

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
    int m_SelectMouseButton = 0;

    // --- Extracted subsystem controllers ---
    EditorUI::SpatialDebugController m_SpatialDebug{};
    EditorUI::GeometryWorkflowController m_GeometryWorkflow{};
    EditorUI::InspectorController m_Inspector{};

    // --- Transform Gizmo ---
    Graphics::TransformGizmo m_Gizmo{};

    // =========================================================================
    // Asset loading helpers
    // =========================================================================
    void LoadDuckAssets(GraphicsBackend& gfx)
    {
        auto textureLoader = [this, &gfx](const std::filesystem::path& path, Core::Assets::AssetHandle handle)
            -> std::shared_ptr<RHI::Texture>
        {
            auto result = Graphics::TextureLoader::LoadAsync(path, GetGraphicsBackend().GetDevice(),
                                                             gfx.GetTransferManager(), gfx.GetTextureSystem());

            if (result)
            {
                GetAssetPipeline().RegisterAssetLoad(handle, result->Token, [this, texHandle = result->TextureHandle]()
                {
                    auto& g = GetGraphicsBackend();
                    if (const auto* data = g.GetTextureSystem().Get(texHandle))
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
            GetRenderOrchestrator().GetMaterialSystem(),
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
                    GetRenderOrchestrator().GetRenderSystem().RequestPipelineSwap(std::move(pipeline));
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

                const auto gpuPick = GetRenderOrchestrator().GetRenderSystem().GetLastPickResult();
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

        // Status bar
        Interface::GUI::RegisterPanel("Status Bar", [this]()
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
                ImGui::Text("Frame: %.2f ms (%.1f FPS)", frameMs, fps);
                VerticalSeparator();
                ImGui::Text("Entities: %d", static_cast<int>(GetSceneManager().GetScene().Size()));
                VerticalSeparator();
                ImGui::Text("Render Mode: DefaultPipeline");
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }, false, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

        // Viewport toolbar (gizmo)
        Interface::GUI::RegisterPanel("Viewport Toolbar", [this]()
        {
            auto& cfg = m_Gizmo.GetConfig();

            ImGui::SeparatorText("Gizmo Mode");
            int mode = static_cast<int>(cfg.Mode);
            if (ImGui::RadioButton("Translate (W)", mode == 0)) m_Gizmo.SetMode(Graphics::GizmoMode::Translate);
            ImGui::SameLine();
            if (ImGui::RadioButton("Rotate (E)", mode == 1)) m_Gizmo.SetMode(Graphics::GizmoMode::Rotate);
            ImGui::SameLine();
            if (ImGui::RadioButton("Scale (R)", mode == 2)) m_Gizmo.SetMode(Graphics::GizmoMode::Scale);

            ImGui::SeparatorText("Transform Space");
            int space = static_cast<int>(cfg.Space);
            if (ImGui::RadioButton("World", space == 0)) cfg.Space = Graphics::GizmoSpace::World;
            ImGui::SameLine();
            if (ImGui::RadioButton("Local (X)", space == 1)) cfg.Space = Graphics::GizmoSpace::Local;

            ImGui::SeparatorText("Pivot");
            int pivot = static_cast<int>(cfg.Pivot);
            if (ImGui::RadioButton("Centroid", pivot == 0)) cfg.Pivot = Graphics::GizmoPivot::Centroid;
            ImGui::SameLine();
            if (ImGui::RadioButton("First Selected", pivot == 1)) cfg.Pivot = Graphics::GizmoPivot::FirstSelected;

            ImGui::SeparatorText("Snapping");
            ImGui::Checkbox("Enable Snap", &cfg.SnapEnabled);
            if (cfg.SnapEnabled)
            {
                ImGui::DragFloat("Translate Snap", &cfg.TranslateSnap, 0.05f, 0.01f, 10.0f, "%.2f");
                ImGui::DragFloat("Rotate Snap (deg)", &cfg.RotateSnap, 1.0f, 1.0f, 90.0f, "%.1f");
                ImGui::DragFloat("Scale Snap", &cfg.ScaleSnap, 0.01f, 0.01f, 1.0f, "%.2f");
            }

            ImGui::SeparatorText("Appearance");
            ImGui::SliderFloat("Handle Size", &cfg.HandleLength, 0.3f, 3.0f, "%.1f");
            ImGui::SliderFloat("Handle Thickness", &cfg.HandleThickness, 1.0f, 8.0f, "%.1f px");

            ImGui::Separator();
            const char* stateNames[] = {"Idle", "Hovered", "Active"};
            ImGui::Text("State: %s", stateNames[static_cast<int>(m_Gizmo.GetState())]);
        }, true, 0, false);

        Interface::GUI::RegisterOverlay("Transform Gizmo", [this]()
        {
            m_Gizmo.DrawImGui();
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
        // --- Post-Processing ---
        auto* postSettings = GetRenderOrchestrator().GetRenderSystem().GetPostProcessSettings();
        if (postSettings)
        {
            ImGui::SeparatorText("Post Processing");

            const char* toneMapOps[] = {"ACES", "Reinhard", "Uncharted 2"};
            int toneOp = static_cast<int>(postSettings->ToneOperator);
            if (ImGui::Combo("Tone Mapping", &toneOp, toneMapOps, 3))
                postSettings->ToneOperator = static_cast<Graphics::Passes::ToneMapOperator>(toneOp);

            ImGui::SliderFloat("Exposure", &postSettings->Exposure, 0.1f, 10.0f, "%.2f");

            // Bloom
            ImGui::Spacing();
            ImGui::Checkbox("Bloom", &postSettings->BloomEnabled);
            if (postSettings->BloomEnabled)
            {
                ImGui::SliderFloat("Bloom Threshold", &postSettings->BloomThreshold, 0.0f, 5.0f, "%.2f");
                ImGui::SliderFloat("Bloom Intensity", &postSettings->BloomIntensity, 0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("Bloom Radius", &postSettings->BloomFilterRadius, 0.5f, 3.0f, "%.2f");
            }

            // Anti-Aliasing
            ImGui::Spacing();
            {
                const char* aaModeNames[] = {"None", "FXAA", "SMAA"};
                int aaIdx = static_cast<int>(postSettings->AntiAliasingMode);
                if (ImGui::Combo("Anti-Aliasing", &aaIdx, aaModeNames, 3))
                    postSettings->AntiAliasingMode = static_cast<Graphics::Passes::AAMode>(aaIdx);
            }
            if (postSettings->AntiAliasingMode == Graphics::Passes::AAMode::FXAA)
            {
                ImGui::SliderFloat("FXAA Contrast", &postSettings->FXAAContrastThreshold, 0.01f, 0.1f, "%.4f");
                ImGui::SliderFloat("FXAA Relative", &postSettings->FXAARelativeThreshold, 0.01f, 0.2f, "%.4f");
                ImGui::SliderFloat("FXAA Subpixel", &postSettings->FXAASubpixelBlending, 0.0f, 1.0f, "%.2f");
            }
            if (postSettings->AntiAliasingMode == Graphics::Passes::AAMode::SMAA)
            {
                ImGui::SliderFloat("SMAA Edge Threshold", &postSettings->SMAAEdgeThreshold, 0.01f, 0.5f, "%.3f");
                ImGui::SliderInt("SMAA Search Steps", &postSettings->SMAAMaxSearchSteps, 4, 32);
                ImGui::SliderInt("SMAA Diag Steps", &postSettings->SMAAMaxSearchStepsDiag, 0, 16);
            }

            // Luminance Histogram
            ImGui::Spacing();
            ImGui::Checkbox("Exposure Histogram", &postSettings->HistogramEnabled);
            if (postSettings->HistogramEnabled)
            {
                ImGui::SliderFloat("Min EV", &postSettings->HistogramMinEV, -20.0f, 0.0f, "%.1f");
                ImGui::SliderFloat("Max EV", &postSettings->HistogramMaxEV, 0.0f, 20.0f, "%.1f");

                const auto* histo = GetRenderOrchestrator().GetRenderSystem().GetHistogramReadback();
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
            if (postSettings->ColorGradingEnabled)
            {
                ImGui::SliderFloat("Saturation", &postSettings->Saturation, 0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Contrast##CG", &postSettings->Contrast, 0.5f, 2.0f, "%.2f");
                ImGui::SliderFloat("Temperature", &postSettings->ColorTempOffset, -1.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Tint", &postSettings->TintOffset, -1.0f, 1.0f, "%.2f");

                ImGui::Spacing();
                ImGui::TextDisabled("Lift (Shadows)");
                ImGui::SliderFloat3("Lift", &postSettings->Lift.x, -0.5f, 0.5f, "%.3f");

                ImGui::TextDisabled("Gamma (Midtones)");
                ImGui::SliderFloat3("Gamma", &postSettings->Gamma.x, 0.2f, 3.0f, "%.2f");

                ImGui::TextDisabled("Gain (Highlights)");
                ImGui::SliderFloat3("Gain", &postSettings->Gain.x, 0.0f, 3.0f, "%.2f");

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
        auto* outlineSettings = GetRenderOrchestrator().GetRenderSystem().GetSelectionOutlineSettings();
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

        ImGui::SliderFloat("Outline Width", &outlineSettings->OutlineWidth, 1.0f, 10.0f, "%.1f px");
        ImGui::SliderFloat("Selection Fill", &outlineSettings->SelectionFillAlpha, 0.0f, 0.5f, "%.2f");
        ImGui::SliderFloat("Hover Fill", &outlineSettings->HoverFillAlpha, 0.0f, 0.5f, "%.2f");

        if (outlineSettings->Mode == Graphics::Passes::OutlineMode::Pulse)
        {
            ImGui::SliderFloat("Pulse Speed", &outlineSettings->PulseSpeed, 0.5f, 10.0f, "%.1f");
            ImGui::SliderFloat("Pulse Min Alpha", &outlineSettings->PulseMin, 0.0f, 1.0f, "%.2f");
            ImGui::SliderFloat("Pulse Max Alpha", &outlineSettings->PulseMax, 0.0f, 1.0f, "%.2f");
        }
        else if (outlineSettings->Mode == Graphics::Passes::OutlineMode::Glow)
        {
            ImGui::SliderFloat("Glow Falloff", &outlineSettings->GlowFalloff, 0.5f, 8.0f, "%.1f");
        }

        if (ImGui::Button("Reset Outline Defaults"))
        {
            *outlineSettings = Graphics::Passes::SelectionOutlineSettings{};
        }

        // Spatial structure debug visualization (delegated to controller).
        m_SpatialDebug.DrawUI(*this);
    }

    // =========================================================================
    // Hierarchy panel
    // =========================================================================
    void DrawHierarchyPanel()
    {
        ImGui::Begin("Scene Hierarchy");

        if (ImGui::CollapsingHeader("Selection", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextUnformatted("Pick mouse button:");
            ImGui::SameLine();
            ImGui::RadioButton("LMB", &m_SelectMouseButton, 0);
            ImGui::SameLine();
            ImGui::RadioButton("RMB", &m_SelectMouseButton, 1);
            ImGui::SameLine();
            ImGui::RadioButton("MMB", &m_SelectMouseButton, 2);
        }

        const entt::entity selected = m_CachedSelectedEntity;

        GetSceneManager().GetScene().GetRegistry().view<entt::entity>().each([&](auto entityID)
        {
            if (GetSceneManager().GetScene().GetRegistry().all_of<Runtime::EditorUI::HiddenEditorEntityTag>(entityID))
                return;

            std::string name = "Entity";
            if (GetSceneManager().GetScene().GetRegistry().all_of<ECS::Components::NameTag::Component>(entityID))
            {
                name = GetSceneManager().GetScene().GetRegistry().get<ECS::Components::NameTag::Component>(entityID).Name;
            }

            ImGuiTreeNodeFlags flags = ((selected == entityID) ? ImGuiTreeNodeFlags_Selected : 0) |
                ImGuiTreeNodeFlags_OpenOnArrow;
            flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

            bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<entt::id_type>(entityID)), flags, "%s",
                                            name.c_str());

            if (ImGui::IsItemClicked())
            {
                GetSelection().SetSelectedEntity(GetSceneManager().GetScene(), entityID);
            }

            if (opened)
            {
                ImGui::TreePop();
            }
        });

        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        {
            GetSelection().ClearSelection(GetSceneManager().GetScene());
        }

        if (ImGui::BeginPopupContextWindow(nullptr, 1))
        {
            if (ImGui::MenuItem("Create Empty Entity"))
            {
                GetSceneManager().GetScene().CreateEntity("Empty Entity");
            }
            if (ImGui::MenuItem("Create Demo Point Cloud"))
            {
                SpawnDemoPointCloud();
            }
            if (ImGui::MenuItem("Remove Entity"))
            {
                const entt::entity cur = m_CachedSelectedEntity;
                if (cur != entt::null && GetSceneManager().GetScene().GetRegistry().valid(cur))
                {
                    GetSceneManager().GetScene().GetRegistry().destroy(cur);
                    GetSelection().ClearSelection(GetSceneManager().GetScene());
                }
            }
            ImGui::EndPopup();
        }

        ImGui::End();
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
            if (auto* orbit = GetSceneManager().GetScene().GetRegistry().try_get<Graphics::OrbitControlComponent>(m_CameraEntity))
            {
                const entt::entity selected = m_CachedSelectedEntity;
                if (selected != entt::null && GetSceneManager().GetScene().GetRegistry().valid(selected))
                {
                    glm::vec3 center{0.0f};
                    float radius = 0.0f;
                    bool found = false;

                    auto& reg = GetSceneManager().GetScene().GetRegistry();

                    // 1) Mesh entity: use world OBB.
                    if (auto* collider = reg.try_get<ECS::MeshCollider::Component>(selected))
                    {
                        if (collider->CollisionRef)
                        {
                            center = collider->WorldOBB.Center;
                            radius = glm::length(collider->WorldOBB.Extents);
                            found = true;
                        }
                    }

                    // 2) Point cloud entity: compute AABB from cloud positions.
                    if (!found)
                    {
                        if (auto* pcd = reg.try_get<ECS::PointCloud::Data>(selected))
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
                                if (auto* xf = reg.try_get<ECS::Components::Transform::Component>(selected))
                                {
                                    glm::mat4 world = GetMatrix(*xf);
                                    glm::vec3 wLo, wHi;
                                    Runtime::EditorUI::TransformAABB(aabb.Min, aabb.Max, world, wLo, wHi);
                                    center = (wLo + wHi) * 0.5f;
                                    radius = glm::length((wHi - wLo) * 0.5f);
                                }
                                else
                                {
                                    center = aabb.GetCenter();
                                    radius = glm::length(aabb.GetExtents());
                                }
                                found = true;
                            }
                        }
                    }

                    // 3) Graph entity: compute AABB from node positions.
                    if (!found)
                    {
                        if (auto* gd = reg.try_get<ECS::Graph::Data>(selected))
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
                                if (auto* xf = reg.try_get<ECS::Components::Transform::Component>(selected))
                                {
                                    glm::mat4 world = GetMatrix(*xf);
                                    glm::vec3 wLo, wHi;
                                    Runtime::EditorUI::TransformAABB(aabb.Min, aabb.Max, world, wLo, wHi);
                                    center = (wLo + wHi) * 0.5f;
                                    radius = glm::length((wHi - wLo) * 0.5f);
                                }
                                else
                                {
                                    center = aabb.GetCenter();
                                    radius = glm::length(aabb.GetExtents());
                                }
                                found = true;
                            }
                        }
                    }

                    if (found)
                    {
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
                }
            }
        }

        // Q Key: Reset camera to defaults
        if (!uiCapturesKeyboard && m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::Q))
        {
            if (auto* orbit = GetSceneManager().GetScene().GetRegistry().try_get<Graphics::OrbitControlComponent>(m_CameraEntity))
            {
                orbit->Target = glm::vec3(0.0f);
                orbit->Distance = 5.0f;
                cameraComponent->Position = glm::vec3(0.0f, 0.0f, 4.0f);
                cameraComponent->Orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }
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
    Runtime::EngineConfig config{};
    config.AppName = "Sandbox";
    config.Width = 1600;
    config.Height = 900;

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--benchmark" && i + 1 < argc)
        {
            config.BenchmarkMode = true;
            config.BenchmarkFrames = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--warmup" && i + 1 < argc)
        {
            config.BenchmarkWarmupFrames = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--out" && i + 1 < argc)
        {
            config.BenchmarkOutputPath = argv[++i];
        }
        else if (arg == "--fixed-hz" && i + 1 < argc)
        {
            config.FixedStepHz = std::stod(argv[++i]);
        }
        else if (arg == "--max-frame-dt" && i + 1 < argc)
        {
            config.MaxFrameDeltaSeconds = std::stod(argv[++i]);
        }
        else if (arg == "--max-substeps" && i + 1 < argc)
        {
            config.MaxSubstepsPerFrame = std::stoi(argv[++i]);
        }
    }

    SandboxApp app(config);
    app.Run();
    return 0;
}
