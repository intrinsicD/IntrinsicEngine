// RUNTIME-093 Slice B2 — composition coverage for the pick-readback → primitive
// refinement bridge wired into Engine::RunFrame.
//
// Slice B2 closes the standalone refinement core (Slice A) and the CPU ray
// fallback (Slice B1) into the runtime frame loop: RunFrame drains each pick
// readback and refines its encoded primitive hint against the hit entity's
// authoritative GeometrySources via `RefinePickReadbackResult`, caching the
// result for the editor (`Engine::GetLastRefinedPrimitiveSelection`).
//
// These tests drive `RefinePickReadbackResult` against a live Scene::Registry —
// the exact bridge RunFrame calls — and a small loop mirroring the readback
// drain, so the wiring contract (hint refinement against registry-backed
// geometry, world-transform application, recycling-safe staleness, newest-pick
// wins, background clears) is covered on a pure CPU path without a live GPU
// picking pass.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <gtest/gtest.h>

import Extrinsic.ECS.Component.Transform.WorldMatrix;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.SelectionSystem;
import Extrinsic.Runtime.PrimitiveSelectionRefinement;
import Geometry.Properties;

using Extrinsic::ECS::EntityHandle;
using Extrinsic::ECS::Scene::Registry;
using Extrinsic::ECS::Components::GeometrySources::Edges;
using Extrinsic::ECS::Components::GeometrySources::Faces;
using Extrinsic::ECS::Components::GeometrySources::Halfedges;
using Extrinsic::ECS::Components::GeometrySources::HasMeshTopology;
using Extrinsic::ECS::Components::GeometrySources::Nodes;
using Extrinsic::ECS::Components::GeometrySources::Vertices;
using Extrinsic::ECS::Components::GeometrySources::Domain;
using Extrinsic::ECS::Components::Transform::WorldMatrix;
using Extrinsic::Graphics::EncodeSelectionId;
using Extrinsic::Graphics::PickReadbackResult;
using Extrinsic::Graphics::SelectionPrimitiveDomain;
using Extrinsic::Runtime::PrimitiveRefineStatus;
using Extrinsic::Runtime::PrimitiveSelectionResult;
using Extrinsic::Runtime::RefinedPrimitiveKind;
using Extrinsic::Runtime::RefinePickReadbackResult;

namespace pn = Extrinsic::ECS::Components::GeometrySources::PropertyNames;

namespace
{
    void SetPositions(Geometry::PropertySet& set, const std::vector<glm::vec3>& positions)
    {
        set.Resize(positions.size());
        auto pos = set.GetOrAdd<glm::vec3>(std::string{pn::kPosition}, glm::vec3(0.0f));
        pos.Vector() = positions;
    }

    void SetU32(Geometry::PropertySet& set,
                std::string_view name,
                const std::vector<std::uint32_t>& values,
                std::size_t rows)
    {
        if (set.Size() != rows)
        {
            set.Resize(rows);
        }
        auto prop = set.GetOrAdd<std::uint32_t>(std::string{name}, 0u);
        prop.Vector() = values;
    }

    [[nodiscard]] std::uint32_t RenderId(EntityHandle entity) noexcept
    {
        return static_cast<std::uint32_t>(entity);
    }

    // Emplaces a single CCW triangle mesh (v0,v1,v2) onto `entity`, mirroring the
    // standalone refinement test's MeshScratch but registry-backed so BuildConstView
    // detects the mesh domain. Face ring vertices are (1,2,0).
    void EmplaceTriangleMesh(Registry& registry, EntityHandle entity)
    {
        Vertices vertices{};
        SetPositions(vertices.Properties, {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
        });

        Edges edges{};
        SetU32(edges.Properties, pn::kEdgeV0, {0u, 1u, 2u}, 3);
        SetU32(edges.Properties, pn::kEdgeV1, {1u, 2u, 0u}, 3);

        Halfedges halfedges{};
        SetU32(halfedges.Properties, pn::kHalfedgeToVertex, {1u, 2u, 0u}, 3);
        SetU32(halfedges.Properties, pn::kHalfedgeNext, {1u, 2u, 0u}, 3);
        SetU32(halfedges.Properties, pn::kHalfedgeFace, {0u, 0u, 0u}, 3);

        Faces faces{};
        SetU32(faces.Properties, pn::kFaceHalfedge, {0u}, 1);

        registry.Raw().emplace<Vertices>(entity, std::move(vertices));
        registry.Raw().emplace<Edges>(entity, std::move(edges));
        registry.Raw().emplace<Halfedges>(entity, std::move(halfedges));
        registry.Raw().emplace<Faces>(entity, std::move(faces));
        registry.Raw().emplace<HasMeshTopology>(entity);
    }

    // Emplaces a point cloud of three points onto `entity`.
    void EmplaceCloud(Registry& registry, EntityHandle entity)
    {
        Vertices vertices{};
        SetPositions(vertices.Properties, {
            {0.0f, 0.0f, 0.0f},
            {1.0f, 1.0f, 1.0f},
            {2.0f, 2.0f, 2.0f},
        });
        registry.Raw().emplace<Vertices>(entity, std::move(vertices));
    }

    [[nodiscard]] PickReadbackResult Hit(EntityHandle entity,
                                         SelectionPrimitiveDomain domain,
                                         std::uint32_t payload,
                                         std::uint64_t sequence = 1u) noexcept
    {
        return PickReadbackResult{
            .EncodedId      = EncodeSelectionId(domain, payload),
            .StableEntityId = RenderId(entity),
            .Hit            = true,
            .Sequence       = sequence,
        };
    }

    [[nodiscard]] PickReadbackResult NoHit(std::uint64_t sequence = 1u) noexcept
    {
        return PickReadbackResult{
            .EncodedId      = EncodeSelectionId(SelectionPrimitiveDomain::None, 0u),
            .StableEntityId = 0u,
            .Hit            = false,
            .Sequence       = sequence,
        };
    }
}

// ---- hit refinement against registry-backed geometry -----------------------

TEST(PrimitiveSelectionRefinementWiring, FaceHitResolvesToFaceRowAndWorldHit)
{
    Registry registry{};
    const EntityHandle entity = registry.Create();
    EmplaceTriangleMesh(registry, entity);
    // Translate the entity so the world hit differs from the local hit.
    registry.Raw().emplace<WorldMatrix>(entity,
        WorldMatrix{glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f))});

    // gl_PrimitiveID 0 is the single surface triangle of face row 0.
    const std::optional<PrimitiveSelectionResult> result =
        RefinePickReadbackResult(registry, Hit(entity, SelectionPrimitiveDomain::Face, 0u));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result->Domain, Domain::Mesh);
    EXPECT_EQ(result->Kind, RefinedPrimitiveKind::Face);
    EXPECT_EQ(result->FaceId, 0u);
    EXPECT_EQ(result->EntityId, RenderId(entity));
    ASSERT_TRUE(result->HasHitPosition);
    // Face centroid of (1,0,0),(0,1,0),(0,0,0) is (1/3,1/3,0); +10 in x in world.
    EXPECT_NEAR(result->WorldHit.x, result->LocalHit.x + 10.0f, 1e-4f);
    EXPECT_NEAR(result->WorldHit.y, result->LocalHit.y, 1e-4f);
}

TEST(PrimitiveSelectionRefinementWiring, PointHitResolvesToMeshVertex)
{
    Registry registry{};
    const EntityHandle entity = registry.Create();
    EmplaceTriangleMesh(registry, entity);

    const std::optional<PrimitiveSelectionResult> result =
        RefinePickReadbackResult(registry, Hit(entity, SelectionPrimitiveDomain::Point, 2u));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result->Kind, RefinedPrimitiveKind::Vertex);
    EXPECT_EQ(result->VertexId, 2u);
}

TEST(PrimitiveSelectionRefinementWiring, PointHitResolvesToCloudPoint)
{
    Registry registry{};
    const EntityHandle entity = registry.Create();
    EmplaceCloud(registry, entity);

    const std::optional<PrimitiveSelectionResult> result =
        RefinePickReadbackResult(registry, Hit(entity, SelectionPrimitiveDomain::Point, 1u));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result->Domain, Domain::PointCloud);
    EXPECT_EQ(result->Kind, RefinedPrimitiveKind::Point);
    EXPECT_EQ(result->PointId, 1u);
}

TEST(PrimitiveSelectionRefinementWiring, EntityHintResolvesToWholeEntity)
{
    Registry registry{};
    const EntityHandle entity = registry.Create();
    EmplaceTriangleMesh(registry, entity);

    const std::optional<PrimitiveSelectionResult> result =
        RefinePickReadbackResult(registry, Hit(entity, SelectionPrimitiveDomain::Entity, 0u));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result->Kind, RefinedPrimitiveKind::Entity);
}

// ---- fail-closed / deterministic states ------------------------------------

TEST(PrimitiveSelectionRefinementWiring, BackgroundReadbackResolvesToNoPrimitive)
{
    Registry registry{};
    EXPECT_FALSE(RefinePickReadbackResult(registry, NoHit()).has_value());
}

TEST(PrimitiveSelectionRefinementWiring, StaleRenderIdReportsStaleEntity)
{
    Registry registry{};
    const EntityHandle entity = registry.Create();
    EmplaceTriangleMesh(registry, entity);
    const std::uint32_t staleRenderId = RenderId(entity);
    registry.Destroy(entity);

    const std::optional<PrimitiveSelectionResult> result = RefinePickReadbackResult(
        registry,
        PickReadbackResult{
            .EncodedId      = EncodeSelectionId(SelectionPrimitiveDomain::Face, 0u),
            .StableEntityId = staleRenderId,
            .Hit            = true,
            .Sequence       = 1u,
        });

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, PrimitiveRefineStatus::StaleEntity);
    EXPECT_FALSE(result->Resolved());
}

TEST(PrimitiveSelectionRefinementWiring, LiveEntityWithoutGeometryIsUnsupportedDomain)
{
    Registry registry{};
    const EntityHandle entity = registry.Create();
    // No GeometrySources emplaced: BuildConstView detects Domain::None.

    const std::optional<PrimitiveSelectionResult> result =
        RefinePickReadbackResult(registry, Hit(entity, SelectionPrimitiveDomain::Entity, 0u));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, PrimitiveRefineStatus::UnsupportedDomain);
    EXPECT_FALSE(result->Resolved());
}

// ---- frame-loop drain semantics (newest pick wins; background clears) ------

namespace
{
    // Mirrors the Engine::RunFrame readback-drain assignment: process readbacks
    // oldest→newest, assigning the bridge result each time so the newest wins.
    [[nodiscard]] std::optional<PrimitiveSelectionResult> DrainCache(
        Registry& registry,
        std::optional<PrimitiveSelectionResult> previous,
        const std::vector<PickReadbackResult>& readbacks)
    {
        for (const PickReadbackResult& readback : readbacks)
        {
            previous = RefinePickReadbackResult(registry, readback);
        }
        return previous;
    }
}

TEST(PrimitiveSelectionRefinementWiring, NewestReadbackWinsAndBackgroundClears)
{
    Registry registry{};
    const EntityHandle entity = registry.Create();
    EmplaceTriangleMesh(registry, entity);

    // [face hit, background] ends cleared (background is newest).
    const std::optional<PrimitiveSelectionResult> afterBackground = DrainCache(
        registry, std::nullopt,
        {Hit(entity, SelectionPrimitiveDomain::Face, 0u, 1u), NoHit(2u)});
    EXPECT_FALSE(afterBackground.has_value());

    // [background, point hit] ends resolved to the point (hit is newest).
    const std::optional<PrimitiveSelectionResult> afterHit = DrainCache(
        registry, std::nullopt,
        {NoHit(1u), Hit(entity, SelectionPrimitiveDomain::Point, 1u, 2u)});
    ASSERT_TRUE(afterHit.has_value());
    EXPECT_EQ(afterHit->Kind, RefinedPrimitiveKind::Vertex);
    EXPECT_EQ(afterHit->VertexId, 1u);
}

TEST(PrimitiveSelectionRefinementWiring, EmptyDrainRetainsPriorCache)
{
    Registry registry{};
    const EntityHandle entity = registry.Create();
    EmplaceTriangleMesh(registry, entity);

    PrimitiveSelectionResult prior{};
    prior.Status = PrimitiveRefineStatus::Success;
    prior.Kind = RefinedPrimitiveKind::Face;
    prior.FaceId = 0u;

    const std::optional<PrimitiveSelectionResult> after =
        DrainCache(registry, prior, {});
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->Kind, RefinedPrimitiveKind::Face);
    EXPECT_EQ(after->FaceId, 0u);
}
