#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <memory>
#include <imgui.h>
#include <entt/entity/registry.hpp>

// Include Macros for Vulkan (VK_NO_PROTOTYPES)
#include "tiny_gltf.h"
#include "RHI/RHI.Vulkan.hpp"

import Runtime.Engine;
import Core.Logging;
import Core.Input;
import Core.Assets;
import Runtime.Graphics.Geometry;
import Runtime.Graphics.Model;
import Runtime.Graphics.Material;
import Runtime.Graphics.RenderSystem;
import Runtime.Graphics.ModelLoader;
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
    Assets::AssetHandle m_DuckModel;
    Assets::AssetHandle m_DuckTexture;

    std::shared_ptr<Graphics::Material> m_DuckMaterial;

    // State to track if we have spawned the entity yet
    bool m_IsEntitySpawned = false;
    entt::entity m_SelectedEntity = entt::null;

    // Camera State
    Graphics::Camera m_Camera;
    std::unique_ptr<Graphics::CameraController> m_CameraController;

    void OnStart() override
    {
        Log::Info("Sandbox Started!");

        m_Camera.Position = {0.0f, 0.0f, 4.0f};
        m_CameraController = std::make_unique<Graphics::OrbitCameraController>();

        auto textureLoader = [&](const std::string& path)
        {
            // Note: RHI::Texture constructor does IO + GPU upload synchronously here
            // In a real engine, IO is separate from Upload.
            return std::make_shared<RHI::Texture>(GetDevice(), path);
        };
        m_DuckTexture = m_AssetManager.Load<RHI::Texture>("assets/textures/DuckCM.png", textureLoader);

        auto modelLoader = [&](const std::string& path)
        {
            return Graphics::ModelLoader::Load(GetDevice(), path);
        };
        m_DuckModel = m_AssetManager.Load<Graphics::Model>("assets/models/Duck.glb", modelLoader);

        // 3. Setup Material (Assuming texture loads synchronously or is handled)
        m_DuckMaterial = std::make_shared<Graphics::Material>(
            GetDevice(),
            GetDescriptorPool(),
            GetDescriptorLayout(),
            m_DuckTexture,
            m_DefaultTexture, // Inherited from Engine
            m_AssetManager // Inherited from Engine
        );
        m_DuckMaterial->WriteDescriptor(GetGlobalUBO()->GetHandle(), sizeof(RHI::CameraBufferObject));

        Log::Info("Asset Load Requested. Waiting for background thread...");

        /*Interface::GUI::RegisterPanel("Renderer Stats", [camera = &m_Camera]()
        {
            ImGui::Text("Application Average: %.3f ms/frame (%.1f FPS)",
                        1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

            static glm::vec3 sunDir = {1.0, 1.0, 1.0};
            ImGui::DragFloat3("Sun Direction", &sunDir.x, 0.1f);

            // Camera info
            ImGui::Separator();
            glm::vec3 camPos = camera->Position;
            ImGui::Text("Camera Pos: %.2f, %.2f, %.2f", camPos.x, camPos.y, camPos.z);
        }, true, ImGuiWindowFlags_AlwaysAutoResize);

        Interface::GUI::RegisterPanel("Inspector", [this]()
        {
            // Just the content!
            if (ImGui::Button("Spawn Duck"))
            {
                /* ... #1#
            }

            auto view = m_Scene.GetRegistry().view<ECS::TagComponent>();
            for (auto [entity, tag] : view.each())
            {
                ImGui::Text("Entity: %s", tag.Name.c_str());
            }
        });*/

        Interface::GUI::RegisterPanel("Hierarchy", [this]() { DrawHierarchyPanel(); });
        Interface::GUI::RegisterPanel("Inspector", [this]() { DrawInspectorPanel(); });
        Interface::GUI::RegisterPanel("Stats", [this]() {
       ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
       ImGui::Text("Entities: %d", (int)m_Scene.Size());
  });
    }

    void OnUpdate(float dt) override
    {
        m_AssetManager.Update();

        bool uiCapturesMouse = Interface::GUI::WantCaptureMouse();
        bool uiCapturesKeyboard = Interface::GUI::WantCaptureKeyboard();

        if (m_CameraController)
        {
            m_CameraController->OnUpdate(m_Camera, dt, uiCapturesMouse);
        }

        if (m_Window->GetWidth() != 0 && m_Window->GetHeight() != 0)
        {
            m_CameraController->OnResize(m_Camera, m_Window->GetWidth(), m_Window->GetHeight());
        }

        if (!m_IsEntitySpawned)
        {
            if (m_AssetManager.GetState(m_DuckModel) == Assets::LoadState::Ready)
            {
                auto model = m_AssetManager.Get<Graphics::Model>(m_DuckModel);
                if (model && !model->Meshes.empty())
                {
                    auto e = m_Scene.CreateEntity("Duck");
                    m_SelectedEntity = e;
                    auto& t = m_Scene.GetRegistry().get<ECS::TransformComponent>(e);
                    t.Scale = glm::vec3(0.01f);

                    auto& mr = m_Scene.GetRegistry().emplace<ECS::MeshRendererComponent>(e);
                    mr.GeometryRef = model->Meshes[0];
                    mr.MaterialRef = m_DuckMaterial; // Assign material with handle

                    m_IsEntitySpawned = true;
                    Log::Info("Duck Entity Spawned.");
                }
            }
        }

        // --- Rotate Entities ---
        auto view = m_Scene.GetRegistry().view<ECS::TransformComponent>();
        for (auto [entity, transform] : view.each())
        {
            ECS::TransformRotating rotator;
            rotator.OnUpdate(transform, dt);
        }

        // Draw
        m_RenderSystem->OnUpdate(m_Scene, m_Camera);
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
            if (m_Scene.GetRegistry().all_of<ECS::TagComponent>(entityID)) {
                name = m_Scene.GetRegistry().get<ECS::TagComponent>(entityID).Name;
            }

            // Selection flags
            ImGuiTreeNodeFlags flags = ((m_SelectedEntity == entityID) ? ImGuiTreeNodeFlags_Selected : 0) | ImGuiTreeNodeFlags_OpenOnArrow;
            flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

            bool opened = ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entityID, flags, "%s", name.c_str());

            if (ImGui::IsItemClicked()) {
                m_SelectedEntity = entityID;
            }

            if (opened) {
                // If we had children, we'd loop here. For now, flat hierarchy.
                ImGui::TreePop();
            }
        });

        // Deselect if clicking in empty space
        if (ImGui::IsMouseDown(0) && ImGui::IsWindowHovered()) {
            m_SelectedEntity = entt::null;
        }

        // Context Menu for creating new entities
        if (ImGui::BeginPopupContextWindow(0, 1)) {
            if (ImGui::MenuItem("Create Empty Entity")) {
                m_Scene.CreateEntity("Empty Entity");
            }
            if (ImGui::MenuItem("Remove Entity")) {
                m_Scene.GetRegistry().destroy(m_SelectedEntity);
                m_SelectedEntity = entt::null;
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
            if (reg.all_of<ECS::TagComponent>(m_SelectedEntity))
            {
                auto& tag = reg.get<ECS::TagComponent>(m_SelectedEntity);
                char buffer[256];
                memset(buffer, 0, sizeof(buffer));
                strncpy(buffer, tag.Name.c_str(), sizeof(buffer) - 1);
                if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
                    tag.Name = std::string(buffer);
                }
            }

            ImGui::Separator();

            // 2. Transform Component
            if (reg.all_of<ECS::TransformComponent>(m_SelectedEntity))
            {
                if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    auto& transform = reg.get<ECS::TransformComponent>(m_SelectedEntity);

                    Interface::GUI::DrawVec3Control("Position", transform.Position);

                    // Convert to degrees for display
                    glm::vec3 rotationDegrees = transform.Rotation;
                    if (Interface::GUI::DrawVec3Control("Rotation", rotationDegrees)) {
                        transform.Rotation = rotationDegrees;
                    }

                    Interface::GUI::DrawVec3Control("Scale", transform.Scale, 1.0f);
                }
            }

            // 3. Mesh Info
            if (reg.all_of<ECS::MeshRendererComponent>(m_SelectedEntity))
            {
                if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen))
                {
                    auto& mr = reg.get<ECS::MeshRendererComponent>(m_SelectedEntity);
                    if (mr.GeometryRef) {
                        ImGui::Text("Vertices: %u", mr.GeometryRef->GetLayout().PositionsSize / sizeof(glm::vec3));
                        ImGui::Text("Indices: %u", mr.GeometryRef->GetIndexCount());

                        std::string topoName = "Unknown";
                        switch(mr.GeometryRef->GetTopology()) {
                            case Graphics::PrimitiveTopology::Triangles: topoName = "Triangles"; break;
                            case Graphics::PrimitiveTopology::Lines: topoName = "Lines"; break;
                            case Graphics::PrimitiveTopology::Points: topoName = "Points"; break;
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
