module;

#include <glm/glm.hpp>
#include <entt/entity/registry.hpp>

module Runtime.SelectionModule;

import Core;
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
                                          const Graphics::RenderSystem::PickResultGpu& pick,
                                          Runtime::Selection::PickMode mode)
    {
        if (pick.HasHit)
        {
            const auto e = static_cast<entt::entity>(static_cast<entt::id_type>(pick.EntityID));
            if (scene.GetRegistry().valid(e))
            {
                Runtime::Selection::ApplySelection(scene, e, mode);
                return;
            }
        }

        // No hit or invalid -> clear selection (depending on mode Replace semantics).
        Runtime::Selection::ApplySelection(scene, entt::null, mode);
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
        Runtime::Selection::ApplySelection(scene, e, Runtime::Selection::PickMode::Replace);
    }

    void SelectionModule::ClearSelection(ECS::Scene& scene)
    {
        Runtime::Selection::ApplySelection(scene, entt::null, Runtime::Selection::PickMode::Replace);
    }

    void SelectionModule::Update(ECS::Scene& scene,
                                 Graphics::RenderSystem& renderSystem,
                                 const Graphics::CameraComponent* camera,
                                 const Core::Windowing::Window& window,
                                 bool uiCapturesMouse)
    {
        if (m_Config.Active != Activation::Enabled)
            return;

        // 1) On click: schedule GPU pick or do CPU pick immediately.
        if (camera != nullptr && !uiCapturesMouse)
        {
            if (window.GetInput().IsMouseButtonJustPressed(m_Config.MouseButton))
            {
                if (m_Config.Backend == Runtime::Selection::PickBackend::GPU)
                {
                    const glm::uvec2 px = WindowToFramebufferPixel(window, window.GetInput().GetMousePosition());
                    renderSystem.RequestPick(px.x, px.y);
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

                        Runtime::Selection::PickRequest req{};
                        req.WorldRay = Runtime::Selection::RayFromNDC(*camera, {nx, ny});
                        req.Backend = Runtime::Selection::PickBackend::CPU;
                        req.Mode = m_Config.Mode;

                        const auto hit = Runtime::Selection::PickCPU(scene, req);
                        Runtime::Selection::ApplySelection(scene, hit.Entity, req.Mode);
                    }
                }
            }
        }

        // 2) For GPU: consume resolved results whenever they become ready.
        if (m_Config.Backend == Runtime::Selection::PickBackend::GPU)
        {
            if (auto pickOpt = renderSystem.TryConsumePickResult(); pickOpt.has_value())
            {
                ApplyFromGpuPick(scene, *pickOpt, m_Config.Mode);
            }
        }
    }
}

