#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <glm/glm.hpp>

import Geometry;
import Geometry.HalfedgeMesh.Features;

#include "Test_MeshBuilders.h"

namespace
{
    namespace Features = Geometry::HalfedgeMesh::Features;

    [[nodiscard]] std::vector<glm::dvec3> CanonicalFaceNormals(
        const Geometry::HalfedgeMesh::Mesh& mesh)
    {
        std::vector<glm::dvec3> normals(mesh.FacesSize(), glm::dvec3(0.0));
        for (std::size_t faceIndex = 0u; faceIndex < mesh.FacesSize(); ++faceIndex)
        {
            const Geometry::FaceHandle face{
                static_cast<Geometry::PropertyIndex>(faceIndex)};
            if (!mesh.IsDeleted(face))
            {
                normals[faceIndex] = Geometry::MeshUtils::FaceAreaVector(mesh, face);
            }
        }
        return normals;
    }

    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeFoldedTrianglePair()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const Geometry::VertexHandle v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
        const Geometry::VertexHandle v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
        const Geometry::VertexHandle v2 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
        const Geometry::VertexHandle v3 = mesh.AddVertex({0.0f, 0.0f, 1.0f});
        (void)mesh.AddTriangle(v0, v1, v2);
        (void)mesh.AddTriangle(v1, v0, v3);
        return mesh;
    }
} // namespace

TEST(HalfedgeMeshFeatures, PlanarSquareHonorsBoundaryPolicyAndSlotAlignment)
{
    const auto mesh = MakeTwoTriangleSquare();
    const auto normals = CanonicalFaceNormals(mesh);

    const Features::Classification withBoundary = Features::Classify(mesh, normals);
    ASSERT_EQ(withBoundary.Status, Features::ClassificationStatus::Success);
    EXPECT_EQ(withBoundary.EdgeFeatureMask.size(), mesh.EdgesSize());
    EXPECT_EQ(withBoundary.IncidentFeatureEdgeCount.size(), mesh.VerticesSize());
    EXPECT_EQ(withBoundary.VertexIncidence.size(), mesh.VerticesSize());
    EXPECT_EQ(withBoundary.FeatureEdgeCount, 4u);
    EXPECT_EQ(withBoundary.BoundaryFeatureEdgeCount, 4u);
    EXPECT_EQ(withBoundary.InvalidNormalFeatureEdgeCount, 0u);
    EXPECT_EQ(withBoundary.InvalidTopologyFeatureEdgeCount, 0u);
    for (std::size_t vertexIndex = 0u; vertexIndex < mesh.VerticesSize(); ++vertexIndex)
    {
        EXPECT_EQ(withBoundary.IncidentFeatureEdgeCount[vertexIndex], 2u);
        EXPECT_EQ(
            withBoundary.VertexIncidence[vertexIndex],
            Features::VertexIncidenceCategory::Crease);
    }

    Features::Params smoothBoundary{};
    smoothBoundary.BoundaryIsFeature = false;
    const Features::Classification withoutBoundary =
        Features::Classify(mesh, normals, smoothBoundary);
    ASSERT_EQ(withoutBoundary.Status, Features::ClassificationStatus::Success);
    EXPECT_EQ(withoutBoundary.FeatureEdgeCount, 0u);
    EXPECT_TRUE(std::ranges::all_of(
        withoutBoundary.EdgeFeatureMask,
        [](const std::uint8_t value) { return value == 0u; }));
    EXPECT_TRUE(std::ranges::all_of(
        withoutBoundary.VertexIncidence,
        [](const Features::VertexIncidenceCategory category) {
            return category == Features::VertexIncidenceCategory::None;
        }));

    const Features::Classification repeated = Features::Classify(mesh, normals);
    EXPECT_EQ(repeated.EdgeFeatureMask, withBoundary.EdgeFeatureMask);
    EXPECT_EQ(repeated.IncidentFeatureEdgeCount, withBoundary.IncidentFeatureEdgeCount);
    EXPECT_EQ(repeated.VertexIncidence, withBoundary.VertexIncidence);
}

TEST(HalfedgeMeshFeatures, FoldedCreaseIsMagnitudeInvariantAndKeepsEndpoints)
{
    const auto mesh = MakeFoldedTrianglePair();
    const auto sharedEdge = mesh.FindEdge(
        Geometry::VertexHandle{0u}, Geometry::VertexHandle{1u});
    ASSERT_TRUE(sharedEdge.has_value());

    Features::Params params{};
    params.BoundaryIsFeature = false;
    auto normals = CanonicalFaceNormals(mesh);
    const Features::Classification unit = Features::Classify(mesh, normals, params);
    ASSERT_EQ(unit.Status, Features::ClassificationStatus::Success);
    EXPECT_EQ(unit.FeatureEdgeCount, 1u);
    EXPECT_EQ(unit.EdgeFeatureMask[sharedEdge->Index], 1u);
    EXPECT_EQ(unit.IncidentFeatureEdgeCount[0u], 1u);
    EXPECT_EQ(unit.IncidentFeatureEdgeCount[1u], 1u);
    EXPECT_EQ(unit.VertexIncidence[0u], Features::VertexIncidenceCategory::Endpoint);
    EXPECT_EQ(unit.VertexIncidence[1u], Features::VertexIncidenceCategory::Endpoint);
    EXPECT_EQ(unit.VertexIncidence[2u], Features::VertexIncidenceCategory::None);
    EXPECT_EQ(unit.VertexIncidence[3u], Features::VertexIncidenceCategory::None);

    normals[0u] *= 17.0;
    normals[1u] *= 0.125;
    const Features::Classification scaled = Features::Classify(mesh, normals, params);
    ASSERT_EQ(scaled.Status, Features::ClassificationStatus::Success);
    EXPECT_EQ(scaled.EdgeFeatureMask, unit.EdgeFeatureMask);
    EXPECT_EQ(scaled.IncidentFeatureEdgeCount, unit.IncidentFeatureEdgeCount);
    EXPECT_EQ(scaled.VertexIncidence, unit.VertexIncidence);

    normals = CanonicalFaceNormals(mesh);
    normals[0u] = glm::normalize(normals[0u])
        * std::numeric_limits<double>::max();
    normals[1u] = glm::normalize(normals[1u])
        * std::numeric_limits<double>::denorm_min();
    const Features::Classification extreme =
        Features::Classify(mesh, normals, params);
    ASSERT_EQ(extreme.Status, Features::ClassificationStatus::Success);
    EXPECT_EQ(extreme.InvalidNormalFeatureEdgeCount, 0u);
    EXPECT_EQ(extreme.EdgeFeatureMask, unit.EdgeFeatureMask);
    EXPECT_EQ(extreme.IncidentFeatureEdgeCount, unit.IncidentFeatureEdgeCount);
    EXPECT_EQ(extreme.VertexIncidence, unit.VertexIncidence);

    normals = CanonicalFaceNormals(mesh);
    params.DihedralThresholdDegrees = 90.0;
    const Features::Classification atExactThreshold =
        Features::Classify(mesh, normals, params);
    ASSERT_EQ(atExactThreshold.Status, Features::ClassificationStatus::Success);
    EXPECT_EQ(atExactThreshold.FeatureEdgeCount, 0u)
        << "the dihedral angle must strictly exceed the threshold";
}

TEST(HalfedgeMeshFeatures, CubeHasTwelveFeatureEdgesAndEightJunctions)
{
    const auto mesh = MakeCube();
    const auto normals = CanonicalFaceNormals(mesh);
    const Features::Classification classification = Features::Classify(mesh, normals);

    ASSERT_EQ(classification.Status, Features::ClassificationStatus::Success);
    EXPECT_EQ(mesh.EdgesSize(), 18u);
    EXPECT_EQ(classification.FeatureEdgeCount, 12u);
    EXPECT_EQ(classification.BoundaryFeatureEdgeCount, 0u);
    for (std::size_t vertexIndex = 0u; vertexIndex < mesh.VerticesSize(); ++vertexIndex)
    {
        EXPECT_EQ(classification.IncidentFeatureEdgeCount[vertexIndex], 3u);
        EXPECT_EQ(
            classification.VertexIncidence[vertexIndex],
            Features::VertexIncidenceCategory::Junction);
    }
}

TEST(HalfedgeMeshFeatures, RejectsInvalidThresholdAndNormalSlotCount)
{
    const auto mesh = MakeTwoTriangleSquare();
    auto normals = CanonicalFaceNormals(mesh);

    Features::Params params{};
    params.DihedralThresholdDegrees = -1.0;
    EXPECT_EQ(
        Features::Classify(mesh, normals, params).Status,
        Features::ClassificationStatus::InvalidDihedralThreshold);
    params.DihedralThresholdDegrees = 181.0;
    EXPECT_EQ(
        Features::Classify(mesh, normals, params).Status,
        Features::ClassificationStatus::InvalidDihedralThreshold);
    params.DihedralThresholdDegrees = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(
        Features::Classify(mesh, normals, params).Status,
        Features::ClassificationStatus::InvalidDihedralThreshold);

    normals.pop_back();
    params.DihedralThresholdDegrees = 45.0;
    const Features::Classification wrongSize = Features::Classify(mesh, normals, params);
    EXPECT_EQ(
        wrongSize.Status,
        Features::ClassificationStatus::FaceNormalSlotCountMismatch);
    EXPECT_TRUE(wrongSize.EdgeFeatureMask.empty());
    EXPECT_TRUE(Features::IsFeatureEdgeFailClosed(
        mesh, Geometry::EdgeHandle{0u}, normals, params));
}

TEST(HalfedgeMeshFeatures, RejectsSubmeshViewsBeforeReadingAbsoluteHandles)
{
    const auto mesh = MakeTwoTriangleSquare();
    auto view = Geometry::HalfedgeMesh::Mesh::CreateView(
        mesh,
        {.Offset = 1u, .Size = 2u},
        {.Offset = 1u, .Size = 2u},
        {.Offset = 1u, .Size = 1u});
    ASSERT_TRUE(view.IsSubmeshView());

    const std::vector<glm::dvec3> normals(
        view.FacesSize(), glm::dvec3(0.0, 0.0, 1.0));
    const Features::Classification classification =
        Features::Classify(view, normals);
    EXPECT_EQ(
        classification.Status,
        Features::ClassificationStatus::UnsupportedSubmeshView);
    EXPECT_TRUE(classification.EdgeFeatureMask.empty());
    EXPECT_TRUE(Features::IsFeatureEdgeFailClosed(
        view, Geometry::EdgeHandle{1u}, normals));
    EXPECT_FALSE(Features::MaterializeEdgeProperty(view, classification).has_value());
}

TEST(HalfedgeMeshFeatures, InvalidNormalsAndNonFiniteGeometryFailClosed)
{
    Features::Params params{};
    params.BoundaryIsFeature = false;

    auto mesh = MakeFoldedTrianglePair();
    auto normals = CanonicalFaceNormals(mesh);
    normals[0u] = glm::dvec3(0.0);
    Features::Classification zeroNormal = Features::Classify(mesh, normals, params);
    ASSERT_EQ(zeroNormal.Status, Features::ClassificationStatus::Success);
    EXPECT_EQ(zeroNormal.FeatureEdgeCount, 3u);
    EXPECT_EQ(zeroNormal.InvalidNormalFeatureEdgeCount, 3u);

    normals = CanonicalFaceNormals(mesh);
    normals[0u].x = std::numeric_limits<double>::infinity();
    Features::Classification nonFiniteNormal = Features::Classify(mesh, normals, params);
    ASSERT_EQ(nonFiniteNormal.Status, Features::ClassificationStatus::Success);
    EXPECT_EQ(nonFiniteNormal.FeatureEdgeCount, 3u);
    EXPECT_EQ(nonFiniteNormal.InvalidNormalFeatureEdgeCount, 3u);

    mesh.Position(Geometry::VertexHandle{2u}) = glm::vec3(0.5f, 0.0f, 0.0f);
    normals = CanonicalFaceNormals(mesh);
    Features::Classification zeroAreaFace = Features::Classify(mesh, normals, params);
    ASSERT_EQ(zeroAreaFace.Status, Features::ClassificationStatus::Success);
    EXPECT_EQ(zeroAreaFace.FeatureEdgeCount, 3u);
    EXPECT_EQ(zeroAreaFace.InvalidNormalFeatureEdgeCount, 3u);

    mesh.Position(Geometry::VertexHandle{2u}).x =
        std::numeric_limits<float>::quiet_NaN();
    normals = CanonicalFaceNormals(mesh);
    Features::Classification nonFiniteGeometry = Features::Classify(mesh, normals, params);
    ASSERT_EQ(nonFiniteGeometry.Status, Features::ClassificationStatus::Success);
    EXPECT_EQ(nonFiniteGeometry.FeatureEdgeCount, 3u);
    EXPECT_EQ(nonFiniteGeometry.InvalidNormalFeatureEdgeCount, 3u);
}

TEST(HalfedgeMeshFeatures, InvalidBoundaryFaceNormalOverridesDisabledBoundaryPolicy)
{
    const auto mesh = MakeSingleTriangle();
    const std::vector<glm::dvec3> normals(mesh.FacesSize(), glm::dvec3(0.0));
    Features::Params params{};
    params.BoundaryIsFeature = false;

    const Features::Classification classification =
        Features::Classify(mesh, normals, params);
    ASSERT_EQ(classification.Status, Features::ClassificationStatus::Success);
    EXPECT_EQ(classification.FeatureEdgeCount, 3u);
    EXPECT_EQ(classification.BoundaryFeatureEdgeCount, 0u);
    EXPECT_EQ(classification.InvalidNormalFeatureEdgeCount, 3u);
}

TEST(HalfedgeMeshFeatures, DeletedSlotsStayClearAndDanglingEdgesFailClosed)
{
    {
        auto mesh = MakeTwoTriangleSquare();
        mesh.DeleteFace(Geometry::FaceHandle{0u});
        const auto normals = CanonicalFaceNormals(mesh);
        const Features::Classification classification = Features::Classify(mesh, normals);
        ASSERT_EQ(classification.Status, Features::ClassificationStatus::Success);
        EXPECT_EQ(classification.DeletedEdgeSlotCount, mesh.DeletedEdgeCount());
        ASSERT_GT(classification.DeletedEdgeSlotCount, 0u);
        for (std::size_t edgeIndex = 0u; edgeIndex < mesh.EdgesSize(); ++edgeIndex)
        {
            const Geometry::EdgeHandle edge{
                static_cast<Geometry::PropertyIndex>(edgeIndex)};
            if (mesh.IsDeleted(edge))
            {
                EXPECT_EQ(classification.EdgeFeatureMask[edgeIndex], 0u);
            }
        }
    }

    {
        auto mesh = MakeTwoTriangleSquare();
        const auto dangling = mesh.FindEdge(
            Geometry::VertexHandle{0u}, Geometry::VertexHandle{2u});
        ASSERT_TRUE(dangling.has_value());
        mesh.SetFace(mesh.Halfedge(*dangling, 0u), Geometry::FaceHandle{});
        mesh.SetFace(mesh.Halfedge(*dangling, 1u), Geometry::FaceHandle{});

        Features::Params params{};
        params.BoundaryIsFeature = false;
        const auto normals = CanonicalFaceNormals(mesh);
        const Features::Classification classification =
            Features::Classify(mesh, normals, params);
        ASSERT_EQ(classification.Status, Features::ClassificationStatus::Success);
        EXPECT_EQ(classification.FeatureEdgeCount, 1u);
        EXPECT_EQ(classification.InvalidTopologyFeatureEdgeCount, 1u);
        EXPECT_EQ(classification.EdgeFeatureMask[dangling->Index], 1u);
    }
}

TEST(HalfedgeMeshFeatures, CanonicalPolygonNormalsClassifyQuadPair)
{
    const auto mesh = MakeQuadPair();
    const auto normals = CanonicalFaceNormals(mesh);
    ASSERT_EQ(normals.size(), 2u);
    ASSERT_GT(glm::length(normals[0u]), 0.0);
    ASSERT_GT(glm::length(normals[1u]), 0.0);

    Features::Params params{};
    params.BoundaryIsFeature = false;
    const Features::Classification classification =
        Features::Classify(mesh, normals, params);
    ASSERT_EQ(classification.Status, Features::ClassificationStatus::Success);
    EXPECT_EQ(classification.FeatureEdgeCount, 0u);
}

// Simplification calls this predicate after recomputing the surviving one-ring
// normals. Pin the seam's current-view behavior so adoption cannot regress to
// consulting the initialization snapshot during later collapse checks.
TEST(HalfedgeMeshFeatures, EvolvingSimplificationPredicateReadsCurrentNormals)
{
    const auto mesh = MakeFoldedTrianglePair();
    const auto shared = mesh.FindEdge(
        Geometry::VertexHandle{0u}, Geometry::VertexHandle{1u});
    ASSERT_TRUE(shared.has_value());

    Features::Params params{};
    params.BoundaryIsFeature = false;
    std::vector<glm::dvec3> evolvingNormals{
        glm::dvec3(0.0, 0.0, 1.0),
        glm::dvec3(0.0, 0.0, 1.0),
    };
    EXPECT_FALSE(Features::IsFeatureEdgeFailClosed(
        mesh, *shared, evolvingNormals, params));

    evolvingNormals[1u] = glm::dvec3(0.0, 1.0, 0.0);
    EXPECT_TRUE(Features::IsFeatureEdgeFailClosed(
        mesh, *shared, evolvingNormals, params));
}

TEST(HalfedgeMeshFeatures, MaterializesExactCanonicalBoolProperty)
{
    auto mesh = MakeTwoTriangleSquare();
    const auto normals = CanonicalFaceNormals(mesh);
    const Features::Classification classification = Features::Classify(mesh, normals);
    ASSERT_EQ(classification.Status, Features::ClassificationStatus::Success);

    auto stale = mesh.EdgeProperties().GetOrAdd<bool>(
        std::string(Features::kDefaultEdgeFeatureProperty), true);
    ASSERT_TRUE(stale.IsValid());
    auto materialized = Features::MaterializeEdgeProperty(mesh, classification);
    ASSERT_TRUE(materialized.has_value());
    for (std::size_t edgeIndex = 0u; edgeIndex < mesh.EdgesSize(); ++edgeIndex)
    {
        EXPECT_EQ(
            (*materialized)[Geometry::EdgeHandle{
                static_cast<Geometry::PropertyIndex>(edgeIndex)}],
            classification.EdgeFeatureMask[edgeIndex] != 0u);
    }

    (void)mesh.EdgeProperties().GetOrAdd<float>("e:feature_type_collision", 0.0f);
    EXPECT_FALSE(Features::MaterializeEdgeProperty(
        mesh, classification, "e:feature_type_collision").has_value());
    EXPECT_FALSE(Features::MaterializeEdgeProperty(
        mesh, classification, "").has_value());

    Features::Classification tampered = classification;
    tampered.EdgeFeatureMask[0u] = 2u;
    EXPECT_FALSE(Features::MaterializeEdgeProperty(
        mesh, tampered, "e:tampered_feature").has_value());
}
