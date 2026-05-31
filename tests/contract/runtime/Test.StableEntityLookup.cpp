#include <array>
#include <cstdint>
#include <optional>

#include <gtest/gtest.h>
#include <entt/entity/registry.hpp>

import Extrinsic.ECS.Component.StableId;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.StableEntityLookup;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::ECS::Components::StableId;
using Extrinsic::Runtime::StableEntityLookup;

namespace
{
    [[nodiscard]] EntityHandle MakeWithStableId(Registry& registry, StableId id)
    {
        const EntityHandle entity = registry.Create();
        registry.Raw().emplace<StableId>(entity, id);
        return entity;
    }

    [[nodiscard]] std::uint32_t RenderId(EntityHandle entity)
    {
        return StableEntityLookup::ToRenderId(entity);
    }
}

// --- valid StableId resolution -------------------------------------------

TEST(StableEntityLookup, ResolvesEntityWithValidStableId)
{
    Registry           registry;
    StableEntityLookup lookup;
    const StableId     id{0x1122u, 0x3344u};
    const EntityHandle entity = MakeWithStableId(registry, id);

    lookup.Rebuild(registry);

    EXPECT_EQ(lookup.StableIdCount(), 1u);
    EXPECT_TRUE(lookup.ContainsStableId(id));

    const std::optional<EntityHandle> resolved = lookup.ResolveByStableId(registry, id);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, entity);

    const auto& diag = lookup.GetDiagnostics();
    EXPECT_EQ(diag.Rebuilds, 1u);
    EXPECT_EQ(diag.TrackedStableIds, 1u);
    EXPECT_EQ(diag.MissingStableIdLookups, 0u);
    EXPECT_EQ(diag.StaleEntityResolves, 0u);
}

TEST(StableEntityLookup, MissingAndSentinelLookupsAreCountedAndReturnNullopt)
{
    Registry           registry;
    StableEntityLookup lookup;
    lookup.Rebuild(registry);

    EXPECT_FALSE(lookup.ResolveByStableId(registry, StableId{7u, 8u}).has_value());
    // The sentinel (kInvalidStableId) is never a tracked durable id.
    EXPECT_FALSE(lookup.ResolveByStableId(registry, StableId{0u, 0u}).has_value());

    EXPECT_EQ(lookup.GetDiagnostics().MissingStableIdLookups, 2u);
}

TEST(StableEntityLookup, SentinelStableIdIsNotTracked)
{
    Registry           registry;
    StableEntityLookup lookup;
    // An entity carrying the sentinel id contributes nothing to the map.
    (void)MakeWithStableId(registry, StableId{0u, 0u});

    lookup.Rebuild(registry);

    EXPECT_EQ(lookup.StableIdCount(), 0u);
    EXPECT_EQ(lookup.GetDiagnostics().TrackedStableIds, 0u);
}

// --- incremental maintenance ---------------------------------------------

TEST(StableEntityLookup, TrackFoldsSingleEntityAndForgetDropsIt)
{
    Registry           registry;
    StableEntityLookup lookup;
    const StableId     id{42u, 99u};
    const EntityHandle entity = MakeWithStableId(registry, id);

    EXPECT_TRUE(lookup.Track(registry, entity));
    EXPECT_EQ(lookup.StableIdCount(), 1u);
    ASSERT_TRUE(lookup.ResolveByStableId(registry, id).has_value());

    // Re-tracking the same entity is idempotent (no duplicate counted).
    EXPECT_TRUE(lookup.Track(registry, entity));
    EXPECT_EQ(lookup.GetDiagnostics().DuplicateStableIds, 0u);
    EXPECT_EQ(lookup.GetDiagnostics().IncrementalTracks, 2u);

    lookup.Forget(entity);
    EXPECT_EQ(lookup.StableIdCount(), 0u);
    EXPECT_FALSE(lookup.ResolveByStableId(registry, id).has_value());
    EXPECT_EQ(lookup.GetDiagnostics().IncrementalForgets, 1u);
}

TEST(StableEntityLookup, TrackAfterStableIdReassignmentDropsOldWinnerEntry)
{
    Registry           registry;
    StableEntityLookup lookup;
    const StableId     oldId{0x100u, 0u};
    const StableId     newId{0x200u, 0u};
    const EntityHandle entity = MakeWithStableId(registry, oldId);

    EXPECT_TRUE(lookup.Track(registry, entity));
    ASSERT_TRUE(lookup.ResolveByStableId(registry, oldId).has_value());

    // Hot-reload / undo / editor reassigns the durable id, then incrementally
    // re-tracks (rather than doing a full Rebuild).
    registry.Raw().emplace_or_replace<StableId>(entity, newId);
    EXPECT_TRUE(lookup.Track(registry, entity));

    // The old id must no longer resolve to this entity, and only the new id
    // remains tracked.
    EXPECT_FALSE(lookup.ResolveByStableId(registry, oldId).has_value());
    const std::optional<EntityHandle> resolved = lookup.ResolveByStableId(registry, newId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, entity);
    EXPECT_EQ(lookup.StableIdCount(), 1u);
    EXPECT_FALSE(lookup.ContainsStableId(oldId));
}

TEST(StableEntityLookup, ForgetAfterReassignmentLeavesNoStaleEntry)
{
    Registry           registry;
    StableEntityLookup lookup;
    const StableId     oldId{11u, 0u};
    const StableId     newId{22u, 0u};
    const EntityHandle entity = MakeWithStableId(registry, oldId);

    lookup.Track(registry, entity);
    registry.Raw().emplace_or_replace<StableId>(entity, newId);
    lookup.Track(registry, entity);

    // Forget must reach the (single, reconciled) winner entry and leave nothing
    // behind for either id.
    lookup.Forget(entity);
    EXPECT_EQ(lookup.StableIdCount(), 0u);
    EXPECT_FALSE(lookup.ResolveByStableId(registry, oldId).has_value());
    EXPECT_FALSE(lookup.ResolveByStableId(registry, newId).has_value());
}

TEST(StableEntityLookup, TrackIgnoresEntityWithoutValidStableId)
{
    Registry           registry;
    StableEntityLookup lookup;

    const EntityHandle noComponent = registry.Create();
    EXPECT_FALSE(lookup.Track(registry, noComponent));

    const EntityHandle sentinel = MakeWithStableId(registry, StableId{0u, 0u});
    EXPECT_FALSE(lookup.Track(registry, sentinel));

    EXPECT_EQ(lookup.StableIdCount(), 0u);
    EXPECT_EQ(lookup.GetDiagnostics().IncrementalTracks, 0u);
}

// --- duplicate StableId policy -------------------------------------------

TEST(StableEntityLookup, DuplicateStableIdKeepsSmallestRenderIdWinnerDeterministically)
{
    Registry           registry;
    StableEntityLookup lookup;
    const StableId     shared{0xABCDu, 0xEF01u};

    // Two distinct live entities claim the same durable id.
    const EntityHandle first  = MakeWithStableId(registry, shared);
    const EntityHandle second = MakeWithStableId(registry, shared);

    const EntityHandle smaller = RenderId(first) <= RenderId(second) ? first : second;

    lookup.Rebuild(registry);

    EXPECT_EQ(lookup.StableIdCount(), 1u);
    EXPECT_EQ(lookup.GetDiagnostics().DuplicateStableIds, 1u);

    const std::optional<EntityHandle> resolved = lookup.ResolveByStableId(registry, shared);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, smaller);
}

TEST(StableEntityLookup, IncrementalTrackOrderDoesNotChangeDuplicateWinner)
{
    Registry           registry;
    StableEntityLookup lookup;
    const StableId     shared{5u, 6u};

    const EntityHandle first  = MakeWithStableId(registry, shared);
    const EntityHandle second = MakeWithStableId(registry, shared);
    const EntityHandle winner = RenderId(first) <= RenderId(second) ? first : second;

    // Track the larger-render-id entity first; the smaller must still win.
    const EntityHandle loser = (winner == first) ? second : first;
    lookup.Track(registry, loser);
    lookup.Track(registry, winner);

    EXPECT_EQ(lookup.GetDiagnostics().DuplicateStableIds, 1u);
    const std::optional<EntityHandle> resolved = lookup.ResolveByStableId(registry, shared);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, winner);
}

// --- stale entity invalidation -------------------------------------------

TEST(StableEntityLookup, StaleEntityResolutionInvalidatesDeterministically)
{
    Registry           registry;
    StableEntityLookup lookup;
    const StableId     id{0xDEADu, 0xBEEFu};
    const EntityHandle entity = MakeWithStableId(registry, id);

    lookup.Rebuild(registry);
    ASSERT_TRUE(lookup.ResolveByStableId(registry, id).has_value());

    // Destroy the entity without a matching Forget; the next resolve must
    // reject and lazily self-heal the stale winner entry.
    registry.Destroy(entity);

    EXPECT_FALSE(lookup.ResolveByStableId(registry, id).has_value());
    EXPECT_EQ(lookup.StableIdCount(), 0u);

    const auto& diag = lookup.GetDiagnostics();
    EXPECT_EQ(diag.StaleEntityResolves, 1u);
    EXPECT_EQ(diag.StaleEntriesPruned, 1u);

    // A second resolve now reports a plain miss (entry already healed away).
    EXPECT_FALSE(lookup.ResolveByStableId(registry, id).has_value());
    EXPECT_EQ(lookup.GetDiagnostics().MissingStableIdLookups, 1u);
}

TEST(StableEntityLookup, PruneStaleDropsDestroyedWinnersInBulk)
{
    Registry           registry;
    StableEntityLookup lookup;
    const EntityHandle keep    = MakeWithStableId(registry, StableId{1u, 1u});
    const EntityHandle dropOne = MakeWithStableId(registry, StableId{2u, 2u});
    const EntityHandle dropTwo = MakeWithStableId(registry, StableId{3u, 3u});

    lookup.Rebuild(registry);
    EXPECT_EQ(lookup.StableIdCount(), 3u);

    registry.Destroy(dropOne);
    registry.Destroy(dropTwo);

    EXPECT_EQ(lookup.PruneStale(registry), 2u);
    EXPECT_EQ(lookup.StableIdCount(), 1u);
    EXPECT_EQ(lookup.GetDiagnostics().StaleEntriesPruned, 2u);

    ASSERT_TRUE(lookup.ResolveByStableId(registry, StableId{1u, 1u}).has_value());
    EXPECT_EQ(*lookup.ResolveByStableId(registry, StableId{1u, 1u}), keep);
}

// --- render-id (extraction stable id) compatibility ----------------------

TEST(StableEntityLookup, ResolvesByEnttBackedRenderId)
{
    Registry           registry;
    StableEntityLookup lookup;
    const EntityHandle entity = registry.Create(); // no StableId needed for render-id path

    const std::uint32_t renderId = RenderId(entity);
    const std::optional<EntityHandle> resolved = lookup.ResolveByRenderId(registry, renderId);
    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(*resolved, entity);
    EXPECT_EQ(StableEntityLookup::ToEntityHandle(renderId), entity);
}

TEST(StableEntityLookup, RenderIdOfDestroyedEntityIsRejectedAsStale)
{
    Registry           registry;
    StableEntityLookup lookup;
    const EntityHandle entity   = registry.Create();
    const std::uint32_t renderId = RenderId(entity);

    registry.Destroy(entity);

    EXPECT_FALSE(lookup.ResolveByRenderId(registry, renderId).has_value());
    EXPECT_EQ(lookup.GetDiagnostics().StaleEntityResolves, 1u);
}

// --- batch resolution for selection consumers ----------------------------

TEST(StableEntityLookup, ResolveSelectedReturnsLiveSubsetInInputOrder)
{
    Registry           registry;
    StableEntityLookup lookup;
    const StableId     a{10u, 0u};
    const StableId     b{20u, 0u};
    const StableId     c{30u, 0u};
    const EntityHandle ea = MakeWithStableId(registry, a);
    const EntityHandle eb = MakeWithStableId(registry, b);
    const EntityHandle ec = MakeWithStableId(registry, c);

    lookup.Rebuild(registry);
    registry.Destroy(eb); // middle id goes stale

    const std::array<StableId, 4> request{a, b, c, StableId{99u, 99u}};
    const std::vector<EntityHandle> live = lookup.ResolveSelected(registry, request);

    ASSERT_EQ(live.size(), 2u);
    EXPECT_EQ(live[0], ea);
    EXPECT_EQ(live[1], ec);

    const auto& diag = lookup.GetDiagnostics();
    EXPECT_EQ(diag.StaleEntityResolves, 1u);   // b
    EXPECT_EQ(diag.MissingStableIdLookups, 1u); // {99,99}
    (void)eb;
}

// --- rebuild / clear semantics -------------------------------------------

TEST(StableEntityLookup, RebuildReplacesPriorStateAndClearEmptiesMap)
{
    Registry           registry;
    StableEntityLookup lookup;
    (void)MakeWithStableId(registry, StableId{1u, 0u});
    lookup.Rebuild(registry);
    EXPECT_EQ(lookup.StableIdCount(), 1u);

    (void)MakeWithStableId(registry, StableId{2u, 0u});
    lookup.Rebuild(registry);
    EXPECT_EQ(lookup.StableIdCount(), 2u);
    EXPECT_EQ(lookup.GetDiagnostics().Rebuilds, 2u);

    lookup.Clear();
    EXPECT_EQ(lookup.StableIdCount(), 0u);
    EXPECT_EQ(lookup.GetDiagnostics().TrackedStableIds, 0u);
    EXPECT_FALSE(lookup.ResolveByStableId(registry, StableId{1u, 0u}).has_value());
}
