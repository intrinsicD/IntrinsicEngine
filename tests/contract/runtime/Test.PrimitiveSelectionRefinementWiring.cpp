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
import Extrinsic.Runtime.StableEntityLookup;
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

    void SetTexcoords(Geometry::PropertySet& set, const std::vector<glm::vec2>& texcoords)
    {
        auto uv = set.GetOrAdd<glm::vec2>("v:texcoord", glm::vec2(0.0f));
        uv.Vector() = texcoords;
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
        // BUG-026: render id = entt handle + 1 (0 = background sentinel),
        // owned by StableEntityLookup::ToRenderId.
        return Extrinsic::Runtime::StableEntityLookup::ToRenderId(entity);
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
        SetTexcoords(vertices.Properties, {
            {0.0f, 0.0f},
            {1.0f, 0.0f},
            {0.0f, 1.0f},
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

// --- BUG-026: depth-anchored cursor reconstruction + closest primitives ----

namespace
{
    using Extrinsic::Runtime::PickReadbackContext;
    using Extrinsic::Runtime::UnprojectPickDepth;

    [[nodiscard]] PickReadbackResult HitWithDepth(EntityHandle entity,
                                                  SelectionPrimitiveDomain domain,
                                                  std::uint32_t payload,
                                                  std::uint32_t pixelX,
                                                  std::uint32_t pixelY,
                                                  float depth)
    {
        return PickReadbackResult{
            .EncodedId = EncodeSelectionId(domain, payload),
            .StableEntityId = RenderId(entity),
            .Hit = true,
            .Sequence = 7u,
            .HasDepth = true,
            .Depth = depth,
            .PixelX = pixelX,
            .PixelY = pixelY,
        };
    }
}

// Golden math: unprojecting (pixel, depth) through the inverse of a Vulkan-style
// view-projection must land on the production pick ray for the same pixel, at
// the parametric depth that re-projects to the same clip-space sample.
TEST(PrimitiveSelectionRefinementWiring, UnprojectPickDepthLandsOnCameraPickRay)
{
    const glm::vec3 eye{0.0f, 0.0f, 3.0f};
    const glm::mat4 view = glm::lookAt(eye, glm::vec3{0.0f}, glm::vec3{0.0f, 1.0f, 0.0f});
    glm::mat4 projection = glm::perspective(glm::radians(45.0f), 4.0f / 3.0f, 0.1f, 100.0f);
    projection[1][1] *= -1.0f; // Vulkan Y flip, matching runtime camera controllers.
    const glm::mat4 viewProjection = projection * view;
    const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);

    constexpr std::uint32_t kWidth = 640u;
    constexpr std::uint32_t kHeight = 480u;
    constexpr std::uint32_t kPixelX = 137u;
    constexpr std::uint32_t kPixelY = 89u;
    constexpr float kDepth = 0.7f;

    const std::optional<glm::vec3> world = UnprojectPickDepth(
        inverseViewProjection, kPixelX, kPixelY, kWidth, kHeight, kDepth);
    ASSERT_TRUE(world.has_value());

    // Re-project: the round trip must reproduce the pixel-center NDC + depth.
    const glm::vec4 clip = viewProjection * glm::vec4{*world, 1.0f};
    ASSERT_GT(std::abs(clip.w), 0.000001f);
    const glm::vec3 ndc = glm::vec3{clip} / clip.w;
    const float expectedNdcX = ((static_cast<float>(kPixelX) + 0.5f) / kWidth) * 2.f - 1.f;
    const float expectedNdcY = 1.f - ((static_cast<float>(kPixelY) + 0.5f) / kHeight) * 2.f;
    EXPECT_NEAR(ndc.x, expectedNdcX, 1.0e-4f);
    EXPECT_NEAR(ndc.y, expectedNdcY, 1.0e-4f);
    EXPECT_NEAR(ndc.z, kDepth, 1.0e-4f);

    // Degenerate inputs fail closed.
    EXPECT_FALSE(UnprojectPickDepth(inverseViewProjection, kPixelX, kPixelY, 0u, 0u, kDepth)
                     .has_value());
    EXPECT_FALSE(UnprojectPickDepth(inverseViewProjection, kWidth, kPixelY, kWidth, kHeight, kDepth)
                     .has_value());
}

// A face hit with a depth-reconstructed cursor anchors the closest-vertex and
// closest-edge refinement on the hinted face and reports the cursor in both
// entity-local and world space. Identity camera (InverseViewProjection = I)
// makes the expected cursor analytic: pixel (99, 24) of a 100x100 viewport
// maps to NDC (0.99, 0.51), depth 0.5 -> world (0.99, 0.51, 0.5).
TEST(PrimitiveSelectionRefinementWiring, FaceHitWithDepthContextAnchorsClosestVertexAndEdge)
{
    Registry registry;
    const EntityHandle entity = registry.Create();
    EmplaceTriangleMesh(registry, entity);

    PickReadbackContext context{};
    context.InverseViewProjection = glm::mat4{1.0f};
    context.ViewportWidth = 100u;
    context.ViewportHeight = 100u;

    const std::optional<PrimitiveSelectionResult> result = RefinePickReadbackResult(
        registry,
        HitWithDepth(entity, SelectionPrimitiveDomain::Face, 0u, 99u, 24u, 0.5f),
        &context);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result->Kind, RefinedPrimitiveKind::Face);
    EXPECT_EQ(result->FaceId, 0u);
    // Cursor (0.99, 0.51, 0.5): nearest face vertex is v1 = (1, 0, 0); nearest
    // boundary edge is (v1, v2), the edge row (1, 2) = EdgeId 1.
    EXPECT_EQ(result->VertexId, 1u);
    EXPECT_EQ(result->EdgeId, 1u);

    EXPECT_TRUE(result->CursorFromDepth);
    EXPECT_FLOAT_EQ(result->Depth, 0.5f);
    EXPECT_NEAR(result->LocalCursor.x, 0.99f, 1.0e-4f);
    EXPECT_NEAR(result->LocalCursor.y, 0.51f, 1.0e-4f);
    EXPECT_NEAR(result->LocalCursor.z, 0.5f, 1.0e-4f);
    ASSERT_TRUE(result->HasHitPosition);
    // Hint-anchored refinement reports the cursor as the hit position too.
    EXPECT_NEAR(result->LocalHit.x, 0.99f, 1.0e-4f);
    EXPECT_NEAR(result->LocalHit.y, 0.51f, 1.0e-4f);
    EXPECT_NEAR(result->LocalHit.z, 0.5f, 1.0e-4f);
    // Identity transform: world cursor == local cursor.
    EXPECT_NEAR(result->WorldCursor.x, result->LocalCursor.x, 1.0e-5f);
    EXPECT_NEAR(result->WorldCursor.y, result->LocalCursor.y, 1.0e-5f);
    EXPECT_NEAR(result->WorldCursor.z, result->LocalCursor.z, 1.0e-5f);
}

// The entity transform maps the world cursor into entity-local space before
// anchoring: with the mesh translated by (5, 0, 0) the same world cursor
// anchors relative to local coordinates, and the reported WorldHit stays the
// cursor in world space.
TEST(PrimitiveSelectionRefinementWiring, DepthCursorIsTransformedIntoEntityLocalSpace)
{
    Registry registry;
    const EntityHandle entity = registry.Create();
    EmplaceTriangleMesh(registry, entity);
    registry.Raw().emplace<WorldMatrix>(entity).Matrix =
        glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 0.0f, 0.0f});

    PickReadbackContext context{};
    context.InverseViewProjection =
        glm::translate(glm::mat4{1.0f}, glm::vec3{5.0f, 0.0f, 0.0f});
    context.ViewportWidth = 100u;
    context.ViewportHeight = 100u;

    // NDC (0.99, 0.51, 0.5) -> world (5.99, 0.51, 0.5) -> local (0.99, 0.51, 0.5).
    const std::optional<PrimitiveSelectionResult> result = RefinePickReadbackResult(
        registry,
        HitWithDepth(entity, SelectionPrimitiveDomain::Face, 0u, 99u, 24u, 0.5f),
        &context);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result->VertexId, 1u);
    EXPECT_EQ(result->EdgeId, 1u);
    EXPECT_TRUE(result->CursorFromDepth);
    EXPECT_NEAR(result->LocalCursor.x, 0.99f, 1.0e-4f);
    EXPECT_NEAR(result->WorldCursor.x, 5.99f, 1.0e-4f);
    EXPECT_NEAR(result->WorldCursor.y, 0.51f, 1.0e-4f);
}

// Depth 1.0 is the clear value — no geometry under the cursor — so no anchor
// is derived: the face hint still resolves but reports the representative
// (centroid) position rather than a cursor.
TEST(PrimitiveSelectionRefinementWiring, DepthClearValueSkipsCursorAnchor)
{
    Registry registry;
    const EntityHandle entity = registry.Create();
    EmplaceTriangleMesh(registry, entity);

    PickReadbackContext context{};
    context.InverseViewProjection = glm::mat4{1.0f};
    context.ViewportWidth = 100u;
    context.ViewportHeight = 100u;

    const std::optional<PrimitiveSelectionResult> result = RefinePickReadbackResult(
        registry,
        HitWithDepth(entity, SelectionPrimitiveDomain::Face, 0u, 99u, 24u, 1.0f),
        &context);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, PrimitiveRefineStatus::Success);
    EXPECT_EQ(result->Kind, RefinedPrimitiveKind::Face);
    EXPECT_FALSE(result->CursorFromDepth);
    // Without an anchor the nearest-vertex/edge refinement does not run.
    EXPECT_EQ(result->VertexId, Extrinsic::Runtime::kInvalidPrimitiveIndex);
}

// A hit with no sub-primitive hint resolves through the context-supplied pick
// ray: the entity-local ray fallback picks the nearest mesh vertex within the
// distance-scaled pixel radius.
TEST(PrimitiveSelectionRefinementWiring, MissingHintResolvesThroughContextRayFallback)
{
    Registry registry;
    const EntityHandle entity = registry.Create();
    EmplaceTriangleMesh(registry, entity);

    PickReadbackContext context{};
    context.InverseViewProjection = glm::mat4{1.0f};
    context.ViewportWidth = 100u;
    context.ViewportHeight = 100u;
    context.HasWorldRay = true;
    context.WorldRayOrigin = glm::vec3{0.99f, 0.51f, -1.0f};
    context.WorldRayDirection = glm::vec3{0.0f, 0.0f, 1.0f};
    context.WorldUnitsPerPixelAtUnitDepth = 0.05f;
    context.PickRadiusPixels = 12.0f;

    // Domain None = no usable hint; the cursor depth keeps the radius scaled
    // at the hit distance. v1 = (1, 0, 0) is within radius of the ray; v0/v2
    // are not.
    const std::optional<PrimitiveSelectionResult> result = RefinePickReadbackResult(
        registry,
        HitWithDepth(entity, SelectionPrimitiveDomain::None, 0u, 99u, 24u, 0.5f),
        &context);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, PrimitiveRefineStatus::CpuFallbackResolved);
    EXPECT_EQ(result->Kind, RefinedPrimitiveKind::Vertex);
    EXPECT_EQ(result->VertexId, 1u);
    // The fallback reports the resolved vertex as the hit position while the
    // depth-derived cursor stays available in the dedicated cursor fields.
    EXPECT_NEAR(result->LocalHit.x, 1.0f, 1.0e-5f);
    EXPECT_TRUE(result->CursorFromDepth);
    EXPECT_NEAR(result->WorldCursor.x, 0.99f, 1.0e-4f);
    EXPECT_NEAR(result->WorldCursor.y, 0.51f, 1.0e-4f);
    EXPECT_NEAR(result->WorldCursor.z, 0.5f, 1.0e-4f);
}

// --- BUG-026 review follow-up: orthographic pick radius -------------------

// The promoted TopDownCameraController renders through glm::ortho (with the
// Vulkan Y flip); perspective cameras come from glm::perspective. The
// projection-kind detector keys off the perspective divide coefficient.
TEST(PrimitiveSelectionRefinementWiring, IsOrthographicProjectionDistinguishesProjectionKinds)
{
    using Extrinsic::Runtime::IsOrthographicProjection;

    glm::mat4 ortho = glm::ortho(-4.0f, 4.0f, -3.0f, 3.0f, 0.1f, 100.0f);
    EXPECT_TRUE(IsOrthographicProjection(ortho));
    ortho[1][1] *= -1.0f; // Vulkan Y flip must not change the answer.
    EXPECT_TRUE(IsOrthographicProjection(ortho));

    glm::mat4 perspective =
        glm::perspective(glm::radians(45.0f), 4.0f / 3.0f, 0.1f, 100.0f);
    EXPECT_FALSE(IsOrthographicProjection(perspective));
    perspective[1][1] *= -1.0f;
    EXPECT_FALSE(IsOrthographicProjection(perspective));
}

// Orthographic units-per-pixel is depth-invariant, so the missing-hint
// fallback radius must stay the constant pixel footprint instead of growing
// with the hit distance. The same context resolves a vertex under perspective
// scaling (radius 12 * 0.03 * 1.5 = 0.54 > the 0.51 ray-to-vertex distance)
// but must fail closed under the orthographic flag (radius 12 * 0.03 = 0.36):
// without the flag, a top-down pick radius grows with camera altitude and
// resolves primitives far outside the intended 12-pixel radius.
TEST(PrimitiveSelectionRefinementWiring, OrthographicFallbackRadiusDoesNotScaleWithHitDistance)
{
    Registry registry;
    const EntityHandle entity = registry.Create();
    EmplaceTriangleMesh(registry, entity);

    PickReadbackContext context{};
    context.InverseViewProjection = glm::mat4{1.0f};
    context.ViewportWidth = 100u;
    context.ViewportHeight = 100u;
    context.HasWorldRay = true;
    context.WorldRayOrigin = glm::vec3{0.99f, 0.51f, -1.0f};
    context.WorldRayDirection = glm::vec3{0.0f, 0.0f, 1.0f};
    context.WorldUnitsPerPixelAtUnitDepth = 0.03f;
    context.PickRadiusPixels = 12.0f;

    const Extrinsic::Graphics::PickReadbackResult readback =
        HitWithDepth(entity, SelectionPrimitiveDomain::None, 0u, 99u, 24u, 0.5f);

    // Perspective semantics (flag off): the hit distance (1.5) inflates the
    // radius past v1's 0.51 perpendicular distance and the fallback resolves.
    context.OrthographicProjection = false;
    const std::optional<PrimitiveSelectionResult> perspectiveResult =
        RefinePickReadbackResult(registry, readback, &context);
    ASSERT_TRUE(perspectiveResult.has_value());
    EXPECT_EQ(perspectiveResult->Status, PrimitiveRefineStatus::CpuFallbackResolved);
    EXPECT_EQ(perspectiveResult->VertexId, 1u);

    // Orthographic semantics (flag on): the radius stays the 0.36 pixel
    // footprint, v1 is outside it, and the fallback fails closed.
    context.OrthographicProjection = true;
    const std::optional<PrimitiveSelectionResult> orthographicResult =
        RefinePickReadbackResult(registry, readback, &context);
    ASSERT_TRUE(orthographicResult.has_value());
    EXPECT_EQ(orthographicResult->Status, PrimitiveRefineStatus::CpuFallbackMiss);
    EXPECT_EQ(orthographicResult->VertexId, Extrinsic::Runtime::kInvalidPrimitiveIndex);
}

// A vertex genuinely inside the orthographic pixel footprint still resolves:
// the flag tightens the radius, it does not disable the fallback.
TEST(PrimitiveSelectionRefinementWiring, OrthographicFallbackStillResolvesWithinPixelFootprint)
{
    Registry registry;
    const EntityHandle entity = registry.Create();
    EmplaceTriangleMesh(registry, entity);

    PickReadbackContext context{};
    context.InverseViewProjection = glm::mat4{1.0f};
    context.ViewportWidth = 100u;
    context.ViewportHeight = 100u;
    context.HasWorldRay = true;
    // Perpendicular distance to v1 = (1, 0, 0) is ~0.2002 < the 0.36 radius.
    context.WorldRayOrigin = glm::vec3{0.99f, 0.2f, -1.0f};
    context.WorldRayDirection = glm::vec3{0.0f, 0.0f, 1.0f};
    context.WorldUnitsPerPixelAtUnitDepth = 0.03f;
    context.PickRadiusPixels = 12.0f;
    context.OrthographicProjection = true;

    const std::optional<PrimitiveSelectionResult> result = RefinePickReadbackResult(
        registry,
        HitWithDepth(entity, SelectionPrimitiveDomain::None, 0u, 99u, 24u, 0.5f),
        &context);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->Status, PrimitiveRefineStatus::CpuFallbackResolved);
    EXPECT_EQ(result->Kind, RefinedPrimitiveKind::Vertex);
    EXPECT_EQ(result->VertexId, 1u);
}
