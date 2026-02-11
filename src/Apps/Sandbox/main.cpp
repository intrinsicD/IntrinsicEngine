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
import Runtime.Selection;
import Core;
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

    void OnStart() override
    {
        Log::Info("Sandbox Started!");

        auto& gfx = GetGraphicsBackend();


        m_CameraEntity = m_Scene.CreateEntity("Main Camera");
        m_Camera = m_Scene.GetRegistry().emplace<Graphics::CameraComponent>(m_CameraEntity);
        m_Scene.GetRegistry().emplace<Graphics::OrbitControlComponent>(m_CameraEntity);

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

                m_AssetManager.MoveToProcessing(handle);
                return std::move(result->Texture);
            }

            Log::Warn("Texture load failed: {} ({})", path.string(), Graphics::AssetErrorToString(result.error()));
            return {};
        };
        m_DuckTexture = m_AssetManager.Load<RHI::Texture>(Filesystem::GetAssetPath("textures/DuckCM.png"),
                                                          textureLoader);

        auto modelLoader = [&](const std::string& path, Assets::AssetHandle handle)
            -> std::unique_ptr<Graphics::Model>
        {
            auto result = Graphics::ModelLoader::LoadAsync(GetDevice(), gfx.GetTransferManager(), m_GeometryStorage, path);

            if (result)
            {
                // 1. Notify Engine to track the GPU work
                RegisterAssetLoad(handle, result->Token);

                // 2. Notify AssetManager to wait
                m_AssetManager.MoveToProcessing(handle);

                // 3. Return the model (valid CPU pointers, GPU buffers are allocated but content is uploading)
                return std::move(result->ModelData);
            }

            Log::Warn("Model load failed: {} ({})", path, Graphics::AssetErrorToString(result.error()));
            return nullptr; // Failed
        };

        m_DuckModel = m_AssetManager.Load<Graphics::Model>(
            Filesystem::GetAssetPath("models/Duck.glb"),
            modelLoader
        );


        // 3. Setup Material (Assuming texture loads synchronously or is handled)
        Graphics::MaterialData matData;
        matData.AlbedoID = gfx.GetDefaultTextureIndex(); // Fallback until loaded
        matData.RoughnessFactor = 1.0f;
        matData.MetallicFactor = 0.0f;

        auto DuckMaterial = std::make_unique<Graphics::Material>(
            *m_MaterialSystem,
            matData
        );

        // Link the texture asset to the material (this registers the listener)
        DuckMaterial->SetAlbedoTexture(m_DuckTexture);

        // Track handle only; AssetManager owns the actual Material object.
        m_DuckMaterialHandle = m_AssetManager.Create("DuckMaterial", std::move(DuckMaterial));
        m_LoadedMaterials.push_back(m_DuckMaterialHandle);

        Log::Info("Asset Load Requested. Waiting for background thread...");

        Interface::GUI::RegisterPanel("Hierarchy", [this]() { DrawHierarchyPanel(); });
        Interface::GUI::RegisterPanel("Inspector", [this]() { DrawInspectorPanel(); });
        Interface::GUI::RegisterPanel("Assets", [this]() { m_AssetManager.AssetsUiPanel(); });
        Interface::GUI::RegisterPanel("Stats", [this]()
        {
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Entities: %d", (int)m_Scene.Size());

            // --- Render Pipeline ---
            ImGui::SeparatorText("Render Pipeline");
            if (m_RenderSystem)
            {
                if (ImGui::Button("Hot-swap: DefaultPipeline"))
                {
                    // Request swap; RenderSystem owns lifetime and applies at the start of the next frame.
                    m_RenderSystem->RequestPipelineSwap(std::make_unique<Graphics::DefaultPipeline>());
                }
            }
            else
            {
                ImGui::TextDisabled("RenderSystem not initialized.");
            }

            // --- Selection Debug ---
            ImGui::Separator();
            ImGui::Text("Select Mouse Button: %d", m_SelectMouseButton);

            const entt::entity selected = GetSelection().GetSelectedEntity(m_Scene);
            const bool selectedValid = (selected != entt::null) && m_Scene.GetRegistry().valid(selected);

            ImGui::Text("Selected: %u (%s)",
                        static_cast<uint32_t>(static_cast<entt::id_type>(selected)),
                        selectedValid ? "valid" : "invalid");

            if (selectedValid)
            {
                const auto& reg = m_Scene.GetRegistry();
                const bool hasSelectedTag = reg.all_of<ECS::Components::Selection::SelectedTag>(selected);
                const bool hasSelectableTag = reg.all_of<ECS::Components::Selection::SelectableTag>(selected);
                const bool hasMeshRenderer = reg.all_of<ECS::MeshRenderer::Component>(selected);
                const bool hasMeshCollider = reg.all_of<ECS::MeshCollider::Component>(selected);

                ImGui::Text("Tags: Selectable=%d Selected=%d", (int)hasSelectableTag, (int)hasSelectedTag);
                ImGui::Text("Components: MeshRenderer=%d MeshCollider=%d", (int)hasMeshRenderer, (int)hasMeshCollider);
            }
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
                // One line to rule them all
                SpawnModel(m_DuckModel, m_DuckMaterialHandle, glm::vec3(0.0f), glm::vec3(0.01f));

                // (Optional) If you need to add specific behaviors like rotation:
                // entt::entity duck = SpawnModel(...);
                // m_Scene.GetRegistry().emplace<ECS::Components::AxisRotator::Component>(duck, ...);

                m_IsEntitySpawned = true;
                Log::Info("Duck Entity Spawned.");
            }
        }

        {
            auto view = m_Scene.GetRegistry().view<
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
        // Selection: delegate click-to-pick-to-registry-tags to the Engine module.
        // ---------------------------------------------------------------------
        if (cameraComponent != nullptr && m_RenderSystem)
        {
            // Keep module config in sync with the UI setting.
            GetSelection().GetConfig().MouseButton = m_SelectMouseButton;

            GetSelection().Update(
                m_Scene,
                *m_RenderSystem,
                cameraComponent,
                *m_Window,
                uiCapturesMouse);
        }

        // Draw
        if (cameraComponent != nullptr && m_RenderSystem)
        {
            m_RenderSystem->OnUpdate(m_Scene, *cameraComponent, m_AssetManager);
        }
    }

    void OnRender() override
    {
    }

    void OnRegisterSystems(Core::FrameGraph& graph, float deltaTime) override
    {
        ECS::Systems::AxisRotator::RegisterSystem(graph, m_Scene.GetRegistry(), deltaTime);
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

        const entt::entity selected = GetSelection().GetSelectedEntity(m_Scene);

        m_Scene.GetRegistry().view<entt::entity>().each([&](auto entityID)
        {
            // Try to get tag, default to "Entity"
            std::string name = "Entity";
            if (m_Scene.GetRegistry().all_of<ECS::Components::NameTag::Component>(entityID))
            {
                name = m_Scene.GetRegistry().get<ECS::Components::NameTag::Component>(entityID).Name;
            }

            // Selection flags
            ImGuiTreeNodeFlags flags = ((selected == entityID) ? ImGuiTreeNodeFlags_Selected : 0) |
                ImGuiTreeNodeFlags_OpenOnArrow;
            flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

            bool opened = ImGui::TreeNodeEx(reinterpret_cast<void*>(static_cast<entt::id_type>(entityID)), flags, "%s",
                                            name.c_str());

            if (ImGui::IsItemClicked())
            {
                GetSelection().SetSelectedEntity(m_Scene, entityID);
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
            GetSelection().ClearSelection(m_Scene);
        }

        // Context Menu for creating new entities
        if (ImGui::BeginPopupContextWindow(nullptr, 1))
        {
            if (ImGui::MenuItem("Create Empty Entity"))
            {
                m_Scene.CreateEntity("Empty Entity");
            }
            if (ImGui::MenuItem("Remove Entity"))
            {
                const entt::entity cur = GetSelection().GetSelectedEntity(m_Scene);
                if (cur != entt::null && m_Scene.GetRegistry().valid(cur))
                {
                    m_Scene.GetRegistry().destroy(cur);
                    GetSelection().ClearSelection(m_Scene);
                }
            }
            ImGui::EndPopup();
        }

        ImGui::End();
    }

    void DrawInspectorPanel()
    {
        ImGui::Begin("Inspector");

        const entt::entity selected = GetSelection().GetSelectedEntity(m_Scene);

        if (selected != entt::null && m_Scene.GetRegistry().valid(selected))
        {
            auto& reg = m_Scene.GetRegistry();

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
