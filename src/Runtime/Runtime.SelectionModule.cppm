module;
#include <cstdint>

#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>

export module Runtime.SelectionModule;

import Core;
import ECS;
import Graphics;
import Runtime.Selection;

export namespace Runtime
{
    // High-level selection controller owned by the Engine.
    // Bridges:
    //  - platform input (GLFW via Core::Input)
    //  - RenderSystem GPU picking (async)
    //  - registry tags (SelectedTag/HoveredTag)
    class SelectionModule
    {
    public:
        enum class Activation : uint8_t
        {
            Disabled = 0,
            Enabled  = 1,
        };

        struct Config
        {
            // Which mouse button triggers selection.
            // 0=LMB, 1=RMB, 2=MMB.
            int MouseButton = 1;

            // If true, we do GPU ID-buffer picking.
            // If false, we'll fall back to CPU raycast picking.
            Selection::PickBackend Backend = Selection::PickBackend::GPU;

            // Selection mode on click.
            Selection::PickMode Mode = Selection::PickMode::Replace;

            Activation Active = Activation::Enabled;
        };

        SelectionModule();
        explicit SelectionModule(const Config& cfg);

        // Per-frame update.
        // Contract:
        //  - Call this once per frame, before/after RenderSystem update is fine.
        //  - For GPU: schedules pick on click and consumes results when ready.
        //  - For CPU: performs a raycast immediately on click.
        void Update(ECS::Scene& scene,
                    Graphics::RenderSystem& renderSystem,
                    const Graphics::CameraComponent* camera,
                    const Core::Windowing::Window& window,
                    bool uiCapturesMouse);

        [[nodiscard]] entt::entity GetSelectedEntity(const ECS::Scene& scene) const;
        void SetSelectedEntity(ECS::Scene& scene, entt::entity e);
        void ClearSelection(ECS::Scene& scene);

        [[nodiscard]] Config& GetConfig() { return m_Config; }
        [[nodiscard]] const Config& GetConfig() const { return m_Config; }

    private:
        Config m_Config;

        static void ApplyFromGpuPick(ECS::Scene& scene,
                                    const Graphics::RenderSystem::PickResultGpu& pick,
                                    Runtime::Selection::PickMode mode);

        [[nodiscard]] static glm::uvec2 WindowToFramebufferPixel(const Core::Windowing::Window& window,
                                                                const glm::vec2& mouseWindowCoords);
    };
}
