#include <gtest/gtest.h>

#include <type_traits>

import Extrinsic.ECS.Events;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::InvalidEntityHandle;
using Extrinsic::ECS::Scene::Registry;
namespace Events = Extrinsic::ECS::Events;

TEST(ECSEvents, SelectionChangedDefaultsToInvalidEntityHandle)
{
    constexpr Events::SelectionChanged event{};
    EXPECT_EQ(event.Entity, InvalidEntityHandle);
}

TEST(ECSEvents, SelectionChangedStoresProvidedEntity)
{
    Registry registry;
    const EntityHandle entity = registry.Create();

    const Events::SelectionChanged event{entity};
    EXPECT_EQ(event.Entity, entity);
    EXPECT_NE(event.Entity, InvalidEntityHandle);
}

TEST(ECSEvents, HoverChangedDefaultsToInvalidEntityHandle)
{
    constexpr Events::HoverChanged event{};
    EXPECT_EQ(event.Entity, InvalidEntityHandle);
}

TEST(ECSEvents, HoverChangedStoresProvidedEntity)
{
    Registry registry;
    const EntityHandle entity = registry.Create();

    const Events::HoverChanged event{entity};
    EXPECT_EQ(event.Entity, entity);
}

TEST(ECSEvents, EntitySpawnedDefaultsToInvalidEntityHandle)
{
    constexpr Events::EntitySpawned event{};
    EXPECT_EQ(event.Entity, InvalidEntityHandle);
}

TEST(ECSEvents, EntitySpawnedStoresProvidedEntity)
{
    Registry registry;
    const EntityHandle entity = registry.Create();

    const Events::EntitySpawned event{entity};
    EXPECT_EQ(event.Entity, entity);
}

TEST(ECSEvents, GeometryModifiedDefaultsToInvalidEntityHandle)
{
    constexpr Events::GeometryModified event{};
    EXPECT_EQ(event.Entity, InvalidEntityHandle);
}

TEST(ECSEvents, GeometryModifiedStoresProvidedEntity)
{
    Registry registry;
    const EntityHandle entity = registry.Create();

    const Events::GeometryModified event{entity};
    EXPECT_EQ(event.Entity, entity);
}

TEST(ECSEvents, AllPromotedEventsAreTriviallyCopyable)
{
    // CPU-only payload contract: events must be trivially copyable so
    // dispatchers and queues can move them without observability concerns.
    static_assert(std::is_trivially_copyable_v<Events::SelectionChanged>);
    static_assert(std::is_trivially_copyable_v<Events::HoverChanged>);
    static_assert(std::is_trivially_copyable_v<Events::EntitySpawned>);
    static_assert(std::is_trivially_copyable_v<Events::GeometryModified>);

    SUCCEED();
}

TEST(ECSEvents, DistinctEntitiesProduceDistinctSelectionChangedPayloads)
{
    Registry registry;
    const EntityHandle a = registry.Create();
    const EntityHandle b = registry.Create();
    ASSERT_NE(a, b);

    const Events::SelectionChanged event_a{a};
    const Events::SelectionChanged event_b{b};
    EXPECT_NE(event_a.Entity, event_b.Entity);
}
