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
import Core.Logging;
import Core.Input;
import Core.Assets;
import Core.Filesystem;
import Runtime.Graphics.Geometry;
import Runtime.Graphics.Model;
import Runtime.Graphics.Material;
import Runtime.Graphics.RenderSystem;
import Runtime.Graphics.ModelLoader;
import Runtime.Graphics.TextureLoader;
import Runtime.Graphics.Camera;
import Runtime.ECS.Components;
import Runtime.RHI.Types;
import Runtime.RHI.Texture;
import Runtime.Interface.GUI;

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

    std::shared_ptr<Graphics::Material> m_DuckMaterial;

    // State to track if we have spawned the entity yet
    bool m_IsEntitySpawned = false;
    entt::entity m_SelectedEntity = entt::null;

    // Camera State
    entt::entity m_CameraEntity = entt::null;
    Graphics::CameraComponent m_Camera;

    void OnStart() override
    {
        Log::Info("Sandbox Started!");

        m_CameraEntity = m_Scene.CreateEntity("Main Camera");
        m_Camera = m_Scene.GetRegistry().emplace<Graphics::CameraComponent>(m_CameraEntity);
        m_Scene.GetRegistry().emplace<Graphics::OrbitControlComponent>(m_CameraEntity);

        auto textureLoader = [this](const std::filesystem::path& path, Core::Assets::AssetHandle handle)
            -> std::shared_ptr<RHI::Texture>
        {
            // Delegate complex logic to the subsystem
            auto result = Graphics::TextureLoader::LoadAsync(path, GetDevice(), *m_TransferManager);

            if (result)
            {
                // 1. Notify Engine to track the GPU work
                RegisterAssetLoad(handle, result->Token);

                // 2. Notify AssetManager to wait
                m_AssetManager.MoveToProcessing(handle);

                // 3. Return the resource (even though it's not ready yet, it's valid memory)
                return result->Resource;
            }

            return nullptr; // Load failed
        };
        m_DuckTexture = m_AssetManager.Load<RHI::Texture>(Filesystem::GetAssetPath("textures/DuckCM.png"),
                                                          textureLoader);

        auto modelLoader = [&](const std::string& path, Assets::AssetHandle /*handle*/)
        {
            // Handle is unused for models (cpu-only load or blocking load for now), so we ignore it
            return Graphics::ModelLoader::Load(GetDevice(), path);
        };

        m_DuckModel = m_AssetManager.Load<Graphics::Model>(
            Filesystem::GetAssetPath("models/Duck.glb"),
            modelLoader
        );

        // 3. Setup Material (Assuming texture loads synchronously or is handled)
        m_DuckMaterial = std::make_shared<Graphics::Material>(
            GetDevice(),
            *m_BindlessSystem,
            m_DuckTexture,
            m_DefaultTexture,
            m_AssetManager
        );

        Log::Info("Asset Load Requested. Waiting for background thread...");

        Interface::GUI::RegisterPanel("Hierarchy", [this]() { DrawHierarchyPanel(); });
        Interface::GUI::RegisterPanel("Inspector", [this]() { DrawInspectorPanel(); });
        Interface::GUI::RegisterPanel("Assets", [this]() { m_AssetManager.AssetsUiPanel(); });
        Interface::GUI::RegisterPanel("Stats", [this]()
        {
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Entities: %d", (int)m_Scene.Size());
        });
    }

    void OnUpdate(float dt) override
    {
        m_AssetManager.Update();

        bool uiCapturesMouse = Interface::GUI::WantCaptureMouse();
        bool uiCapturesKeyboard = Interface::GUI::WantCaptureKeyboard();
        bool inputCaptured = uiCapturesMouse || uiCapturesKeyboard;

        float aspectRatio = 1.0f;
        if (m_Window->GetWindowHeight() > 0)
        {
            aspectRatio = (float)m_Window->GetWindowWidth() / (float)m_Window->GetWindowHeight();
        }

        Graphics::CameraComponent* cameraComponent = nullptr;
        if (m_Scene.GetRegistry().valid(m_CameraEntity))
        {
            // Check if it has Orbit controls
            cameraComponent = m_Scene.GetRegistry().try_get<Graphics::CameraComponent>(m_CameraEntity);
            if (auto* orbit = m_Scene.GetRegistry().try_get<Graphics::OrbitControlComponent>(m_CameraEntity))
            {
                Graphics::OnUpdate(*cameraComponent, *orbit, m_Window->GetInput(), dt, inputCaptured);
            }
            // Check if it has Fly controls
            else if (auto* fly = m_Scene.GetRegistry().try_get<Graphics::FlyControlComponent>(m_CameraEntity))
            {
                Graphics::OnUpdate(*cameraComponent, *fly, m_Window->GetInput(), dt, inputCaptured);
            }

            if (m_Window->GetWindowWidth() != 0 && m_Window->GetWindowHeight() != 0)
            {
                Graphics::OnResize(*cameraComponent, m_Window->GetWindowWidth(), m_Window->GetWindowHeight());
            }
        }

        {
            auto view = m_Scene.GetRegistry().view<Graphics::CameraComponent>();
            for (auto [entity, cam] : view.each())
            {
                Graphics::UpdateMatrices(cam, aspectRatio);
            }
        }

        if (!m_IsEntitySpawned)
        {
            if (m_AssetManager.GetState(m_DuckModel) == Assets::LoadState::Ready)
            {
                auto model = m_AssetManager.Get<Graphics::Model>(m_DuckModel);

                for (const auto& meshSegment : model->Meshes)
                {
                    auto entity = m_Scene.CreateEntity(meshSegment->Name);

                    m_SelectedEntity = entity;
                    auto& t = m_Scene.GetRegistry().get<ECS::Transform::Component>(entity);
                    t.Scale = glm::vec3(0.01f);
                    m_Scene.GetRegistry().emplace<ECS::Transform::Rotator>(entity, ECS::Transform::Rotator::Y());

                    auto& mr = m_Scene.GetRegistry().emplace<ECS::MeshRenderer::Component>(entity);
                    mr.GeometryRef = meshSegment->GpuGeometry;
                    mr.MaterialRef = m_DuckMaterial;

                    auto& collider = m_Scene.GetRegistry().emplace<ECS::MeshCollider::Component>(entity);
                    collider.CollisionRef = meshSegment->CollisionGeometry; // Shared Ptr
                    collider.WorldOBB.Center = meshSegment->CollisionGeometry->LocalAABB.GetCenter();

                    m_IsEntitySpawned = true;
                    Log::Info("Duck Entity Spawned.");
                }
            }
        }

        {
            // --- Rotate Entities ---
            auto view = m_Scene.GetRegistry().view<ECS::Transform::Component, ECS::Transform::Rotator>();
            for (auto [entity, transform, rotator] : view.each())
            {
                ECS::Transform::OnUpdate(transform, rotator, dt);
            }
        }

        {
            auto view = m_Scene.GetRegistry().view<ECS::Transform::Component, ECS::MeshCollider::Component>();
            for (auto [entity, transform, collider] : view.each())
            {
                // World center: transform the local center point
                // Explicitly cast vec4 result to vec3
                glm::vec3 localCenter = collider.CollisionRef->LocalAABB.GetCenter();
                collider.WorldOBB.Center = glm::vec3(transform.GetTransform() * glm::vec4(localCenter, 1.0f));

                // Extents: scale component-wise by absolute scale (handles negative/non-uniform scale)
                glm::vec3 localExtents = collider.CollisionRef->LocalAABB.GetExtents();
                collider.WorldOBB.Extents = localExtents * glm::abs(transform.Scale);

                // Rotation: object rotation in world space.
                collider.WorldOBB.Rotation = transform.Rotation;
            }
        }

        // Draw
        if (cameraComponent != nullptr)
        {
            m_RenderSystem->OnUpdate(m_Scene, *cameraComponent);
        }
    }

    void OnRender() override
    {
    }

    void DrawHierarchyPanel()
    {
        ImGui::Begin("Scene Hierarchy");

        m_Scene.GetRegistry().view<entt::entity>().each([&](auto entityID)
        {
            // Try to get tag, default to "Entity"
            std::string name = "Entity";
            if (m_Scene.GetRegistry().all_of<ECS::Tag::Component>(entityID))
            {
                name = m_Scene.GetRegistry().get<ECS::Tag::Component>(entityID).Name;
            }

            // Selection flags
            ImGuiTreeNodeFlags flags = ((m_SelectedEntity == entityID) ? ImGuiTreeNodeFlags_Selected : 0) |
                ImGuiTreeNodeFlags_OpenOnArrow;
            flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

            bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<entt::id_type>(entityID)), flags, "%s",
                                            name.c_str());

            if (ImGui::IsItemClicked())
            {
                m_SelectedEntity = entityID;
            }

            if (opened)
            {
                // If we had children, we'd loop here. For now, flat hierarchy.
                ImGui::TreePop();
            }
        });

        // Deselect if clicking in empty space
        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered())
        {
            m_SelectedEntity = entt::null;
        }

        // Context Menu for creating new entities
        if (ImGui::BeginPopupContextWindow(0, 1))
        {
            if (ImGui::MenuItem("Create Empty Entity"))
            {
                m_Scene.CreateEntity("Empty Entity");
            }
            if (ImGui::MenuItem("Remove Entity"))
            {
                if (m_SelectedEntity != entt::null && m_Scene.GetRegistry().valid(m_SelectedEntity))
                {
                    m_Scene.GetRegistry().destroy(m_SelectedEntity);
                    m_SelectedEntity = entt::null;
                }
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    void DrawInspectorPanel()
    {
        ImGui::Begin("Inspector");

        if (m_SelectedEntity != entt::null && m_Scene.GetRegistry().valid(m_SelectedEntity))
        {
            auto& reg = m_Scene.GetRegistry();

            // 1. Tag Component
            if (reg.all_of<ECS::Tag::Component>(m_SelectedEntity))
            {
                auto& tag = reg.get<ECS::Tag::Component>(m_SelectedEntity);
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
            if (reg.all_of<ECS::Transform::Component>(m_SelectedEntity))
            {
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    auto& transform = reg.get<ECS::Transform::Component>(m_SelectedEntity);

                    Interface::GUI::DrawVec3Control("Position", transform.Position);

                    // Convert to degrees for display
                    //quaternion to euler angles
                    glm::vec3 rotationDegrees = glm::degrees(glm::eulerAngles(transform.Rotation));
                    if (Interface::GUI::DrawVec3Control("Rotation", rotationDegrees))
                    {
                        //euler angles to quaternion
                        transform.Rotation = glm::quat(glm::radians(rotationDegrees));
                    }

                    Interface::GUI::DrawVec3Control("Scale", transform.Scale, 1.0f);
                }
            }

            // 3. Mesh Info
            if (reg.all_of<ECS::MeshRenderer::Component>(m_SelectedEntity))
            {
                if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    auto& mr = reg.get<ECS::MeshRenderer::Component>(m_SelectedEntity);
                    if (mr.GeometryRef)
                    {
                        ImGui::Text("Vertices: %lu", mr.GeometryRef->GetLayout().PositionsSize / sizeof(glm::vec3));
                        ImGui::Text("Indices: %u", mr.GeometryRef->GetIndexCount());

                        std::string topoName = "Unknown";
                        switch (mr.GeometryRef->GetTopology())
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
