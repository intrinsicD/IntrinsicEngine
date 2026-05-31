module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

export module Extrinsic.Runtime.SelectionController;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;

export namespace Extrinsic::Runtime
{
    // How a resolved *click* pick combines with the existing selection set.
    enum class SelectionPickMode : std::uint8_t
    {
        Replace = 0, // selection becomes exactly the picked entity
        Add     = 1, // picked entity is added to the selection set
        Toggle  = 2, // picked entity is toggled in / out of the selection set
    };

    // Distinguishes the two pointer intents the controller coalesces into the
    // single per-frame pick the graphics readback seam supports. The kind is
    // captured when the pending pick is drained and replayed when the matching
    // readback arrives, so a hover result never mutates the selection set and a
    // click result never silently updates hover-only state.
    enum class SelectionPickKind : std::uint8_t
    {
        Hover = 0,
        Click = 1,
    };

    // Coalesced pixel-space pick the controller emits at most once per frame.
    // Slice B maps this onto `Graphics::PickRequest` / `RenderFrameInput::Pick`
    // before `IRenderer::ExtractRenderWorld()`.
    struct PendingSelectionPick
    {
        std::uint32_t     PixelX = 0u;
        std::uint32_t     PixelY = 0u;
        SelectionPickKind Kind   = SelectionPickKind::Click;
        SelectionPickMode Mode   = SelectionPickMode::Replace;
    };

    // Default sandbox selection policy knobs (RUNTIME-089 required policy:
    // single-select click, hover outline, additive modifier, clear on
    // background click).
    struct SelectionControllerConfig
    {
        // Combination mode applied to a click pick when no per-request modifier
        // is supplied. The sandbox default is single-select.
        SelectionPickMode ClickMode = SelectionPickMode::Replace;
        // Clear the whole selection when a Replace-mode click resolves to the
        // background (no hit). Add / Toggle background clicks never clear.
        bool ClearSelectionOnBackgroundClick = true;
        // Clear the hovered entity when a hover pick resolves to the background.
        bool ClearHoverOnBackgroundHover = true;
    };

    // Counters the controller surfaces for diagnostics / editor overlays. Every
    // request, drain, readback, and state change increments exactly one field so
    // the totals reconcile (e.g. ReadbacksConsumed == Hits + NoHits).
    struct SelectionControllerDiagnostics
    {
        std::uint32_t HoverRequestsSubmitted    = 0u;
        std::uint32_t ClickRequestsSubmitted    = 0u;
        std::uint32_t PickRequestsCoalesced     = 0u; // incoming events merged into a pending pick
        std::uint32_t PicksDrained              = 0u; // pending picks handed to the renderer seam
        std::uint32_t ReadbacksConsumed         = 0u;
        std::uint32_t Hits                      = 0u;
        std::uint32_t NoHits                    = 0u;
        std::uint32_t StaleEntityHits           = 0u; // hit id no longer a valid entity
        std::uint32_t NonSelectableHitsRejected = 0u; // hit entity lacks SelectableTag
        std::uint32_t SelectionChangesEmitted   = 0u;
        std::uint32_t HoverChangesEmitted       = 0u;
    };

    // Runtime / editor-owned selection controller (RUNTIME-089, Slice A).
    //
    // The controller is the authority for selected / hovered state. Input ports
    // submit hover / click picks; the controller coalesces them into one
    // pending pixel pick per frame (click supersedes hover, latest position
    // wins). Slice B drains the pending pick into the renderer / SelectionSystem
    // before extraction, then feeds the readback result back through
    // `ConsumeHit` / `ConsumeNoHit`. The controller resolves the runtime stable
    // entity id to a live `entt::entity`, rejects stale / non-selectable hits,
    // mutates ECS `SelectedTag` / `HoveredTag` per the documented policy, and
    // maintains the `RenderWorld.Selection` snapshot buffers that Slice B copies
    // into the render world without graphics ever reading live ECS.
    //
    // Layering: this module imports only the promoted ECS registry / handle and
    // selection components; it never imports graphics, platform input, or the
    // renderer. The renderer / SelectionSystem bridge lives in Slice B
    // (`Engine::RunFrame`), keeping graphics reporting-only.
    class SelectionController
    {
    public:
        using Registry     = Extrinsic::ECS::Scene::Registry;
        using EntityHandle = Extrinsic::ECS::EntityHandle;

        SelectionController() = default;
        explicit SelectionController(const SelectionControllerConfig& config) noexcept;

        // --- input-facing request APIs (coalesced into one pending pick) ---
        void RequestHoverPick(std::uint32_t pixelX, std::uint32_t pixelY) noexcept;
        void RequestClickPick(std::uint32_t pixelX, std::uint32_t pixelY) noexcept;
        void RequestClickPick(std::uint32_t pixelX, std::uint32_t pixelY,
                              SelectionPickMode mode) noexcept;

        // --- coalesced pick drain (Slice B feeds the renderer / SelectionSystem) ---
        [[nodiscard]] bool                                HasPendingPick() const noexcept;
        [[nodiscard]] std::optional<PendingSelectionPick> PeekPendingPick() const noexcept;
        // Removes and returns the pending pick, recording it as in-flight so the
        // matching readback applies the correct (hover vs click) mutation.
        std::optional<PendingSelectionPick> ConsumePendingPick() noexcept;

        // --- readback consumption (Slice B feeds SelectionSystem::GetLastPickResult) ---
        // Apply a hit readback for the in-flight pick. `stableEntityId` is the
        // runtime stable entity id (see the lookup seam). If there is no
        // in-flight pick the readback is treated as a click using the configured
        // ClickMode. Stale (no longer valid) and non-selectable hits are
        // rejected without mutating state.
        void ConsumeHit(Registry& registry, std::uint32_t stableEntityId);
        void ConsumeNoHit(Registry& registry);

        // --- programmatic selection (editor / tools; bypasses SelectableTag) ---
        void ClearSelection(Registry& registry);
        bool SetSelectedEntity(Registry& registry, EntityHandle entity);
        bool SetSelectedByStableEntityId(Registry& registry, std::uint32_t stableEntityId);

        // --- selection snapshot data (Slice B copies into RenderWorld.Selection) ---
        [[nodiscard]] std::span<const std::uint32_t> SelectedStableIds() const noexcept;
        [[nodiscard]] bool                           HasHovered() const noexcept;
        [[nodiscard]] std::uint32_t                  HoveredStableId() const noexcept;

        // --- introspection ---
        [[nodiscard]] std::size_t  SelectedCount() const noexcept;
        [[nodiscard]] bool         IsSelected(EntityHandle entity) const noexcept;
        [[nodiscard]] EntityHandle HoveredEntity() const noexcept;

        [[nodiscard]] const SelectionControllerDiagnostics& GetDiagnostics() const noexcept;
        [[nodiscard]] SelectionControllerConfig&            GetConfig() noexcept;
        [[nodiscard]] const SelectionControllerConfig&      GetConfig() const noexcept;

        // --- runtime-owned lookup seam (RUNTIME-092 upgrade point) ---
        // The stable entity id used by extraction / graphics is currently the
        // `entt::entity` value cast to uint32, mirroring
        // `RenderExtractionCache::StableEntityId`. RUNTIME-092 replaces this with
        // a real `StableId` sidecar; centralising the cast here keeps that swap
        // to one place.
        [[nodiscard]] static std::uint32_t ToStableEntityId(EntityHandle entity) noexcept
        {
            return static_cast<std::uint32_t>(entity);
        }
        [[nodiscard]] static EntityHandle ToEntityHandle(std::uint32_t stableEntityId) noexcept
        {
            return static_cast<EntityHandle>(stableEntityId);
        }

    private:
        // Diff `desired` against the current set, sync `SelectedTag`, refresh the
        // snapshot, and bump the change counter (no-op when already equal).
        void SetSelectionSet(Registry& registry, const std::vector<EntityHandle>& desired);
        void ApplyClickSelection(Registry& registry, EntityHandle entity, SelectionPickMode mode);
        void ApplyHover(Registry& registry, EntityHandle entity);
        void ClearHover(Registry& registry);
        void RebuildSnapshot();

        SelectionControllerConfig      m_Config{};
        SelectionControllerDiagnostics m_Diagnostics{};

        std::optional<PendingSelectionPick> m_PendingPick{};
        std::optional<PendingSelectionPick> m_InFlightPick{};

        // Insertion-ordered authoritative selection set; `m_SelectedStableIds`
        // mirrors it for the snapshot span and is rebuilt on every change.
        std::vector<EntityHandle>  m_Selected{};
        std::vector<std::uint32_t> m_SelectedStableIds{};

        EntityHandle m_Hovered    = Extrinsic::ECS::InvalidEntityHandle;
        bool         m_HasHovered = false;
    };
}
