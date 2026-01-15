#include <gtest/gtest.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>
#include <vector>
#include <unordered_set>

import ECS;

using namespace ECS;
using namespace ECS::Components;

// Helper to check if an entity is null (avoids gtest printing issues)
inline bool IsNull(entt::entity e) { return e == entt::null; }
inline bool IsNotNull(entt::entity e) { return e != entt::null; }

// -----------------------------------------------------------------------------
// Scene Tests
// -----------------------------------------------------------------------------

TEST(ECS_Scene, CreateEntity_ReturnsValidEntity)
{
    Scene scene;

    entt::entity e = scene.CreateEntity("TestEntity");

    EXPECT_TRUE(scene.GetRegistry().valid(e));
}

TEST(ECS_Scene, CreateEntity_HasNameTag)
{
    Scene scene;

    entt::entity e = scene.CreateEntity("MyObject");

    ASSERT_TRUE(scene.GetRegistry().all_of<NameTag::Component>(e));
    auto& name = scene.GetRegistry().get<NameTag::Component>(e);
    EXPECT_EQ(name.Name, "MyObject");
}

TEST(ECS_Scene, CreateEntity_HasTransform)
{
    Scene scene;

    entt::entity e = scene.CreateEntity("Entity");

    ASSERT_TRUE(scene.GetRegistry().all_of<Transform::Component>(e));
    auto& transform = scene.GetRegistry().get<Transform::Component>(e);

    // Default values
    EXPECT_EQ(transform.Position, glm::vec3(0.0f));
    EXPECT_EQ(transform.Scale, glm::vec3(1.0f));
    // Identity quaternion
    EXPECT_FLOAT_EQ(transform.Rotation.w, 1.0f);
    EXPECT_FLOAT_EQ(transform.Rotation.x, 0.0f);
    EXPECT_FLOAT_EQ(transform.Rotation.y, 0.0f);
    EXPECT_FLOAT_EQ(transform.Rotation.z, 0.0f);
}

TEST(ECS_Scene, CreateEntity_HasHierarchy)
{
    Scene scene;

    entt::entity e = scene.CreateEntity("Entity");

    ASSERT_TRUE(scene.GetRegistry().all_of<Hierarchy::Component>(e));
    auto& hierarchy = scene.GetRegistry().get<Hierarchy::Component>(e);

    // Default: no parent, no children
    EXPECT_TRUE(IsNull(hierarchy.Parent));
    EXPECT_TRUE(IsNull(hierarchy.FirstChild));
    EXPECT_TRUE(IsNull(hierarchy.NextSibling));
    EXPECT_TRUE(IsNull(hierarchy.PrevSibling));
    EXPECT_EQ(hierarchy.ChildCount, 0u);
}

TEST(ECS_Scene, Size_CountsEntities)
{
    Scene scene;

    EXPECT_EQ(scene.Size(), 0u);

    scene.CreateEntity("A");
    EXPECT_EQ(scene.Size(), 1u);

    scene.CreateEntity("B");
    scene.CreateEntity("C");
    EXPECT_EQ(scene.Size(), 3u);
}

TEST(ECS_Scene, CreateMultipleEntities_UniqueHandles)
{
    Scene scene;

    std::vector<entt::entity> entities;
    for (int i = 0; i < 100; ++i)
    {
        entities.push_back(scene.CreateEntity("Entity_" + std::to_string(i)));
    }

    // All should be unique
    std::unordered_set<entt::entity> uniqueSet(entities.begin(), entities.end());
    EXPECT_EQ(uniqueSet.size(), entities.size());
}

// -----------------------------------------------------------------------------
// Hierarchy Attach/Detach Tests
// -----------------------------------------------------------------------------

TEST(ECS_Hierarchy, Attach_BasicParentChild)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity child = scene.CreateEntity("Child");

    Hierarchy::Attach(reg, child, parent);

    auto& parentHier = reg.get<Hierarchy::Component>(parent);
    auto& childHier = reg.get<Hierarchy::Component>(child);

    EXPECT_TRUE(childHier.Parent == parent);
    EXPECT_TRUE(parentHier.FirstChild == child);
    EXPECT_EQ(parentHier.ChildCount, 1u);
}

TEST(ECS_Hierarchy, Attach_MultipleChildren)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity child1 = scene.CreateEntity("Child1");
    entt::entity child2 = scene.CreateEntity("Child2");
    entt::entity child3 = scene.CreateEntity("Child3");

    Hierarchy::Attach(reg, child1, parent);
    Hierarchy::Attach(reg, child2, parent);
    Hierarchy::Attach(reg, child3, parent);

    auto& parentHier = reg.get<Hierarchy::Component>(parent);
    EXPECT_EQ(parentHier.ChildCount, 3u);

    // Children are added at head of list, so child3 is first
    EXPECT_TRUE(parentHier.FirstChild == child3);

    // Verify sibling chain
    auto& h3 = reg.get<Hierarchy::Component>(child3);
    auto& h2 = reg.get<Hierarchy::Component>(child2);
    auto& h1 = reg.get<Hierarchy::Component>(child1);

    EXPECT_TRUE(h3.NextSibling == child2);
    EXPECT_TRUE(h2.PrevSibling == child3);
    EXPECT_TRUE(h2.NextSibling == child1);
    EXPECT_TRUE(h1.PrevSibling == child2);
    EXPECT_TRUE(IsNull(h1.NextSibling));
}

TEST(ECS_Hierarchy, Detach_RemovesFromParent)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity child = scene.CreateEntity("Child");

    Hierarchy::Attach(reg, child, parent);
    Hierarchy::Detach(reg, child);

    auto& parentHier = reg.get<Hierarchy::Component>(parent);
    auto& childHier = reg.get<Hierarchy::Component>(child);

    EXPECT_TRUE(IsNull(childHier.Parent));
    EXPECT_TRUE(IsNull(parentHier.FirstChild));
    EXPECT_EQ(parentHier.ChildCount, 0u);
}

TEST(ECS_Hierarchy, Detach_MiddleChild)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity c1 = scene.CreateEntity("C1");
    entt::entity c2 = scene.CreateEntity("C2");
    entt::entity c3 = scene.CreateEntity("C3");

    Hierarchy::Attach(reg, c1, parent);
    Hierarchy::Attach(reg, c2, parent);
    Hierarchy::Attach(reg, c3, parent);

    // Order: c3 -> c2 -> c1 (newest at head)

    // Detach middle child (c2)
    Hierarchy::Detach(reg, c2);

    auto& parentHier = reg.get<Hierarchy::Component>(parent);
    EXPECT_EQ(parentHier.ChildCount, 2u);

    // c3 should now link directly to c1
    auto& h3 = reg.get<Hierarchy::Component>(c3);
    auto& h1 = reg.get<Hierarchy::Component>(c1);

    EXPECT_TRUE(h3.NextSibling == c1);
    EXPECT_TRUE(h1.PrevSibling == c3);
}

TEST(ECS_Hierarchy, Attach_SelfParenting_Ignored)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("SelfLoop");

    // Try to parent to self - should be silently ignored
    Hierarchy::Attach(reg, e, e);

    auto& hier = reg.get<Hierarchy::Component>(e);
    EXPECT_TRUE(IsNull(hier.Parent));
    EXPECT_TRUE(IsNull(hier.FirstChild));
}

TEST(ECS_Hierarchy, Attach_CycleDetection)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity grandparent = scene.CreateEntity("Grandparent");
    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity child = scene.CreateEntity("Child");

    Hierarchy::Attach(reg, parent, grandparent);
    Hierarchy::Attach(reg, child, parent);

    // Try to create a cycle: grandparent -> child (would create GP -> P -> C -> GP)
    Hierarchy::Attach(reg, grandparent, child);

    // Should be rejected - grandparent should still be root
    auto& gpHier = reg.get<Hierarchy::Component>(grandparent);
    EXPECT_TRUE(IsNull(gpHier.Parent));
}

TEST(ECS_Hierarchy, Attach_Reparenting)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity parent1 = scene.CreateEntity("Parent1");
    entt::entity parent2 = scene.CreateEntity("Parent2");
    entt::entity child = scene.CreateEntity("Child");

    Hierarchy::Attach(reg, child, parent1);

    auto& p1Hier = reg.get<Hierarchy::Component>(parent1);
    EXPECT_EQ(p1Hier.ChildCount, 1u);

    // Reparent to parent2
    Hierarchy::Attach(reg, child, parent2);

    EXPECT_EQ(p1Hier.ChildCount, 0u);
    EXPECT_TRUE(IsNull(p1Hier.FirstChild));

    auto& p2Hier = reg.get<Hierarchy::Component>(parent2);
    EXPECT_EQ(p2Hier.ChildCount, 1u);
    EXPECT_TRUE(p2Hier.FirstChild == child);

    auto& childHier = reg.get<Hierarchy::Component>(child);
    EXPECT_TRUE(childHier.Parent == parent2);
}

TEST(ECS_Hierarchy, Attach_NullParent_Detaches)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity child = scene.CreateEntity("Child");

    Hierarchy::Attach(reg, child, parent);

    // Attach to null = detach
    Hierarchy::Attach(reg, child, entt::null);

    auto& childHier = reg.get<Hierarchy::Component>(child);
    EXPECT_TRUE(IsNull(childHier.Parent));

    auto& parentHier = reg.get<Hierarchy::Component>(parent);
    EXPECT_EQ(parentHier.ChildCount, 0u);
}

TEST(ECS_Hierarchy, Attach_AlreadyAttachedToSameParent)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity child = scene.CreateEntity("Child");

    Hierarchy::Attach(reg, child, parent);
    Hierarchy::Attach(reg, child, parent);  // Second attach to same parent

    auto& parentHier = reg.get<Hierarchy::Component>(parent);
    EXPECT_EQ(parentHier.ChildCount, 1u);  // Should still be 1
}

// -----------------------------------------------------------------------------
// Transform Component Tests
// -----------------------------------------------------------------------------

TEST(ECS_Transform, GetMatrix_Identity)
{
    Transform::Component t;

    glm::mat4 mat = Transform::GetMatrix(t);

    EXPECT_EQ(mat, glm::mat4(1.0f));
}

TEST(ECS_Transform, GetMatrix_Translation)
{
    Transform::Component t;
    t.Position = {10.0f, 20.0f, 30.0f};

    glm::mat4 mat = Transform::GetMatrix(t);

    // Extract translation
    EXPECT_FLOAT_EQ(mat[3][0], 10.0f);
    EXPECT_FLOAT_EQ(mat[3][1], 20.0f);
    EXPECT_FLOAT_EQ(mat[3][2], 30.0f);
}

TEST(ECS_Transform, GetMatrix_Scale)
{
    Transform::Component t;
    t.Scale = {2.0f, 3.0f, 4.0f};

    glm::mat4 mat = Transform::GetMatrix(t);

    // Diagonal should have scale values
    EXPECT_FLOAT_EQ(mat[0][0], 2.0f);
    EXPECT_FLOAT_EQ(mat[1][1], 3.0f);
    EXPECT_FLOAT_EQ(mat[2][2], 4.0f);
}

TEST(ECS_Transform, GetMatrix_Rotation90Y)
{
    Transform::Component t;
    t.Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 1, 0));

    glm::mat4 mat = Transform::GetMatrix(t);

    // A 90° rotation around Y should map X -> -Z, Z -> X
    glm::vec4 xAxis = mat * glm::vec4(1, 0, 0, 0);
    EXPECT_NEAR(xAxis.x, 0.0f, 0.001f);
    EXPECT_NEAR(xAxis.z, -1.0f, 0.001f);
}

TEST(ECS_Transform, GetMatrix_Combined)
{
    Transform::Component t;
    t.Position = {5.0f, 0.0f, 0.0f};
    t.Scale = {2.0f, 2.0f, 2.0f};
    t.Rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0, 0, 1));  // Z-axis rotation

    glm::mat4 mat = Transform::GetMatrix(t);

    // Origin in local space should transform to position
    glm::vec4 origin = mat * glm::vec4(0, 0, 0, 1);
    EXPECT_NEAR(origin.x, 5.0f, 0.001f);
    EXPECT_NEAR(origin.y, 0.0f, 0.001f);
    EXPECT_NEAR(origin.z, 0.0f, 0.001f);
}

// -----------------------------------------------------------------------------
// Transform System Tests
// -----------------------------------------------------------------------------

TEST(ECS_TransformSystem, UpdatesWorldMatrix)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Entity");

    // Add WorldMatrix component
    reg.emplace<Transform::WorldMatrix>(e);

    // Modify transform
    auto& t = reg.get<Transform::Component>(e);
    t.Position = {10.0f, 0.0f, 0.0f};

    // Mark dirty
    reg.emplace_or_replace<Transform::IsDirtyTag>(e);

    // Run transform system
    Systems::Transform::OnUpdate(reg);

    auto& world = reg.get<Transform::WorldMatrix>(e);

    // World matrix should now have the translation
    EXPECT_FLOAT_EQ(world.Matrix[3][0], 10.0f);
}

TEST(ECS_TransformSystem, HierarchicalPropagation)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity child = scene.CreateEntity("Child");

    reg.emplace<Transform::WorldMatrix>(parent);
    reg.emplace<Transform::WorldMatrix>(child);

    Hierarchy::Attach(reg, child, parent);

    // Parent at (10, 0, 0)
    auto& parentT = reg.get<Transform::Component>(parent);
    parentT.Position = {10.0f, 0.0f, 0.0f};
    reg.emplace_or_replace<Transform::IsDirtyTag>(parent);

    // Child at (5, 0, 0) local
    auto& childT = reg.get<Transform::Component>(child);
    childT.Position = {5.0f, 0.0f, 0.0f};
    reg.emplace_or_replace<Transform::IsDirtyTag>(child);

    Systems::Transform::OnUpdate(reg);

    // Child world position should be parent + local = (15, 0, 0)
    auto& childWorld = reg.get<Transform::WorldMatrix>(child);
    EXPECT_FLOAT_EQ(childWorld.Matrix[3][0], 15.0f);
}

TEST(ECS_TransformSystem, ParentDirty_PropagatesToChildren)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity child = scene.CreateEntity("Child");

    reg.emplace<Transform::WorldMatrix>(parent);
    reg.emplace<Transform::WorldMatrix>(child);

    Hierarchy::Attach(reg, child, parent);

    // Initial update
    auto& parentT = reg.get<Transform::Component>(parent);
    parentT.Position = {0.0f, 0.0f, 0.0f};
    auto& childT = reg.get<Transform::Component>(child);
    childT.Position = {5.0f, 0.0f, 0.0f};

    reg.emplace_or_replace<Transform::IsDirtyTag>(parent);
    reg.emplace_or_replace<Transform::IsDirtyTag>(child);
    Systems::Transform::OnUpdate(reg);

    // Now move parent only
    parentT.Position = {10.0f, 0.0f, 0.0f};
    reg.emplace_or_replace<Transform::IsDirtyTag>(parent);
    // Child is NOT marked dirty

    Systems::Transform::OnUpdate(reg);

    // Child should still be updated because parent moved
    auto& childWorld = reg.get<Transform::WorldMatrix>(child);
    EXPECT_FLOAT_EQ(childWorld.Matrix[3][0], 15.0f);
}

TEST(ECS_TransformSystem, RemovesDirtyTagAfterUpdate)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Entity");
    reg.emplace<Transform::WorldMatrix>(e);

    reg.emplace_or_replace<Transform::IsDirtyTag>(e);
    EXPECT_TRUE(reg.all_of<Transform::IsDirtyTag>(e));

    Systems::Transform::OnUpdate(reg);

    EXPECT_FALSE(reg.all_of<Transform::IsDirtyTag>(e));
}
