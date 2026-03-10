module;

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>

module Runtime.SelectionModule;

import Core.Window;
import Core.Input;
import ECS;
import Graphics;
import Runtime.Selection;

namespace Runtime
{
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
        // Register a dispatcher sink for GpuPickCompleted.
        // When RenderSystem fires the event (after GPU readback), this caches
        // the result for consumption in the next Update() call.
        scene.GetDispatcher().sink<ECS::Events::GpuPickCompleted>().connect<
            [](SelectionModule& self, const ECS::Events::GpuPickCompleted& evt) {
                self.m_CachedGpuPick = CachedGpuPick{evt.PickID, evt.HasHit};
            }>(*this);
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
                                          uint32_t pickID, bool hasHit,
                                          Selection::PickMode mode)
    {
        auto& reg = scene.GetRegistry();

        if (hasHit && pickID != 0u)
        {
            // Resolve pick ID -> entity via component lookup.
            // Keep this restricted to selectable entities to avoid picking internal/non-editor entities.
            auto view = reg.view<ECS::Components::Selection::PickID, ECS::Components::Selection::SelectableTag>();
            for (auto e : view)
            {
                const auto& pid = view.get<ECS::Components::Selection::PickID>(e);
                if (pid.Value == pickID)
                {
                    if (reg.valid(e))
                    {
                        Selection::ApplySelection(scene, e, mode);
                        return;
                    }
                }
            }
        }

        // No hit or invalid -> treat as background.
        Selection::ApplySelection(scene, entt::null, mode);
    }

    entt::entity SelectionModule::GetSelectedEntity(const ECS::Scene& scene) const
    {
        const auto& reg = scene.GetRegistry();
        auto view = reg.view<ECS::Components::Selection::SelectedTag>();
        for (auto e : view)
            return e; // NOTE: single-selection for now.
        return entt::null;
    }

    void SelectionModule::SetSelectedEntity(ECS::Scene& scene, entt::entity e)
    {
        Selection::ApplySelection(scene, e, Selection::PickMode::Replace);
    }

    void SelectionModule::ClearSelection(ECS::Scene& scene)
    {
        Selection::ApplySelection(scene, entt::null, Selection::PickMode::Replace);
    }

    void SelectionModule::Update(ECS::Scene& scene,
                                 Graphics::RenderSystem& renderSystem,
                                 const Graphics::CameraComponent* camera,
                                 const Core::Windowing::Window& window,
                                 bool uiCapturesMouse)
    {
        if (m_Config.Active != Activation::Enabled)
            return;

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
                const glm::vec2 m = window.GetInput().GetMousePosition();

                // NDC: x in [-1,1], y in [-1,1] with +y down (Vulkan).
                const float nx = (2.0f * (m.x / static_cast<float>(winW))) - 1.0f;
                const float ny = (2.0f * (m.y / static_cast<float>(winH))) - 1.0f;

                Selection::PickRequest req{};
                req.WorldRay = Selection::RayFromNDC(*camera, {nx, ny});
                req.Backend = Selection::PickBackend::CPU;

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
                    const glm::uvec2 px = WindowToFramebufferPixel(window, window.GetInput().GetMousePosition());
                    renderSystem.RequestPick(px.x, px.y);
                    // Capture click mode at request time so the correct mode is applied
                    // when the async GPU readback result arrives (potentially frames later).
                    m_PendingGpuClickMode = clickMode;
                }
                else
                {
                    // CPU: build a ray from NDC.
                    const uint32_t winW = static_cast<uint32_t>(window.GetWindowWidth());
                    const uint32_t winH = static_cast<uint32_t>(window.GetWindowHeight());
                    if (winW > 0 && winH > 0)
                    {
                        const glm::vec2 m = window.GetInput().GetMousePosition();

                        // NDC: x in [-1,1], y in [-1,1] with +y down (Vulkan).
                        const float nx = (2.0f * (m.x / static_cast<float>(winW))) - 1.0f;
                        const float ny = (2.0f * (m.y / static_cast<float>(winH))) - 1.0f;

                        Selection::PickRequest req{};
                        req.WorldRay = Selection::RayFromNDC(*camera, {nx, ny});
                        req.Backend = Selection::PickBackend::CPU;
                        req.Mode = clickMode;

                        const auto hit = Selection::PickCPU(scene, req);
                        Selection::ApplySelection(scene, hit.Entity, req.Mode);
                    }
                }
            }
        }

        // 2) For GPU: consume cached result from GpuPickCompleted event.
        if (m_Config.Backend == Selection::PickBackend::GPU && m_CachedGpuPick.has_value())
        {
            const auto pick = *m_CachedGpuPick;
            m_CachedGpuPick.reset();

            ApplyFromGpuPick(scene, pick.PickID, pick.HasHit, m_PendingGpuClickMode);
        }
    }
}
