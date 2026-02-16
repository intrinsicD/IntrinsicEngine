#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>
#include <filesystem>
#include <imgui.h>
#include <entt/entity/registry.hpp>
#include <tiny_gltf.h>

import Runtime.Engine;
import Runtime.GraphicsBackend;
import Runtime.AssetPipeline;
import Runtime.RenderOrchestrator;
import Runtime.Selection;
import Core.Logging;
import Core.Filesystem;
import Core.Assets;
import Core.FrameGraph;
import Core.FeatureRegistry;
import Core.Hash;
import Graphics;
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
            auto result = Graphics::TextureLoader::LoadAsync(path, *GetDevice(),
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
                GetDevice(), gfx.GetTransferManager(), GetGeometryStorage(), path,
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

        Log::Info("FeatureRegistry: {} total features after client registration", features.Count());

        Interface::GUI::RegisterPanel("Hierarchy", [this]() { DrawHierarchyPanel(); });
        Interface::GUI::RegisterPanel("Inspector", [this]() { DrawInspectorPanel(); });
        Interface::GUI::RegisterPanel("Assets", [this]() { GetAssetManager().AssetsUiPanel(); });
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

            // NOTE: Actual DrawOctree() emission happens in OnUpdate() BEFORE renderSys.OnUpdate(),
            // because ImGui panels run AFTER the render graph has already executed.
            // The settings above will take effect on the next frame.

            // Show status feedback
            if (m_DrawSelectedColliderOctree || m_DrawSelectedColliderBounds)
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
        if (m_DrawSelectedColliderOctree || m_DrawSelectedColliderBounds)
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
