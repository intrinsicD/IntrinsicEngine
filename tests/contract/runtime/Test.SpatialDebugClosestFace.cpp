#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>
#include <glm/glm.hpp>

import Geometry;
import Extrinsic.Runtime.SpatialDebugClosestFace;

namespace
{
    namespace Runtime = Extrinsic::Runtime;
    namespace MU = Geometry::MeshUtils;

    [[nodiscard]] bool IsFinite(const glm::vec3& p) noexcept
    {
        return std::isfinite(p.x) &&
               std::isfinite(p.y) &&
               std::isfinite(p.z);
    }

    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeTwoTriangleMesh(
        const glm::vec3 offset = glm::vec3{0.0f})
    {
        const std::vector<glm::vec3> positions{
            offset + glm::vec3{0.0f, 0.0f, 0.0f},
            offset + glm::vec3{1.0f, 0.0f, 0.0f},
            offset + glm::vec3{0.0f, 1.0f, 0.0f},
            offset + glm::vec3{2.0f, 0.0f, 0.0f},
            offset + glm::vec3{3.0f, 0.0f, 0.0f},
            offset + glm::vec3{2.0f, 1.0f, 0.0f},
        };
        const std::vector<std::uint32_t> indices{0u, 1u, 2u, 3u, 4u, 5u};
        auto mesh = MU::BuildHalfedgeMeshFromIndexedTriangles(positions, indices);
        EXPECT_TRUE(mesh.has_value());
        return std::move(*mesh);
    }

    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeDegenerateTriangleMesh()
    {
        Geometry::HalfedgeMesh::Mesh mesh{};
        const auto v0 = mesh.AddVertex(glm::vec3{0.0f, 0.0f, 0.0f});
        const auto v1 = mesh.AddVertex(glm::vec3{1.0f, 0.0f, 0.0f});
        const auto v2 = mesh.AddVertex(glm::vec3{2.0f, 0.0f, 0.0f});
        EXPECT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());
        return mesh;
    }

    [[nodiscard]] Geometry::MeshClosestFaceResult DirectQuery(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const glm::vec3&                    probe)
    {
        Geometry::MeshClosestFaceIndex index{};
        EXPECT_TRUE(index.Build(mesh));
        return index.Query(probe);
    }

    void ExpectFailureIsFinite(const Runtime::SpatialDebugClosestFaceOverlay& overlay)
    {
        EXPECT_FALSE(overlay.Found);
        EXPECT_FALSE(overlay.Succeeded());
        EXPECT_TRUE(IsFinite(overlay.ProbePoint));
        EXPECT_TRUE(IsFinite(overlay.ClosestPoint));
        EXPECT_TRUE(IsFinite(overlay.Normal));
        EXPECT_TRUE(std::isfinite(overlay.Distance));
        EXPECT_TRUE(std::isfinite(overlay.SquaredDistance));
        EXPECT_FLOAT_EQ(overlay.Distance, 0.0f);
        EXPECT_FLOAT_EQ(overlay.SquaredDistance, 0.0f);
    }
}

TEST(SpatialDebugClosestFace, MatchesDirectGeometryQueryAndPublishesOverlay)
{
    const Geometry::HalfedgeMesh::Mesh mesh = MakeTwoTriangleMesh();
    const glm::vec3 probe{2.2f, 0.25f, 0.35f};
    const Geometry::MeshClosestFaceResult direct = DirectQuery(mesh, probe);
    ASSERT_TRUE(direct.Found);

    Runtime::SpatialDebugClosestFaceConsumer consumer{};
    const Runtime::SpatialDebugClosestFaceMeshSource source{
        .Mesh = &mesh,
        .MeshKey = 42u,
        .Revision = 7u,
        .Active = true,
    };

    const Runtime::SpatialDebugClosestFaceOverlay overlay =
        consumer.Resolve(source, probe);

    ASSERT_TRUE(overlay.Succeeded());
    EXPECT_EQ(overlay.Status, Runtime::SpatialDebugClosestFaceStatus::Resolved);
    EXPECT_EQ(overlay.QueryStatus, Geometry::MeshClosestFaceStatus::Success);
    EXPECT_EQ(overlay.Face.Index, direct.Face.Index);
    EXPECT_EQ(overlay.PrimitiveIndex, direct.PrimitiveIndex);
    EXPECT_TRUE(mesh.IsValid(overlay.Face));
    EXPECT_FALSE(mesh.IsDeleted(overlay.Face));
    EXPECT_TRUE(IsFinite(overlay.ClosestPoint));
    EXPECT_TRUE(IsFinite(overlay.Normal));
    EXPECT_NEAR(glm::length(overlay.ClosestPoint - direct.Point), 0.0f, 1.0e-5f);
    EXPECT_NEAR(overlay.SquaredDistance, direct.SquaredDistance, 1.0e-5f);
    EXPECT_NEAR(overlay.Distance * overlay.Distance, direct.SquaredDistance, 1.0e-5f);
    EXPECT_EQ(overlay.MeshKey, source.MeshKey);
    EXPECT_EQ(overlay.MeshRevision, source.Revision);
    EXPECT_TRUE(consumer.HasCachedIndex());
    EXPECT_EQ(consumer.CachedMeshKey(), source.MeshKey);
    EXPECT_EQ(consumer.CachedMeshRevision(), source.Revision);
    EXPECT_EQ(consumer.RebuildCount(), 1u);
}

TEST(SpatialDebugClosestFace, RebuildsOnMeshRevisionChange)
{
    const Geometry::HalfedgeMesh::Mesh firstMesh = MakeTwoTriangleMesh();
    const Geometry::HalfedgeMesh::Mesh secondMesh =
        MakeTwoTriangleMesh(glm::vec3{10.0f, 0.0f, 0.0f});

    Runtime::SpatialDebugClosestFaceConsumer consumer{};
    const Runtime::SpatialDebugClosestFaceMeshSource firstSource{
        .Mesh = &firstMesh,
        .MeshKey = 11u,
        .Revision = 1u,
        .Active = true,
    };
    const Runtime::SpatialDebugClosestFaceMeshSource secondSource{
        .Mesh = &secondMesh,
        .MeshKey = 11u,
        .Revision = 2u,
        .Active = true,
    };

    const Runtime::SpatialDebugClosestFaceOverlay first =
        consumer.Resolve(firstSource, glm::vec3{0.2f, 0.2f, 0.1f});
    ASSERT_TRUE(first.Succeeded());
    EXPECT_EQ(consumer.RebuildCount(), 1u);

    const glm::vec3 secondProbe{10.2f, 0.2f, 0.1f};
    const Geometry::MeshClosestFaceResult directSecond =
        DirectQuery(secondMesh, secondProbe);
    ASSERT_TRUE(directSecond.Found);

    const Runtime::SpatialDebugClosestFaceOverlay second =
        consumer.Resolve(secondSource, secondProbe);
    ASSERT_TRUE(second.Succeeded());
    EXPECT_EQ(consumer.RebuildCount(), 2u);
    EXPECT_EQ(consumer.CachedMeshRevision(), secondSource.Revision);
    EXPECT_EQ(second.Face.Index, directSecond.Face.Index);
    EXPECT_NEAR(glm::length(second.ClosestPoint - directSecond.Point), 0.0f, 1.0e-5f);
    EXPECT_LT(second.Distance, 0.2f);

    const Runtime::SpatialDebugClosestFaceOverlay third =
        consumer.Resolve(secondSource, glm::vec3{12.2f, 0.2f, 0.1f});
    ASSERT_TRUE(third.Succeeded());
    EXPECT_EQ(consumer.RebuildCount(), 2u);
}

TEST(SpatialDebugClosestFace, NoActiveOrMissingMeshFailsClosed)
{
    Runtime::SpatialDebugClosestFaceConsumer consumer{};
    const glm::vec3 probe{0.2f, 0.2f, 0.1f};

    const Runtime::SpatialDebugClosestFaceOverlay noActive =
        consumer.Resolve(Runtime::SpatialDebugClosestFaceMeshSource{}, probe);
    EXPECT_EQ(noActive.Status, Runtime::SpatialDebugClosestFaceStatus::NoActiveMesh);
    ExpectFailureIsFinite(noActive);
    EXPECT_FALSE(consumer.HasCachedIndex());
    EXPECT_EQ(consumer.RebuildCount(), 0u);

    const Runtime::SpatialDebugClosestFaceMeshSource missingMesh{
        .Mesh = nullptr,
        .MeshKey = 5u,
        .Revision = 1u,
        .Active = true,
    };
    const Runtime::SpatialDebugClosestFaceOverlay missing =
        consumer.Resolve(missingMesh, probe);
    EXPECT_EQ(missing.Status, Runtime::SpatialDebugClosestFaceStatus::NoMeshSource);
    ExpectFailureIsFinite(missing);
    EXPECT_FALSE(consumer.HasCachedIndex());
    EXPECT_EQ(consumer.RebuildCount(), 0u);
}

TEST(SpatialDebugClosestFace, DegenerateAndInvalidProbeFailClosed)
{
    Runtime::SpatialDebugClosestFaceConsumer consumer{};

    const Geometry::HalfedgeMesh::Mesh validMesh = MakeTwoTriangleMesh();
    const Runtime::SpatialDebugClosestFaceMeshSource validSource{
        .Mesh = &validMesh,
        .MeshKey = 6u,
        .Revision = 1u,
        .Active = true,
    };
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const Runtime::SpatialDebugClosestFaceOverlay invalidProbe =
        consumer.Resolve(validSource, glm::vec3{nan, 0.0f, 0.0f});
    EXPECT_EQ(invalidProbe.Status, Runtime::SpatialDebugClosestFaceStatus::InvalidProbe);
    EXPECT_EQ(invalidProbe.QueryStatus, Geometry::MeshClosestFaceStatus::InvalidQueryPoint);
    ExpectFailureIsFinite(invalidProbe);
    EXPECT_FALSE(consumer.HasCachedIndex());
    EXPECT_EQ(consumer.RebuildCount(), 0u);

    Geometry::HalfedgeMesh::Mesh emptyMesh{};
    const Runtime::SpatialDebugClosestFaceMeshSource emptySource{
        .Mesh = &emptyMesh,
        .MeshKey = 7u,
        .Revision = 1u,
        .Active = true,
    };
    const Runtime::SpatialDebugClosestFaceOverlay empty =
        consumer.Resolve(emptySource, glm::vec3{0.0f, 0.0f, 0.0f});
    EXPECT_EQ(empty.Status, Runtime::SpatialDebugClosestFaceStatus::EmptyMesh);
    EXPECT_EQ(empty.QueryStatus, Geometry::MeshClosestFaceStatus::EmptyIndex);
    ExpectFailureIsFinite(empty);
    EXPECT_FALSE(consumer.HasCachedIndex());
    EXPECT_EQ(consumer.RebuildCount(), 1u);

    const Geometry::HalfedgeMesh::Mesh degenerateMesh = MakeDegenerateTriangleMesh();
    const Runtime::SpatialDebugClosestFaceMeshSource degenerateSource{
        .Mesh = &degenerateMesh,
        .MeshKey = 8u,
        .Revision = 1u,
        .Active = true,
    };
    const Runtime::SpatialDebugClosestFaceOverlay degenerate =
        consumer.Resolve(degenerateSource, glm::vec3{0.2f, 0.0f, 0.0f});
    EXPECT_EQ(degenerate.Status, Runtime::SpatialDebugClosestFaceStatus::IndexBuildFailed);
    EXPECT_EQ(degenerate.QueryStatus, Geometry::MeshClosestFaceStatus::EmptyIndex);
    ExpectFailureIsFinite(degenerate);
    EXPECT_FALSE(consumer.HasCachedIndex());
    EXPECT_EQ(consumer.RebuildCount(), 2u);
}
