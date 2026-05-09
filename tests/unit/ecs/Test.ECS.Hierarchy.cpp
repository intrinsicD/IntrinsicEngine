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
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Hierarchy.Structure;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::InvalidEntityHandle;
using Extrinsic::ECS::Hierarchy::Attach;
using Extrinsic::ECS::Hierarchy::Detach;
using Extrinsic::ECS::Scene::CreateDefault;
using Extrinsic::ECS::Scene::Registry;
namespace Components = Extrinsic::ECS::Components;
namespace Structure = Extrinsic::ECS::Hierarchy::Structure;

namespace
{
    Components::Hierarchy::Component const& Hier(const Registry& r, EntityHandle e)
    {
        return r.Raw().get<Components::Hierarchy::Component>(e);
    }
}

TEST(ECSHierarchy, AttachSetsParentChildPointersAndChildCount)
{
    Registry r;
    const EntityHandle parent = CreateDefault(r, "P");
    const EntityHandle child = CreateDefault(r, "C");

    Attach(r.Raw(), child, parent);

    EXPECT_EQ(Hier(r, child).Parent, parent);
    EXPECT_EQ(Hier(r, parent).FirstChild, child);
    EXPECT_EQ(Hier(r, parent).ChildCount, 1u);
    EXPECT_EQ(Hier(r, child).NextSibling, InvalidEntityHandle);
    EXPECT_EQ(Hier(r, child).PrevSibling, InvalidEntityHandle);
}

TEST(ECSHierarchy, AttachIsHeadInsertionAndLinksOldHeadAsNextSibling)
{
    Registry r;
    const EntityHandle parent = CreateDefault(r, "P");
    const EntityHandle a = CreateDefault(r, "A");
    const EntityHandle b = CreateDefault(r, "B");

    Attach(r.Raw(), a, parent);
    Attach(r.Raw(), b, parent);

    // Newest attach becomes head.
    EXPECT_EQ(Hier(r, parent).FirstChild, b);
    EXPECT_EQ(Hier(r, parent).ChildCount, 2u);
    EXPECT_EQ(Hier(r, b).NextSibling, a);
    EXPECT_EQ(Hier(r, a).PrevSibling, b);
    EXPECT_EQ(Hier(r, a).NextSibling, InvalidEntityHandle);
    EXPECT_EQ(Hier(r, b).PrevSibling, InvalidEntityHandle);
}

TEST(ECSHierarchy, DetachClearsLinksAndDecrementsChildCount)
{
    Registry r;
    const EntityHandle parent = CreateDefault(r, "P");
    const EntityHandle child = CreateDefault(r, "C");
    Attach(r.Raw(), child, parent);

    Detach(r.Raw(), child);

    EXPECT_EQ(Hier(r, child).Parent, InvalidEntityHandle);
    EXPECT_EQ(Hier(r, child).NextSibling, InvalidEntityHandle);
    EXPECT_EQ(Hier(r, child).PrevSibling, InvalidEntityHandle);
    EXPECT_EQ(Hier(r, parent).FirstChild, InvalidEntityHandle);
    EXPECT_EQ(Hier(r, parent).ChildCount, 0u);
}

TEST(ECSHierarchy, DetachMiddleSiblingPreservesChainIntegrity)
{
    Registry r;
    const EntityHandle parent = CreateDefault(r, "P");
    const EntityHandle a = CreateDefault(r, "A");
    const EntityHandle b = CreateDefault(r, "B");
    const EntityHandle c = CreateDefault(r, "C");
    Attach(r.Raw(), a, parent);
    Attach(r.Raw(), b, parent);
    Attach(r.Raw(), c, parent); // head insertion → order: c, b, a

    Detach(r.Raw(), b);

    EXPECT_EQ(Hier(r, parent).ChildCount, 2u);
    EXPECT_EQ(Hier(r, parent).FirstChild, c);
    EXPECT_EQ(Hier(r, c).NextSibling, a);
    EXPECT_EQ(Hier(r, a).PrevSibling, c);
    EXPECT_EQ(Hier(r, b).Parent, InvalidEntityHandle);
}

TEST(ECSHierarchy, AttachToInvalidHandleIsNoOp)
{
    Registry r;
    const EntityHandle child = CreateDefault(r, "C");

    Attach(r.Raw(), child, InvalidEntityHandle);

    EXPECT_EQ(Hier(r, child).Parent, InvalidEntityHandle);
}

TEST(ECSHierarchy, AttachSelfRejected)
{
    Registry r;
    const EntityHandle e = CreateDefault(r, "E");

    Attach(r.Raw(), e, e);

    EXPECT_EQ(Hier(r, e).Parent, InvalidEntityHandle);
    EXPECT_EQ(Hier(r, e).FirstChild, InvalidEntityHandle);
    EXPECT_EQ(Hier(r, e).ChildCount, 0u);
}

TEST(ECSHierarchy, AttachCycleRejected)
{
    Registry r;
    const EntityHandle a = CreateDefault(r, "A");
    const EntityHandle b = CreateDefault(r, "B");
    const EntityHandle c = CreateDefault(r, "C");

    Attach(r.Raw(), b, a);
    Attach(r.Raw(), c, b);

    // Attempt to make A a child of C (its grandchild) — must be rejected.
    Attach(r.Raw(), a, c);

    EXPECT_EQ(Hier(r, a).Parent, InvalidEntityHandle);
    EXPECT_EQ(Hier(r, c).FirstChild, InvalidEntityHandle);
    EXPECT_EQ(Hier(r, c).ChildCount, 0u);
}

TEST(ECSHierarchy, AttachToSameParentIsIdempotent)
{
    Registry r;
    const EntityHandle parent = CreateDefault(r, "P");
    const EntityHandle child = CreateDefault(r, "C");
    Attach(r.Raw(), child, parent);

    Attach(r.Raw(), child, parent);

    EXPECT_EQ(Hier(r, parent).ChildCount, 1u);
    EXPECT_EQ(Hier(r, parent).FirstChild, child);
    EXPECT_EQ(Hier(r, child).Parent, parent);
}

TEST(ECSHierarchy, ReparentMovesChildBetweenParents)
{
    Registry r;
    const EntityHandle p1 = CreateDefault(r, "P1");
    const EntityHandle p2 = CreateDefault(r, "P2");
    const EntityHandle child = CreateDefault(r, "C");
    Attach(r.Raw(), child, p1);

    Attach(r.Raw(), child, p2);

    EXPECT_EQ(Hier(r, child).Parent, p2);
    EXPECT_EQ(Hier(r, p1).ChildCount, 0u);
    EXPECT_EQ(Hier(r, p1).FirstChild, InvalidEntityHandle);
    EXPECT_EQ(Hier(r, p2).ChildCount, 1u);
    EXPECT_EQ(Hier(r, p2).FirstChild, child);
}

TEST(ECSHierarchy, ReparentingPreservesChildWorldPosition)
{
    Registry r;
    auto& raw = r.Raw();

    // Two parents at distinct world translations; their world matrices must be
    // populated and clean (no IsDirtyTag) so the mutation can run the
    // world-preservation path.
    const EntityHandle p1 = CreateDefault(r, "P1");
    const EntityHandle p2 = CreateDefault(r, "P2");
    const EntityHandle child = CreateDefault(r, "C");

    raw.get<Components::Transform::WorldMatrix>(p1).Matrix = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));
    raw.get<Components::Transform::WorldMatrix>(p2).Matrix = glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 0.0f, 0.0f));

    auto& childLocal = raw.get<Components::Transform::Component>(child);
    childLocal.Position = glm::vec3(2.0f, 0.0f, 0.0f);
    raw.get<Components::Transform::WorldMatrix>(child).Matrix = glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 0.0f, 0.0f));

    Attach(r.Raw(), child, p1);
    EXPECT_TRUE(raw.all_of<Components::Transform::IsDirtyTag>(child));
    raw.remove<Components::Transform::IsDirtyTag>(child);
    raw.get<Components::Transform::WorldMatrix>(child).Matrix = glm::translate(glm::mat4(1.0f), glm::vec3(7.0f, 0.0f, 0.0f));
    raw.get<Components::Transform::Component>(child).Position = glm::vec3(2.0f, 0.0f, 0.0f);

    Attach(r.Raw(), child, p2);

    // Mutation must mark dirty so the next traversal recomputes the matrix.
    EXPECT_TRUE(raw.all_of<Components::Transform::IsDirtyTag>(child));

    // Local TRS now expresses the child in p2's frame: world (7,0,0) under
    // parent (-3,0,0) → local (10,0,0).
    EXPECT_NEAR(childLocal.Position.x, 10.0f, 1e-5f);
    EXPECT_NEAR(childLocal.Position.y, 0.0f, 1e-5f);
    EXPECT_NEAR(childLocal.Position.z, 0.0f, 1e-5f);
}

TEST(ECSHierarchy, ReparentingWithSingularParentResetsLocalToIdentity)
{
    Registry r;
    auto& raw = r.Raw();

    const EntityHandle parent = CreateDefault(r, "P");
    const EntityHandle child = CreateDefault(r, "C");

    // Singular world matrix (rank-deficient): zero scale on X.
    auto& parentWorld = raw.get<Components::Transform::WorldMatrix>(parent).Matrix;
    parentWorld = glm::mat4(1.0f);
    parentWorld[0][0] = 0.0f;

    raw.get<Components::Transform::Component>(child).Position = glm::vec3(4.0f, 5.0f, 6.0f);
    raw.get<Components::Transform::WorldMatrix>(child).Matrix = glm::translate(glm::mat4(1.0f), glm::vec3(4.0f, 5.0f, 6.0f));

    Attach(r.Raw(), child, parent);

    const auto& local = raw.get<Components::Transform::Component>(child);
    EXPECT_FLOAT_EQ(local.Position.x, 0.0f);
    EXPECT_FLOAT_EQ(local.Position.y, 0.0f);
    EXPECT_FLOAT_EQ(local.Position.z, 0.0f);
    EXPECT_FLOAT_EQ(local.Scale.x, 1.0f);
    EXPECT_TRUE(raw.all_of<Components::Transform::IsDirtyTag>(child));
}

TEST(ECSHierarchy, AttachMarksChildDirtyEvenWhenParentTransformNotReady)
{
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle parent = CreateDefault(r, "P");
    const EntityHandle child = CreateDefault(r, "C");

    // Parent flagged dirty → mutation should not run preserve-world path,
    // but child must still be marked IsDirtyTag for the next traversal.
    raw.emplace<Components::Transform::IsDirtyTag>(parent);

    Attach(r.Raw(), child, parent);

    EXPECT_TRUE(raw.all_of<Components::Transform::IsDirtyTag>(child));
    EXPECT_EQ(Hier(r, child).Parent, parent);
}

TEST(ECSHierarchy, StructureValidateInvariantsHoldsForLinkedChildren)
{
    Registry r;
    const EntityHandle parent = CreateDefault(r, "P");
    const EntityHandle a = CreateDefault(r, "A");
    const EntityHandle b = CreateDefault(r, "B");
    Attach(r.Raw(), a, parent);
    Attach(r.Raw(), b, parent);

    EXPECT_TRUE(Structure::ValidateInvariants(r.Raw(), parent));
    EXPECT_TRUE(Structure::ValidateInvariants(r.Raw(), a));
    EXPECT_TRUE(Structure::ValidateInvariants(r.Raw(), b));
}

TEST(ECSHierarchy, IsDescendantWalksAncestryChain)
{
    Registry r;
    const EntityHandle a = CreateDefault(r, "A");
    const EntityHandle b = CreateDefault(r, "B");
    const EntityHandle c = CreateDefault(r, "C");
    Attach(r.Raw(), b, a);
    Attach(r.Raw(), c, b);

    EXPECT_TRUE(Structure::IsDescendant(r.Raw(), a, c));
    EXPECT_TRUE(Structure::IsDescendant(r.Raw(), a, b));
    EXPECT_FALSE(Structure::IsDescendant(r.Raw(), c, a));
}
