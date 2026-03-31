#include <gtest/gtest.h>

#include <array>
#include <vector>
#include <string>

#include <glm/glm.hpp>
#include <entt/entity/entity.hpp>
#include <entt/entity/registry.hpp>

import Geometry;
import ECS;

using namespace ECS;
using namespace ECS::Components;

// =============================================================================
// C1: SpatialQueries shared concept — verify Octree/KDTree/BVH all work with
// the unified SpatialQueryShape concept and result types
// =============================================================================

TEST(SpatialQueries, OctreeQueryParity_AfterRefactor)
{
    // Regression: ensure Octree queries still work after SpatialQueries extraction
    const std::array<glm::vec3, 8> points{
        glm::vec3{0, 0, 0}, glm::vec3{1, 0, 0}, glm::vec3{0, 1, 0}, glm::vec3{0, 0, 1},
        glm::vec3{1, 1, 0}, glm::vec3{1, 0, 1}, glm::vec3{0, 1, 1}, glm::vec3{1, 1, 1},
    };

    Geometry::Octree octree;
    Geometry::Octree::SplitPolicy policy{};
    ASSERT_TRUE(octree.BuildFromPoints(points, policy, 2u, 8u));

    std::vector<std::size_t> result;
    // Radius must be large enough to overlap the zero-volume point AABBs
    octree.QuerySphere(Geometry::Sphere{.Center = {0.5f, 0.5f, 0.5f}, .Radius = 1.0f}, result);
    EXPECT_FALSE(result.empty());

    std::size_t nearest = Geometry::Octree::kInvalidIndex;
    octree.QueryNearest(glm::vec3{0.1f, 0.1f, 0.1f}, nearest);
    EXPECT_EQ(nearest, 0u); // closest to origin

    std::vector<std::size_t> knn;
    octree.QueryKNN(glm::vec3{0.5f, 0.5f, 0.5f}, 4u, knn);
    EXPECT_EQ(knn.size(), 4u);
}

TEST(SpatialQueries, KDTreeUsesSharedResultTypes)
{
    std::vector<Geometry::AABB> aabbs;
    for (int i = 0; i < 50; ++i)
    {
        const float x = static_cast<float>(i);
        aabbs.push_back(Geometry::AABB{.Min = {x, 0, 0}, .Max = {x + 0.5f, 0.5f, 0.5f}});
    }

    Geometry::KDTree kdtree;
    auto buildResult = kdtree.Build(std::move(aabbs));
    ASSERT_TRUE(buildResult.has_value());
    EXPECT_EQ(buildResult->ElementCount, 50u);
    EXPECT_GT(buildResult->NodeCount, 0u);

    std::vector<Geometry::KDTree::ElementIndex> knnOut;
    auto knnResult = kdtree.QueryKNN(glm::vec3{25.0f, 0.25f, 0.25f}, 5, knnOut);
    ASSERT_TRUE(knnResult.has_value());
    EXPECT_EQ(knnResult->ReturnedCount, 5u);
    EXPECT_GT(knnResult->VisitedNodes, 0u);
}

TEST(SpatialQueries, BVHUsesSharedResultTypes)
{
    std::vector<Geometry::AABB> aabbs;
    for (int i = 0; i < 20; ++i)
    {
        const float x = static_cast<float>(i) * 2.0f;
        aabbs.push_back(Geometry::AABB{.Min = {x, 0, 0}, .Max = {x + 1.0f, 1.0f, 1.0f}});
    }

    Geometry::BVH bvh;
    auto buildResult = bvh.Build(std::move(aabbs));
    ASSERT_TRUE(buildResult.has_value());
    EXPECT_EQ(buildResult->ElementCount, 20u);
    EXPECT_GT(buildResult->NodeCount, 0u);

    std::vector<Geometry::BVH::ElementIndex> queryOut;
    bvh.QuerySphere(Geometry::Sphere{.Center = {10.0f, 0.5f, 0.5f}, .Radius = 3.0f}, queryOut);
    EXPECT_FALSE(queryOut.empty());
}

// =============================================================================
// C5: SceneBootstrap — verify entity creation still emplaces all defaults
// =============================================================================

TEST(SceneBootstrap, EmplaceDefaults_AllComponentsPresent)
{
    entt::registry registry;
    entt::entity e = registry.create();

    SceneBootstrap::EmplaceDefaults(registry, e, "TestEntity");

    EXPECT_TRUE(registry.all_of<NameTag::Component>(e));
    EXPECT_TRUE(registry.all_of<Transform::Component>(e));
    EXPECT_TRUE(registry.all_of<Transform::WorldMatrix>(e));
    EXPECT_TRUE(registry.all_of<Transform::IsDirtyTag>(e));
    EXPECT_TRUE(registry.all_of<Hierarchy::Component>(e));

    auto& name = registry.get<NameTag::Component>(e);
    EXPECT_EQ(name.Name, "TestEntity");
}

TEST(SceneBootstrap, EmplaceDefaults_IdentityTransform)
{
    entt::registry registry;
    entt::entity e = registry.create();

    SceneBootstrap::EmplaceDefaults(registry, e, "Entity");

    auto& t = registry.get<Transform::Component>(e);
    EXPECT_EQ(t.Position, glm::vec3(0.0f));
    EXPECT_EQ(t.Scale, glm::vec3(1.0f));
    EXPECT_FLOAT_EQ(t.Rotation.w, 1.0f);
}

TEST(SceneBootstrap, Scene_CreateEntity_DelegatesToBootstrap)
{
    // Scene::CreateEntity should produce the same result as direct SceneBootstrap
    Scene scene;
    entt::entity e = scene.CreateEntity("Bootstrapped");

    auto& reg = scene.GetRegistry();
    EXPECT_TRUE(reg.all_of<NameTag::Component>(e));
    EXPECT_TRUE(reg.all_of<Transform::Component>(e));
    EXPECT_TRUE(reg.all_of<Transform::WorldMatrix>(e));
    EXPECT_TRUE(reg.all_of<Transform::IsDirtyTag>(e));
    EXPECT_TRUE(reg.all_of<Hierarchy::Component>(e));
}

// =============================================================================
// C6: HierarchyStructure — pure structural invariants
// =============================================================================

TEST(HierarchyStructure, ValidateInvariants_RootEntity)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity e = scene.CreateEntity("Root");

    EXPECT_TRUE(Hierarchy::Structure::ValidateInvariants(reg, e));
}

TEST(HierarchyStructure, ValidateInvariants_ParentChild)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity child = scene.CreateEntity("Child");

    Hierarchy::Attach(reg, child, parent);

    EXPECT_TRUE(Hierarchy::Structure::ValidateInvariants(reg, parent));
    EXPECT_TRUE(Hierarchy::Structure::ValidateInvariants(reg, child));
}

TEST(HierarchyStructure, ValidateInvariants_MultipleChildren)
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

    EXPECT_TRUE(Hierarchy::Structure::ValidateInvariants(reg, parent));
    EXPECT_TRUE(Hierarchy::Structure::ValidateInvariants(reg, c1));
    EXPECT_TRUE(Hierarchy::Structure::ValidateInvariants(reg, c2));
    EXPECT_TRUE(Hierarchy::Structure::ValidateInvariants(reg, c3));
}

TEST(HierarchyStructure, ValidateInvariants_AfterDetach)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity child = scene.CreateEntity("Child");

    Hierarchy::Attach(reg, child, parent);
    Hierarchy::Detach(reg, child);

    EXPECT_TRUE(Hierarchy::Structure::ValidateInvariants(reg, parent));
    EXPECT_TRUE(Hierarchy::Structure::ValidateInvariants(reg, child));
}

TEST(HierarchyStructure, ValidateInvariants_DeepHierarchy)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    // Build a chain of 100 entities
    std::vector<entt::entity> chain;
    chain.push_back(scene.CreateEntity("Root"));

    for (int i = 1; i < 100; ++i)
    {
        entt::entity e = scene.CreateEntity("Node_" + std::to_string(i));
        Hierarchy::Attach(reg, e, chain.back());
        chain.push_back(e);
    }

    // All should validate
    for (auto e : chain)
    {
        EXPECT_TRUE(Hierarchy::Structure::ValidateInvariants(reg, e));
    }
}

TEST(HierarchyStructure, IsDescendant_DetectsAncestry)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity gp = scene.CreateEntity("GP");
    entt::entity p = scene.CreateEntity("P");
    entt::entity c = scene.CreateEntity("C");

    Hierarchy::Attach(reg, p, gp);
    Hierarchy::Attach(reg, c, p);

    // c is a descendant of gp
    EXPECT_TRUE(Hierarchy::Structure::IsDescendant(reg, gp, c));
    EXPECT_TRUE(Hierarchy::Structure::IsDescendant(reg, gp, p));

    // gp is NOT a descendant of c
    EXPECT_FALSE(Hierarchy::Structure::IsDescendant(reg, c, gp));
}

TEST(HierarchyStructure, IsDescendant_TerminatesOnCorruptedCycle)
{
    // Manually corrupt the hierarchy to create a parent cycle: A -> B -> C -> A.
    // IsDescendant must terminate (bounded walk) instead of looping forever.
    entt::registry reg;
    entt::entity a = reg.create();
    entt::entity b = reg.create();
    entt::entity c = reg.create();
    entt::entity unrelated = reg.create();

    auto& ha = reg.emplace<Hierarchy::Component>(a);
    auto& hb = reg.emplace<Hierarchy::Component>(b);
    auto& hc = reg.emplace<Hierarchy::Component>(c);
    reg.emplace<Hierarchy::Component>(unrelated);

    // Corrupt: A -> B -> C -> A (parent cycle)
    ha.Parent = c;
    hb.Parent = a;
    hc.Parent = b;

    // Query: is 'unrelated' a descendant of 'a'?
    // Walk from 'unrelated' upward: unrelated.Parent = null → terminates immediately.
    EXPECT_FALSE(Hierarchy::Structure::IsDescendant(reg, a, unrelated));

    // The critical test: is 'unrelated' an ancestor of 'b'?
    // Walk from 'b' upward: b→a→c→b→a→c→... (infinite cycle).
    // Never matches 'unrelated'. Must terminate via bounded walk, returning false.
    EXPECT_FALSE(Hierarchy::Structure::IsDescendant(reg, unrelated, b));
    EXPECT_FALSE(Hierarchy::Structure::IsDescendant(reg, unrelated, a));
    EXPECT_FALSE(Hierarchy::Structure::IsDescendant(reg, unrelated, c));
}

TEST(HierarchyStructure, ValidateInvariants_DetectsCorruptedChildCount)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity parent = scene.CreateEntity("Parent");
    entt::entity child = scene.CreateEntity("Child");

    Hierarchy::Attach(reg, child, parent);

    // Corrupt the ChildCount
    auto& parentHier = reg.get<Hierarchy::Component>(parent);
    parentHier.ChildCount = 5; // wrong — should be 1

    EXPECT_FALSE(Hierarchy::Structure::ValidateInvariants(reg, parent));
}

TEST(HierarchyStructure, StressTest_WideTree)
{
    Scene scene;
    auto& reg = scene.GetRegistry();

    entt::entity root = scene.CreateEntity("Root");

    // Attach 500 children to root
    for (int i = 0; i < 500; ++i)
    {
        entt::entity child = scene.CreateEntity("Child_" + std::to_string(i));
        Hierarchy::Attach(reg, child, root);
    }

    EXPECT_TRUE(Hierarchy::Structure::ValidateInvariants(reg, root));

    auto& rootHier = reg.get<Hierarchy::Component>(root);
    EXPECT_EQ(rootHier.ChildCount, 500u);
}
