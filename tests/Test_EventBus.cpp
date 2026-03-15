#include <gtest/gtest.h>
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <entt/signal/dispatcher.hpp>
#include <vector>

import ECS;
import Runtime.Selection;

using namespace ECS;
using namespace ECS::Events;

namespace
{
    [[nodiscard]] bool IsNullEntity(entt::entity entity)
    {
        return entity == entt::null;
    }
}

// -----------------------------------------------------------------------------
// Event Bus — Scene Dispatcher Contract Tests
// -----------------------------------------------------------------------------

TEST(EventBus, SceneHasDispatcher)
{
    Scene scene;
    // Dispatcher should be default-constructed and accessible.
    auto& dispatcher = scene.GetDispatcher();
    // Drain should be safe on an empty dispatcher.
    dispatcher.update();
}

TEST(EventBus, SelectionChanged_FiredOnReplace)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Target");
    reg.emplace<Components::Selection::SelectableTag>(e);

    std::vector<SelectionChanged> received;
    scene.GetDispatcher().sink<SelectionChanged>().connect<
        [](std::vector<SelectionChanged>& out, const SelectionChanged& evt) {
            out.push_back(evt);
        }>(received);

    Runtime::Selection::ApplySelection(scene, e, Runtime::Selection::PickMode::Replace);
    scene.GetDispatcher().update();

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].Entity, e);
}

TEST(EventBus, SelectionChanged_FiredOnDeselectAll)
{
    Scene scene;

    std::vector<SelectionChanged> received;
    scene.GetDispatcher().sink<SelectionChanged>().connect<
        [](std::vector<SelectionChanged>& out, const SelectionChanged& evt) {
            out.push_back(evt);
        }>(received);

    // Replace with null entity = deselect all.
    Runtime::Selection::ApplySelection(scene, entt::null, Runtime::Selection::PickMode::Replace);
    scene.GetDispatcher().update();

    ASSERT_EQ(received.size(), 1u);
    EXPECT_TRUE(IsNullEntity(received[0].Entity));
}

TEST(EventBus, SelectionChanged_FiredOnToggle)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Target");
    reg.emplace<Components::Selection::SelectableTag>(e);

    std::vector<SelectionChanged> received;
    scene.GetDispatcher().sink<SelectionChanged>().connect<
        [](std::vector<SelectionChanged>& out, const SelectionChanged& evt) {
            out.push_back(evt);
        }>(received);

    // Select.
    Runtime::Selection::ApplySelection(scene, e, Runtime::Selection::PickMode::Toggle);
    scene.GetDispatcher().update();
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].Entity, e);

    // Toggle off.
    Runtime::Selection::ApplySelection(scene, e, Runtime::Selection::PickMode::Toggle);
    scene.GetDispatcher().update();
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[1].Entity, e);
}

TEST(EventBus, HoverChanged_FiredOnHover)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Target");
    reg.emplace<Components::Selection::SelectableTag>(e);

    std::vector<HoverChanged> received;
    scene.GetDispatcher().sink<HoverChanged>().connect<
        [](std::vector<HoverChanged>& out, const HoverChanged& evt) {
            out.push_back(evt);
        }>(received);

    Runtime::Selection::ApplyHover(scene, e);
    scene.GetDispatcher().update();

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].Entity, e);
}

TEST(EventBus, HoverChanged_FiredOnClearHover)
{
    Scene scene;

    std::vector<HoverChanged> received;
    scene.GetDispatcher().sink<HoverChanged>().connect<
        [](std::vector<HoverChanged>& out, const HoverChanged& evt) {
            out.push_back(evt);
        }>(received);

    Runtime::Selection::ApplyHover(scene, entt::null);
    scene.GetDispatcher().update();

    ASSERT_EQ(received.size(), 1u);
    EXPECT_TRUE(IsNullEntity(received[0].Entity));
}

TEST(EventBus, EventsAreDeferredUntilUpdate)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Target");
    reg.emplace<Components::Selection::SelectableTag>(e);

    int callCount = 0;
    scene.GetDispatcher().sink<SelectionChanged>().connect<
        [](int& count, const SelectionChanged&) {
            ++count;
        }>(callCount);

    Runtime::Selection::ApplySelection(scene, e, Runtime::Selection::PickMode::Replace);

    // Not yet drained — callback should not have fired.
    EXPECT_EQ(callCount, 0);

    scene.GetDispatcher().update();
    EXPECT_EQ(callCount, 1);
}

TEST(EventBus, MultipleEventsPerFrame)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e1 = scene.CreateEntity("A");
    entt::entity e2 = scene.CreateEntity("B");
    reg.emplace<Components::Selection::SelectableTag>(e1);
    reg.emplace<Components::Selection::SelectableTag>(e2);

    std::vector<SelectionChanged> received;
    scene.GetDispatcher().sink<SelectionChanged>().connect<
        [](std::vector<SelectionChanged>& out, const SelectionChanged& evt) {
            out.push_back(evt);
        }>(received);

    Runtime::Selection::ApplySelection(scene, e1, Runtime::Selection::PickMode::Replace);
    Runtime::Selection::ApplySelection(scene, e2, Runtime::Selection::PickMode::Add);

    scene.GetDispatcher().update();

    // Two events should have been delivered in order.
    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0].Entity, e1);
    EXPECT_EQ(received[1].Entity, e2);
}

// -----------------------------------------------------------------------------
// EntitySpawned / GeometryModified — Dispatcher Contract Tests
// -----------------------------------------------------------------------------

TEST(EventBus, EntitySpawned_SinkReceivesEnqueuedEvent)
{
    Scene scene;
    entt::entity e = scene.CreateEntity("Spawned");

    std::vector<EntitySpawned> received;
    scene.GetDispatcher().sink<EntitySpawned>().connect<
        [](std::vector<EntitySpawned>& out, const EntitySpawned& evt) {
            out.push_back(evt);
        }>(received);

    scene.GetDispatcher().enqueue<EntitySpawned>({e});
    EXPECT_TRUE(received.empty()); // Deferred.

    scene.GetDispatcher().update();
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].Entity, e);
}

TEST(EventBus, GeometryModified_SinkReceivesEnqueuedEvent)
{
    Scene scene;
    entt::entity e = scene.CreateEntity("Mesh");

    std::vector<GeometryModified> received;
    scene.GetDispatcher().sink<GeometryModified>().connect<
        [](std::vector<GeometryModified>& out, const GeometryModified& evt) {
            out.push_back(evt);
        }>(received);

    scene.GetDispatcher().enqueue<GeometryModified>({e});
    scene.GetDispatcher().update();

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].Entity, e);
}

TEST(EventBus, SelectionChanged_CachedEntityUpdatedBySink)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Target");
    reg.emplace<Components::Selection::SelectableTag>(e);

    // Simulate the Sandbox pattern: cache entity via sink.
    entt::entity cached = entt::null;
    scene.GetDispatcher().sink<SelectionChanged>().connect<
        [](entt::entity& out, const SelectionChanged& evt) {
            out = evt.Entity;
        }>(cached);

    Runtime::Selection::ApplySelection(scene, e, Runtime::Selection::PickMode::Replace);
    EXPECT_TRUE(IsNullEntity(cached)); // Still deferred.

    scene.GetDispatcher().update();
    EXPECT_EQ(cached, e); // Now updated.

    // Deselect updates cache to null.
    Runtime::Selection::ApplySelection(scene, entt::null, Runtime::Selection::PickMode::Replace);
    scene.GetDispatcher().update();
    EXPECT_TRUE(IsNullEntity(cached));
}

// -----------------------------------------------------------------------------
// GpuPickCompleted — Dispatcher Contract Tests
// -----------------------------------------------------------------------------

TEST(EventBus, GpuPickCompleted_SinkReceivesEnqueuedEvent)
{
    Scene scene;

    std::vector<GpuPickCompleted> received;
    scene.GetDispatcher().sink<GpuPickCompleted>().connect<
        [](std::vector<GpuPickCompleted>& out, const GpuPickCompleted& evt) {
            out.push_back(evt);
        }>(received);

    scene.GetDispatcher().enqueue<GpuPickCompleted>({42u, true});
    EXPECT_TRUE(received.empty()); // Deferred.

    scene.GetDispatcher().update();
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].PickID, 42u);
    EXPECT_TRUE(received[0].HasHit);
}

TEST(EventBus, GpuPickCompleted_BackgroundHitHasZeroPickID)
{
    Scene scene;

    std::vector<GpuPickCompleted> received;
    scene.GetDispatcher().sink<GpuPickCompleted>().connect<
        [](std::vector<GpuPickCompleted>& out, const GpuPickCompleted& evt) {
            out.push_back(evt);
        }>(received);

    // Background click: no hit, PickID == 0.
    scene.GetDispatcher().enqueue<GpuPickCompleted>({0u, false});
    scene.GetDispatcher().update();

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].PickID, 0u);
    EXPECT_FALSE(received[0].HasHit);
}

// -----------------------------------------------------------------------------
// GeometryUploadFailed — Dispatcher Contract Tests
// -----------------------------------------------------------------------------

TEST(EventBus, GeometryUploadFailed_SinkReceivesEnqueuedEvent)
{
    Scene scene;
    entt::entity e = scene.CreateEntity("FailedUpload");

    std::vector<GeometryUploadFailed> received;
    scene.GetDispatcher().sink<GeometryUploadFailed>().connect<
        [](std::vector<GeometryUploadFailed>& out, const GeometryUploadFailed& evt) {
            out.push_back(evt);
        }>(received);

    scene.GetDispatcher().enqueue<GeometryUploadFailed>({e});
    EXPECT_TRUE(received.empty()); // Deferred until update().

    scene.GetDispatcher().update();
    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0].Entity, e);
}

TEST(EventBus, GeometryUploadFailed_MultipleEntitiesPerFrame)
{
    Scene scene;
    entt::entity e1 = scene.CreateEntity("Mesh");
    entt::entity e2 = scene.CreateEntity("Cloud");

    std::vector<GeometryUploadFailed> received;
    scene.GetDispatcher().sink<GeometryUploadFailed>().connect<
        [](std::vector<GeometryUploadFailed>& out, const GeometryUploadFailed& evt) {
            out.push_back(evt);
        }>(received);

    scene.GetDispatcher().enqueue<GeometryUploadFailed>({e1});
    scene.GetDispatcher().enqueue<GeometryUploadFailed>({e2});
    scene.GetDispatcher().update();

    ASSERT_EQ(received.size(), 2u);
    EXPECT_EQ(received[0].Entity, e1);
    EXPECT_EQ(received[1].Entity, e2);
}
