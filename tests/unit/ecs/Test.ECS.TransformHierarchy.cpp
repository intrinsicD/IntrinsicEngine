#include <gtest/gtest.h>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.System.TransformHierarchy;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Hierarchy::Attach;
using Extrinsic::ECS::Scene::CreateDefault;
using Extrinsic::ECS::Scene::Registry;
namespace Components = Extrinsic::ECS::Components;
namespace TransformSystem = Extrinsic::ECS::Systems::TransformHierarchy;

namespace
{
    bool MatricesNear(const glm::mat4& a, const glm::mat4& b, float eps = 1e-5f)
    {
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
                if (std::abs(a[col][row] - b[col][row]) > eps) return false;
        return true;
    }
}

TEST(ECSTransformHierarchy, RootDirtyEntityGetsWorldMatrixFromLocal)
{
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle e = CreateDefault(r, "Root");

    auto& local = raw.get<Components::Transform::Component>(e);
    local.Position = glm::vec3(1.0f, 2.0f, 3.0f);
    raw.emplace<Components::Transform::IsDirtyTag>(e);

    TransformSystem::OnUpdate(raw);

    const auto& world = raw.get<Components::Transform::WorldMatrix>(e);
    const glm::mat4 expected = glm::translate(glm::mat4(1.0f), glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_TRUE(MatricesNear(world.Matrix, expected));
    EXPECT_FALSE(raw.all_of<Components::Transform::IsDirtyTag>(e));
    EXPECT_TRUE(raw.all_of<Components::Transform::WorldUpdatedTag>(e));
}

TEST(ECSTransformHierarchy, NonDirtyRootIsSkipped)
{
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle e = CreateDefault(r, "Static");

    // Pre-populated identity world matrix; no dirty tag.
    const glm::mat4 before = raw.get<Components::Transform::WorldMatrix>(e).Matrix;

    TransformSystem::OnUpdate(raw);

    EXPECT_TRUE(MatricesNear(raw.get<Components::Transform::WorldMatrix>(e).Matrix, before));
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(e));
}

TEST(ECSTransformHierarchy, ChildWorldComposesParentTimesLocal)
{
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle parent = CreateDefault(r, "P");
    const EntityHandle child = CreateDefault(r, "C");
    Attach(raw, child, parent);

    raw.get<Components::Transform::Component>(parent).Position = glm::vec3(10.0f, 0.0f, 0.0f);
    raw.get<Components::Transform::Component>(child).Position = glm::vec3(0.0f, 4.0f, 0.0f);
    raw.emplace_or_replace<Components::Transform::IsDirtyTag>(parent);
    raw.emplace_or_replace<Components::Transform::IsDirtyTag>(child);

    TransformSystem::OnUpdate(raw);

    const auto& childWorld = raw.get<Components::Transform::WorldMatrix>(child).Matrix;
    const glm::vec4 origin = childWorld * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(origin.x, 10.0f, 1e-5f);
    EXPECT_NEAR(origin.y, 4.0f, 1e-5f);
    EXPECT_NEAR(origin.z, 0.0f, 1e-5f);
}

TEST(ECSTransformHierarchy, ParentDirtyPropagatesToCleanChild)
{
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle parent = CreateDefault(r, "P");
    const EntityHandle child = CreateDefault(r, "C");
    Attach(raw, child, parent);

    raw.get<Components::Transform::Component>(child).Position = glm::vec3(0.0f, 1.0f, 0.0f);
    raw.emplace_or_replace<Components::Transform::IsDirtyTag>(child);
    TransformSystem::OnUpdate(raw);
    raw.remove<Components::Transform::WorldUpdatedTag>(parent);
    raw.remove<Components::Transform::WorldUpdatedTag>(child);

    raw.get<Components::Transform::Component>(parent).Position = glm::vec3(0.0f, 0.0f, 5.0f);
    raw.emplace<Components::Transform::IsDirtyTag>(parent);
    // Child has no IsDirtyTag, but must still be recomputed via parent dirty.

    TransformSystem::OnUpdate(raw);

    EXPECT_TRUE(raw.all_of<Components::Transform::WorldUpdatedTag>(parent));
    EXPECT_TRUE(raw.all_of<Components::Transform::WorldUpdatedTag>(child));
    const auto& childWorld = raw.get<Components::Transform::WorldMatrix>(child).Matrix;
    const glm::vec4 origin = childWorld * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_NEAR(origin.x, 0.0f, 1e-5f);
    EXPECT_NEAR(origin.y, 1.0f, 1e-5f);
    EXPECT_NEAR(origin.z, 5.0f, 1e-5f);
    EXPECT_FALSE(raw.all_of<Components::Transform::IsDirtyTag>(parent));
    EXPECT_FALSE(raw.all_of<Components::Transform::IsDirtyTag>(child));
}

TEST(ECSTransformHierarchy, CleanSubtreeIsLeftAlone)
{
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle parent = CreateDefault(r, "P");
    const EntityHandle child = CreateDefault(r, "C");
    Attach(raw, child, parent);

    raw.get<Components::Transform::Component>(parent).Position = glm::vec3(2.0f, 0.0f, 0.0f);
    raw.emplace<Components::Transform::IsDirtyTag>(parent);
    raw.emplace_or_replace<Components::Transform::IsDirtyTag>(child);
    TransformSystem::OnUpdate(raw);

    const glm::mat4 parentBefore = raw.get<Components::Transform::WorldMatrix>(parent).Matrix;
    const glm::mat4 childBefore = raw.get<Components::Transform::WorldMatrix>(child).Matrix;
    raw.remove<Components::Transform::WorldUpdatedTag>(parent);
    raw.remove<Components::Transform::WorldUpdatedTag>(child);

    TransformSystem::OnUpdate(raw);

    EXPECT_TRUE(MatricesNear(raw.get<Components::Transform::WorldMatrix>(parent).Matrix, parentBefore));
    EXPECT_TRUE(MatricesNear(raw.get<Components::Transform::WorldMatrix>(child).Matrix, childBefore));
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(parent));
    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(child));
}

TEST(ECSTransformHierarchy, OnUpdateDoesNotEmitGpuSyncDirtyTransform)
{
    // The promoted CPU traversal is forbidden from stamping the GPU-sync tag;
    // render-sync owns that hand-off (see HARDEN-061 contract decisions).
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle e = CreateDefault(r, "Root");
    raw.emplace<Components::Transform::IsDirtyTag>(e);

    TransformSystem::OnUpdate(raw);

    EXPECT_FALSE(raw.all_of<Components::DirtyTags::DirtyTransform>(e));
}

TEST(ECSTransformHierarchy, EntityWithoutTransformIsIgnored)
{
    Registry r;
    auto& raw = r.Raw();
    const auto bare = raw.create();
    raw.emplace<Components::Hierarchy::Component>(bare);

    // Should not throw, should not crash, should not modify anything.
    TransformSystem::OnUpdate(raw);

    EXPECT_FALSE(raw.all_of<Components::Transform::WorldUpdatedTag>(bare));
}
