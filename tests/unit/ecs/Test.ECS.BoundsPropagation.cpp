#include <cmath>
#include <limits>

#include <gtest/gtest.h>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

import Extrinsic.Core.FrameGraph;
import Extrinsic.Core.Hash;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Component.Culling.Local;
import Extrinsic.ECS.Component.Culling.World;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.System.BoundsPropagation;
import Extrinsic.ECS.System.TransformHierarchy;
import Geometry.AABB;
import Geometry.Sphere;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::CreateDefault;
using Extrinsic::ECS::Scene::Registry;
namespace Components = Extrinsic::ECS::Components;
namespace BoundsSystem = Extrinsic::ECS::Systems::BoundsPropagation;
namespace TransformSystem = Extrinsic::ECS::Systems::TransformHierarchy;

namespace
{
    Components::Culling::Local::Bounds MakeUnitLocalBounds()
    {
        Components::Culling::Local::Bounds local{};
        local.LocalBoundingAABB.Min = glm::vec3{-1.0f};
        local.LocalBoundingAABB.Max = glm::vec3{1.0f};
        local.LocalBoundingSphere.Center = glm::vec3{0.0f};
        local.LocalBoundingSphere.Radius = 1.0f;
        return local;
    }
}

TEST(ECSBoundsPropagation, TranslatedRootProducesWorldBounds)
{
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle e = CreateDefault(r, "Translated");

    auto& local = raw.get<Components::Transform::Component>(e);
    local.Position = glm::vec3{5.0f, -2.0f, 7.0f};
    raw.emplace<Components::Transform::IsDirtyTag>(e);
    raw.emplace<Components::Culling::Local::Bounds>(e, MakeUnitLocalBounds());

    TransformSystem::OnUpdate(raw);

    BoundsSystem::Stats stats{};
    BoundsSystem::OnUpdate(raw, stats);
    EXPECT_EQ(stats.Recomputed, 1u);
    EXPECT_EQ(stats.SkippedMissingLocalBounds, 0u);
    EXPECT_EQ(stats.SkippedMissingWorldMatrix, 0u);
    EXPECT_EQ(stats.NonFiniteResults, 0u);

    ASSERT_TRUE(raw.all_of<Components::Culling::World::Bounds>(e));
    const auto& world = raw.get<Components::Culling::World::Bounds>(e);
    EXPECT_NEAR(world.WorldBoundingOBB.Center.x, 5.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingOBB.Center.y, -2.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingOBB.Center.z, 7.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingOBB.Extents.x, 1.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingOBB.Extents.y, 1.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingOBB.Extents.z, 1.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingSphere.Center.x, 5.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingSphere.Center.y, -2.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingSphere.Center.z, 7.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingSphere.Radius, std::sqrt(3.0f), 1e-5f);
}

TEST(ECSBoundsPropagation, NonUniformScaleStretchesExtentsPerAxis)
{
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle e = CreateDefault(r, "Scaled");

    auto& local = raw.get<Components::Transform::Component>(e);
    local.Scale = glm::vec3{2.0f, 3.0f, 4.0f};
    raw.emplace<Components::Transform::IsDirtyTag>(e);
    raw.emplace<Components::Culling::Local::Bounds>(e, MakeUnitLocalBounds());

    TransformSystem::OnUpdate(raw);
    BoundsSystem::OnUpdate(raw);

    const auto& world = raw.get<Components::Culling::World::Bounds>(e);
    EXPECT_NEAR(world.WorldBoundingOBB.Extents.x, 2.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingOBB.Extents.y, 3.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingOBB.Extents.z, 4.0f, 1e-5f);
    // The sphere circumscribes the transformed local AABB.
    EXPECT_NEAR(world.WorldBoundingSphere.Radius, std::sqrt(29.0f), 1e-5f);
}

TEST(ECSBoundsPropagation, ShearedWorldMatrixUsesTransformedAabbSphereInPureAndSystemPaths)
{
    Components::Culling::Local::Bounds local = MakeUnitLocalBounds();
    constexpr float kPi = 3.14159265358979323846f;
    const glm::mat4 worldMatrix =
        glm::scale(glm::mat4{1.0f}, glm::vec3{4.0f, 1.0f, 1.0f})
        * glm::rotate(
            glm::mat4{1.0f},
            kPi * 0.25f,
            glm::vec3{0.0f, 0.0f, 1.0f});

    Components::Culling::World::Bounds pure{};
    ASSERT_TRUE(BoundsSystem::TryComputeWorldBounds(local, worldMatrix, pure));

    glm::vec3 expectedMinimum{std::numeric_limits<float>::max()};
    glm::vec3 expectedMaximum{std::numeric_limits<float>::lowest()};
    for (std::uint32_t corner = 0u; corner < 8u; ++corner)
    {
        const glm::vec3 localCorner{
            (corner & 1u) != 0u ? local.LocalBoundingAABB.Max.x : local.LocalBoundingAABB.Min.x,
            (corner & 2u) != 0u ? local.LocalBoundingAABB.Max.y : local.LocalBoundingAABB.Min.y,
            (corner & 4u) != 0u ? local.LocalBoundingAABB.Max.z : local.LocalBoundingAABB.Min.z,
        };
        const glm::vec3 worldCorner{
            worldMatrix * glm::vec4{localCorner, 1.0f}};
        expectedMinimum = glm::min(expectedMinimum, worldCorner);
        expectedMaximum = glm::max(expectedMaximum, worldCorner);
    }
    const glm::vec3 expectedCenter = 0.5f * (expectedMinimum + expectedMaximum);
    const float expectedRadius = 0.5f * glm::length(expectedMaximum - expectedMinimum);

    EXPECT_NEAR(pure.WorldBoundingSphere.Center.x, expectedCenter.x, 1e-5f);
    EXPECT_NEAR(pure.WorldBoundingSphere.Center.y, expectedCenter.y, 1e-5f);
    EXPECT_NEAR(pure.WorldBoundingSphere.Center.z, expectedCenter.z, 1e-5f);
    EXPECT_NEAR(pure.WorldBoundingSphere.Radius, expectedRadius, 1e-5f);

    Registry r;
    auto& raw = r.Raw();
    const auto entity = raw.create();
    raw.emplace<Components::Transform::WorldUpdatedTag>(entity);
    raw.emplace<Components::Transform::WorldMatrix>(
        entity,
        Components::Transform::WorldMatrix{.Matrix = worldMatrix});
    raw.emplace<Components::Culling::Local::Bounds>(entity, local);

    BoundsSystem::OnUpdate(raw);

    const auto& system = raw.get<Components::Culling::World::Bounds>(entity);
    EXPECT_EQ(system.WorldBoundingSphere.Center, pure.WorldBoundingSphere.Center);
    EXPECT_FLOAT_EQ(system.WorldBoundingSphere.Radius, pure.WorldBoundingSphere.Radius);
}

TEST(ECSBoundsPropagation, RotationIsEmbeddedInWorldObb)
{
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle e = CreateDefault(r, "Rotated");

    constexpr float kPi = 3.14159265358979323846f;
    auto& local = raw.get<Components::Transform::Component>(e);
    local.Rotation = glm::angleAxis(kPi * 0.5f, glm::vec3{0.0f, 1.0f, 0.0f});
    raw.emplace<Components::Transform::IsDirtyTag>(e);
    raw.emplace<Components::Culling::Local::Bounds>(e, MakeUnitLocalBounds());

    TransformSystem::OnUpdate(raw);
    BoundsSystem::OnUpdate(raw);

    const auto& world = raw.get<Components::Culling::World::Bounds>(e);
    // A 90-degree yaw rotates the local x-axis onto -z.
    const glm::vec3 localX{1.0f, 0.0f, 0.0f};
    const glm::vec3 rotatedX = world.WorldBoundingOBB.Rotation * localX;
    EXPECT_NEAR(rotatedX.x, 0.0f, 1e-5f);
    EXPECT_NEAR(rotatedX.y, 0.0f, 1e-5f);
    EXPECT_NEAR(rotatedX.z, -1.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingOBB.Extents.x, 1.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingOBB.Extents.y, 1.0f, 1e-5f);
    EXPECT_NEAR(world.WorldBoundingOBB.Extents.z, 1.0f, 1e-5f);
}

TEST(ECSBoundsPropagation, EntityWithoutWorldUpdatedTagIsSkipped)
{
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle e = CreateDefault(r, "Clean");
    raw.emplace<Components::Culling::Local::Bounds>(e, MakeUnitLocalBounds());

    // Pre-populate world bounds so we can detect any unwanted rewrite.
    Components::Culling::World::Bounds preset{};
    preset.WorldBoundingSphere.Radius = 99.0f;
    raw.emplace<Components::Culling::World::Bounds>(e, preset);

    // No transform dirty -> no WorldUpdatedTag is stamped.
    TransformSystem::OnUpdate(raw);
    BoundsSystem::Stats stats{};
    BoundsSystem::OnUpdate(raw, stats);

    EXPECT_EQ(stats.Recomputed, 0u);
    EXPECT_NEAR(raw.get<Components::Culling::World::Bounds>(e).WorldBoundingSphere.Radius, 99.0f, 1e-5f);
}

TEST(ECSBoundsPropagation, MissingLocalBoundsIsCountedAsSkipped)
{
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle e = CreateDefault(r, "NoLocal");
    raw.emplace<Components::Transform::IsDirtyTag>(e);

    TransformSystem::OnUpdate(raw);  // stamps WorldUpdatedTag

    BoundsSystem::Stats stats{};
    BoundsSystem::OnUpdate(raw, stats);

    EXPECT_EQ(stats.Recomputed, 0u);
    EXPECT_EQ(stats.SkippedMissingLocalBounds, 1u);
    EXPECT_FALSE(raw.all_of<Components::Culling::World::Bounds>(e));
}

TEST(ECSBoundsPropagation, NonFiniteWorldMatrixIsCountedAndDoesNotOverwrite)
{
    Registry r;
    auto& raw = r.Raw();
    const auto e = raw.create();
    raw.emplace<Components::Transform::WorldUpdatedTag>(e);
    raw.emplace<Components::Culling::Local::Bounds>(e, MakeUnitLocalBounds());

    Components::Transform::WorldMatrix wm{};
    wm.Matrix = glm::mat4{1.0f};
    wm.Matrix[3][0] = std::numeric_limits<float>::quiet_NaN();
    raw.emplace<Components::Transform::WorldMatrix>(e, wm);

    BoundsSystem::Stats stats{};
    BoundsSystem::OnUpdate(raw, stats);

    EXPECT_EQ(stats.NonFiniteResults, 1u);
    EXPECT_EQ(stats.Recomputed, 0u);
    EXPECT_FALSE(raw.all_of<Components::Culling::World::Bounds>(e));
}

TEST(ECSBoundsPropagation, HierarchyChildInheritsParentTransformInWorldBounds)
{
    Registry r;
    auto& raw = r.Raw();

    const EntityHandle parent = CreateDefault(r, "Parent");
    const EntityHandle child = CreateDefault(r, "Child");
    Extrinsic::ECS::Hierarchy::Attach(raw, child, parent);

    raw.get<Components::Transform::Component>(parent).Position = glm::vec3{0.0f, 0.0f, 10.0f};
    raw.get<Components::Transform::Component>(child).Position = glm::vec3{2.0f, 0.0f, 0.0f};
    raw.emplace_or_replace<Components::Transform::IsDirtyTag>(parent);
    raw.emplace_or_replace<Components::Transform::IsDirtyTag>(child);
    raw.emplace<Components::Culling::Local::Bounds>(child, MakeUnitLocalBounds());

    TransformSystem::OnUpdate(raw);
    BoundsSystem::OnUpdate(raw);

    ASSERT_TRUE(raw.all_of<Components::Culling::World::Bounds>(child));
    const auto& worldBounds = raw.get<Components::Culling::World::Bounds>(child);
    EXPECT_NEAR(worldBounds.WorldBoundingOBB.Center.x, 2.0f, 1e-5f);
    EXPECT_NEAR(worldBounds.WorldBoundingOBB.Center.y, 0.0f, 1e-5f);
    EXPECT_NEAR(worldBounds.WorldBoundingOBB.Center.z, 10.0f, 1e-5f);
}

TEST(ECSBoundsPropagation, FrameGraphRegistersAfterTransformHierarchy)
{
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle e = CreateDefault(r, "Single");

    raw.get<Components::Transform::Component>(e).Position = glm::vec3{1.0f, 2.0f, 3.0f};
    raw.emplace<Components::Transform::IsDirtyTag>(e);
    raw.emplace<Components::Culling::Local::Bounds>(e, MakeUnitLocalBounds());

    Extrinsic::Core::FrameGraph fg;
    TransformSystem::RegisterSystem(fg, raw);
    BoundsSystem::RegisterSystem(fg, raw);
    ASSERT_TRUE(fg.Compile().has_value());

    // Both passes are registered; the WaitFor edge means BoundsPropagation
    // executes in a later layer than TransformHierarchy.
    EXPECT_EQ(fg.PassCount(), 2u);
    const auto& layers = fg.GetExecutionLayers();
    ASSERT_GE(layers.size(), 2u);
    // First layer must contain TransformUpdate.
    bool foundTransformFirst = false;
    for (const auto idx : layers.front())
    {
        if (fg.PassName(idx) == TransformSystem::PassName) foundTransformFirst = true;
    }
    EXPECT_TRUE(foundTransformFirst);
    // A later layer must contain WorldBoundsUpdate.
    bool foundBoundsLater = false;
    for (std::size_t li = 1; li < layers.size(); ++li)
    {
        for (const auto idx : layers[li])
        {
            if (fg.PassName(idx) == BoundsSystem::PassName) foundBoundsLater = true;
        }
    }
    EXPECT_TRUE(foundBoundsLater);
}
