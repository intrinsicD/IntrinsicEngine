#include <cstddef>
#include <cstdint>
#include <vector>

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
        raw.emplace<Components::Hierarchy::Component>(
            children[i],
            Components::Hierarchy::Component{
                .Parent = root,
                .NextSibling =
                    i + 1u < children.size()
                        ? children[i + 1u]
                        : InvalidEntityHandle,
                .PrevSibling =
                    i > 0u ? children[i - 1u] : InvalidEntityHandle,
            });
    }

    ExpectQueryFailure(
        Structure::CollectChildren(raw, root),
        Structure::HierarchyQueryStatus::TraversalLimitExceeded);
    ExpectQueryFailure(
        Structure::CollectDescendantsPreorder(raw, root),
        Structure::HierarchyQueryStatus::TraversalLimitExceeded);
}
