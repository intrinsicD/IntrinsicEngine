module;

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

export module Extrinsic.Runtime.SelectionController;

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.StableEntityLookup;

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
    //
    // `Sequence` is the correlation token: it is `0` while the pick is the
    // (not-yet-drained) coalescing slot and is assigned a unique, monotonically
    // increasing value by `ConsumePendingPick`. Because GPU picking runs several
    // frames in flight, more than one pick can be outstanding before the first
    // readback is published; Slice B threads this token from the drained pick
    // through to the matching readback so `ConsumeHit` / `ConsumeNoHit` replay
    // the correct request's kind / mode instead of whichever pick was drained
    // last. (The renderer's `DrainCompletedPickingSlots` publishes per-slot
    // results into `SelectionSystem`'s single last-result holder and is not
    // guaranteed to publish in issue order, so positional correlation is unsafe.)
    struct PendingSelectionPick
    {
        std::uint64_t     Sequence = 0u;
        std::uint32_t     PixelX   = 0u;
        std::uint32_t     PixelY   = 0u;
        SelectionPickKind Kind     = SelectionPickKind::Click;
        SelectionPickMode Mode     = SelectionPickMode::Replace;
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
        // Upper bound on picks tracked as in-flight (drained but not yet
        // resolved by a readback). When a new drain would exceed this, the
        // oldest tracked pick is dropped and `InFlightPicksDropped` is bumped,
        // so a readback that is never published (e.g. a recycled / invalidated
        // GPU slot) cannot grow the queue without bound. The default comfortably
        // exceeds any realistic frames-in-flight depth.
        std::size_t MaxTrackedInFlightPicks = 16u;
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
        std::uint32_t InFlightPicksDropped      = 0u; // tracked picks evicted unresolved (capacity)
        std::uint32_t UntrackedReadbacks        = 0u; // readbacks with no matching in-flight pick
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
        // Removes and returns the pending pick, assigning it a unique `Sequence`
        // and tracking it as in-flight so the matching readback can replay the
        // correct (hover vs click) mutation. Returns the drained pick (with its
        // assigned `Sequence`) so the caller can correlate the eventual readback.
        std::optional<PendingSelectionPick> ConsumePendingPick() noexcept;

        // --- readback consumption (Slice B feeds SelectionSystem::GetLastPickResult) ---
        // Apply a hit / miss readback for a specific in-flight pick identified by
        // its `pickSequence` (the value `ConsumePendingPick` returned). The
        // matching tracked pick supplies the kind / mode and is then released. A
        // sequence that is not tracked (already resolved, evicted, or `0`) is
        // treated as an untracked readback: the result is applied as a click in
        // the configured `ClickMode` and `UntrackedReadbacks` is bumped. Hits
        // resolve `stableEntityId` through the lookup seam; stale (no longer
        // valid) and non-selectable hits are rejected without mutating state.
        void ConsumeHit(Registry& registry, std::uint32_t stableEntityId, std::uint64_t pickSequence);
        void ConsumeNoHit(Registry& registry, std::uint64_t pickSequence);
        // Convenience overloads for callers with at most one pick outstanding (or
        // that resolve strictly in drain order): they consume the oldest tracked
        // in-flight pick. Prefer the sequence-correlated overloads whenever more
        // than one pick can be in flight.
        void ConsumeHit(Registry& registry, std::uint32_t stableEntityId);
        void ConsumeNoHit(Registry& registry);

        // --- programmatic selection (editor / tools; bypasses SelectableTag) ---
        void ClearSelection(Registry& registry);
        // Scene replacement boundary: drop all scene-local selected/hovered state
        // and outstanding pick correlation so stale entity handles cannot cross
        // load/new/close operations.
        void ClearSceneState(Registry& registry);
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
        [[nodiscard]] std::size_t                  InFlightPickCount() const noexcept;
        [[nodiscard]] std::optional<std::uint64_t> OldestInFlightSequence() const noexcept;

        [[nodiscard]] const SelectionControllerDiagnostics& GetDiagnostics() const noexcept;
        [[nodiscard]] SelectionControllerConfig&            GetConfig() noexcept;
        [[nodiscard]] const SelectionControllerConfig&      GetConfig() const noexcept;

        // --- runtime-owned lookup seam (RUNTIME-092) ---
        // The stable entity id used by extraction / graphics is the
        // `entt::entity` value cast to uint32, mirroring
        // `RenderExtractionCache::StableEntityId`. The bare cast is the
        // identity encoding; centralising it here keeps the encode/decode in
        // one place.
        [[nodiscard]] static std::uint32_t ToStableEntityId(EntityHandle entity) noexcept
        {
            return static_cast<std::uint32_t>(entity);
        }
        [[nodiscard]] static EntityHandle ToEntityHandle(std::uint32_t stableEntityId) noexcept
        {
            return static_cast<EntityHandle>(stableEntityId);
        }

        // RUNTIME-092 Slice B: route render-id resolution through the
        // runtime-owned `StableEntityLookup` sidecar. When a lookup is attached
        // the controller resolves an incoming render id with
        // `ResolveByRenderId` (which decodes the handle *and* validates it
        // against the registry, so a recycled/destroyed slot is rejected by the
        // single runtime-owned authority and counted in the lookup diagnostics)
        // instead of the bare cast. When no lookup is attached (the controller's
        // standalone unit/contract use) resolution falls back to
        // `ToEntityHandle` + the controller's own validity check, so existing
        // direct-drive callers are unaffected. The controller does not own the
        // lookup's lifetime; `Engine` owns it and rebuilds it each frame before
        // readback consumption.
        void SetStableEntityLookup(StableEntityLookup* lookup) noexcept;
        [[nodiscard]] const StableEntityLookup* GetStableEntityLookup() const noexcept;

    private:
        // Diff `desired` against the current set, sync `SelectedTag`, refresh the
        // snapshot, and bump the change counter (no-op when already equal).
        void SetSelectionSet(Registry& registry, const std::vector<EntityHandle>& desired);
        void ApplyClickSelection(Registry& registry, EntityHandle entity, SelectionPickMode mode);
        void ApplyHover(Registry& registry, EntityHandle entity);
        void ClearHover(Registry& registry);
        void RebuildSnapshot();
        // Release the tracked in-flight pick matching `pickSequence` (or the
        // oldest when `pickSequence` is empty) and return its kind / mode. Falls
        // back to a click in the configured mode (bumping `UntrackedReadbacks`)
        // when nothing matches.
        [[nodiscard]] PendingSelectionPick TakeInFlightPick(std::optional<std::uint64_t> pickSequence) noexcept;
        void ApplyHitReadback(Registry& registry, std::uint32_t stableEntityId,
                              std::optional<std::uint64_t> pickSequence);
        void ApplyNoHitReadback(Registry& registry, std::optional<std::uint64_t> pickSequence);
        // Resolve an incoming render/extraction stable id to a live entity
        // through the attached `StableEntityLookup` (if any), else via the bare
        // decode + the registry validity check. Returns `InvalidEntityHandle`
        // for a stale / recycled / invalid id.
        [[nodiscard]] EntityHandle ResolveStableEntityId(Registry& registry,
                                                         std::uint32_t stableEntityId);

        SelectionControllerConfig      m_Config{};
        SelectionControllerDiagnostics m_Diagnostics{};

        std::optional<PendingSelectionPick> m_PendingPick{};
        // Drained-but-unresolved picks, ordered oldest-first. Tracked by unique
        // `Sequence` so a readback resolves the exact request that produced it,
        // even with several picks in flight and out-of-order publication.
        std::deque<PendingSelectionPick> m_InFlightPicks{};
        std::uint64_t                    m_NextPickSequence = 1u;

        // Insertion-ordered authoritative selection set; `m_SelectedStableIds`
        // mirrors it for the snapshot span and is rebuilt on every change.
        std::vector<EntityHandle>  m_Selected{};
        std::vector<std::uint32_t> m_SelectedStableIds{};

        EntityHandle m_Hovered    = Extrinsic::ECS::InvalidEntityHandle;
        bool         m_HasHovered = false;

        // Non-owning. `Engine` owns the lookup and rebuilds it each frame before
        // readback consumption; null in the controller's standalone use.
        StableEntityLookup* m_StableLookup = nullptr;
    };
}
