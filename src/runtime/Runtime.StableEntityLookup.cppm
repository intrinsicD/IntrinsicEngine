module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

export module Extrinsic.Runtime.StableEntityLookup;

import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;

export namespace Extrinsic::Runtime
{
    // Render id reserved for "no entity": the GPU selection-ID targets clear to
    // 0 and the picking readback drain treats `EntityId == 0` as background, so
    // no live entity may ever encode to it (BUG-026).
    inline constexpr std::uint32_t kBackgroundRenderId = 0u;

    // Diagnostics the lookup surfaces for editor overlays / tests.
    //
    // `TrackedStableIds` is *state* (it mirrors the current winner-map size);
    // every other field is a *cumulative event counter* that only ever grows so
    // that test assertions over a known sequence of operations are stable.
    struct StableEntityLookupDiagnostics
    {
        std::uint32_t TrackedStableIds      = 0u; // current winner entries (state, not cumulative)
        std::uint32_t Rebuilds              = 0u; // full Rebuild() calls
        std::uint32_t IncrementalTracks     = 0u; // Track() calls that observed a valid StableId
        std::uint32_t IncrementalForgets    = 0u; // Forget() calls that dropped a winner entry
        std::uint32_t DuplicateStableIds    = 0u; // duplicate StableId occurrences not made winner
        std::uint32_t MissingStableIdLookups = 0u; // resolve found no entry (or sentinel id)
        std::uint32_t StaleEntityResolves   = 0u; // entry/handle no longer a valid live entity
        std::uint32_t StaleEntriesPruned     = 0u; // winner entries dropped by PruneStale/lazy heal
    };

    // Runtime-owned scene-local lookup sidecar (RUNTIME-092).
    //
    // `HARDEN-068` Decision 3 left ECS owning only the `StableId` value type and
    // deferred any `StableId -> entt::entity` lookup to a runtime consumer. This
    // sidecar is that consumer: it maps the optional, durable
    // `ECS::Components::StableId` of an entity to its current live
    // `entt::entity`, so selection, serialization-adjacent tooling, and sandbox
    // UI can resolve durable identities without widening ECS dependencies or
    // putting lookup state in graphics.
    //
    // Two resolution paths exist:
    //
    //  * **By `StableId`** — a stored, maintained winner-map. `StableId` is
    //    independent of the `entt::entity` bit pattern (it survives recycling /
    //    save-load), so the mapping must be materialized.
    //  * **By render/extraction stable id** (`std::uint32_t`) — the id
    //    `RenderExtractionCache::StableEntityId` / `SelectionController` emit is
    //    `static_cast<std::uint32_t>(entt::entity) + 1`, a reversible encoding
    //    of the live handle (index + version) shifted so render id 0 stays
    //    reserved for the GPU background sentinel (`kBackgroundRenderId`; the
    //    first entity of a fresh registry casts to 0 and would otherwise be
    //    unpickable — BUG-026). `entt::null` (0xFFFFFFFF) wraps to 0 under the
    //    shift, so "no entity" and "background" coincide. Resolution decodes
    //    the handle and validates it against the registry; no separate
    //    container is stored because the id *is* the key. A recycled slot
    //    carries a bumped version, so a stale render id fails
    //    `registry.IsValid` and is rejected.
    //
    // Duplicate-`StableId` policy: **keep one deterministic winner** — the live
    // entity with the smallest `ToRenderId` value wins, independent of insertion
    // / iteration order, and each duplicate occurrence bumps `DuplicateStableIds`.
    // Losing duplicates are not retained; a `Rebuild` re-derives winners and
    // `Forget` of a winner drops the mapping until the next `Rebuild`/`Track`.
    // The sentinel `kInvalidStableId` is never tracked (transient entities skip
    // the 16-byte cost, per the `StableId` contract).
    //
    // Stale handling: a winner whose entity is destroyed without a matching
    // `Forget` is detected lazily on resolve (rejected, erased, and counted as
    // `StaleEntityResolves`) or in bulk by `PruneStale`.
    //
    // Layering: this module imports only the promoted ECS registry / handle and
    // the `StableId` value type; it never imports graphics or platform. ECS keeps
    // no registry-wide lookup map. Wiring the update into the runtime frame /
    // extraction lifecycle before selection consumption is owned by Slice B.
    //
    // Thread model: single-threaded, matching `ECS::Scene::Registry`.
    class StableEntityLookup
    {
    public:
        using Registry     = Extrinsic::ECS::Scene::Registry;
        using EntityHandle = Extrinsic::ECS::EntityHandle;
        using StableId     = Extrinsic::ECS::Components::StableId;

        StableEntityLookup() = default;

        // --- maintenance ---------------------------------------------------
        // Drop all entries and rebuild the winner-map from every live entity
        // that carries a valid `StableId` component. Deterministic regardless of
        // registry iteration order.
        void Rebuild(const Registry& registry);
        // Incrementally fold a single entity into the winner-map. Reads the
        // entity's `StableId` component if present; a no-op when the entity is
        // invalid or carries no valid `StableId`. Returns true when a winner
        // entry was created or changed.
        bool Track(const Registry& registry, EntityHandle entity);
        // Drop the winner entry whose value equals `entity` (if any). A loser
        // duplicate is not tracked, so forgetting one is a no-op.
        void Forget(EntityHandle entity);
        void Clear() noexcept;

        // --- resolution ----------------------------------------------------
        [[nodiscard]] std::optional<EntityHandle> ResolveByStableId(const Registry& registry,
                                                                    StableId id);
        [[nodiscard]] std::optional<EntityHandle> ResolveByRenderId(const Registry& registry,
                                                                    std::uint32_t renderId);
        // Resolve a batch of durable ids to the subset that is still live, in
        // input order. Missing / stale ids are skipped and counted.
        [[nodiscard]] std::vector<EntityHandle> ResolveSelected(const Registry&            registry,
                                                                std::span<const StableId> ids);

        // Drop every winner entry whose entity is no longer valid; returns the
        // number pruned.
        std::size_t PruneStale(const Registry& registry);

        // --- render-id derivation seam (the single authority; mirrored by
        //     RenderExtractionCache / SelectionController::ToStableEntityId) --
        // `+1` keeps render id 0 reserved for the GPU background sentinel;
        // `entt::null` (0xFFFFFFFF) intentionally wraps to 0 = "no entity".
        [[nodiscard]] static std::uint32_t ToRenderId(EntityHandle entity) noexcept
        {
            return static_cast<std::uint32_t>(entity) + 1u;
        }
        [[nodiscard]] static EntityHandle ToEntityHandle(std::uint32_t renderId) noexcept
        {
            if (renderId == kBackgroundRenderId)
                return Extrinsic::ECS::InvalidEntityHandle;
            return static_cast<EntityHandle>(renderId - 1u);
        }

        // --- introspection -------------------------------------------------
        [[nodiscard]] std::size_t StableIdCount() const noexcept { return m_StableIdToEntity.size(); }
        [[nodiscard]] bool        ContainsStableId(StableId id) const noexcept
        {
            return m_StableIdToEntity.find(id) != m_StableIdToEntity.end();
        }
        [[nodiscard]] const StableEntityLookupDiagnostics& GetDiagnostics() const noexcept
        {
            return m_Diagnostics;
        }

    private:
        // Apply the deterministic smallest-render-id-wins dedup for one entry.
        void InsertWinner(EntityHandle entity, StableId id);
        // Erase a winner entry plus its reverse mirror.
        void EraseWinner(StableId id, std::uint32_t renderId);

        // Winner map and a render-id -> StableId reverse mirror (winners only)
        // so Forget / lazy-heal can locate the durable key from a live handle.
        std::unordered_map<StableId, EntityHandle, Extrinsic::ECS::Components::StableIdHash>
            m_StableIdToEntity{};
        std::unordered_map<std::uint32_t, StableId> m_RenderIdToStableId{};

        StableEntityLookupDiagnostics m_Diagnostics{};
    };
}
