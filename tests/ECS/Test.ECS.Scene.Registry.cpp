#include <gtest/gtest.h>

#include <entt/entity/registry.hpp>

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;

using namespace Extrinsic::ECS::Scene;

namespace
{
    struct TestTag
    {
        int value = 0;
    };
}

TEST(ECSSceneRegistry, CreatedEntityIsValid)
{
    Registry registry;
    const auto e = registry.Create();
    EXPECT_NE(e, Extrinsic::ECS::InvalidEntityHandle);
    EXPECT_TRUE(registry.IsValid(e));
}

TEST(ECSSceneRegistry, DestroyedEntityIsInvalid)
{
    Registry registry;
    const auto e = registry.Create();
    ASSERT_TRUE(registry.IsValid(e));

    registry.Destroy(e);
    EXPECT_FALSE(registry.IsValid(e));
}

TEST(ECSSceneRegistry, DistinctCreatesProduceDistinctHandles)
{
    Registry registry;
    const auto a = registry.Create();
    const auto b = registry.Create();
    const auto c = registry.Create();
    EXPECT_NE(a, b);
    EXPECT_NE(b, c);
    EXPECT_NE(a, c);
    EXPECT_TRUE(registry.IsValid(a));
    EXPECT_TRUE(registry.IsValid(b));
    EXPECT_TRUE(registry.IsValid(c));
}

TEST(ECSSceneRegistry, ClearInvalidatesAllEntities)
{
    Registry registry;
    const auto a = registry.Create();
    const auto b = registry.Create();
    const auto c = registry.Create();

    registry.Clear();

    EXPECT_FALSE(registry.IsValid(a));
    EXPECT_FALSE(registry.IsValid(b));
    EXPECT_FALSE(registry.IsValid(c));
}

TEST(ECSSceneRegistry, RawAccessorExposesEnttRegistryForComponents)
{
    // The documented escape hatch: systems reach through Raw() for views,
    // groups, and component storage. This test asserts the contract that
    // components emplaced via Raw() are visible through Raw(), and that the
    // entity lifecycle managed by the typed API integrates correctly.
    Registry registry;
    const auto e = registry.Create();

    registry.Raw().emplace<TestTag>(e, TestTag{.value = 42});

    ASSERT_TRUE(registry.Raw().all_of<TestTag>(e));
    EXPECT_EQ(registry.Raw().get<TestTag>(e).value, 42);

    registry.Destroy(e);
    EXPECT_FALSE(registry.IsValid(e));
}

TEST(ECSSceneRegistry, RawConstAccessorIsReadOnly)
{
    Registry registry;
    const auto e = registry.Create();
    registry.Raw().emplace<TestTag>(e, TestTag{.value = 7});

    const Registry& constRef = registry;
    const auto& rawConst = constRef.Raw();
    ASSERT_TRUE(rawConst.all_of<TestTag>(e));
    EXPECT_EQ(rawConst.get<TestTag>(e).value, 7);
}

TEST(ECSSceneRegistry, InvalidHandleIsNotValid)
{
    const Registry registry;
    EXPECT_FALSE(registry.IsValid(Extrinsic::ECS::InvalidEntityHandle));
}
