module;

#include <algorithm>
#include <set>
#include <vector>
#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

module Runtime.SelectionModule;

import Core.Window;
import Core.Input;
import ECS;
import Graphics.Components;
import Geometry.Ray;
import Graphics.Camera;
import Graphics.RenderDriver;
import Runtime.Selection;

namespace Runtime
{
    namespace
    {
        [[nodiscard]] Selection::PickRequest BuildPickRequest(const Graphics::CameraComponent& camera,
                                                              const Core::Windowing::Window& window,
                                                              Selection::PickMode mode,
                                                              float pickRadiusPixels)
        {
            const auto winW = static_cast<uint32_t>(window.GetWindowWidth());
            const auto winH = static_cast<uint32_t>(window.GetWindowHeight());

            const glm::vec2 m = window.GetInput().GetMousePosition();
            const Graphics::CameraRay worldRay = Graphics::RayFromScreen(
                camera,
                m,
                static_cast<float>(std::max(winW, 1u)),
                static_cast<float>(std::max(winH, 1u)));

            Selection::PickRequest req{};
            req.WorldRay = Geometry::Ray{worldRay.Origin, worldRay.Direction};
            req.ViewMatrix = camera.ViewMatrix;
            req.ProjectionMatrix = camera.ProjectionMatrix;
            req.CursorPositionPixels = m;
            req.Backend = Selection::PickBackend::CPU;
            req.Mode = mode;
            req.PickRadiusPixels = pickRadiusPixels;
            req.CameraFovYRadians = glm::radians(camera.Fov);
            req.ViewportWidthPixels = static_cast<float>(std::max(winW, 1u));
            req.ViewportHeightPixels = static_cast<float>(std::max(winH, 1u));
            return req;
        }
    }

    namespace
    {
        void ToggleElement(std::set<uint32_t>& elements, uint32_t idx)
        {
            if (auto it = elements.find(idx); it != elements.end())
                elements.erase(it);
            else
                elements.insert(idx);
        }
    }

    SelectionModule::SelectionModule()
        : m_Config{}
    {
    }

    SelectionModule::SelectionModule(const Config& cfg)
        : m_Config(cfg)
    {
    }

    void SelectionModule::ConnectToScene(ECS::Scene& scene)
    {
        m_ConnectedScene = &scene;
        scene.GetDispatcher().sink<ECS::Events::GpuPickCompleted>().connect<&SelectionModule::OnGpuPickCompleted>(*this);
        scene.GetDispatcher().sink<ECS::Events::SelectionChanged>().connect<&SelectionModule::OnSelectionChanged>(*this);
    }

    void SelectionModule::OnGpuPickCompleted(const ECS::Events::GpuPickCompleted& evt)
    {
        m_CachedGpuPick = CachedGpuPick{evt.PickID, evt.PrimitiveID, evt.HasHit};
    }

    void SelectionModule::OnSelectionChanged(const ECS::Events::SelectionChanged&)
    {
        if (m_ConnectedScene != nullptr)
            SyncPickedToSelection(*m_ConnectedScene, nullptr);
    }

    glm::uvec2 SelectionModule::WindowToFramebufferPixel(const Core::Windowing::Window& window,
                                                        const glm::vec2& mouseWindowCoords)
    {
        const uint32_t fbW = static_cast<uint32_t>(window.GetFramebufferWidth());
        const uint32_t fbH = static_cast<uint32_t>(window.GetFramebufferHeight());

        const uint32_t winW = static_cast<uint32_t>(window.GetWindowWidth());
        const uint32_t winH = static_cast<uint32_t>(window.GetWindowHeight());

        if (fbW == 0 || fbH == 0 || winW == 0 || winH == 0)
            return {0u, 0u};

        // Window -> framebuffer scale factors (HiDPI aware).
        const float sx = static_cast<float>(fbW) / static_cast<float>(winW);
        const float sy = static_cast<float>(fbH) / static_cast<float>(winH);

        const float mxFb = mouseWindowCoords.x * sx;
        const float myFb = mouseWindowCoords.y * sy;

        const float xClamped = glm::clamp(mxFb, 0.0f, static_cast<float>(fbW - 1));

        // No Flip Y needed: Vulkan images are top-left origin (0,0), same as GLFW window coords.
        const float yClamped = glm::clamp(myFb, 0.0f, static_cast<float>(fbH - 1));

        return {static_cast<uint32_t>(xClamped), static_cast<uint32_t>(yClamped)};
    }

    void SelectionModule::ApplyFromGpuPick(ECS::Scene& scene,
                                          uint32_t pickID, uint32_t primitiveID,
                                          bool hasHit,
                                          Selection::PickMode mode,
                                          const Selection::PickRequest* request,
                                          Selection::ElementMode elementMode,
                                          Selection::Picked& picked)
    {
        auto& reg = scene.GetRegistry();

        if (hasHit && pickID != 0u)
        {
            // Resolve pick ID -> entity via component lookup.
            auto view = reg.view<ECS::Components::Selection::PickID, ECS::Components::Selection::SelectableTag>();
            for (auto e : view)
            {
                const auto& pid = view.get<ECS::Components::Selection::PickID>(e);
                if (pid.Value == pickID)
                {
                    if (reg.valid(e))
                    {
                        if (elementMode != Selection::ElementMode::Entity)
                        {
                            picked = Selection::ResolveGpuSubElementPick(scene,
                                                                         e,
                                                                         primitiveID,
                                                                         elementMode,
                                                                         request);
                        }
                        else
                        {
                            // Fallback: CPU refinement when PrimitiveID is not
                            // available or we're in Entity selection mode.
                            if (request != nullptr)
                            {
                                const auto entityPick = Selection::PickEntityCPU(scene, e, *request);
                                if (entityPick.Entity == e)
                                    picked = entityPick.PickedData;
                            }
                        }

                        if (picked.entity.id == entt::null)
                        {
                            picked.entity.id = e;
                            picked.entity.is_background = false;
                        }

                        Selection::ApplySelection(scene, e, mode);
                        return;
                    }
                }
            }
        }

        picked = {};
        Selection::ApplySelection(scene, entt::null, mode);
    }

    void SelectionModule::SyncPickedToSelection(ECS::Scene& scene, const Selection::PickResult* clickResult)
    {
        const entt::entity selected = GetSelectedEntity(scene);
        if (selected == entt::null || !scene.GetRegistry().valid(selected))
        {
            m_Picked = {};
            return;
        }

        if (clickResult != nullptr && clickResult->Entity == selected && clickResult->PickedData.entity)
        {
            m_Picked = clickResult->PickedData;
            return;
        }

        if (m_Picked.entity.id == selected && m_Picked.entity)
            return;

        m_Picked = {};
        m_Picked.entity.id = selected;
        m_Picked.entity.is_background = false;
    }

    entt::entity SelectionModule::GetSelectedEntity(const ECS::Scene& scene) const
    {
        const auto& reg = scene.GetRegistry();
        auto view = reg.view<ECS::Components::Selection::SelectedTag>();
        for (auto e : view)
            return e;
        return entt::null;
    }

    std::vector<entt::entity> SelectionModule::GetSelectedEntities(const ECS::Scene& scene) const
    {
        const auto& reg = scene.GetRegistry();
        auto view = reg.view<ECS::Components::Selection::SelectedTag>();
        std::vector<entt::entity> result;
        for (auto e : view)
            result.push_back(e);
        return result;
    }

    void SelectionModule::SetSelectedEntity(ECS::Scene& scene, entt::entity e)
    {
        Selection::ApplySelection(scene, e, Selection::PickMode::Replace);
        SyncPickedToSelection(scene, nullptr);
    }

    void SelectionModule::ClearSelection(ECS::Scene& scene)
    {
        Selection::ApplySelection(scene, entt::null, Selection::PickMode::Replace);
        m_Picked = {};
        m_SubElements.Clear();
    }

    void SelectionModule::Update(ECS::Scene& scene,
                                 Graphics::RenderDriver& renderSystem,
                                 const Graphics::CameraComponent* camera,
                                 const Core::Windowing::Window& window,
                                 bool uiCapturesMouse)
    {
        if (m_Config.Active != Activation::Enabled)
            return;

        // Keep renderable scene entities selectable/pickable even when they were
        // authored procedurally and did not come through SceneManager.
        {
            auto& reg = scene.GetRegistry();
            static uint32_t s_NextAutoPickId = 2'000'000u;
            const auto ensurePickability = [&](entt::entity entity)
            {
                if (!reg.all_of<ECS::Components::Selection::SelectableTag>(entity))
                    reg.emplace<ECS::Components::Selection::SelectableTag>(entity);
                if (!reg.all_of<ECS::Components::Selection::PickID>(entity))
                    reg.emplace<ECS::Components::Selection::PickID>(entity, s_NextAutoPickId++);
            };

            for (auto entity : reg.view<ECS::Surface::Component>()) ensurePickability(entity);
            for (auto entity : reg.view<ECS::PointCloud::Data>()) ensurePickability(entity);
            for (auto entity : reg.view<ECS::Graph::Data>()) ensurePickability(entity);
        }

        // Shift modifies selection semantics:
        //  - no shift: replace selection with the clicked entity (standard click-to-select)
        //  - shift: toggle clicked entity; background clears only if shift is NOT held
        const bool shiftDown = window.GetInput().IsKeyPressed(340) || window.GetInput().IsKeyPressed(344); // GLFW_KEY_LEFT_SHIFT / RIGHT_SHIFT
        const Selection::PickMode clickMode = shiftDown
            ? Selection::PickMode::Toggle
            : Selection::PickMode::Replace;

        // 0) Hover highlight.
        // IMPORTANT: CPU hover raycasts are O(total_triangle_count) and can dominate frame time
        // on larger meshes. We therefore keep continuous hover picking only on CPU backend
        // (where clicks are already CPU raycasts anyway). On GPU backend we skip per-frame CPU
        // hover to avoid scaling render cost with model complexity.
        const bool doCpuHover = (m_Config.Backend == Selection::PickBackend::CPU);

        if (doCpuHover && camera != nullptr && !uiCapturesMouse)
        {
            const uint32_t winW = static_cast<uint32_t>(window.GetWindowWidth());
            const uint32_t winH = static_cast<uint32_t>(window.GetWindowHeight());
            if (winW > 0 && winH > 0)
            {
                const auto req = BuildPickRequest(*camera, window,
                                                  Selection::PickMode::Replace,
                                                  m_Config.PickRadiusPixels);
                const auto hit = Selection::PickCPU(scene, req);
                Selection::ApplyHover(scene, hit.Entity);
            }
        }
        else
        {
            // UI captures mouse/no camera, or hover raycast disabled for current backend.
            Selection::ApplyHover(scene, entt::null);
        }

        // 1) On click: schedule GPU pick or do CPU pick immediately.
        if (camera != nullptr && !uiCapturesMouse)
        {
            if (window.GetInput().IsMouseButtonJustPressed(m_Config.MouseButton))
            {
                if (m_Config.Backend == Selection::PickBackend::GPU)
                {
                    if (!renderSystem.GetInteraction().GetPendingPick().Pending)
                    {
                        m_PendingPickRequest = BuildPickRequest(*camera, window, clickMode, m_Config.PickRadiusPixels);
                        m_HasPendingPickRequest = true;
                        const glm::uvec2 px = WindowToFramebufferPixel(window, window.GetInput().GetMousePosition());
                        renderSystem.RequestPick(px.x, px.y);
                        // Capture click mode at request time so the correct mode is applied
                        // when the async GPU readback result arrives (potentially frames later).
                        m_PendingGpuClickMode = clickMode;
                    }
                }
                else
                {
                    const uint32_t winW = static_cast<uint32_t>(window.GetWindowWidth());
                    const uint32_t winH = static_cast<uint32_t>(window.GetWindowHeight());
                    if (winW > 0 && winH > 0)
                    {
                        m_PendingPickRequest = BuildPickRequest(*camera, window, clickMode, m_Config.PickRadiusPixels);
                        m_HasPendingPickRequest = true;

                        const auto hit = Selection::PickCPU(scene, m_PendingPickRequest);

                        if (m_Config.ElementMode != Selection::ElementMode::Entity && hit.Entity != entt::null)
                        {
                            // Sub-element selection mode: select the entity first, then
                            // add/toggle the picked sub-element.
                            if (m_SubElements.Entity != hit.Entity)
                            {
                                // Switched to a different entity — reset sub-element state.
                                m_SubElements.Clear();
                                m_SubElements.Entity = hit.Entity;
                                Selection::ApplySelection(scene, hit.Entity, Selection::PickMode::Replace);
                            }
                            else if (!scene.GetRegistry().all_of<ECS::Components::Selection::SelectedTag>(hit.Entity))
                            {
                                Selection::ApplySelection(scene, hit.Entity, Selection::PickMode::Replace);
                            }

                            ApplySubElementPick(hit.PickedData, clickMode);
                        }
                        else
                        {
                            Selection::ApplySelection(scene, hit.Entity, m_PendingPickRequest.Mode);
                            if (m_Config.ElementMode == Selection::ElementMode::Entity || hit.Entity == entt::null)
                                m_SubElements.Clear();
                        }

                        SyncPickedToSelection(scene, hit.Entity != entt::null ? &hit : nullptr);
                        m_HasPendingPickRequest = false;
                    }
                }
            }
        }

        // 2) For GPU: consume cached result from GpuPickCompleted event.
        if (m_Config.Backend == Selection::PickBackend::GPU && m_CachedGpuPick.has_value())
        {
            const auto pick = *m_CachedGpuPick;
            m_CachedGpuPick.reset();

            Selection::Picked picked{};
            ApplyFromGpuPick(scene,
                             pick.PickID,
                             pick.PrimitiveID,
                             pick.HasHit,
                             m_PendingGpuClickMode,
                             m_HasPendingPickRequest ? &m_PendingPickRequest : nullptr,
                             m_Config.ElementMode,
                             picked);

            // GPU path sub-element selection (same logic as CPU path).
            if (m_Config.ElementMode != Selection::ElementMode::Entity && picked.entity)
            {
                if (m_SubElements.Entity != picked.entity.id)
                {
                    m_SubElements.Clear();
                    m_SubElements.Entity = picked.entity.id;
                }
                ApplySubElementPick(picked, m_PendingGpuClickMode);
            }
            else if (m_Config.ElementMode == Selection::ElementMode::Entity)
            {
                m_SubElements.Clear();
            }
            else if (!picked.entity)
            {
                m_SubElements.Clear();
            }

            Selection::PickResult resolved{};
            resolved.Entity = picked.entity.id;
            resolved.PickedData = picked;
            SyncPickedToSelection(scene, resolved.Entity != entt::null ? &resolved : nullptr);
            m_HasPendingPickRequest = false;
        }
    }

    void SelectionModule::ApplySubElementPick(const Selection::Picked& picked, Selection::PickMode mode)
    {
        constexpr uint32_t Invalid = Selection::Picked::Entity::InvalidIndex;

        uint32_t idx = Invalid;
        std::set<uint32_t>* target = nullptr;

        switch (m_Config.ElementMode)
        {
        case Selection::ElementMode::Vertex:
            idx = picked.entity.vertex_idx;
            target = &m_SubElements.SelectedVertices;
            break;
        case Selection::ElementMode::Edge:
            idx = picked.entity.edge_idx;
            target = &m_SubElements.SelectedEdges;
            break;
        case Selection::ElementMode::Face:
            idx = picked.entity.face_idx;
            target = &m_SubElements.SelectedFaces;
            break;
        default:
            return;
        }

        if (idx == Invalid || target == nullptr)
            return;

        switch (mode)
        {
        case Selection::PickMode::Replace:
            m_SubElements.SelectedVertices.clear();
            m_SubElements.SelectedEdges.clear();
            m_SubElements.SelectedFaces.clear();
            target->insert(idx);
            break;
        case Selection::PickMode::Add:
            target->insert(idx);
            break;
        case Selection::PickMode::Toggle:
            ToggleElement(*target, idx);
            break;
        }
    }
}
