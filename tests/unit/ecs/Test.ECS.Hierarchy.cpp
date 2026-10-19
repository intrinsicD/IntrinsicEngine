#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Bootstrap;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Hierarchy.Mutation;
import Extrinsic.ECS.Hierarchy.Structure;
import Extrinsic.ECS.System.TransformHierarchy;

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

    void ExpectQueryFailure(
        const Structure::HierarchyQueryResult& result,
        const Structure::HierarchyQueryStatus expectedStatus)
    {
        EXPECT_EQ(result.Status, expectedStatus);
        EXPECT_FALSE(result.Succeeded());
        EXPECT_TRUE(result.Entities.empty());
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

TEST(ECSHierarchy, CollectQueriesPreserveOrderLeafSemanticsAndDeterminism)
{
    Registry r;
    const EntityHandle root = CreateDefault(r, "Root");
    const EntityHandle a = CreateDefault(r, "A");
    const EntityHandle b = CreateDefault(r, "B");
    const EntityHandle a1 = CreateDefault(r, "A1");
    const EntityHandle a2 = CreateDefault(r, "A2");
    const EntityHandle b1 = CreateDefault(r, "B1");
    const EntityHandle bareLeaf = r.Create();

    Attach(r.Raw(), a, root);
    Attach(r.Raw(), b, root);
    Attach(r.Raw(), a1, a);
    Attach(r.Raw(), a2, a);
    Attach(r.Raw(), b1, b);

    const Structure::HierarchyQueryResult children =
        Structure::CollectChildren(r.Raw(), root);
    ASSERT_TRUE(children.Succeeded());
    EXPECT_EQ(children.Entities, (std::vector<EntityHandle>{b, a}));

    const Structure::HierarchyQueryResult descendants =
        Structure::CollectDescendantsPreorder(r.Raw(), root);
    ASSERT_TRUE(descendants.Succeeded());
    EXPECT_EQ(
        descendants.Entities,
        (std::vector<EntityHandle>{b, b1, a, a2, a1}));

    const Structure::HierarchyQueryResult repeated =
        Structure::CollectDescendantsPreorder(r.Raw(), root);
    EXPECT_EQ(repeated.Status, descendants.Status);
    EXPECT_EQ(repeated.Entities, descendants.Entities);

    const Structure::HierarchyQueryResult componentLeaf =
        Structure::CollectChildren(r.Raw(), a1);
    EXPECT_TRUE(componentLeaf.Succeeded());
    EXPECT_TRUE(componentLeaf.Entities.empty());

    const Structure::HierarchyQueryResult noComponentLeaf =
        Structure::CollectDescendantsPreorder(r.Raw(), bareLeaf);
    EXPECT_TRUE(noComponentLeaf.Succeeded());
    EXPECT_TRUE(noComponentLeaf.Entities.empty());
}

TEST(ECSHierarchy, CollectQueriesRejectInvalidDanglingAndMissingDataWithoutPrefixes)
{
    {
        Registry r;
        ExpectQueryFailure(
            Structure::CollectChildren(r.Raw(), InvalidEntityHandle),
            Structure::HierarchyQueryStatus::InvalidRoot);
        ExpectQueryFailure(
            Structure::CollectDescendantsPreorder(
                r.Raw(),
                InvalidEntityHandle),
            Structure::HierarchyQueryStatus::InvalidRoot);
    }

    {
        Registry r;
        auto& raw = r.Raw();
        const EntityHandle root = raw.create();
        const EntityHandle danglingChild = raw.create();
        raw.destroy(danglingChild);
        raw.emplace<Components::Hierarchy::Component>(
            root,
            Components::Hierarchy::Component{
                .FirstChild = danglingChild,
                .ChildCount = 1u,
            });

        ExpectQueryFailure(
            Structure::CollectChildren(raw, root),
            Structure::HierarchyQueryStatus::DanglingLink);
    }

    {
        Registry r;
        auto& raw = r.Raw();
        const EntityHandle root = raw.create();
        const EntityHandle child = raw.create();
        const EntityHandle danglingSibling = raw.create();
        raw.destroy(danglingSibling);
        raw.emplace<Components::Hierarchy::Component>(
            root,
            Components::Hierarchy::Component{
                .FirstChild = child,
                .ChildCount = 2u,
            });
        raw.emplace<Components::Hierarchy::Component>(
            child,
            Components::Hierarchy::Component{
                .Parent = root,
                .NextSibling = danglingSibling,
            });

        ExpectQueryFailure(
            Structure::CollectChildren(raw, root),
            Structure::HierarchyQueryStatus::DanglingLink);
    }

    {
        Registry r;
        auto& raw = r.Raw();
        const EntityHandle root = raw.create();
        const EntityHandle childWithoutHierarchy = raw.create();
        raw.emplace<Components::Hierarchy::Component>(
            root,
            Components::Hierarchy::Component{
                .FirstChild = childWithoutHierarchy,
                .ChildCount = 1u,
            });

        ExpectQueryFailure(
            Structure::CollectChildren(raw, root),
            Structure::HierarchyQueryStatus::MissingChildHierarchy);
    }
}

TEST(ECSHierarchy, CollectChildrenRejectsParentBacklinkAndCountCorruption)
{
    {
        Registry r;
        auto& raw = r.Raw();
        const EntityHandle root = raw.create();
        const EntityHandle child = raw.create();
        raw.emplace<Components::Hierarchy::Component>(
            root,
            Components::Hierarchy::Component{
                .FirstChild = child,
                .ChildCount = 1u,
            });
        raw.emplace<Components::Hierarchy::Component>(
            child,
            Components::Hierarchy::Component{});

        ExpectQueryFailure(
            Structure::CollectChildren(raw, root),
            Structure::HierarchyQueryStatus::ParentMismatch);
    }

    {
        Registry r;
        auto& raw = r.Raw();
        const EntityHandle root = raw.create();
        const EntityHandle child = raw.create();
        const EntityHandle wrongPrevious = raw.create();
        raw.emplace<Components::Hierarchy::Component>(
            root,
            Components::Hierarchy::Component{
                .FirstChild = child,
                .ChildCount = 1u,
            });
        raw.emplace<Components::Hierarchy::Component>(
            child,
            Components::Hierarchy::Component{
                .Parent = root,
                .PrevSibling = wrongPrevious,
            });

        ExpectQueryFailure(
            Structure::CollectChildren(raw, root),
            Structure::HierarchyQueryStatus::SiblingBacklinkMismatch);
    }

    {
        Registry r;
        auto& raw = r.Raw();
        const EntityHandle root = raw.create();
        const EntityHandle first = raw.create();
        const EntityHandle second = raw.create();
        raw.emplace<Components::Hierarchy::Component>(
            root,
            Components::Hierarchy::Component{
                .FirstChild = first,
                .ChildCount = 2u,
            });
        raw.emplace<Components::Hierarchy::Component>(
            first,
            Components::Hierarchy::Component{
                .Parent = root,
                .NextSibling = second,
            });
        raw.emplace<Components::Hierarchy::Component>(
            second,
            Components::Hierarchy::Component{
                .Parent = root,
            });

        ExpectQueryFailure(
            Structure::CollectChildren(raw, root),
            Structure::HierarchyQueryStatus::SiblingBacklinkMismatch);
        EXPECT_FALSE(Structure::ValidateInvariants(raw, root));
    }

    {
        Registry r;
        auto& raw = r.Raw();
        const EntityHandle root = raw.create();
        const EntityHandle child = raw.create();
        raw.emplace<Components::Hierarchy::Component>(
            root,
            Components::Hierarchy::Component{
                .FirstChild = child,
                .ChildCount = 0u,
            });
        raw.emplace<Components::Hierarchy::Component>(
            child,
            Components::Hierarchy::Component{.Parent = root});

        ExpectQueryFailure(
            Structure::CollectChildren(raw, root),
            Structure::HierarchyQueryStatus::ChildCountMismatch);
    }

    {
        Registry r;
        auto& raw = r.Raw();
        const EntityHandle root = raw.create();
        raw.emplace<Components::Hierarchy::Component>(
            root,
            Components::Hierarchy::Component{.ChildCount = 1u});

        ExpectQueryFailure(
            Structure::CollectChildren(raw, root),
            Structure::HierarchyQueryStatus::ChildCountMismatch);
    }

    {
        Registry r;
        auto& raw = r.Raw();
        const EntityHandle root = raw.create();
        const EntityHandle child = raw.create();
        raw.emplace<Components::Hierarchy::Component>(
            root,
            Components::Hierarchy::Component{
                .FirstChild = child,
                .ChildCount = 2u,
            });
        raw.emplace<Components::Hierarchy::Component>(
            child,
            Components::Hierarchy::Component{.Parent = root});

        ExpectQueryFailure(
            Structure::CollectChildren(raw, root),
            Structure::HierarchyQueryStatus::ChildCountMismatch);
    }
}

TEST(ECSHierarchy, CollectQueriesRejectDuplicateLinksAndHierarchyCycles)
{
    {
        Registry r;
        auto& raw = r.Raw();
        const EntityHandle root = raw.create();
        const EntityHandle first = raw.create();
        const EntityHandle second = raw.create();
        raw.emplace<Components::Hierarchy::Component>(
            root,
            Components::Hierarchy::Component{
                .FirstChild = first,
                .ChildCount = 3u,
            });
        raw.emplace<Components::Hierarchy::Component>(
            first,
            Components::Hierarchy::Component{
                .Parent = root,
                .NextSibling = second,
            });
        raw.emplace<Components::Hierarchy::Component>(
            second,
            Components::Hierarchy::Component{
                .Parent = root,
                .NextSibling = first,
                .PrevSibling = first,
            });

        ExpectQueryFailure(
            Structure::CollectChildren(raw, root),
            Structure::HierarchyQueryStatus::DuplicateOrCycle);
    }

    {
        Registry r;
        auto& raw = r.Raw();
        const EntityHandle root = raw.create();
        const EntityHandle child = raw.create();
        raw.emplace<Components::Hierarchy::Component>(
            root,
            Components::Hierarchy::Component{
                .Parent = child,
                .FirstChild = child,
                .ChildCount = 1u,
            });
        raw.emplace<Components::Hierarchy::Component>(
            child,
            Components::Hierarchy::Component{
                .Parent = root,
                .FirstChild = root,
                .ChildCount = 1u,
            });

        ExpectQueryFailure(
            Structure::CollectDescendantsPreorder(raw, root),
            Structure::HierarchyQueryStatus::DuplicateOrCycle);
    }
}

TEST(ECSHierarchy, CollectQueriesFailClosedWhenTraversalLimitIsExceeded)
{
    Registry r;
    auto& raw = r.Raw();
    const EntityHandle root = raw.create();
    std::vector<EntityHandle> children{};
    children.reserve(
        static_cast<std::size_t>(Structure::kMaxHierarchyQueryEntities) + 1u);
    for (std::uint32_t i = 0u;
         i <= Structure::kMaxHierarchyQueryEntities;
         ++i)
    {
        children.push_back(raw.create());
    }

    raw.emplace<Components::Hierarchy::Component>(
        root,
        Components::Hierarchy::Component{
            .FirstChild = children.front(),
            .ChildCount = static_cast<std::uint32_t>(children.size()),
        });
    for (std::size_t i = 0u; i < children.size(); ++i)
    {
        // Outside the initializer: Clang coverage miscounts a ?: with a constant operand inside braces (BUG-242).
        const EntityHandle next = i + 1u < children.size() ? children[i + 1u] : InvalidEntityHandle;
        const EntityHandle prev = i > 0u ? children[i - 1u] : InvalidEntityHandle;
        raw.emplace<Components::Hierarchy::Component>(children[i],
            Components::Hierarchy::Component{
                .Parent = root,
                .NextSibling = next,
                .PrevSibling = prev,
            });
    }

    ExpectQueryFailure(
        Structure::CollectChildren(raw, root),
        Structure::HierarchyQueryStatus::TraversalLimitExceeded);
    ExpectQueryFailure(
        Structure::CollectDescendantsPreorder(raw, root),
        Structure::HierarchyQueryStatus::TraversalLimitExceeded);
}


TEST(ECSHierarchy, DetachPreservesWorldPoseThroughUpdateAndNextLocalEdit)
{
    Registry scene;
    auto& raw = scene.Raw();
    const auto parent = CreateDefault(scene, "Parent");
    const auto child = CreateDefault(scene, "Child");
    auto& parentLocal = raw.get<Components::Transform::Component>(parent);
    parentLocal.Position.x = 10.0f;
    parentLocal.Rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3{0, 0, 1});
    parentLocal.Scale = glm::vec3{2.0f};
    raw.get<Components::Transform::Component>(child).Position.x = 2.0f;
    raw.emplace<Components::Transform::IsDirtyTag>(parent);
    raw.emplace<Components::Transform::IsDirtyTag>(child);
    Attach(raw, child, parent);
    Extrinsic::ECS::Systems::TransformHierarchy::OnUpdate(raw);
    const auto before = raw.get<Components::Transform::WorldMatrix>(child).Matrix;

    Detach(raw, child);

    EXPECT_EQ(Hier(scene, child).Parent, InvalidEntityHandle);
    for (int axis = 0; axis < 3; ++axis)
        EXPECT_NEAR(raw.get<Components::Transform::Component>(child).Position[axis], before[3][axis], 1.0e-5f);
    EXPECT_TRUE(raw.all_of<Components::Transform::IsDirtyTag>(child));
    Extrinsic::ECS::Systems::TransformHierarchy::OnUpdate(raw);
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            EXPECT_NEAR(raw.get<Components::Transform::WorldMatrix>(child).Matrix[column][row], before[column][row], 1.0e-5f);
    raw.get<Components::Transform::Component>(child).Position.x += 1.0f;
    raw.emplace<Components::Transform::IsDirtyTag>(child);
    Extrinsic::ECS::Systems::TransformHierarchy::OnUpdate(raw);
    auto afterEdit = before;
    afterEdit[3].x += 1.0f;
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            EXPECT_NEAR(raw.get<Components::Transform::WorldMatrix>(child).Matrix[column][row], afterEdit[column][row], 1.0e-5f);
}

TEST(ECSHierarchy, ReparentRejectsUnrepresentableLocalWithoutChangingHierarchy)
{
    for (const bool nonFiniteParent : {false, true})
    {
        SCOPED_TRACE(nonFiniteParent ? "non-finite parent" : "required shear");
        Registry scene;
        auto& raw = scene.Raw();
        const auto parent = CreateDefault(scene, "Old parent");
        const auto target = CreateDefault(scene, "Scaled parent");
        const auto child = CreateDefault(scene, "Rotated child");
        Attach(raw, child, parent);
        raw.remove<Components::Transform::IsDirtyTag>(child);
        auto& local = raw.get<Components::Transform::Component>(child);
        local.Rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3{0, 0, 1});
        const auto before = Components::Transform::GetMatrix(local);
        raw.get<Components::Transform::WorldMatrix>(child).Matrix = before;
        raw.get<Components::Transform::WorldMatrix>(target).Matrix =
            glm::scale(glm::mat4{1.0f}, glm::vec3{2, 1, 1});

        if (nonFiniteParent)
            raw.get<Components::Transform::WorldMatrix>(target).Matrix[0][0] =
                std::numeric_limits<float>::quiet_NaN();

        Attach(raw, child, target);

        EXPECT_EQ(Hier(scene, child).Parent, parent);
        EXPECT_EQ(Hier(scene, parent).FirstChild, child);
        EXPECT_EQ(Hier(scene, parent).ChildCount, 1u);
        EXPECT_EQ(Hier(scene, target).FirstChild, InvalidEntityHandle);
        EXPECT_EQ(Hier(scene, target).ChildCount, 0u);
        EXPECT_EQ(Components::Transform::GetMatrix(local), before);
        EXPECT_EQ(raw.get<Components::Transform::WorldMatrix>(child).Matrix, before);
        EXPECT_FALSE(raw.all_of<Components::Transform::IsDirtyTag>(child));
    }
}

TEST(ECSHierarchy, DetachShearedReadyChildUnlinksAndPreservesTranslation)
{
    Registry scene;
    auto& raw = scene.Raw();
    const auto parent = CreateDefault(scene, "Parent");
    const auto child = CreateDefault(scene, "Child");
    Attach(raw, child, parent);
    raw.remove<Components::Transform::IsDirtyTag>(child);
    auto& world = raw.get<Components::Transform::WorldMatrix>(child).Matrix;
    world[1][0] = 0.5f;
    world[3] = glm::vec4{7, 8, 9, 1};

    Detach(raw, child);

    EXPECT_EQ(Hier(scene, child).Parent, InvalidEntityHandle);
    EXPECT_EQ(Hier(scene, parent).FirstChild, InvalidEntityHandle);
    EXPECT_EQ(Hier(scene, parent).ChildCount, 0u);
    EXPECT_TRUE(raw.all_of<Components::Transform::IsDirtyTag>(child));
    EXPECT_EQ(raw.get<Components::Transform::Component>(child).Position, (glm::vec3{7, 8, 9}));
    Extrinsic::ECS::Systems::TransformHierarchy::OnUpdate(raw);
    EXPECT_EQ(glm::vec3(raw.get<Components::Transform::WorldMatrix>(child).Matrix[3]), (glm::vec3{7, 8, 9}));
}

TEST(ECSHierarchy, DetachAllowsTeardownLoopToProgressForShearedAndNonFiniteChildren)
{
    Registry scene;
    auto& raw = scene.Raw();
    const auto parent = CreateDefault(scene, "Parent");
    const auto sheared = CreateDefault(scene, "Sheared child");
    const auto nonFinite = CreateDefault(scene, "Non-finite child");
    Attach(raw, sheared, parent);
    Attach(raw, nonFinite, parent);
    raw.remove<Components::Transform::IsDirtyTag>(sheared);
    raw.remove<Components::Transform::IsDirtyTag>(nonFinite);
    raw.get<Components::Transform::WorldMatrix>(sheared).Matrix[1][0] = 0.5f;
    raw.get<Components::Transform::WorldMatrix>(nonFinite).Matrix[3].x =
        std::numeric_limits<float>::infinity();
    raw.get<Components::Transform::Component>(nonFinite).Position = glm::vec3{3, 4, 5};
    const auto authored = Components::Transform::GetMatrix(
        raw.get<Components::Transform::Component>(nonFinite));

    // Same progress requirement as model materialization teardown, bounded here
    // so refusing Detach fails the assertion instead of hanging the test runner.
    unsigned attempts = 0;
    while (Hier(scene, parent).FirstChild != InvalidEntityHandle && attempts < 3u)
    {
        Detach(raw, Hier(scene, parent).FirstChild);
        ++attempts;
    }

    EXPECT_EQ(attempts, 2u);
    EXPECT_EQ(Hier(scene, parent).FirstChild, InvalidEntityHandle);
    EXPECT_EQ(Hier(scene, parent).ChildCount, 0u);
    EXPECT_EQ(Hier(scene, sheared).Parent, InvalidEntityHandle);
    EXPECT_EQ(Hier(scene, nonFinite).Parent, InvalidEntityHandle);
    EXPECT_EQ(Components::Transform::GetMatrix(
        raw.get<Components::Transform::Component>(nonFinite)), authored);
}

TEST(ECSHierarchy, DecomposeRejectsNonFiniteTranslationWithoutChangingOutput)
{
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity()})
    {
        glm::mat4 matrix{1.0f};
        matrix[3].x = invalid;
        Components::Transform::Component result{.Position = {7, 8, 9}, .Scale = {2, 3, 4}};
        const auto before = Components::Transform::GetMatrix(result);
        EXPECT_FALSE(Components::Transform::TryDecomposeMatrix(matrix, result));
        EXPECT_EQ(Components::Transform::GetMatrix(result), before);
    }
}

TEST(ECSHierarchy, LocalTransformRejectsShearAndPerspectiveWithoutChangingOutput)
{
    for (const bool perspective : {false, true})
    {
        glm::mat4 matrix{1.0f};
        matrix[3].x = 1.0e8f; // Translation magnitude must not hide basis/affine errors.
        if (perspective) matrix[0][3] = 0.25f;
        else matrix[1][0] = 0.5f;
        Components::Transform::Component result{.Position = {7, 8, 9}, .Scale = {2, 3, 4}};
        const auto before = Components::Transform::GetMatrix(result);
        EXPECT_FALSE(Components::Transform::TryComputeLocalTransform(matrix, glm::mat4{1.0f}, result));
        EXPECT_EQ(Components::Transform::GetMatrix(result), before);
    }
}

TEST(ECSHierarchy, LocalTransformRoundTripsReflectedTrs)
{
    Components::Transform::Component original{
        .Position = {7, 8, 9},
        .Rotation = glm::angleAxis(glm::radians(45.0f), glm::vec3{0, 0, 1}),
        .Scale = {-2, 3, 4}};
    const auto matrix = Components::Transform::GetMatrix(original);
    Components::Transform::Component result;
    ASSERT_TRUE(Components::Transform::TryComputeLocalTransform(matrix, glm::mat4{1.0f}, result));
    const auto reconstructed = Components::Transform::GetMatrix(result);
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            EXPECT_NEAR(reconstructed[column][row], matrix[column][row], 1.0e-5f);
}
