module;
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>

export module Runtime.SelectionModule;

import Core.Window;
import Core.Input;
import ECS;
import Graphics.Camera;
import Graphics.RenderDriver;
import Runtime.Selection;
import Graphics.SubElementHighlightSettings;

export namespace Runtime
{
    // High-level selection controller owned by the Engine.
    // Bridges:
    //  - platform input (GLFW via Core::Input)
    //  - GpuPickCompleted dispatcher event (from RenderDriver readback)
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
            int MouseButton = 0;

            // If true, we do GPU ID-buffer picking.
            // If false, we'll fall back to CPU raycast picking.
            Selection::PickBackend Backend = Selection::PickBackend::GPU;

            // Selection mode on click.
            Selection::PickMode Mode = Selection::PickMode::Replace;

            // Screen-space radius used to resolve mesh/graph/point-cloud
            // sub-elements after the entity pick resolves.
            float PickRadiusPixels = 12.0f;

            Activation Active = Activation::Enabled;

            // Element-level selection mode (Entity/Vertex/Edge/Face).
            Selection::ElementMode ElementMode = Selection::ElementMode::Entity;
        };

        SelectionModule();
        explicit SelectionModule(const Config& cfg);

        // Connect the GpuPickCompleted dispatcher sink to this module.
        // Must be called once after the scene is available (e.g. during app init).
        void ConnectToScene(ECS::Scene& scene);

        // Per-frame update.
        // Contract:
        //  - Call this once per frame, before/after RenderDriver update is fine.
        //  - For GPU: schedules pick on click; results arrive via GpuPickCompleted event.
        //  - For CPU: performs a raycast immediately on click.
        void Update(ECS::Scene& scene,
                    Graphics::RenderDriver& renderSystem,
                    const Graphics::CameraComponent* camera,
                    const Core::Windowing::Window& window,
                    bool uiCapturesMouse);

        [[nodiscard]] entt::entity GetSelectedEntity(const ECS::Scene& scene) const;
        [[nodiscard]] std::vector<entt::entity> GetSelectedEntities(const ECS::Scene& scene) const;
        [[nodiscard]] const Selection::Picked& GetPicked() const { return m_Picked; }
        void SetSelectedEntity(ECS::Scene& scene, entt::entity e);
        void ClearSelection(ECS::Scene& scene);

        [[nodiscard]] Config& GetConfig() { return m_Config; }
        [[nodiscard]] const Config& GetConfig() const { return m_Config; }

        // Sub-element selection state (per-entity vertex/edge/face sets).
        [[nodiscard]] const Selection::SubElementSelection& GetSubElementSelection() const { return m_SubElements; }
        [[nodiscard]] Selection::SubElementSelection& GetSubElementSelection() { return m_SubElements; }
        void ClearSubElementSelection() { m_SubElements.Clear(); }

        // Sub-element highlight appearance (colors, sizes).
        [[nodiscard]] Graphics::SubElementHighlightSettings& GetHighlightSettings() { return m_HighlightSettings; }
        [[nodiscard]] const Graphics::SubElementHighlightSettings& GetHighlightSettings() const { return m_HighlightSettings; }

    private:
        Config m_Config;
        Graphics::SubElementHighlightSettings m_HighlightSettings;

        // Click mode captured at the time the GPU pick was requested,
        // so the correct mode is applied when the async result arrives.
        Selection::PickMode m_PendingGpuClickMode = Selection::PickMode::Replace;

        ECS::Scene* m_ConnectedScene = nullptr;
        Selection::PickRequest m_PendingPickRequest{};
        bool m_HasPendingPickRequest = false;
        Selection::Picked m_Picked{};
        Selection::SubElementSelection m_SubElements{};

        // Cached GPU pick result received via GpuPickCompleted dispatcher event.
        struct CachedGpuPick
        {
            uint32_t PickID = 0;
            uint32_t PrimitiveID = std::numeric_limits<uint32_t>::max();
            bool HasHit = false;
        };
        std::optional<CachedGpuPick> m_CachedGpuPick;

        void OnGpuPickCompleted(const ECS::Events::GpuPickCompleted& evt);
        void OnSelectionChanged(const ECS::Events::SelectionChanged& evt);
        void SyncPickedToSelection(ECS::Scene& scene, const Selection::PickResult* clickResult = nullptr);
        void ApplySubElementPick(const Selection::Picked& picked, Selection::PickMode mode);

        static void ApplyFromGpuPick(ECS::Scene& scene,
                                    uint32_t pickID, uint32_t primitiveID,
                                    bool hasHit,
                                    Runtime::Selection::PickMode mode,
                                    const Selection::PickRequest* request,
                                    Selection::ElementMode elementMode,
                                    Selection::Picked& picked);

        [[nodiscard]] static glm::uvec2 WindowToFramebufferPixel(const Core::Windowing::Window& window,
                                                                const glm::vec2& mouseWindowCoords);
    };
}
