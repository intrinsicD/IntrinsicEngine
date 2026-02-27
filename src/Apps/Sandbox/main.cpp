#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <functional>
#include <memory>
#include <filesystem>
#include <cmath>
#include <imgui.h>
#include <entt/entity/registry.hpp>
#include <tiny_gltf.h>
#include <unordered_set>

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
import Graphics;
import Geometry;
import ECS;
import RHI;
import Interface;

using namespace Core;
using namespace Runtime;

// --- The Application Class ---
class SandboxApp : public Engine
{
public:
    SandboxApp() : Engine({"Sandbox", 1600, 900})
    {
    }

    // Resources
    Assets::AssetHandle m_DuckModel{};
    Assets::AssetHandle m_DuckTexture{};

    Assets::AssetHandle m_DuckMaterialHandle;

    // State to track if we have spawned the entity yet
    bool m_IsEntitySpawned = false;

    // Camera State
    entt::entity m_CameraEntity = entt::null;
    Graphics::CameraComponent m_Camera;

    // Editor / Selection Settings
    int m_SelectMouseButton = 1; // 0=LMB, 1=RMB, 2=MMB. Default: RMB to avoid conflict with LMB-drag orbit.

    // Octree Debug Visualization Settings (shared between UI and OnUpdate)
    Graphics::OctreeDebugDrawSettings m_OctreeDebugSettings{};
    bool m_DrawSelectedColliderOctree = false;

    Graphics::BoundingDebugDrawSettings m_BoundsDebugSettings{};
    bool m_DrawSelectedColliderBounds = false;

    Graphics::KDTreeDebugDrawSettings m_KDTreeDebugSettings{};
    bool m_DrawSelectedColliderKDTree = false;

    Graphics::BVHDebugDrawSettings m_BVHDebugSettings{};
    bool m_DrawSelectedColliderBVH = false;

    bool m_DrawSelectedColliderConvexHull = false;
    bool m_DrawSelectedColliderContacts = false;
    bool m_ContactDebugOverlay = true;
    float m_ContactNormalScale = 0.3f;
    float m_ContactPointRadius = 0.03f;

    Geometry::KDTree m_SelectedColliderKDTree{};
    entt::entity m_SelectedKDTreeEntity = entt::null;
    const Graphics::GeometryCollisionData* m_SelectedKDTreeSource = nullptr;

    Graphics::ConvexHullDebugDrawSettings m_ConvexHullDebugSettings{};
    Geometry::Halfedge::Mesh m_SelectedColliderHullMesh{};
    entt::entity m_SelectedHullEntity = entt::null;
    const Graphics::GeometryCollisionData* m_SelectedHullSource = nullptr;


    bool EnsureSelectedColliderKDTree(entt::entity selected,
                                     const Graphics::GeometryCollisionData& collision)
    {
        const bool cacheValid =
            (m_SelectedKDTreeEntity == selected) &&
            (m_SelectedKDTreeSource == &collision) &&
            !m_SelectedColliderKDTree.Nodes().empty();

        if (cacheValid)
            return true;

        m_SelectedColliderKDTree = Geometry::KDTree{};
        m_SelectedKDTreeEntity = entt::null;
        m_SelectedKDTreeSource = nullptr;

        if (collision.Positions.empty())
            return false;

        Geometry::KDTreeBuildParams params{};
        params.LeafSize = 24;
        params.MaxDepth = 24;

        auto build = m_SelectedColliderKDTree.BuildFromPoints(collision.Positions, params);
        if (!build)
            return false;

        m_SelectedKDTreeEntity = selected;
        m_SelectedKDTreeSource = &collision;
        return true;
    }

    bool EnsureSelectedColliderConvexHull(entt::entity selected,
                                         const Graphics::GeometryCollisionData& collision)
    {
        const bool cacheValid =
            (m_SelectedHullEntity == selected) &&
            (m_SelectedHullSource == &collision) &&
            !m_SelectedColliderHullMesh.IsEmpty();

        if (cacheValid)
            return true;

        m_SelectedColliderHullMesh = Geometry::Halfedge::Mesh{};
        m_SelectedHullEntity = entt::null;
        m_SelectedHullSource = nullptr;

        if (collision.Positions.size() < 4)
            return false;

        Geometry::ConvexHullBuilder::ConvexHullParams params{};
        params.BuildMesh = true;
        params.ComputePlanes = false;

        auto hull = Geometry::ConvexHullBuilder::Build(collision.Positions, params);
        if (!hull || hull->Mesh.IsEmpty())
            return false;

        m_SelectedColliderHullMesh = std::move(hull->Mesh);
        m_SelectedHullEntity = selected;
        m_SelectedHullSource = &collision;
        return true;
    }

    void OnStart() override
    {
        Log::Info("Sandbox Started!");

        auto& gfx = GetGraphicsBackend();


        m_CameraEntity = GetScene().CreateEntity("Main Camera");
        m_Camera = GetScene().GetRegistry().emplace<Graphics::CameraComponent>(m_CameraEntity);
        GetScene().GetRegistry().emplace<Graphics::OrbitControlComponent>(m_CameraEntity);

        auto textureLoader = [this, &gfx](const std::filesystem::path& path, Core::Assets::AssetHandle handle)
            -> std::shared_ptr<RHI::Texture>
        {
            auto result = Graphics::TextureLoader::LoadAsync(path, GetDevice(),
                gfx.GetTransferManager(), gfx.GetTextureSystem());

            if (result)
            {
                // When the transfer finishes, publish the real descriptor for this texture's bindless slot.
                RegisterAssetLoad(handle, result->Token, [this, texHandle = result->TextureHandle]()
                {
                    auto& g = GetGraphicsBackend();
                    if (const auto* data = g.GetTextureSystem().Get(texHandle))
                    {
                        // Flip bindless slot from default -> real view/sampler.
                        // This is the critical publish step in Phase 1.
                        // (No GPU waits; safe because token completion implies transfer queue copy is done.)
                        g.GetBindlessSystem().EnqueueUpdate(data->BindlessSlot, data->Image->GetView(), data->Sampler);
                    }
                });

                GetAssetManager().MoveToProcessing(handle);
                return std::move(result->Texture);
            }

            Log::Warn("Texture load failed: {} ({})", path.string(), Graphics::AssetErrorToString(result.error()));
            return {};
        };
        m_DuckTexture = GetAssetManager().Load<RHI::Texture>(Filesystem::GetAssetPath("textures/DuckCM.png"),
                                                              textureLoader);

        auto modelLoader = [&](const std::string& path, Assets::AssetHandle handle)
            -> std::unique_ptr<Graphics::Model>
        {
            auto result = Graphics::ModelLoader::LoadAsync(
                GetDeviceShared(), gfx.GetTransferManager(), GetGeometryStorage(), path,
                GetIORegistry(), GetIOBackend());

            if (result)
            {
                // 1. Notify Engine to track the GPU work
                RegisterAssetLoad(handle, result->Token);

                // 2. Notify AssetManager to wait
                GetAssetManager().MoveToProcessing(handle);

                // 3. Return the model (valid CPU pointers, GPU buffers are allocated but content is uploading)
                return std::move(result->ModelData);
            }

            Log::Warn("Model load failed: {} ({})", path, Graphics::AssetErrorToString(result.error()));
            return nullptr; // Failed
        };

        m_DuckModel = GetAssetManager().Load<Graphics::Model>(
            Filesystem::GetAssetPath("models/Duck.glb"),
            modelLoader
        );


        // 3. Setup Material (Assuming texture loads synchronously or is handled)
        Graphics::MaterialData matData;
        matData.AlbedoID = gfx.GetDefaultTextureIndex(); // Fallback until loaded
        matData.RoughnessFactor = 1.0f;
        matData.MetallicFactor = 0.0f;

        auto DuckMaterial = std::make_unique<Graphics::Material>(
            GetRenderOrchestrator().GetMaterialSystem(),
            matData
        );

        // Link the texture asset to the material (this registers the listener)
        DuckMaterial->SetAlbedoTexture(m_DuckTexture);

        // Track handle only; AssetManager owns the actual Material object.
        m_DuckMaterialHandle = GetAssetManager().Create("DuckMaterial", std::move(DuckMaterial));
        GetAssetPipeline().TrackMaterial(m_DuckMaterialHandle);

        Log::Info("Asset Load Requested. Waiting for background thread...");

        // --- Register client features in the central FeatureRegistry ---
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
            features.Register(std::move(info), []() -> void* { return nullptr; }, [](void*) {});
        }

        // UI panels
        auto registerPanelFeature = [&features](const std::string& name, const std::string& desc) {
            Core::FeatureInfo info{};
            info.Name = name;
            info.Id = Core::Hash::StringID(Core::Hash::HashString(info.Name));
            info.Category = Cat::Panel;
            info.Description = desc;
            info.Enabled = true;
            features.Register(std::move(info), []() -> void* { return nullptr; }, [](void*) {});
        };
        registerPanelFeature("Hierarchy", "Scene entity hierarchy browser");
        registerPanelFeature("Inspector", "Component property editor");
        registerPanelFeature("Assets", "Asset manager browser");
        registerPanelFeature("Stats", "Performance statistics and debug controls");
        registerPanelFeature("View Settings", "Selection outline and viewport display settings");
        registerPanelFeature("Render Target Viewer", "Render target debug visualization");
        registerPanelFeature("Geometry Processing", "Interactive Geometry Processing operators");
        registerPanelFeature("Status Bar", "Bottom-of-viewport frame summary (frame time, entity count, active renderer)");

        Log::Info("FeatureRegistry: {} total features after client registration", features.Count());

        Interface::GUI::RegisterPanel("Hierarchy", [this]() { DrawHierarchyPanel(); });
        Interface::GUI::RegisterPanel("Inspector", [this]() { DrawInspectorPanel(); });
        Interface::GUI::RegisterPanel("Assets", [this]() { GetAssetManager().AssetsUiPanel(); });
        Interface::GUI::RegisterPanel("Geometry Processing", [this]() { DrawGeometryProcessingPanel(); });

        // Register shared editor-facing panels (Feature browser, FrameGraph inspector, Selection config).
        Runtime::EditorUI::RegisterDefaultPanels(*this);

        Interface::GUI::RegisterPanel("Stats", [this]()
        {
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Entities: %d", (int)GetScene().Size());

            // --- Render Pipeline ---
            ImGui::SeparatorText("Render Pipeline");
            {
                if (ImGui::Button("Hot-swap: DefaultPipeline"))
                {
                    // Request swap; RenderSystem owns lifetime and applies at the start of the next frame.
                    auto pipeline = std::make_unique<Graphics::DefaultPipeline>();
                    pipeline->SetFeatureRegistry(&GetFeatureRegistry());
                    GetRenderOrchestrator().GetRenderSystem().RequestPipelineSwap(std::move(pipeline));
                }
            }

            // --- Selection Debug ---
            ImGui::Separator();
            ImGui::Text("Select Mouse Button: %d", m_SelectMouseButton);

            const entt::entity selected = GetSelection().GetSelectedEntity(GetScene());
            const bool selectedValid = (selected != entt::null) && GetScene().GetRegistry().valid(selected);

            ImGui::Text("Selected: %u (%s)",
                        static_cast<uint32_t>(static_cast<entt::id_type>(selected)),
                        selectedValid ? "valid" : "invalid");

            if (selectedValid)
            {
                const auto& reg = GetScene().GetRegistry();
                const bool hasSelectedTag = reg.all_of<ECS::Components::Selection::SelectedTag>(selected);
                const bool hasSelectableTag = reg.all_of<ECS::Components::Selection::SelectableTag>(selected);
                const bool hasMeshRenderer = reg.all_of<ECS::MeshRenderer::Component>(selected);
                const bool hasMeshCollider = reg.all_of<ECS::MeshCollider::Component>(selected);

                ImGui::Text("Tags: Selectable=%d Selected=%d", (int)hasSelectableTag, (int)hasSelectedTag);
                ImGui::Text("Components: MeshRenderer=%d MeshCollider=%d", (int)hasMeshRenderer, (int)hasMeshCollider);
            }
        });

        Interface::GUI::RegisterPanel("Status Bar", [this]()
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float statusBarHeight = ImGui::GetFrameHeight() + 10.0f;

            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - statusBarHeight));
            ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, statusBarHeight));

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                     ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_NoBringToFrontOnFocus |
                                     ImGuiWindowFlags_NoNavFocus;

            const float fps = ImGui::GetIO().Framerate;
            const float frameMs = fps > 0.0f ? (1000.0f / fps) : 0.0f;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
            // Draws a vertical separator line at the current cursor position (no SeparatorEx in this ImGui version).
            auto VerticalSeparator = []()
            {
                ImGui::SameLine();
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                const float h    = ImGui::GetFrameHeight();
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
                ImGui::Text("Entities: %d", static_cast<int>(GetScene().Size()));
                VerticalSeparator();
                ImGui::Text("Render Mode: DefaultPipeline");
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }, false, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings);

        // View Settings panel for configuring selection outline, etc.
        Interface::GUI::RegisterPanel("View Settings", [this]()
        {
            auto* outlineSettings = GetRenderOrchestrator().GetRenderSystem().GetSelectionOutlineSettings();
            if (!outlineSettings)
            {
                ImGui::TextDisabled("Selection outline settings not available.");
                return;
            }

            ImGui::SeparatorText("Selection Outline");

            // Selection color
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

            // Hover color
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

            // Outline width
            ImGui::SliderFloat("Outline Width", &outlineSettings->OutlineWidth, 1.0f, 10.0f, "%.1f px");

            // Reset button
            if (ImGui::Button("Reset to Defaults"))
            {
                outlineSettings->SelectionColor = glm::vec4(1.0f, 0.6f, 0.0f, 1.0f);
                outlineSettings->HoverColor = glm::vec4(0.3f, 0.7f, 1.0f, 0.8f);
                outlineSettings->OutlineWidth = 2.0f;
            }

            // ---------------------------------------------------------------------
            // Octree Visualization (MeshCollider local octree)
            // ---------------------------------------------------------------------
            ImGui::Spacing();
            ImGui::SeparatorText("Spatial Debug");

            ImGui::Checkbox("Draw Selected MeshCollider Octree", &m_DrawSelectedColliderOctree);
            ImGui::Checkbox("Draw Selected MeshCollider Bounds", &m_DrawSelectedColliderBounds);
            ImGui::Checkbox("Draw Selected MeshCollider KD-Tree", &m_DrawSelectedColliderKDTree);
            ImGui::Checkbox("Draw Selected MeshCollider BVH", &m_DrawSelectedColliderBVH);
            ImGui::Checkbox("Draw Selected MeshCollider Convex Hull", &m_DrawSelectedColliderConvexHull);
            ImGui::Checkbox("Draw Contact Manifolds", &m_DrawSelectedColliderContacts);
            ImGui::Checkbox("Bounds Overlay (no depth test)", &m_BoundsDebugSettings.Overlay);
            ImGui::Checkbox("Draw World AABB", &m_BoundsDebugSettings.DrawAABB);
            ImGui::Checkbox("Draw World OBB", &m_BoundsDebugSettings.DrawOBB);
            ImGui::Checkbox("Draw Bounding Sphere", &m_BoundsDebugSettings.DrawBoundingSphere);
            ImGui::SliderFloat("Bounds Alpha", &m_BoundsDebugSettings.Alpha, 0.05f, 1.0f, "%.2f");

            float aabbColor[3] = {m_BoundsDebugSettings.AABBColor.r, m_BoundsDebugSettings.AABBColor.g, m_BoundsDebugSettings.AABBColor.b};
            if (ImGui::ColorEdit3("AABB Color", aabbColor))
                m_BoundsDebugSettings.AABBColor = glm::vec3(aabbColor[0], aabbColor[1], aabbColor[2]);

            float obbColor[3] = {m_BoundsDebugSettings.OBBColor.r, m_BoundsDebugSettings.OBBColor.g, m_BoundsDebugSettings.OBBColor.b};
            if (ImGui::ColorEdit3("OBB Color", obbColor))
                m_BoundsDebugSettings.OBBColor = glm::vec3(obbColor[0], obbColor[1], obbColor[2]);

            float sphereColor[3] = {m_BoundsDebugSettings.SphereColor.r, m_BoundsDebugSettings.SphereColor.g, m_BoundsDebugSettings.SphereColor.b};
            if (ImGui::ColorEdit3("Sphere Color", sphereColor))
                m_BoundsDebugSettings.SphereColor = glm::vec3(sphereColor[0], sphereColor[1], sphereColor[2]);

            ImGui::SeparatorText("KD-Tree");
            ImGui::Checkbox("KD Overlay (no depth test)", &m_KDTreeDebugSettings.Overlay);
            ImGui::Checkbox("KD Leaf Only", &m_KDTreeDebugSettings.LeafOnly);
            ImGui::Checkbox("KD Draw Internal", &m_KDTreeDebugSettings.DrawInternal);
            ImGui::Checkbox("KD Occupied Only", &m_KDTreeDebugSettings.OccupiedOnly);
            ImGui::Checkbox("KD Draw Split Planes", &m_KDTreeDebugSettings.DrawSplitPlanes);
            ImGui::SliderInt("KD Max Depth", reinterpret_cast<int*>(&m_KDTreeDebugSettings.MaxDepth), 0, 32);
            ImGui::SliderFloat("KD Alpha", &m_KDTreeDebugSettings.Alpha, 0.05f, 1.0f, "%.2f");

            float kdLeafColor[3] = {m_KDTreeDebugSettings.LeafColor.r, m_KDTreeDebugSettings.LeafColor.g, m_KDTreeDebugSettings.LeafColor.b};
            if (ImGui::ColorEdit3("KD Leaf Color", kdLeafColor))
                m_KDTreeDebugSettings.LeafColor = glm::vec3(kdLeafColor[0], kdLeafColor[1], kdLeafColor[2]);

            float kdInternalColor[3] = {m_KDTreeDebugSettings.InternalColor.r, m_KDTreeDebugSettings.InternalColor.g, m_KDTreeDebugSettings.InternalColor.b};
            if (ImGui::ColorEdit3("KD Internal Color", kdInternalColor))
                m_KDTreeDebugSettings.InternalColor = glm::vec3(kdInternalColor[0], kdInternalColor[1], kdInternalColor[2]);

            float kdSplitColor[3] = {m_KDTreeDebugSettings.SplitPlaneColor.r, m_KDTreeDebugSettings.SplitPlaneColor.g, m_KDTreeDebugSettings.SplitPlaneColor.b};
            if (ImGui::ColorEdit3("KD Split Color", kdSplitColor))
                m_KDTreeDebugSettings.SplitPlaneColor = glm::vec3(kdSplitColor[0], kdSplitColor[1], kdSplitColor[2]);

            ImGui::SeparatorText("Convex Hull");
            ImGui::Checkbox("Hull Overlay (no depth test)", &m_ConvexHullDebugSettings.Overlay);
            ImGui::SliderFloat("Hull Alpha", &m_ConvexHullDebugSettings.Alpha, 0.05f, 1.0f, "%.2f");
            float hullColor[3] = {m_ConvexHullDebugSettings.Color.r, m_ConvexHullDebugSettings.Color.g, m_ConvexHullDebugSettings.Color.b};
            if (ImGui::ColorEdit3("Hull Color", hullColor))
                m_ConvexHullDebugSettings.Color = glm::vec3(hullColor[0], hullColor[1], hullColor[2]);


            ImGui::SeparatorText("BVH");
            ImGui::Checkbox("BVH Overlay (no depth test)", &m_BVHDebugSettings.Overlay);
            ImGui::Checkbox("BVH Leaf Only", &m_BVHDebugSettings.LeafOnly);
            ImGui::Checkbox("BVH Draw Internal", &m_BVHDebugSettings.DrawInternal);
            ImGui::SliderInt("BVH Max Depth", reinterpret_cast<int*>(&m_BVHDebugSettings.MaxDepth), 0, 32);
            ImGui::SliderInt("BVH Leaf Triangles", reinterpret_cast<int*>(&m_BVHDebugSettings.LeafTriangleCount), 1, 64);
            ImGui::SliderFloat("BVH Alpha", &m_BVHDebugSettings.Alpha, 0.05f, 1.0f, "%.2f");

            float bvhLeafColor[3] = {m_BVHDebugSettings.LeafColor.r, m_BVHDebugSettings.LeafColor.g, m_BVHDebugSettings.LeafColor.b};
            if (ImGui::ColorEdit3("BVH Leaf Color", bvhLeafColor))
                m_BVHDebugSettings.LeafColor = glm::vec3(bvhLeafColor[0], bvhLeafColor[1], bvhLeafColor[2]);

            float bvhInternalColor[3] = {m_BVHDebugSettings.InternalColor.r, m_BVHDebugSettings.InternalColor.g, m_BVHDebugSettings.InternalColor.b};
            if (ImGui::ColorEdit3("BVH Internal Color", bvhInternalColor))
                m_BVHDebugSettings.InternalColor = glm::vec3(bvhInternalColor[0], bvhInternalColor[1], bvhInternalColor[2]);

            ImGui::SeparatorText("Octree");
            ImGui::Checkbox("Overlay (no depth test)", &m_OctreeDebugSettings.Overlay);
            ImGui::Checkbox("Leaf Only", &m_OctreeDebugSettings.LeafOnly);
            ImGui::Checkbox("Occupied Only", &m_OctreeDebugSettings.OccupiedOnly);
            ImGui::Checkbox("Color By Depth", &m_OctreeDebugSettings.ColorByDepth);
            ImGui::SliderInt("Max Depth", reinterpret_cast<int*>(&m_OctreeDebugSettings.MaxDepth), 0, 16);
            ImGui::SliderFloat("Alpha", &m_OctreeDebugSettings.Alpha, 0.05f, 1.0f, "%.2f");

            if (!m_OctreeDebugSettings.ColorByDepth)
            {
                float base[3] = {m_OctreeDebugSettings.BaseColor.r, m_OctreeDebugSettings.BaseColor.g, m_OctreeDebugSettings.BaseColor.b};
                if (ImGui::ColorEdit3("Base Color", base))
                    m_OctreeDebugSettings.BaseColor = glm::vec3(base[0], base[1], base[2]);
            }

            ImGui::SeparatorText("Contact Manifold");
            ImGui::Checkbox("Contact Overlay (no depth test)", &m_ContactDebugOverlay);
            ImGui::SliderFloat("Normal Scale", &m_ContactNormalScale, 0.05f, 2.0f, "%.2f");
            ImGui::SliderFloat("Point Radius", &m_ContactPointRadius, 0.005f, 0.2f, "%.3f");

            // NOTE: Actual DrawOctree() emission happens in OnUpdate() BEFORE renderSys.OnUpdate(),
            // because ImGui panels run AFTER the render graph has already executed.
            // The settings above will take effect on the next frame.

            // Show status feedback
            if (m_DrawSelectedColliderOctree || m_DrawSelectedColliderBounds || m_DrawSelectedColliderKDTree || m_DrawSelectedColliderBVH || m_DrawSelectedColliderConvexHull)
            {
                const entt::entity selected = GetSelection().GetSelectedEntity(GetScene());
                if (selected == entt::null || !GetScene().GetRegistry().valid(selected))
                {
                    ImGui::TextDisabled("No valid selected entity.");
                }
                else
                {
                    auto* collider = GetScene().GetRegistry().try_get<ECS::MeshCollider::Component>(selected);
                    if (!collider || !collider->CollisionRef)
                    {
                        ImGui::TextDisabled("Selected entity has no MeshCollider.");
                    }
                }
            }
        });
    }

    void OnUpdate(float dt) override
    {
        GetAssetManager().Update();

        bool uiCapturesMouse = Interface::GUI::WantCaptureMouse();
        bool uiCapturesKeyboard = Interface::GUI::WantCaptureKeyboard();
        bool inputCaptured = uiCapturesMouse || uiCapturesKeyboard;

        float aspectRatio = 1.0f;
        if (m_Window->GetWindowHeight() > 0)
        {
            aspectRatio = (float)m_Window->GetWindowWidth() / (float)m_Window->GetWindowHeight();
        }

        Graphics::CameraComponent* cameraComponent = nullptr;
        if (GetScene().GetRegistry().valid(m_CameraEntity))
        {
            // Check if it has Orbit controls
            cameraComponent = GetScene().GetRegistry().try_get<Graphics::CameraComponent>(m_CameraEntity);
            if (auto* orbit = GetScene().GetRegistry().try_get<Graphics::OrbitControlComponent>(m_CameraEntity))
            {
                Graphics::OnUpdate(*cameraComponent, *orbit, m_Window->GetInput(), dt, inputCaptured);
            }
            // Check if it has Fly controls
            else if (auto* fly = GetScene().GetRegistry().try_get<Graphics::FlyControlComponent>(m_CameraEntity))
            {
                Graphics::OnUpdate(*cameraComponent, *fly, m_Window->GetInput(), dt, inputCaptured);
            }

            if (m_Window->GetWindowWidth() != 0 && m_Window->GetWindowHeight() != 0)
            {
                Graphics::OnResize(*cameraComponent, m_Window->GetWindowWidth(), m_Window->GetWindowHeight());
            }

            // --- F Key: Focus camera on selected model ---
            if (!uiCapturesKeyboard && m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::F))
            {
                if (auto* orbit = GetScene().GetRegistry().try_get<Graphics::OrbitControlComponent>(m_CameraEntity))
                {
                    const entt::entity selected = GetSelection().GetSelectedEntity(GetScene());
                    if (selected != entt::null && GetScene().GetRegistry().valid(selected))
                    {
                        auto* collider = GetScene().GetRegistry().try_get<ECS::MeshCollider::Component>(selected);
                        if (collider && collider->CollisionRef)
                        {
                            // Use the world-space OBB center as the new orbit target.
                            orbit->Target = collider->WorldOBB.Center;

                            // Compute an orbit distance that fits the object in view.
                            float radius = glm::length(collider->WorldOBB.Extents);
                            if (radius < 0.001f) radius = 1.0f;
                            float halfFov = glm::radians(cameraComponent->Fov) * 0.5f;
                            float fitDistance = radius / glm::tan(halfFov);
                            orbit->Distance = fitDistance * 1.5f; // Add margin.

                            // Reposition camera while preserving current viewing direction.
                            glm::vec3 viewDir = glm::normalize(cameraComponent->Position - orbit->Target);
                            cameraComponent->Position = orbit->Target + viewDir * orbit->Distance;
                        }
                    }
                }
            }

            // --- R Key: Reset camera to defaults ---
            if (!uiCapturesKeyboard && m_Window->GetInput().IsKeyJustPressed(Core::Input::Key::R))
            {
                if (auto* orbit = GetScene().GetRegistry().try_get<Graphics::OrbitControlComponent>(m_CameraEntity))
                {
                    // Restore orbit and camera to their struct-default values.
                    orbit->Target = glm::vec3(0.0f);
                    orbit->Distance = 5.0f;
                    cameraComponent->Position = glm::vec3(0.0f, 0.0f, 4.0f);
                    cameraComponent->Orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                }
            }
        }

        {
            auto view = GetScene().GetRegistry().view<Graphics::CameraComponent>();
            for (auto [entity, cam] : view.each())
            {
                Graphics::UpdateMatrices(cam, aspectRatio);
            }
        }

        if (!m_IsEntitySpawned)
        {
            if (GetAssetManager().GetState(m_DuckModel) == Assets::LoadState::Ready)
            {
                // One line to rule them all
                SpawnModel(m_DuckModel, m_DuckMaterialHandle, glm::vec3(0.0f), glm::vec3(0.01f));

                // (Optional) If you need to add specific behaviors like rotation:
                // entt::entity duck = SpawnModel(...);
                // GetScene().GetRegistry().emplace<ECS::Components::AxisRotator::Component>(duck, ...);

                m_IsEntitySpawned = true;
                Log::Info("Duck Entity Spawned.");
            }
        }

        {
            auto view = GetScene().GetRegistry().view<
                ECS::Components::Transform::Component, ECS::MeshCollider::Component>();
            for (auto [entity, transform, collider] : view.each())
            {
                // World center: transform the local center point
                // Explicitly cast vec4 result to vec3
                glm::vec3 localCenter = collider.CollisionRef->LocalAABB.GetCenter();
                collider.WorldOBB.Center = glm::vec3(GetMatrix(transform) * glm::vec4(localCenter, 1.0f));

                // Extents: scale component-wise by absolute scale (handles negative/non-uniform scale)
                glm::vec3 localExtents = collider.CollisionRef->LocalAABB.GetExtents();
                collider.WorldOBB.Extents = localExtents * glm::abs(transform.Scale);

                // Rotation: object rotation in world space.
                collider.WorldOBB.Rotation = transform.Rotation;
            }
        }

        // ---------------------------------------------------------------------
        // Debug Visualization: emit DebugDraw geometry BEFORE render system runs.
        // ImGui panels run AFTER render, so we emit here using settings from last frame.
        // ---------------------------------------------------------------------
        if (m_DrawSelectedColliderOctree || m_DrawSelectedColliderBounds || m_DrawSelectedColliderKDTree || m_DrawSelectedColliderBVH || m_DrawSelectedColliderConvexHull || m_DrawSelectedColliderContacts)
        {
            const entt::entity selected = GetSelection().GetSelectedEntity(GetScene());
            if (selected != entt::null && GetScene().GetRegistry().valid(selected))
            {
                auto& reg = GetScene().GetRegistry();
                auto* collider = reg.try_get<ECS::MeshCollider::Component>(selected);
                auto* xf = reg.try_get<ECS::Components::Transform::Component>(selected);

                if (collider && collider->CollisionRef && xf)
                {
                    if (m_DrawSelectedColliderOctree)
                    {
                        m_OctreeDebugSettings.Enabled = true;

                        // Compute world transform matrix from transform component
                        const glm::mat4 worldMatrix = GetMatrix(*xf);
                        DrawOctree(GetRenderOrchestrator().GetDebugDraw(), collider->CollisionRef->LocalOctree,
                                   m_OctreeDebugSettings, worldMatrix);
                    }

                    if (m_DrawSelectedColliderBounds)
                    {
                        m_BoundsDebugSettings.Enabled = true;
                        DrawBoundingVolumes(GetRenderOrchestrator().GetDebugDraw(),
                                            collider->CollisionRef->LocalAABB,
                                            collider->WorldOBB,
                                            m_BoundsDebugSettings);
                    }


                    if (m_DrawSelectedColliderKDTree)
                    {
                        m_KDTreeDebugSettings.Enabled = true;
                        if (EnsureSelectedColliderKDTree(selected, *collider->CollisionRef))
                        {
                            const glm::mat4 worldMatrix = GetMatrix(*xf);
                            DrawKDTree(GetRenderOrchestrator().GetDebugDraw(),
                                       m_SelectedColliderKDTree,
                                       m_KDTreeDebugSettings,
                                       worldMatrix);
                        }
                    }

                    if (m_DrawSelectedColliderBVH)
                    {
                        m_BVHDebugSettings.Enabled = true;
                        const glm::mat4 worldMatrix = GetMatrix(*xf);
                        DrawBVH(GetRenderOrchestrator().GetDebugDraw(),
                                collider->CollisionRef->Positions,
                                collider->CollisionRef->Indices,
                                m_BVHDebugSettings,
                                worldMatrix);
                    }

                    if (m_DrawSelectedColliderConvexHull)
                    {
                        m_ConvexHullDebugSettings.Enabled = true;
                        if (EnsureSelectedColliderConvexHull(selected, *collider->CollisionRef))
                        {
                            const glm::mat4 worldMatrix = GetMatrix(*xf);
                            DrawConvexHull(GetRenderOrchestrator().GetDebugDraw(),
                                           m_SelectedColliderHullMesh,
                                           m_ConvexHullDebugSettings,
                                           worldMatrix);
                        }
                    }

                    if (m_DrawSelectedColliderContacts)
                    {
                        auto& dd = GetRenderOrchestrator().GetDebugDraw();
                        auto colliders = reg.view<ECS::MeshCollider::Component>();

                        const uint32_t pointAColor = Graphics::DebugDraw::PackColorF(1.0f, 0.85f, 0.2f, 1.0f);
                        const uint32_t pointBColor = Graphics::DebugDraw::PackColorF(1.0f, 0.2f, 0.2f, 1.0f);
                        const uint32_t normalColor = Graphics::DebugDraw::PackColorF(0.2f, 0.85f, 1.0f, 1.0f);

                        for (auto [otherEntity, otherCollider] : colliders.each())
                        {
                            if (otherEntity == selected || !otherCollider.CollisionRef)
                                continue;

                            auto manifold = Geometry::ComputeContact(collider->WorldOBB, otherCollider.WorldOBB);
                            if (!manifold)
                                continue;

                            const glm::vec3 mid = (manifold->ContactPointA + manifold->ContactPointB) * 0.5f;
                            const glm::vec3 normalEnd = mid + manifold->Normal * (m_ContactNormalScale + manifold->PenetrationDepth);

                            if (m_ContactDebugOverlay)
                            {
                                dd.OverlaySphere(manifold->ContactPointA, m_ContactPointRadius, pointAColor, 12);
                                dd.OverlaySphere(manifold->ContactPointB, m_ContactPointRadius, pointBColor, 12);
                                dd.OverlayLine(manifold->ContactPointA, manifold->ContactPointB, pointAColor, pointBColor);
                                dd.OverlayLine(mid, normalEnd, normalColor);
                            }
                            else
                            {
                                dd.Sphere(manifold->ContactPointA, m_ContactPointRadius, pointAColor, 12);
                                dd.Sphere(manifold->ContactPointB, m_ContactPointRadius, pointBColor, 12);
                                dd.Line(manifold->ContactPointA, manifold->ContactPointB, pointAColor, pointBColor);
                                dd.Arrow(mid, normalEnd, glm::max(0.02f, m_ContactPointRadius), normalColor);
                            }
                        }
                    }
                }
            }
        }

        // ---------------------------------------------------------------------
        // Selection: delegate click-to-pick-to-registry-tags to the Engine module.
        // ---------------------------------------------------------------------
        if (cameraComponent != nullptr)
        {
            auto& renderSys = GetRenderOrchestrator().GetRenderSystem();

            // Keep module config in sync with the UI setting.
            GetSelection().GetConfig().MouseButton = m_SelectMouseButton;

            GetSelection().Update(
                GetScene(),
                renderSys,
                cameraComponent,
                *m_Window,
                uiCapturesMouse);

            // Draw
            renderSys.OnUpdate(GetScene(), *cameraComponent, GetAssetManager());
        }
    }

    void OnRender() override
    {
    }

    void OnRegisterSystems(Core::FrameGraph& graph, float deltaTime) override
    {
        using namespace Core::Hash;
        if (GetFeatureRegistry().IsEnabled("AxisRotator"_id))
            ECS::Systems::AxisRotator::RegisterSystem(graph, GetScene().GetRegistry(), deltaTime);
    }

    void DrawHierarchyPanel()
    {
        ImGui::Begin("Scene Hierarchy");

        // Editor settings
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

        const entt::entity selected = GetSelection().GetSelectedEntity(GetScene());

        GetScene().GetRegistry().view<entt::entity>().each([&](auto entityID)
        {
            // Try to get tag, default to "Entity"
            std::string name = "Entity";
            if (GetScene().GetRegistry().all_of<ECS::Components::NameTag::Component>(entityID))
            {
                name = GetScene().GetRegistry().get<ECS::Components::NameTag::Component>(entityID).Name;
            }

            // Selection flags
            ImGuiTreeNodeFlags flags = ((selected == entityID) ? ImGuiTreeNodeFlags_Selected : 0) |
                ImGuiTreeNodeFlags_OpenOnArrow;
            flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

            bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<entt::id_type>(entityID)), flags, "%s",
                                            name.c_str());

            if (ImGui::IsItemClicked())
            {
                GetSelection().SetSelectedEntity(GetScene(), entityID);
            }

            if (opened)
            {
                ImGui::TreePop();
            }
        });

        // Deselect only when clicking empty space in the hierarchy window.
        // (The previous IsMouseDown(...) version cleared selection even when clicking an item.)
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered())
        {
            GetSelection().ClearSelection(GetScene());
        }

        // Context Menu for creating new entities
        if (ImGui::BeginPopupContextWindow(nullptr, 1))
        {
            if (ImGui::MenuItem("Create Empty Entity"))
            {
                GetScene().CreateEntity("Empty Entity");
            }
            if (ImGui::MenuItem("Create Demo Point Cloud"))
            {
                SpawnDemoPointCloud();
            }
            if (ImGui::MenuItem("Remove Entity"))
            {
                const entt::entity cur = GetSelection().GetSelectedEntity(GetScene());
                if (cur != entt::null && GetScene().GetRegistry().valid(cur))
                {
                    GetScene().GetRegistry().destroy(cur);
                    GetSelection().ClearSelection(GetScene());
                }
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    void ApplyGeometryOperator(entt::entity entity, const std::function<void(Geometry::Halfedge::Mesh&)>& op)
    {
        auto& reg = GetScene().GetRegistry();
        auto* collider = reg.try_get<ECS::MeshCollider::Component>(entity);
        auto* mr = reg.try_get<ECS::MeshRenderer::Component>(entity);
        if (!collider || !collider->CollisionRef || !mr) return;

        // 1. Build Halfedge::Mesh from CPU data
        Geometry::Halfedge::Mesh mesh;
        std::vector<Geometry::VertexHandle> vhs(collider->CollisionRef->Positions.size());
        for (size_t i = 0; i < collider->CollisionRef->Positions.size(); ++i)
        {
            vhs[i] = mesh.AddVertex(collider->CollisionRef->Positions[i]);
        }
        for (size_t i = 0; i + 2 < collider->CollisionRef->Indices.size(); i += 3)
        {
            static_cast<void>(mesh.AddTriangle(vhs[collider->CollisionRef->Indices[i]],
                              vhs[collider->CollisionRef->Indices[i + 1]],
                              vhs[collider->CollisionRef->Indices[i + 2]]));
        }

        // 2. Apply operator
        op(mesh);
        mesh.GarbageCollection();

        // 3. Extract back
        std::vector<glm::vec3> newPos;
        std::vector<uint32_t> newIdx;

        newPos.reserve(mesh.VertexCount());
        std::vector<uint32_t> vMap(mesh.VerticesSize(), 0);
        uint32_t currentIdx = 0;
        for (size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            if (!mesh.IsDeleted(v))
            {
                vMap[i] = currentIdx++;
                newPos.push_back(mesh.Position(v));
            }
        }

        newIdx.reserve(mesh.FaceCount() * 3);
        for (size_t i = 0; i < mesh.FacesSize(); ++i)
        {
            Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(i)};
            if (!mesh.IsDeleted(f))
            {
                auto h0 = mesh.Halfedge(f);
                auto h1 = mesh.NextHalfedge(h0);
                auto h2 = mesh.NextHalfedge(h1);

                newIdx.push_back(vMap[mesh.ToVertex(h0).Index]);
                newIdx.push_back(vMap[mesh.ToVertex(h1).Index]);
                newIdx.push_back(vMap[mesh.ToVertex(h2).Index]);
            }
        }

        collider->CollisionRef->Positions = std::move(newPos);
        collider->CollisionRef->Indices = std::move(newIdx);

        std::vector<glm::vec3> newNormals(collider->CollisionRef->Positions.size(), glm::vec3(0, 1, 0));
        Geometry::MeshUtils::CalculateNormals(collider->CollisionRef->Positions, collider->CollisionRef->Indices, newNormals);

        std::vector<glm::vec4> newAux(collider->CollisionRef->Positions.size(), glm::vec4(0.0f));
        Geometry::MeshUtils::GenerateUVs(collider->CollisionRef->Positions, newAux);

        auto aabbs = Geometry::Convert(collider->CollisionRef->Positions);
        collider->CollisionRef->LocalAABB = Geometry::Union(aabbs);

        std::vector<Geometry::AABB> primitiveBounds;
        primitiveBounds.reserve(collider->CollisionRef->Indices.size() / 3);
        for (size_t i = 0; i + 2 < collider->CollisionRef->Indices.size(); i += 3)
        {
            const uint32_t i0 = collider->CollisionRef->Indices[i];
            const uint32_t i1 = collider->CollisionRef->Indices[i + 1];
            const uint32_t i2 = collider->CollisionRef->Indices[i + 2];
            auto aabb = Geometry::AABB{collider->CollisionRef->Positions[i0], collider->CollisionRef->Positions[i0]};
            aabb = Geometry::Union(aabb, collider->CollisionRef->Positions[i1]);
            aabb = Geometry::Union(aabb, collider->CollisionRef->Positions[i2]);
            primitiveBounds.push_back(aabb);
        }
        static_cast<void>(collider->CollisionRef->LocalOctree.Build(primitiveBounds, Geometry::Octree::SplitPolicy{}, 16, 8));

        Graphics::GeometryUploadRequest uploadReq;
        uploadReq.Positions = collider->CollisionRef->Positions;
        uploadReq.Indices = collider->CollisionRef->Indices;
        uploadReq.Normals = newNormals;
        uploadReq.Aux = newAux;
        uploadReq.Topology = Graphics::PrimitiveTopology::Triangles;
        uploadReq.UploadMode = Graphics::GeometryUploadMode::Staged;

        auto [gpuData, token] = Graphics::GeometryGpuData::CreateAsync(
            GetDeviceShared(), GetGraphicsBackend().GetTransferManager(), uploadReq, &GetGeometryStorage());

        auto oldHandle = mr->Geometry;
        mr->Geometry = GetGeometryStorage().Add(std::move(gpuData));

        if (oldHandle.IsValid())
        {
            GetGeometryStorage().Remove(oldHandle, GetDevice().GetGlobalFrameNumber());
        }

        mr->GpuSlot = ECS::MeshRenderer::Component::kInvalidSlot;
        reg.emplace_or_replace<ECS::Components::Transform::WorldUpdatedTag>(entity);

        auto* vis = reg.try_get<ECS::RenderVisualization::Component>(entity);
        if (vis)
        {
            vis->VertexViewDirty = true;
            vis->EdgeCacheDirty = true;
            vis->VertexNormalsDirty = true;
        }
    }

    // =========================================================================
    // SpawnDemoPointCloud — Create a hemisphere point cloud entity with normals,
    // colors, and estimated radii for testing point rendering.
    // =========================================================================
    void SpawnDemoPointCloud()
    {
        // Generate a uniform hemisphere point cloud (Fibonacci lattice on the
        // upper hemisphere for even angular spacing). N ≈ 500 points.
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
            // Uniform distribution on the sphere via Fibonacci lattice
            const float t = static_cast<float>(i) / static_cast<float>(N - 1);
            const float phi = std::acos(1.0f - 2.0f * t);   // polar angle [0, π]
            const float theta = goldenAngle * static_cast<float>(i);

            // Only keep upper hemisphere (y > 0)
            const float y = std::cos(phi);
            if (y < -0.05f) continue;

            const float sinPhi = std::sin(phi);
            const float x = sinPhi * std::cos(theta);
            const float z = sinPhi * std::sin(theta);

            const glm::vec3 pos    = glm::vec3(x, y, z) * radius;
            const glm::vec3 normal = glm::normalize(pos);

            // Color: height-based gradient (blue at base → orange at top)
            const float h = (y + 0.05f) / 1.05f; // normalize to [0,1]
            const glm::vec4 color = glm::mix(
                glm::vec4(0.2f, 0.4f, 0.9f, 1.0f),  // blue
                glm::vec4(1.0f, 0.6f, 0.1f, 1.0f),  // orange
                h);

            auto ph = cloud.AddPoint(pos);
            cloud.Normal(ph) = normal;
            cloud.Color(ph)  = color;
        }

        if (cloud.Empty())
        {
            Log::Warn("SpawnDemoPointCloud: no points generated.");
            return;
        }

        // Estimate per-point radii from local density (Octree kNN).
        Geometry::PointCloud::RadiusEstimationParams radiiParams;
        radiiParams.KNeighbors = 6;
        radiiParams.ScaleFactor = 1.2f; // Slight overlap for hole-free rendering
        auto radiiResult = Geometry::PointCloud::EstimateRadii(cloud, radiiParams);

        // Create the ECS entity
        auto& scene = GetScene();
        entt::entity entity = scene.CreateEntity("Demo Point Cloud");

        // PointCloudRenderer component
        auto positions = cloud.Positions();
        auto normals   = cloud.Normals();
        auto colors    = cloud.Colors();

        auto& pc = scene.GetRegistry().emplace<ECS::PointCloudRenderer::Component>(entity);
        pc.Positions = std::vector<glm::vec3>(positions.begin(), positions.end());
        pc.Normals   = std::vector<glm::vec3>(normals.begin(),   normals.end());
        pc.Colors    = std::vector<glm::vec4>(colors.begin(),    colors.end());
        if (radiiResult)
            pc.Radii = std::move(radiiResult->Radii);
        pc.RenderMode = Geometry::PointCloud::RenderMode::FlatDisc;
        pc.DefaultRadius = 0.02f;
        pc.SizeMultiplier = 1.0f;

        // Make it selectable
        scene.GetRegistry().emplace<ECS::Components::Selection::SelectableTag>(entity);
        static uint32_t s_PointCloudPickId = 10000u;
        scene.GetRegistry().emplace<ECS::Components::Selection::PickID>(entity, s_PointCloudPickId++);

        Log::Info("Spawned demo point cloud: {} points, normals={}, radii={}",
                  pc.PointCount(), pc.HasNormals() ? "yes" : "no", pc.HasRadii() ? "yes" : "no");
    }

    void DrawGeometryProcessingPanel()
    {
        ImGui::Begin("Geometry Processing");

        const entt::entity selected = GetSelection().GetSelectedEntity(GetScene());

        if (selected != entt::null && GetScene().GetRegistry().valid(selected))
        {
            auto& reg = GetScene().GetRegistry();

            if (reg.all_of<ECS::MeshRenderer::Component>(selected))
            {
                if (ImGui::CollapsingHeader("Remeshing", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    static float targetLength = 0.05f;
                    static int iterations = 5;
                    static bool preserveBoundary = true;
                    ImGui::DragFloat("Target Length", &targetLength, 0.01f, 0.001f, 10.0f);
                    ImGui::DragInt("Iterations##Remesh", &iterations, 1, 1, 20);
                    ImGui::Checkbox("Preserve Boundary##Remesh", &preserveBoundary);
                    if (ImGui::Button("Isotropic Remesh"))
                    {
                        ApplyGeometryOperator(selected, [](Geometry::Halfedge::Mesh& mesh) {
                            Geometry::Remeshing::RemeshingParams params;
                            params.TargetLength = targetLength;
                            params.Iterations = iterations;
                            params.PreserveBoundary = preserveBoundary;
                            static_cast<void>(Geometry::Remeshing::Remesh(mesh, params));
                        });
                    }
                    if (ImGui::Button("Adaptive Remesh"))
                    {
                        ApplyGeometryOperator(selected, [](Geometry::Halfedge::Mesh& mesh) {
                            Geometry::AdaptiveRemeshing::AdaptiveRemeshingParams params;
                            params.MinEdgeLength = targetLength * 0.5f;
                            params.MaxEdgeLength = targetLength * 2.0f;
                            params.Iterations = iterations;
                            params.PreserveBoundary = preserveBoundary;
                            static_cast<void>(Geometry::AdaptiveRemeshing::AdaptiveRemesh(mesh, params));
                        });
                    }
                }

                if (ImGui::CollapsingHeader("Simplification", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    static int targetFaces = 1000;
                    static bool preserveBoundarySimp = true;
                    ImGui::DragInt("Target Faces", &targetFaces, 10, 10, 1000000);
                    ImGui::Checkbox("Preserve Boundary##Simp", &preserveBoundarySimp);
                    if (ImGui::Button("Simplify (QEM)"))
                    {
                        ApplyGeometryOperator(selected, [](Geometry::Halfedge::Mesh& mesh) {
                            Geometry::Simplification::SimplificationParams params;
                            params.TargetFaces = targetFaces;
                            params.PreserveBoundary = preserveBoundarySimp;
                            static_cast<void>(Geometry::Simplification::Simplify(mesh, params));
                        });
                    }
                }

                if (ImGui::CollapsingHeader("Smoothing", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    static int smoothIterations = 10;
                    static float lambda = 0.5f;
                    static bool preserveBoundarySmooth = true;
                    ImGui::DragInt("Iterations##Smooth", &smoothIterations, 1, 1, 100);
                    ImGui::DragFloat("Lambda", &lambda, 0.01f, 0.0f, 1.0f);
                    ImGui::Checkbox("Preserve Boundary##Smooth", &preserveBoundarySmooth);

                    if (ImGui::Button("Uniform Laplacian"))
                    {
                        ApplyGeometryOperator(selected, [](Geometry::Halfedge::Mesh& mesh) {
                            Geometry::Smoothing::SmoothingParams params;
                            params.Iterations = smoothIterations;
                            params.Lambda = lambda;
                            params.PreserveBoundary = preserveBoundarySmooth;
                            static_cast<void>(Geometry::Smoothing::UniformLaplacian(mesh, params));
                        });
                    }
                    if (ImGui::Button("Cotan Laplacian"))
                    {
                        ApplyGeometryOperator(selected, [](Geometry::Halfedge::Mesh& mesh) {
                            Geometry::Smoothing::SmoothingParams params;
                            params.Iterations = smoothIterations;
                            params.Lambda = lambda;
                            params.PreserveBoundary = preserveBoundarySmooth;
                            static_cast<void>(Geometry::Smoothing::CotanLaplacian(mesh, params));
                        });
                    }
                    if (ImGui::Button("Taubin Smoothing"))
                    {
                        ApplyGeometryOperator(selected, [](Geometry::Halfedge::Mesh& mesh) {
                            Geometry::Smoothing::TaubinParams params;
                            params.Iterations = smoothIterations;
                            params.Lambda = lambda;
                            params.PreserveBoundary = preserveBoundarySmooth;
                            static_cast<void>(Geometry::Smoothing::Taubin(mesh, params));
                        });
                    }
                    if (ImGui::Button("Implicit Smoothing"))
                    {
                        ApplyGeometryOperator(selected, [](Geometry::Halfedge::Mesh& mesh) {
                            Geometry::Smoothing::ImplicitSmoothingParams params;
                            params.Iterations = smoothIterations;
                            params.Lambda = lambda;
                            params.PreserveBoundary = preserveBoundarySmooth;
                            static_cast<void>(Geometry::Smoothing::ImplicitLaplacian(mesh, params));
                        });
                    }
                }

                if (ImGui::CollapsingHeader("Subdivision", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    static int subdivIterations = 1;
                    ImGui::DragInt("Iterations##Subdiv", &subdivIterations, 1, 1, 5);
                    if (ImGui::Button("Loop Subdivision"))
                    {
                        ApplyGeometryOperator(selected, [](Geometry::Halfedge::Mesh& mesh) {
                            Geometry::Halfedge::Mesh out;
                            Geometry::Subdivision::SubdivisionParams params;
                            params.Iterations = subdivIterations;
                            if (Geometry::Subdivision::Subdivide(mesh, out, params)) {
                                mesh = std::move(out);
                            }
                        });
                    }
                    if (ImGui::Button("Catmull-Clark Subdivision"))
                    {
                        ApplyGeometryOperator(selected, [](Geometry::Halfedge::Mesh& mesh) {
                            Geometry::Halfedge::Mesh out;
                            Geometry::CatmullClark::SubdivisionParams params;
                            params.Iterations = subdivIterations;
                            if (Geometry::CatmullClark::Subdivide(mesh, out, params)) {
                                mesh = std::move(out);
                            }
                        });
                    }
                }

                if (ImGui::CollapsingHeader("Mesh Repair", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    if (ImGui::Button("Repair Mesh"))
                    {
                        ApplyGeometryOperator(selected, [](Geometry::Halfedge::Mesh& mesh) {
                            static_cast<void>(Geometry::MeshRepair::Repair(mesh));
                        });
                    }
                }
            }
            else
            {
                ImGui::Text("Selected entity does not have a MeshRenderer.");
            }
        }
        else
        {
            ImGui::TextDisabled("Select an entity to process geometry.");
        }

        ImGui::End();
    }

    void DrawInspectorPanel()
    {
        ImGui::Begin("Inspector");

        const entt::entity selected = GetSelection().GetSelectedEntity(GetScene());

        if (selected != entt::null && GetScene().GetRegistry().valid(selected))
        {
            auto& reg = GetScene().GetRegistry();

            // 1. Tag Component
            if (reg.all_of<ECS::Components::NameTag::Component>(selected))
            {
                auto& tag = reg.get<ECS::Components::NameTag::Component>(selected);
                char buffer[256];
                memset(buffer, 0, sizeof(buffer));
                strncpy(buffer, tag.Name.c_str(), sizeof(buffer) - 1);
                if (ImGui::InputText("Name", buffer, sizeof(buffer)))
                {
                    tag.Name = std::string(buffer);
                }
            }

            ImGui::Separator();

            // 2. Transform Component
            if (reg.all_of<ECS::Components::Transform::Component>(selected))
            {
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    auto& transform = reg.get<ECS::Components::Transform::Component>(selected);

                    const bool posChanged = Interface::GUI::DrawVec3Control("Position", transform.Position);

                    glm::vec3 rotationDegrees = glm::degrees(glm::eulerAngles(transform.Rotation));
                    const bool rotChanged = Interface::GUI::DrawVec3Control("Rotation", rotationDegrees);
                    if (rotChanged)
                    {
                        transform.Rotation = glm::quat(glm::radians(rotationDegrees));
                    }

                    const bool scaleChanged = Interface::GUI::DrawVec3Control("Scale", transform.Scale, 1.0f);

                    if (posChanged || rotChanged || scaleChanged)
                    {
                        reg.emplace_or_replace<ECS::Components::Transform::IsDirtyTag>(selected);
                    }
                }
            }

            // 3. Mesh Info
            if (reg.all_of<ECS::MeshRenderer::Component>(selected))
            {
                if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    auto& mr = reg.get<ECS::MeshRenderer::Component>(selected);
                    Graphics::GeometryGpuData* geo = GetGeometryStorage().GetUnchecked(mr.Geometry);

                    if (geo)
                    {
                        ImGui::Text("Vertices: %lu", geo->GetLayout().PositionsSize / sizeof(glm::vec3));
                        ImGui::Text("Indices: %u", geo->GetIndexCount());

                        std::string topoName = "Unknown";
                        switch (geo->GetTopology())
                        {
                        case Graphics::PrimitiveTopology::Triangles: topoName = "Triangles";
                            break;
                        case Graphics::PrimitiveTopology::Lines: topoName = "Lines";
                            break;
                        case Graphics::PrimitiveTopology::Points: topoName = "Points";
                            break;
                        }
                        ImGui::Text("Topology: %s", topoName.c_str());
                    }
                    else
                    {
                        ImGui::TextColored({0.8f, 0.2f, 0.2f, 1.0f}, "Invalid or unloaded Geometry Handle");
                    }
                }
            }

            // 4. Visualization — per-entity rendering mode toggles.
            // Shown for any entity with renderable data (mesh, point cloud, graph).
            {
                const bool hasMesh = reg.all_of<ECS::MeshRenderer::Component>(selected);
                const bool hasPointCloud = reg.all_of<ECS::PointCloudRenderer::Component>(selected);

                if (hasMesh || hasPointCloud)
                {
                    // Determine topology for context-appropriate labels.
                    Graphics::PrimitiveTopology topology = Graphics::PrimitiveTopology::Triangles;
                    if (hasMesh)
                    {
                        auto& mr = reg.get<ECS::MeshRenderer::Component>(selected);
                        if (auto* geo = GetGeometryStorage().GetUnchecked(mr.Geometry))
                            topology = geo->GetTopology();
                    }

                    const bool isTriangleMesh = hasMesh && (topology == Graphics::PrimitiveTopology::Triangles);
                    const bool isLineMesh = hasMesh && (topology == Graphics::PrimitiveTopology::Lines);

                    if (ImGui::CollapsingHeader("Visualization", ImGuiTreeNodeFlags_DefaultOpen))
                    {
                        // Lazily attach RenderVisualization component on first interaction.
                        auto* vis = reg.try_get<ECS::RenderVisualization::Component>(selected);
                        bool justCreated = false;
                        if (!vis)
                        {
                            // Show defaults until user interacts.
                            // Create with appropriate defaults for the entity type.
                            bool defaultShowSurface = hasMesh;
                            bool defaultShowVertices = hasPointCloud && !hasMesh;

                            // For lazy display without creating component yet, use temp values.
                            bool showSurface = defaultShowSurface;
                            bool showWireframe = false;
                            bool showVertices = defaultShowVertices;

                            bool changed = false;

                            if (hasMesh)
                            {
                                if (isTriangleMesh)
                                {
                                    changed |= ImGui::Checkbox("Show Surface", &showSurface);
                                    changed |= ImGui::Checkbox("Show Wireframe", &showWireframe);
                                }
                                else if (isLineMesh)
                                {
                                    changed |= ImGui::Checkbox("Show Edges", &showSurface);
                                }
                            }

                            changed |= ImGui::Checkbox("Show Vertices", &showVertices);

                            if (changed)
                            {
                                auto& newVis = reg.emplace<ECS::RenderVisualization::Component>(selected);
                                newVis.ShowSurface = showSurface;
                                newVis.ShowWireframe = showWireframe;
                                newVis.ShowVertices = showVertices;
                                // For point cloud entities, default ShowSurface is irrelevant.
                                if (hasPointCloud && !hasMesh)
                                    newVis.ShowSurface = true;
                                vis = &newVis;
                                justCreated = true;
                            }
                        }

                        if (vis && !justCreated)
                        {
                            // Ensure the entity has a GeometryViewRenderer when it has a MeshRenderer.
                            ECS::GeometryViewRenderer::Component* viewR = nullptr;
                            ECS::MeshRenderer::Component* mrPtr = nullptr;
                            Graphics::GeometryGpuData* srcGeo = nullptr;

                            if (hasMesh)
                            {
                                mrPtr = &reg.get<ECS::MeshRenderer::Component>(selected);
                                srcGeo = GetGeometryStorage().GetUnchecked(mrPtr->Geometry);

                                viewR = reg.try_get<ECS::GeometryViewRenderer::Component>(selected);
                                if (!viewR)
                                {
                                    auto& vr = reg.emplace<ECS::GeometryViewRenderer::Component>(selected);
                                    vr.Surface = mrPtr->Geometry;
                                    viewR = &vr;
                                }
                                else
                                {
                                    viewR->Surface = mrPtr->Geometry;
                                }
                            }

                            // --- Mode Toggles ---
                            if (hasMesh)
                            {
                                if (isTriangleMesh)
                                {
                                    ImGui::Checkbox("Show Surface", &vis->ShowSurface);
                                    ImGui::Checkbox("Show Wireframe", &vis->ShowWireframe);
                                }
                                else if (isLineMesh)
                                {
                                    ImGui::Checkbox("Show Edges", &vis->ShowSurface);
                                }
                            }

                            ImGui::Checkbox("Show Vertices", &vis->ShowVertices);

                            if (vis->ShowVertices)
                            {
                                ImGui::SeparatorText("Vertex Settings");
                                vis->VertexRenderMode = Geometry::PointCloud::RenderMode::FlatDisc;

                                ImGui::SliderFloat("Vertex Size", &vis->VertexSize, 0.0005f, 0.05f, "%.5f", ImGuiSliderFlags_Logarithmic);
                                float vc[4] = {vis->VertexColor.r, vis->VertexColor.g, vis->VertexColor.b, vis->VertexColor.a};
                                if (ImGui::ColorEdit4("Vertex Color", vc))
                                    vis->VertexColor = glm::vec4(vc[0], vc[1], vc[2], vc[3]);
                            }

                            // Mirror visibility into the GPU view renderer (if present).
                            // ShowWireframe is NOT mirrored — wireframe is CPU-driven via
                            // DebugDraw and has no corresponding GPU scene instance.
                            if (viewR)
                            {
                                viewR->ShowSurface = vis->ShowSurface;
                                viewR->ShowVertices = vis->ShowVertices;
                            }

                            // -----------------------------------------------------------------
                            // Lazily create GPU views (one-time allocations/uploads)
                            // -----------------------------------------------------------------
                            // NOTE: Wireframe has NO GPU view. It is rendered entirely by the
                            // MeshRenderPass CPU path → DebugDraw accumulator → LineRenderPass,
                            // which correctly applies WireframeColor and WireframeOverlay.
                            // The previous GPU view approach used triangle.frag which ignores
                            // WireframeColor and produced invisible/incorrect output.

                            // Vertex view (Points): create the GPU view for FlatDisc points.
                            const bool wantGpuVertexView =
                                vis->ShowVertices &&
                                (vis->VertexRenderMode == Geometry::PointCloud::RenderMode::FlatDisc);

                            if (wantGpuVertexView
                                && (vis->VertexViewDirty || !vis->VertexView.IsValid()))
                            {
                                if (mrPtr && srcGeo && mrPtr->Geometry.IsValid())
                                {
                                    const uint32_t vertexCount = static_cast<uint32_t>(srcGeo->GetLayout().PositionsSize / sizeof(glm::vec3));
                                    if (vertexCount > 0)
                                    {
                                        std::vector<uint32_t> pointIndices(vertexCount);
                                        for (uint32_t i = 0; i < vertexCount; ++i)
                                            pointIndices[i] = i;

                                        auto [h, token] = GetRenderOrchestrator().CreateGeometryView(
                                            GetGraphicsBackend().GetTransferManager(),
                                            mrPtr->Geometry,
                                            pointIndices,
                                            Graphics::PrimitiveTopology::Points,
                                            Graphics::GeometryUploadMode::Staged);

                                        if (h.IsValid())
                                        {
                                            vis->VertexView = h;
                                            vis->VertexViewDirty = false;
                                            if (viewR) viewR->Vertices = h;

                                            // Force a GPUScene refresh for this entity so the new view becomes visible immediately.
                                            reg.emplace_or_replace<ECS::Components::Transform::WorldUpdatedTag>(selected);
                                        }
                                    }
                                }
                            }

                            // Publish handles (keep in sync even if created elsewhere).
                            // Wireframe handle is intentionally left empty — wireframe is CPU-driven.
                            if (viewR)
                            {
                                // Only publish the vertex view for FlatDisc.
                                viewR->Vertices = wantGpuVertexView ? vis->VertexView : Geometry::GeometryHandle{};
                            }
                        }
                    }
                }
            }

            // 5. Point Cloud Info
            if (reg.all_of<ECS::PointCloudRenderer::Component>(selected))
            {
                if (ImGui::CollapsingHeader("Point Cloud", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    auto& pc = reg.get<ECS::PointCloudRenderer::Component>(selected);
                    ImGui::Text("Points: %zu", pc.PointCount());
                    ImGui::Text("Has Normals: %s", pc.HasNormals() ? "Yes" : "No");
                    ImGui::Text("Has Colors: %s", pc.HasColors() ? "Yes" : "No");
                    ImGui::Text("Has Radii: %s", pc.HasRadii() ? "Yes" : "No");

                    ImGui::SeparatorText("Rendering");
                    ImGui::Checkbox("Visible", &pc.Visible);

                    pc.RenderMode = Geometry::PointCloud::RenderMode::FlatDisc;

                    ImGui::SliderFloat("Default Radius", &pc.DefaultRadius, 0.0005f, 0.1f, "%.5f", ImGuiSliderFlags_Logarithmic);
                    ImGui::SliderFloat("Size Multiplier", &pc.SizeMultiplier, 0.1f, 10.0f, "%.2f", ImGuiSliderFlags_Logarithmic);

                    float dc[4] = {pc.DefaultColor.r, pc.DefaultColor.g, pc.DefaultColor.b, pc.DefaultColor.a};
                    if (ImGui::ColorEdit4("Default Color", dc))
                        pc.DefaultColor = glm::vec4(dc[0], dc[1], dc[2], dc[3]);
                }
            }
        }
        else
        {
            ImGui::TextDisabled("Select an entity to view details.");
        }

        ImGui::End();
    }
};

int main()
{
    SandboxApp app;
    app.Run();
    return 0;
}
