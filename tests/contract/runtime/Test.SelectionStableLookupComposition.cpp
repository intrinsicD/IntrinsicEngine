// RUNTIME-092 Slice B — composition coverage for the selection controller and
// the runtime-owned StableEntityLookup sidecar.
//
// Slice B wires `StableEntityLookup` into the runtime frame path: `Engine`
// rebuilds the lookup each frame before the pick-readback drain and attaches it
// to the `SelectionController` so render-id resolution flows through the single
// runtime authority (which decodes the `entt::entity` handle *and* validates it
// against the live registry) rather than a bare `static_cast`. The key property
// the wiring buys is recycling safety: a durable/stale render id that names a
// slot now occupied by a *different* entity must be rejected, never resolve to
// the recycled occupant.
//
// These tests drive the controller + lookup + SelectionSystem directly,
// mirroring the Engine::RunFrame maintenance-phase sequence (Rebuild lookup,
// then drain readbacks by Sequence), so they exercise the composition on a pure
// CPU path without a live GPU picking pass.

#include <cstdint>
#include <optional>

#include <gtest/gtest.h>
#include <entt/entity/registry.hpp>

import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.StableEntityLookup;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::ECS::Components::StableId;
using Extrinsic::Runtime::SelectionController;
using Extrinsic::Runtime::StableEntityLookup;

namespace Sel = Extrinsic::ECS::Components::Selection;
namespace G   = Extrinsic::Graphics;

namespace
{
    [[nodiscard]] EntityHandle MakeSelectable(Registry& registry)
    {
        const EntityHandle entity = registry.Create();
        registry.Raw().emplace<Sel::SelectableTag>(entity);
        return entity;
    }

    [[nodiscard]] std::uint32_t RenderId(EntityHandle entity)
    {
        return SelectionController::ToStableEntityId(entity);
    }

    void PublishHit(G::SelectionSystem& system, std::uint32_t renderId, std::uint64_t sequence)
    {
        system.PublishPickResult(G::PickReadbackResult{
            .EncodedId      = G::EncodeSelectionId(G::SelectionPrimitiveDomain::Entity, 1u),
            .StableEntityId = renderId,
            .Hit            = true,
            .Sequence       = sequence,
        });
    }

    // Mirror Engine::RunFrame's maintenance phase: rebuild the lookup from the
    // live registry, then drain every completed readback FIFO and resolve each
    // by its Sequence.
    void RunFrameDrain(G::SelectionSystem& system,
                       SelectionController& controller,
                       StableEntityLookup&  lookup,
                       Registry&            registry)
    {
        lookup.Rebuild(registry);
        while (const std::optional<G::PickReadbackResult> result = system.PopPickResult())
        {
            if (result->Sequence != 0u)
            {
                if (result->Hit)
                    controller.ConsumeHit(registry, result->StableEntityId, result->Sequence);
                else
                    controller.ConsumeNoHit(registry, result->Sequence);
            }
            else if (result->Hit)
            {
                controller.ConsumeHit(registry, result->StableEntityId);
            }
            else
            {
                controller.ConsumeNoHit(registry);
            }
        }
    }
}

// A live entity's pick hit resolves through the attached lookup and selects it,
// proving the sidecar render-id path is parity-correct for a valid handle.
TEST(SelectionStableLookupComposition, HitResolvesThroughAttachedLookupAndSelects)
{
    Registry            registry;
    SelectionController controller;
    StableEntityLookup  lookup;
    G::SelectionSystem  system;
    system.Initialize();

    controller.SetStableEntityLookup(&lookup);
    EXPECT_EQ(controller.GetStableEntityLookup(), &lookup);

    const EntityHandle target = MakeSelectable(registry);

    controller.RequestClickPick(1u, 1u);
    const std::optional<Extrinsic::Runtime::PendingSelectionPick> pick =
        controller.ConsumePendingPick();
    ASSERT_TRUE(pick.has_value());

    PublishHit(system, RenderId(target), pick->Sequence);
    RunFrameDrain(system, controller, lookup, registry);

    EXPECT_TRUE(controller.IsSelected(target));
    EXPECT_TRUE(registry.Raw().all_of<Sel::SelectedTag>(target));
    EXPECT_EQ(controller.GetDiagnostics().Hits, 1u);
    EXPECT_EQ(controller.GetDiagnostics().StaleEntityHits, 0u);

    system.Shutdown();
}

// The selection-survives-recycling guarantee: a stale render id that names a
// slot since recycled to a *different* live entity must be rejected through the
// lookup (decode + registry validation), never resolve to the recycled
// occupant. Without the sidecar's version-aware validation a bare decode of the
// old id could mis-resolve to the new entity.
TEST(SelectionStableLookupComposition, RecycledSlotRenderIdRejectedAsStaleNotMisapplied)
{
    Registry            registry;
    SelectionController controller;
    StableEntityLookup  lookup;
    G::SelectionSystem  system;
    system.Initialize();

    controller.SetStableEntityLookup(&lookup);

    // Pick the original occupant of a slot and capture its render id.
    const EntityHandle original         = MakeSelectable(registry);
    const std::uint32_t staleRenderId   = RenderId(original);

    // The original is destroyed and its index recycled to a brand-new entity
    // (entt bumps the version, so the new render id differs).
    registry.Destroy(original);
    const EntityHandle recycled = MakeSelectable(registry);
    ASSERT_NE(RenderId(recycled), staleRenderId);

    // A readback carrying the now-stale render id arrives (e.g. a pick issued
    // before the entity was destroyed, completing a few frames later).
    controller.RequestClickPick(2u, 2u);
    const std::optional<Extrinsic::Runtime::PendingSelectionPick> pick =
        controller.ConsumePendingPick();
    ASSERT_TRUE(pick.has_value());

    PublishHit(system, staleRenderId, pick->Sequence);
    RunFrameDrain(system, controller, lookup, registry);

    // Nothing is selected: the stale id is rejected, and it is NOT mis-applied
    // to the entity that now occupies the recycled slot.
    EXPECT_EQ(controller.SelectedCount(), 0u);
    EXPECT_FALSE(controller.IsSelected(recycled));
    EXPECT_FALSE(registry.Raw().all_of<Sel::SelectedTag>(recycled));

    EXPECT_EQ(controller.GetDiagnostics().StaleEntityHits, 1u);
    // The runtime-owned lookup recorded the stale resolution.
    EXPECT_EQ(lookup.GetDiagnostics().StaleEntityResolves, 1u);

    system.Shutdown();
}

// Programmatic selection by render id also routes through the sidecar: a stale
// render id is rejected, a live one selects.
TEST(SelectionStableLookupComposition, SetSelectedByStableEntityIdRoutesThroughLookup)
{
    Registry            registry;
    SelectionController controller;
    StableEntityLookup  lookup;
    controller.SetStableEntityLookup(&lookup);

    const EntityHandle live          = registry.Create();
    const std::uint32_t liveRenderId = RenderId(live);

    EXPECT_TRUE(controller.SetSelectedByStableEntityId(registry, liveRenderId));
    EXPECT_TRUE(controller.IsSelected(live));

    // A render id for a never-created / destroyed handle is rejected.
    const EntityHandle ghost          = registry.Create();
    const std::uint32_t ghostRenderId = RenderId(ghost);
    registry.Destroy(ghost);

    EXPECT_FALSE(controller.SetSelectedByStableEntityId(registry, ghostRenderId));
    EXPECT_GE(lookup.GetDiagnostics().StaleEntityResolves, 1u);
}

// The seam is opt-in: with no lookup attached the controller still resolves via
// the bare decode + its own validity check, so standalone callers are
// unaffected (regression guard for the default path).
TEST(SelectionStableLookupComposition, NoAttachedLookupFallsBackToBareDecode)
{
    Registry            registry;
    SelectionController controller; // no SetStableEntityLookup
    G::SelectionSystem  system;
    system.Initialize();

    EXPECT_EQ(controller.GetStableEntityLookup(), nullptr);

    const EntityHandle target = MakeSelectable(registry);
    controller.RequestClickPick(3u, 3u);
    const std::optional<Extrinsic::Runtime::PendingSelectionPick> pick =
        controller.ConsumePendingPick();
    ASSERT_TRUE(pick.has_value());

    PublishHit(system, RenderId(target), pick->Sequence);
    while (const std::optional<G::PickReadbackResult> result = system.PopPickResult())
        controller.ConsumeHit(registry, result->StableEntityId, result->Sequence);

    EXPECT_TRUE(controller.IsSelected(target));
    system.Shutdown();
}

// The per-frame Rebuild keeps the durable StableId map coherent across entity
// recycling: after a durable id is re-emplaced on a fresh entity and the lookup
// is rebuilt (as RunFrame does each frame), ResolveByStableId names the current
// occupant — the editor/serialization-facing path the sidecar exists for.
TEST(SelectionStableLookupComposition, RebuiltDurableMapTracksRecycledStableIdOwner)
{
    Registry           registry;
    StableEntityLookup lookup;
    const StableId     durable{0xCAFEu, 0xF00Du};

    const EntityHandle first = registry.Create();
    registry.Raw().emplace<StableId>(first, durable);
    lookup.Rebuild(registry);

    const std::optional<EntityHandle> beforeRecycle = lookup.ResolveByStableId(registry, durable);
    ASSERT_TRUE(beforeRecycle.has_value());
    EXPECT_EQ(*beforeRecycle, first);

    // The durable owner is destroyed and a new entity adopts the same durable
    // id (e.g. reload of a serialized scene). A fresh Rebuild — the per-frame
    // maintenance point — re-derives the winner against the live registry.
    registry.Destroy(first);
    const EntityHandle second = registry.Create();
    registry.Raw().emplace<StableId>(second, durable);
    lookup.Rebuild(registry);

    const std::optional<EntityHandle> afterRecycle = lookup.ResolveByStableId(registry, durable);
    ASSERT_TRUE(afterRecycle.has_value());
    EXPECT_EQ(*afterRecycle, second);
    EXPECT_EQ(lookup.StableIdCount(), 1u);
}
