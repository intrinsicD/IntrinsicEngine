#include <cstdint>
#include <optional>

#include <gtest/gtest.h>
#include <entt/entity/registry.hpp>

import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.SelectionController;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Runtime::PendingSelectionPick;
using Extrinsic::Runtime::SelectionController;
using Extrinsic::Runtime::SelectionControllerConfig;
using Extrinsic::Runtime::SelectionPickKind;
using Extrinsic::Runtime::SelectionPickMode;

namespace Sel = Extrinsic::ECS::Components::Selection;

namespace
{
    // Mirrors the controller's lookup seam so tests assert against the same
    // stable-entity-id derivation the runtime uses.
    [[nodiscard]] std::uint32_t StableId(EntityHandle entity)
    {
        return SelectionController::ToStableEntityId(entity);
    }

    [[nodiscard]] EntityHandle MakeSelectable(Registry& registry)
    {
        const EntityHandle entity = registry.Create();
        registry.Raw().emplace<Sel::SelectableTag>(entity);
        return entity;
    }

    [[nodiscard]] bool HasSelectedTag(const Registry& registry, EntityHandle entity)
    {
        return registry.Raw().all_of<Sel::SelectedTag>(entity);
    }

    [[nodiscard]] bool HasHoveredTag(const Registry& registry, EntityHandle entity)
    {
        return registry.Raw().all_of<Sel::HoveredTag>(entity);
    }
}

// --- click selection + snapshot ------------------------------------------

TEST(SelectionController, ClickHitSelectsEntityAndPopulatesSnapshot)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle entity = MakeSelectable(registry);

    controller.RequestClickPick(10u, 20u);
    const std::optional<PendingSelectionPick> pick = controller.ConsumePendingPick();
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->Kind, SelectionPickKind::Click);
    EXPECT_EQ(pick->PixelX, 10u);
    EXPECT_EQ(pick->PixelY, 20u);

    controller.ConsumeHit(registry, StableId(entity));

    EXPECT_TRUE(controller.IsSelected(entity));
    EXPECT_EQ(controller.SelectedCount(), 1u);
    EXPECT_TRUE(HasSelectedTag(registry, entity));

    const auto ids = controller.SelectedStableIds();
    ASSERT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], StableId(entity));

    const auto& diag = controller.GetDiagnostics();
    EXPECT_EQ(diag.ClickRequestsSubmitted, 1u);
    EXPECT_EQ(diag.PicksDrained, 1u);
    EXPECT_EQ(diag.ReadbacksConsumed, 1u);
    EXPECT_EQ(diag.Hits, 1u);
    EXPECT_EQ(diag.SelectionChangesEmitted, 1u);
}

TEST(SelectionController, ReplaceClickSwitchesSelection)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);
    const EntityHandle b = MakeSelectable(registry);

    controller.RequestClickPick(0u, 0u);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(a));
    ASSERT_TRUE(controller.IsSelected(a));

    controller.RequestClickPick(0u, 0u);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(b));

    EXPECT_FALSE(controller.IsSelected(a));
    EXPECT_TRUE(controller.IsSelected(b));
    EXPECT_FALSE(HasSelectedTag(registry, a));
    EXPECT_TRUE(HasSelectedTag(registry, b));
    EXPECT_EQ(controller.SelectedCount(), 1u);
}

TEST(SelectionController, AddModeAccumulatesInInsertionOrder)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);
    const EntityHandle b = MakeSelectable(registry);

    controller.RequestClickPick(0u, 0u, SelectionPickMode::Add);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(a));
    controller.RequestClickPick(0u, 0u, SelectionPickMode::Add);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(b));

    EXPECT_EQ(controller.SelectedCount(), 2u);
    const auto ids = controller.SelectedStableIds();
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], StableId(a));
    EXPECT_EQ(ids[1], StableId(b));

    // Re-adding an already-selected entity is a no-op (no extra change).
    const std::uint32_t before = controller.GetDiagnostics().SelectionChangesEmitted;
    controller.RequestClickPick(0u, 0u, SelectionPickMode::Add);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(a));
    EXPECT_EQ(controller.SelectedCount(), 2u);
    EXPECT_EQ(controller.GetDiagnostics().SelectionChangesEmitted, before);
}

TEST(SelectionController, ToggleModeRemovesSelectedEntity)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);

    controller.RequestClickPick(0u, 0u, SelectionPickMode::Toggle);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(a));
    EXPECT_TRUE(controller.IsSelected(a));

    controller.RequestClickPick(0u, 0u, SelectionPickMode::Toggle);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(a));
    EXPECT_FALSE(controller.IsSelected(a));
    EXPECT_FALSE(HasSelectedTag(registry, a));
    EXPECT_EQ(controller.SelectedCount(), 0u);
}

// --- hover --------------------------------------------------------------

TEST(SelectionController, HoverHitSetsHoverWithoutChangingSelection)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);
    const EntityHandle b = MakeSelectable(registry);

    controller.RequestClickPick(0u, 0u);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(a));

    controller.RequestHoverPick(5u, 5u);
    const auto pick = controller.ConsumePendingPick();
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->Kind, SelectionPickKind::Hover);
    controller.ConsumeHit(registry, StableId(b));

    EXPECT_TRUE(controller.HasHovered());
    EXPECT_EQ(controller.HoveredStableId(), StableId(b));
    EXPECT_TRUE(HasHoveredTag(registry, b));

    // Selection is unchanged by the hover.
    EXPECT_TRUE(controller.IsSelected(a));
    EXPECT_FALSE(controller.IsSelected(b));
    EXPECT_EQ(controller.SelectedCount(), 1u);
}

TEST(SelectionController, BackgroundHoverClearsHover)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);

    controller.RequestHoverPick(0u, 0u);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(a));
    ASSERT_TRUE(controller.HasHovered());

    controller.RequestHoverPick(99u, 99u);
    controller.ConsumePendingPick();
    controller.ConsumeNoHit(registry);

    EXPECT_FALSE(controller.HasHovered());
    EXPECT_FALSE(HasHoveredTag(registry, a));
}

// --- background click ----------------------------------------------------

TEST(SelectionController, BackgroundClickClearsSelectionInReplaceMode)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);

    controller.RequestClickPick(0u, 0u);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(a));
    ASSERT_TRUE(controller.IsSelected(a));

    controller.RequestClickPick(99u, 99u);
    controller.ConsumePendingPick();
    controller.ConsumeNoHit(registry);

    EXPECT_EQ(controller.SelectedCount(), 0u);
    EXPECT_FALSE(HasSelectedTag(registry, a));
}

TEST(SelectionController, BackgroundClickKeepsSelectionInAddMode)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);

    controller.RequestClickPick(0u, 0u);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(a));

    controller.RequestClickPick(99u, 99u, SelectionPickMode::Add);
    controller.ConsumePendingPick();
    controller.ConsumeNoHit(registry);

    EXPECT_TRUE(controller.IsSelected(a));
    EXPECT_EQ(controller.SelectedCount(), 1u);
}

// --- rejection ----------------------------------------------------------

TEST(SelectionController, StaleEntityHitIsRejected)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a  = MakeSelectable(registry);
    const std::uint32_t id = StableId(a);
    registry.Destroy(a);

    controller.RequestClickPick(0u, 0u);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, id);

    EXPECT_EQ(controller.SelectedCount(), 0u);
    EXPECT_EQ(controller.GetDiagnostics().StaleEntityHits, 1u);
    EXPECT_EQ(controller.GetDiagnostics().SelectionChangesEmitted, 0u);
}

TEST(SelectionController, NonSelectableHitIsRejected)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle plain = registry.Create(); // no SelectableTag

    controller.RequestClickPick(0u, 0u);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(plain));

    EXPECT_EQ(controller.SelectedCount(), 0u);
    EXPECT_EQ(controller.GetDiagnostics().NonSelectableHitsRejected, 1u);
    EXPECT_FALSE(HasSelectedTag(registry, plain));
}

// --- coalescing ----------------------------------------------------------

TEST(SelectionController, CoalescesMultipleSameFrameEventsIntoOnePick)
{
    Registry           registry;
    SelectionController controller;

    controller.RequestHoverPick(1u, 1u);
    controller.RequestHoverPick(2u, 2u);
    controller.RequestClickPick(3u, 3u);
    controller.RequestHoverPick(4u, 4u); // dropped: a click is already pending

    ASSERT_TRUE(controller.HasPendingPick());
    const auto pick = controller.ConsumePendingPick();
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->Kind, SelectionPickKind::Click);
    EXPECT_EQ(pick->PixelX, 3u);
    EXPECT_EQ(pick->PixelY, 3u);

    // Exactly one pending pick survives; the draining is single-shot.
    EXPECT_FALSE(controller.HasPendingPick());
    EXPECT_FALSE(controller.ConsumePendingPick().has_value());

    const auto& diag = controller.GetDiagnostics();
    EXPECT_EQ(diag.HoverRequestsSubmitted, 3u);
    EXPECT_EQ(diag.ClickRequestsSubmitted, 1u);
    EXPECT_EQ(diag.PickRequestsCoalesced, 3u); // 3 events merged into the survivor
    EXPECT_EQ(diag.PicksDrained, 1u);
}

TEST(SelectionController, ClickSupersedesPendingHover)
{
    Registry           registry;
    SelectionController controller;

    controller.RequestHoverPick(1u, 1u);
    controller.RequestClickPick(2u, 2u);

    const auto pick = controller.ConsumePendingPick();
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->Kind, SelectionPickKind::Click);
    EXPECT_EQ(pick->PixelX, 2u);
}

TEST(SelectionController, LatestHoverPositionWinsWhileNoClickPending)
{
    Registry           registry;
    SelectionController controller;

    controller.RequestHoverPick(1u, 1u);
    controller.RequestHoverPick(7u, 8u);

    const auto pick = controller.PeekPendingPick();
    ASSERT_TRUE(pick.has_value());
    EXPECT_EQ(pick->Kind, SelectionPickKind::Hover);
    EXPECT_EQ(pick->PixelX, 7u);
    EXPECT_EQ(pick->PixelY, 8u);
}

// --- in-flight kind drives mutation -------------------------------------

TEST(SelectionController, DrainedHoverKindAppliesHoverNotSelection)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);

    controller.RequestHoverPick(0u, 0u);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(a));

    EXPECT_TRUE(controller.HasHovered());
    EXPECT_EQ(controller.SelectedCount(), 0u); // hover never selects
    EXPECT_TRUE(HasHoveredTag(registry, a));
    EXPECT_FALSE(HasSelectedTag(registry, a));
}

// --- programmatic --------------------------------------------------------

TEST(SelectionController, ProgrammaticSelectAndClear)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);

    EXPECT_TRUE(controller.SetSelectedEntity(registry, a));
    EXPECT_TRUE(controller.IsSelected(a));
    EXPECT_TRUE(HasSelectedTag(registry, a));

    controller.ClearSelection(registry);
    EXPECT_EQ(controller.SelectedCount(), 0u);
    EXPECT_FALSE(HasSelectedTag(registry, a));
}

TEST(SelectionController, ProgrammaticSelectRejectsInvalidEntity)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a  = registry.Create();
    const std::uint32_t id = StableId(a);
    registry.Destroy(a);

    EXPECT_FALSE(controller.SetSelectedByStableEntityId(registry, id));
    EXPECT_EQ(controller.SelectedCount(), 0u);
}

// --- multiple in-flight picks (correlation) ------------------------------

TEST(SelectionController, DrainedPickCarriesMonotonicSequence)
{
    Registry           registry;
    SelectionController controller;

    controller.RequestClickPick(0u, 0u);
    const auto first = controller.ConsumePendingPick();
    controller.RequestHoverPick(0u, 0u);
    const auto second = controller.ConsumePendingPick();

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(first->Sequence, 0u);
    EXPECT_LT(first->Sequence, second->Sequence);
    EXPECT_EQ(controller.InFlightPickCount(), 2u);
    EXPECT_EQ(controller.OldestInFlightSequence(), first->Sequence);
}

// Regression: two picks drained before any readback must each replay their own
// kind/mode. Previously a single in-flight slot was overwritten, so the click's
// result would be replayed with the hover's kind (selecting nothing) and the
// hover's result would fall back to the default click (wrongly selecting).
TEST(SelectionController, MultipleInFlightPicksReplayOwnKindBySequence)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);
    const EntityHandle b = MakeSelectable(registry);

    controller.RequestClickPick(1u, 1u); // click on a
    const auto clickPick = controller.ConsumePendingPick();
    controller.RequestHoverPick(2u, 2u); // hover over b
    const auto hoverPick = controller.ConsumePendingPick();
    ASSERT_TRUE(clickPick.has_value());
    ASSERT_TRUE(hoverPick.has_value());
    EXPECT_EQ(controller.InFlightPickCount(), 2u);

    // Resolve the click first: a is selected, nothing hovered.
    controller.ConsumeHit(registry, StableId(a), clickPick->Sequence);
    EXPECT_TRUE(controller.IsSelected(a));
    EXPECT_FALSE(controller.HasHovered());

    // Resolve the hover: b becomes hovered, selection unchanged (still {a}).
    controller.ConsumeHit(registry, StableId(b), hoverPick->Sequence);
    EXPECT_TRUE(controller.IsSelected(a));
    EXPECT_FALSE(controller.IsSelected(b));
    EXPECT_TRUE(controller.HasHovered());
    EXPECT_EQ(controller.HoveredStableId(), StableId(b));
    EXPECT_EQ(controller.InFlightPickCount(), 0u);
}

TEST(SelectionController, InFlightPicksResolveCorrectlyOutOfOrder)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);
    const EntityHandle b = MakeSelectable(registry);

    controller.RequestHoverPick(1u, 1u); // hover over a
    const auto hoverPick = controller.ConsumePendingPick();
    controller.RequestClickPick(2u, 2u); // click on b
    const auto clickPick = controller.ConsumePendingPick();
    ASSERT_TRUE(hoverPick.has_value());
    ASSERT_TRUE(clickPick.has_value());

    // Publish the younger (click) result before the older (hover) result.
    controller.ConsumeHit(registry, StableId(b), clickPick->Sequence);
    EXPECT_TRUE(controller.IsSelected(b));
    EXPECT_FALSE(controller.HasHovered());

    controller.ConsumeHit(registry, StableId(a), hoverPick->Sequence);
    EXPECT_TRUE(controller.HasHovered());
    EXPECT_EQ(controller.HoveredStableId(), StableId(a));
    EXPECT_TRUE(controller.IsSelected(b)); // click selection preserved
    EXPECT_FALSE(controller.IsSelected(a));
}

TEST(SelectionController, ConvenienceOverloadConsumesOldestInFlight)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);
    const EntityHandle b = MakeSelectable(registry);

    controller.RequestClickPick(0u, 0u); // a (oldest)
    controller.ConsumePendingPick();
    controller.RequestHoverPick(0u, 0u); // b
    controller.ConsumePendingPick();

    // No-sequence overload resolves the oldest tracked pick first (the click).
    controller.ConsumeHit(registry, StableId(a));
    EXPECT_TRUE(controller.IsSelected(a));
    EXPECT_FALSE(controller.HasHovered());

    controller.ConsumeHit(registry, StableId(b));
    EXPECT_TRUE(controller.HasHovered());
    EXPECT_EQ(controller.HoveredStableId(), StableId(b));
    EXPECT_EQ(controller.InFlightPickCount(), 0u);
}

TEST(SelectionController, InFlightCapacityEvictsOldestUnresolved)
{
    Registry                  registry;
    SelectionControllerConfig config{};
    config.MaxTrackedInFlightPicks = 2u;
    SelectionController controller(config);

    controller.RequestClickPick(0u, 0u);
    const auto first = controller.ConsumePendingPick();
    controller.RequestClickPick(0u, 0u);
    controller.ConsumePendingPick();
    controller.RequestClickPick(0u, 0u);
    const auto third = controller.ConsumePendingPick();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(third.has_value());

    EXPECT_EQ(controller.InFlightPickCount(), 2u);
    EXPECT_EQ(controller.GetDiagnostics().InFlightPicksDropped, 1u);
    // The first (oldest) pick was evicted; resolving its sequence is untracked.
    const EntityHandle a = MakeSelectable(registry);
    controller.ConsumeHit(registry, StableId(a), first->Sequence);
    EXPECT_EQ(controller.GetDiagnostics().UntrackedReadbacks, 1u);
}

TEST(SelectionController, UntrackedReadbackAppliesAsClick)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);

    // No pick was drained: a stray readback is applied as a click.
    controller.ConsumeHit(registry, StableId(a));
    EXPECT_TRUE(controller.IsSelected(a));
    EXPECT_EQ(controller.GetDiagnostics().UntrackedReadbacks, 1u);
}

// --- diagnostics reconcile ----------------------------------------------

TEST(SelectionController, ReadbackCountersReconcile)
{
    Registry           registry;
    SelectionController controller;
    const EntityHandle a = MakeSelectable(registry);

    controller.RequestClickPick(0u, 0u);
    controller.ConsumePendingPick();
    controller.ConsumeHit(registry, StableId(a));

    controller.RequestClickPick(0u, 0u);
    controller.ConsumePendingPick();
    controller.ConsumeNoHit(registry);

    const auto& diag = controller.GetDiagnostics();
    EXPECT_EQ(diag.ReadbacksConsumed, diag.Hits + diag.NoHits);
    EXPECT_EQ(diag.Hits, 1u);
    EXPECT_EQ(diag.NoHits, 1u);
}
