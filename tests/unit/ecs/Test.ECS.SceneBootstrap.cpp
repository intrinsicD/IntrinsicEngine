#include <gtest/gtest.h>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <string>

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.DirtyTags;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::InvalidEntityHandle;
using Extrinsic::ECS::Scene::CreateDefault;
using Extrinsic::ECS::Scene::EmplaceDefaults;
using Extrinsic::ECS::Scene::Registry;
namespace Components = Extrinsic::ECS::Components;

namespace
{
    constexpr float kIdentityEpsilon = 1e-6f;

    bool IsIdentity(const glm::mat4& matrix)
    {
        const glm::mat4 identity{1.0f};
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                if (std::abs(matrix[col][row] - identity[col][row]) > kIdentityEpsilon)
                    return false;
            }
        }
        return true;
    }
}

TEST(ECSSceneBootstrap, CreateDefaultEmplacesAllPromotedDefaults)
{
    Registry registry;
    const EntityHandle entity = CreateDefault(registry, "Root");

    ASSERT_TRUE(registry.IsValid(entity));

    const auto& raw = registry.Raw();
    EXPECT_TRUE(raw.all_of<Components::MetaData>(entity));
    EXPECT_TRUE(raw.all_of<Components::Transform::Component>(entity));
    EXPECT_TRUE(raw.all_of<Components::Transform::WorldMatrix>(entity));
    EXPECT_TRUE(raw.all_of<Components::Hierarchy::Component>(entity));
}

TEST(ECSSceneBootstrap, CreateDefaultStoresProvidedEntityName)
{
    Registry registry;
    const EntityHandle entity = CreateDefault(registry, "Camera");

    const auto& meta = registry.Raw().get<Components::MetaData>(entity);
    EXPECT_EQ(meta.EntityName, std::string{"Camera"});
}

TEST(ECSSceneBootstrap, CreateDefaultInitializesIdentityTransformAndWorldMatrix)
{
    Registry registry;
    const EntityHandle entity = CreateDefault(registry, "Identity");

    const auto& transform = registry.Raw().get<Components::Transform::Component>(entity);
    EXPECT_FLOAT_EQ(transform.Position.x, 0.0f);
    EXPECT_FLOAT_EQ(transform.Position.y, 0.0f);
    EXPECT_FLOAT_EQ(transform.Position.z, 0.0f);
    EXPECT_FLOAT_EQ(transform.Rotation.w, 1.0f);
    EXPECT_FLOAT_EQ(transform.Rotation.x, 0.0f);
    EXPECT_FLOAT_EQ(transform.Rotation.y, 0.0f);
    EXPECT_FLOAT_EQ(transform.Rotation.z, 0.0f);
    EXPECT_FLOAT_EQ(transform.Scale.x, 1.0f);
    EXPECT_FLOAT_EQ(transform.Scale.y, 1.0f);
    EXPECT_FLOAT_EQ(transform.Scale.z, 1.0f);

    const auto& world = registry.Raw().get<Components::Transform::WorldMatrix>(entity);
    EXPECT_TRUE(IsIdentity(world.Matrix));
}

TEST(ECSSceneBootstrap, CreateDefaultLeavesHierarchyDetached)
{
    Registry registry;
    const EntityHandle entity = CreateDefault(registry, "Detached");

    const auto& hierarchy = registry.Raw().get<Components::Hierarchy::Component>(entity);
    EXPECT_EQ(hierarchy.Parent, InvalidEntityHandle);
    EXPECT_EQ(hierarchy.FirstChild, InvalidEntityHandle);
    EXPECT_EQ(hierarchy.NextSibling, InvalidEntityHandle);
    EXPECT_EQ(hierarchy.PrevSibling, InvalidEntityHandle);
    EXPECT_EQ(hierarchy.ChildCount, 0u);
}

TEST(ECSSceneBootstrap, CreateDefaultDoesNotEmplaceDirtyTransformTags)
{
    // The promoted bootstrap contract intentionally leaves dirty-transform
    // marking to the TransformHierarchy system port (HARDEN-061). Asserting
    // absence here locks that contract decision.
    Registry registry;
    const EntityHandle entity = CreateDefault(registry, "NoDirtyTag");

    const auto& raw = registry.Raw();
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(entity));
    EXPECT_FALSE(raw.all_of<Components::DirtyTags::DirtyTransform>(entity));
}

TEST(ECSSceneBootstrap, EmplaceDefaultsOnPreCreatedEntityMatchesCreateDefault)
{
    Registry registry;
    const EntityHandle entity = registry.Create();
    EmplaceDefaults(registry, entity, "Manual");

    const auto& raw = registry.Raw();
    ASSERT_TRUE(raw.all_of<Components::MetaData>(entity));
    EXPECT_EQ(raw.get<Components::MetaData>(entity).EntityName, std::string{"Manual"});
    EXPECT_TRUE(raw.all_of<Components::Transform::Component>(entity));
    EXPECT_TRUE(raw.all_of<Components::Transform::WorldMatrix>(entity));
    EXPECT_TRUE(raw.all_of<Components::Hierarchy::Component>(entity));
}

TEST(ECSSceneBootstrap, DistinctCreateDefaultsProduceDistinctHandlesAndStorage)
{
    Registry registry;
    const EntityHandle a = CreateDefault(registry, "A");
    const EntityHandle b = CreateDefault(registry, "B");

    EXPECT_NE(a, b);
    EXPECT_EQ(registry.Raw().get<Components::MetaData>(a).EntityName, std::string{"A"});
    EXPECT_EQ(registry.Raw().get<Components::MetaData>(b).EntityName, std::string{"B"});
}

TEST(ECSSceneBootstrap, DestroyRemovesAllBootstrapComponents)
{
    Registry registry;
    const EntityHandle entity = CreateDefault(registry, "Doomed");

    registry.Destroy(entity);

    EXPECT_FALSE(registry.IsValid(entity));
    const auto& raw = registry.Raw();
    EXPECT_FALSE(raw.all_of<Components::MetaData>(entity));
    EXPECT_FALSE(raw.all_of<Components::Transform::Component>(entity));
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldMatrix>(entity));
    EXPECT_FALSE(raw.all_of<Components::Hierarchy::Component>(entity));
}

TEST(ECSSceneBootstrap, ClearInvalidatesAllBootstrappedEntities)
{
    Registry registry;
    const EntityHandle a = CreateDefault(registry, "A");
    const EntityHandle b = CreateDefault(registry, "B");
    const EntityHandle c = CreateDefault(registry, "C");

    registry.Clear();

    EXPECT_FALSE(registry.IsValid(a));
    EXPECT_FALSE(registry.IsValid(b));
    EXPECT_FALSE(registry.IsValid(c));
}
