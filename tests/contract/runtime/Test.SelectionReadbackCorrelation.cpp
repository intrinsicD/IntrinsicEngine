// RUNTIME-089 — regression coverage for per-pick readback correlation.
//
// When picks are issued on consecutive frames with more than one frame in
// flight, the renderer can publish several completed picking slots into the
// SelectionSystem during one BeginFrame, and those slots are not guaranteed to
// publish in issue order. The frame-loop bridge must drain ALL completed
// readbacks (FIFO) and resolve each by its correlation Sequence, so a
// hover/click or Add/Toggle result is replayed against the exact request that
// produced it — never the (positionally) oldest in-flight pick.
//
// These tests drive the controller + SelectionSystem directly, mirroring the
// Engine::RunFrame maintenance-phase drain loop, so they exercise the
// correlation without needing a live GPU picking pass.

#include <cstdint>
#include <optional>

#include <gtest/gtest.h>
#include <entt/entity/registry.hpp>

import Extrinsic.ECS.Components.Selection;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Graphics.RenderFrameInput;
import Extrinsic.Platform.Window;
import Extrinsic.Runtime.SelectionReadback;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldHandle;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::Runtime::SelectionController;
using Extrinsic::Runtime::SelectionReadbackState;

namespace Sel = Extrinsic::ECS::Components::Selection;
namespace G = Extrinsic::Graphics;

namespace
{
    constexpr Extrinsic::Runtime::WorldHandle kWorld{7u, 3u};
    constexpr std::uint64_t kEpoch = 11u;

    [[nodiscard]] EntityHandle MakeSelectable(Registry& registry)
    {
        const EntityHandle entity = registry.Create();
        registry.Raw().emplace<Sel::SelectableTag>(entity);
        return entity;
    }

    [[nodiscard]] std::uint32_t StableId(EntityHandle entity)
    {
        return SelectionController::ToStableEntityId(entity);
    }

    [[nodiscard]] bool HasSelectedTag(const Registry& registry, EntityHandle entity)
    {
        return registry.Raw().all_of<Sel::SelectedTag>(entity);
    }

    [[nodiscard]] bool HasHoveredTag(const Registry& registry, EntityHandle entity)
    {
        return registry.Raw().all_of<Sel::HoveredTag>(entity);
    }

    // Publish a hit readback for `entity` tagged with `sequence`, mirroring what
    // the renderer's DrainCompletedPickingSlots emits for a hit slot.
    void PublishHit(G::SelectionSystem& system, EntityHandle entity, std::uint64_t sequence)
    {
        system.PublishPickResult(G::PickReadbackResult{
            .EncodedId      = G::EncodeSelectionId(G::SelectionPrimitiveDomain::Entity, 1u),
            .StableEntityId = StableId(entity),
            .Hit            = true,
            .Sequence       = sequence,
        });
    }

    void DrainReadbacks(SelectionReadbackState& readbacks,
                        G::SelectionSystem& system,
                        SelectionController& controller,
                        Registry& registry,
                        const Extrinsic::Runtime::WorldHandle world = kWorld,
                        const std::uint64_t epoch = kEpoch)
    {
        readbacks.DrainCompletedReadbacksForFrame(
            system, controller, registry, world, epoch);
    }

    [[nodiscard]] std::uint64_t IssuePendingPick(
        SelectionReadbackState& readbacks,
        G::SelectionSystem& system,
        SelectionController& controller,
        const Extrinsic::Runtime::WorldHandle world = kWorld,
        const std::uint64_t epoch = kEpoch)
    {
        G::RenderFrameInput input{};
        readbacks.DrainPendingPickForFrame(
            controller,
            system,
            Extrinsic::Platform::Extent2D{
                .Width = 64,
                .Height = 32,
            },
            input,
            world,
            epoch);
        if (!input.HasPendingPick ||
            input.Pick.Sequence == 0u)
        {
            ADD_FAILURE() << "pending pick was not issued";
            return 0u;
        }
        (void)system.ConsumePick();
        return input.Pick.Sequence;
    }
}

TEST(SelectionReadbackBridge, PendingDrainRequestsRendererPickAndTracksSequence)
{
    SelectionController controller;
    G::SelectionSystem system;
    SelectionReadbackState readbacks;
    system.Initialize();

    controller.RequestClickPick(3u, 4u);
    ASSERT_TRUE(controller.HasPendingPick());

    G::RenderFrameInput input{};
    readbacks.DrainPendingPickForFrame(controller,
                                       system,
                                       Extrinsic::Platform::Extent2D{.Width = 64, .Height = 32},
                                       input,
                                       kWorld,
                                       kEpoch);

    EXPECT_FALSE(controller.HasPendingPick());
    EXPECT_EQ(controller.InFlightPickCount(), 1u);
    EXPECT_TRUE(input.HasPendingPick);
    EXPECT_EQ(input.Pick.X, 3u);
    EXPECT_EQ(input.Pick.Y, 4u);
    EXPECT_TRUE(input.Pick.Pending);
    EXPECT_NE(input.Pick.Sequence, 0u);

    ASSERT_TRUE(system.HasPendingPick());
    const std::optional<G::PickRequest> request = system.ConsumePick();
    ASSERT_TRUE(request.has_value());
    EXPECT_EQ(request->PixelX, 3u);
    EXPECT_EQ(request->PixelY, 4u);

    system.Shutdown();
}

TEST(SelectionReadbackBridge, ZeroSequenceBackgroundIsRejectedBeforeRefinement)
{
    Registry registry;
    SelectionController controller;
    G::SelectionSystem system;
    SelectionReadbackState readbacks;
    system.Initialize();

    system.PublishNoHit();
    DrainReadbacks(readbacks, system, controller, registry);
    EXPECT_FALSE(readbacks.LastRefinedPrimitive().has_value());
    EXPECT_EQ(readbacks.LastRefinedPrimitiveGeneration(), 0u);
    EXPECT_EQ(controller.GetDiagnostics().ReadbacksConsumed, 0u);

    readbacks.ClearRefinedPrimitiveCache();
    EXPECT_FALSE(readbacks.LastRefinedPrimitive().has_value());
    EXPECT_EQ(readbacks.LastRefinedPrimitiveGeneration(), 1u);

    system.Shutdown();
}

TEST(SelectionReadbackBridge,
     SceneClearDropsCorrelationContextBeforeLateReadback)
{
    Registry registry;
    SelectionController controller;
    G::SelectionSystem system;
    SelectionReadbackState readbacks;
    system.Initialize();
    const EntityHandle target = MakeSelectable(registry);

    controller.RequestClickPick(3u, 4u);
    const std::uint64_t sequence =
        IssuePendingPick(readbacks, system, controller);
    ASSERT_NE(sequence, 0u);
    ASSERT_EQ(controller.InFlightPickCount(), 1u);

    // This isolates the readback half of the cohort: clearing its
    // world-bound contexts must reject a late result even while the
    // standalone controller deliberately retains its matching record.
    // SceneInteractionModule::ClearWorldBoundState also calls
    // SelectionController::ClearSceneState, so the composed reset leaves
    // neither half in flight.
    readbacks.ClearSceneState();
    EXPECT_EQ(
        readbacks.LastRefinedPrimitiveGeneration(),
        1u);

    PublishHit(system, target, sequence);
    DrainReadbacks(
        readbacks, system, controller, registry);

    EXPECT_FALSE(controller.IsSelected(target));
    EXPECT_FALSE(HasSelectedTag(registry, target));
    EXPECT_EQ(controller.InFlightPickCount(), 1u);
    EXPECT_EQ(controller.GetDiagnostics().ReadbacksConsumed, 0u);
    EXPECT_FALSE(
        readbacks.LastRefinedPrimitive().has_value());
    EXPECT_EQ(
        readbacks.LastRefinedPrimitiveGeneration(),
        1u);

    controller.ClearSceneState(registry);
    system.Shutdown();
}

// Two picks in flight (a click then a hover); the renderer publishes their
// readbacks out of issue order. Each must apply to the request that produced
// it: the click selects, the hover hovers — not swapped, and neither dropped.
TEST(SelectionReadbackCorrelation, OutOfOrderReadbacksApplyToCorrectRequestBySequence)
{
    Registry            registry;
    SelectionController controller;
    G::SelectionSystem  system;
    SelectionReadbackState readbacks;
    system.Initialize();

    const EntityHandle clickTarget = MakeSelectable(registry);
    const EntityHandle hoverTarget = MakeSelectable(registry);

    // Frame N: issue a click pick.
    controller.RequestClickPick(1u, 1u);
    const std::uint64_t clickSequence =
        IssuePendingPick(readbacks, system, controller);
    ASSERT_NE(clickSequence, 0u);

    // Frame N+1: issue a hover pick. Both are now in flight.
    controller.RequestHoverPick(2u, 2u);
    const std::uint64_t hoverSequence =
        IssuePendingPick(readbacks, system, controller);
    ASSERT_NE(hoverSequence, 0u);
    ASSERT_EQ(controller.InFlightPickCount(), 2u);
    ASSERT_NE(clickSequence, hoverSequence);

    // The renderer publishes the hover result BEFORE the (older) click result.
    PublishHit(system, hoverTarget, hoverSequence);
    PublishHit(system, clickTarget, clickSequence);

    DrainReadbacks(readbacks, system, controller, registry);

    // The click request selected its target; the hover request hovered its own.
    EXPECT_TRUE(controller.IsSelected(clickTarget));
    EXPECT_TRUE(HasSelectedTag(registry, clickTarget));
    EXPECT_FALSE(HasHoveredTag(registry, clickTarget));

    EXPECT_FALSE(controller.IsSelected(hoverTarget));
    EXPECT_FALSE(HasSelectedTag(registry, hoverTarget));
    EXPECT_TRUE(controller.HasHovered());
    EXPECT_EQ(controller.HoveredEntity(), hoverTarget);
    EXPECT_TRUE(HasHoveredTag(registry, hoverTarget));

    EXPECT_EQ(controller.InFlightPickCount(), 0u);
    system.Shutdown();
}

// A hover pick is issued, then a click pick; the hover pick's GPU slot is
// recycled before its result is drained (its readback never arrives). The
// click result must still apply to the click request (select), NOT be
// mis-applied to the older hover request — which is exactly what oldest-first
// correlation would do.
TEST(SelectionReadbackCorrelation, MissingOldestReadbackDoesNotMisapplyNewerBySequence)
{
    Registry            registry;
    SelectionController controller;
    G::SelectionSystem  system;
    SelectionReadbackState readbacks;
    system.Initialize();

    const EntityHandle hoverTarget = MakeSelectable(registry);
    const EntityHandle clickTarget = MakeSelectable(registry);

    controller.RequestHoverPick(1u, 1u);
    const std::uint64_t hoverSequence =
        IssuePendingPick(readbacks, system, controller);
    ASSERT_NE(hoverSequence, 0u);

    controller.RequestClickPick(2u, 2u);
    const std::uint64_t clickSequence =
        IssuePendingPick(readbacks, system, controller);
    ASSERT_NE(clickSequence, 0u);
    ASSERT_EQ(controller.InFlightPickCount(), 2u);

    // Only the click readback is published; the hover slot was recycled and its
    // result lost.
    PublishHit(system, clickTarget, clickSequence);
    DrainReadbacks(readbacks, system, controller, registry);

    // The click selected its own target (click semantics), not the hover target.
    EXPECT_TRUE(controller.IsSelected(clickTarget));
    EXPECT_TRUE(HasSelectedTag(registry, clickTarget));

    // The hover target was never touched — its readback never arrived, so the
    // hover pick is still tracked (it will be evicted by capacity later) and no
    // stray hover/selection state leaked onto it.
    EXPECT_FALSE(controller.IsSelected(hoverTarget));
    EXPECT_FALSE(HasSelectedTag(registry, hoverTarget));
    EXPECT_FALSE(HasHoveredTag(registry, hoverTarget));
    EXPECT_FALSE(controller.HasHovered());

    EXPECT_EQ(controller.InFlightPickCount(), 1u);
    EXPECT_EQ(controller.OldestInFlightSequence(), hoverSequence);
    system.Shutdown();
}

TEST(SelectionReadbackCorrelation,
     ControllerCapacityEvictionRejectsStillTrackedContext)
{
    Registry registry;
    Extrinsic::Runtime::SelectionControllerConfig config{};
    config.MaxTrackedInFlightPicks = 2u;
    SelectionController controller(config);
    G::SelectionSystem system;
    SelectionReadbackState readbacks;
    system.Initialize();
    const EntityHandle target = MakeSelectable(registry);

    controller.RequestClickPick(1u, 1u);
    const std::uint64_t first =
        IssuePendingPick(readbacks, system, controller);
    controller.RequestClickPick(2u, 2u);
    (void)IssuePendingPick(readbacks, system, controller);
    controller.RequestClickPick(3u, 3u);
    (void)IssuePendingPick(readbacks, system, controller);
    ASSERT_EQ(controller.InFlightPickCount(), 2u);
    ASSERT_EQ(controller.GetDiagnostics().InFlightPicksDropped, 1u);

    PublishHit(system, target, first);
    DrainReadbacks(readbacks, system, controller, registry);

    EXPECT_FALSE(controller.IsSelected(target));
    EXPECT_FALSE(HasSelectedTag(registry, target));
    EXPECT_EQ(controller.InFlightPickCount(), 2u);
    EXPECT_EQ(controller.GetDiagnostics().UntrackedReadbacks, 1u);
    EXPECT_EQ(controller.GetDiagnostics().ReadbacksConsumed, 0u);
    EXPECT_EQ(readbacks.LastRefinedPrimitiveGeneration(), 0u);
    system.Shutdown();
}

TEST(SelectionReadbackCorrelation,
     ContextCapacityEvictionDiscardsControllerRecord)
{
    Registry registry;
    Extrinsic::Runtime::SelectionControllerConfig config{};
    config.MaxTrackedInFlightPicks = 0u;
    SelectionController controller(config);
    G::SelectionSystem system;
    SelectionReadbackState readbacks;
    system.Initialize();
    const EntityHandle target = MakeSelectable(registry);

    std::uint64_t first = 0u;
    for (std::uint32_t index = 0u; index < 33u; ++index)
    {
        controller.RequestClickPick(index, index);
        const std::uint64_t sequence =
            IssuePendingPick(readbacks, system, controller);
        if (index == 0u)
            first = sequence;
    }
    ASSERT_NE(first, 0u);
    EXPECT_EQ(controller.InFlightPickCount(), 32u);

    PublishHit(system, target, first);
    DrainReadbacks(readbacks, system, controller, registry);

    EXPECT_FALSE(controller.IsSelected(target));
    EXPECT_FALSE(HasSelectedTag(registry, target));
    EXPECT_EQ(controller.InFlightPickCount(), 32u);
    EXPECT_EQ(controller.GetDiagnostics().UntrackedReadbacks, 0u);
    EXPECT_EQ(controller.GetDiagnostics().ReadbacksConsumed, 0u);
    EXPECT_EQ(readbacks.LastRefinedPrimitiveGeneration(), 0u);
    system.Shutdown();
}

TEST(SelectionReadbackCorrelation,
     UnknownWrongWorldAndWrongEpochResultsFailClosed)
{
    Registry registry;
    SelectionController controller;
    G::SelectionSystem system;
    SelectionReadbackState readbacks;
    system.Initialize();
    const EntityHandle target = MakeSelectable(registry);

    controller.RequestClickPick(1u, 1u);
    const std::uint64_t wrongWorld =
        IssuePendingPick(readbacks, system, controller);
    PublishHit(system, target, wrongWorld);
    DrainReadbacks(
        readbacks,
        system,
        controller,
        registry,
        Extrinsic::Runtime::WorldHandle{8u, 3u},
        kEpoch);

    controller.RequestClickPick(2u, 2u);
    const std::uint64_t wrongEpoch =
        IssuePendingPick(readbacks, system, controller);
    PublishHit(system, target, wrongEpoch);
    DrainReadbacks(
        readbacks,
        system,
        controller,
        registry,
        kWorld,
        kEpoch + 1u);

    PublishHit(system, target, wrongEpoch + 1000u);
    DrainReadbacks(readbacks, system, controller, registry);

    EXPECT_FALSE(controller.IsSelected(target));
    EXPECT_FALSE(HasSelectedTag(registry, target));
    EXPECT_EQ(controller.InFlightPickCount(), 0u);
    EXPECT_EQ(controller.GetDiagnostics().ReadbacksConsumed, 0u);
    EXPECT_EQ(controller.GetDiagnostics().UntrackedReadbacks, 0u);
    EXPECT_EQ(readbacks.LastRefinedPrimitiveGeneration(), 0u);
    system.Shutdown();
}
