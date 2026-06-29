#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <vector>
#include <glm/glm.hpp>

import Geometry;

namespace
{
    using Geometry::FaceHandle;
    using Geometry::PropertyIndex;
    using Geometry::Triangle;
    namespace MU = Geometry::MeshUtils;

    // (N x N) grid of two triangles per cell on the z=0 plane in [0,1]^2.
    Geometry::HalfedgeMesh::Mesh MakeGrid(int n)
    {
        std::vector<glm::vec3> pos;
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i)
                pos.emplace_back(static_cast<float>(i) / n, static_cast<float>(j) / n, 0.0f);
        std::vector<std::uint32_t> idx;
        auto vid = [&](int i, int j) { return static_cast<std::uint32_t>(j * (n + 1) + i); };
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i)
            {
                idx.push_back(vid(i, j)); idx.push_back(vid(i + 1, j)); idx.push_back(vid(i + 1, j + 1));
                idx.push_back(vid(i, j)); idx.push_back(vid(i + 1, j + 1)); idx.push_back(vid(i, j + 1));
            }
        auto mesh = MU::BuildHalfedgeMeshFromIndexedTriangles(pos, idx);
        EXPECT_TRUE(mesh.has_value());
        return std::move(*mesh);
    }

    [[nodiscard]] bool HitLess(const Geometry::MeshClosestFaceHit& lhs,
                               const Geometry::MeshClosestFaceHit& rhs)
    {
        if (lhs.SquaredDistance != rhs.SquaredDistance)
            return lhs.SquaredDistance < rhs.SquaredDistance;
        if (lhs.Face.Index != rhs.Face.Index)
            return lhs.Face.Index < rhs.Face.Index;
        return lhs.PrimitiveIndex < rhs.PrimitiveIndex;
    }

    [[nodiscard]] std::vector<Geometry::MeshClosestFaceHit> BruteForceHits(
        const Geometry::HalfedgeMesh::Mesh& mesh,
        const glm::vec3& p,
        std::optional<std::vector<FaceHandle>> faces = std::nullopt)
    {
        std::vector<Geometry::MeshClosestFaceHit> hits;
        std::uint32_t primitiveIndex = 0u;
        const auto visitFace = [&](const FaceHandle f)
        {
            if (!f.IsValid() || !mesh.IsValid(f) || mesh.IsDeleted(f))
                return;
            std::vector<glm::vec3> c;
            for (const Geometry::VertexHandle v : mesh.VerticesAroundFace(f))
            {
                if (mesh.IsValid(v) && !mesh.IsDeleted(v))
                    c.push_back(mesh.Position(v));
            }
            for (std::size_t i = 1; i + 1 < c.size(); ++i)
            {
                const Triangle tri{c[0], c[i], c[i + 1]};
                if (tri.GetArea() <= 1.0e-6f)
                    continue;
                const glm::vec3 cp = Geometry::ClosestPoint(tri, p);
                const glm::vec3 n = tri.GetNormal();
                const float sd = static_cast<float>(
                    Geometry::SquaredDistance(tri, p));
                hits.push_back(Geometry::MeshClosestFaceHit{
                    .Face = f,
                    .Point = cp,
                    .Normal = n,
                    .SquaredDistance = sd,
                    .PrimitiveIndex = primitiveIndex,
                });
                ++primitiveIndex;
            }
        };

        if (faces.has_value())
        {
            for (const FaceHandle f : *faces)
                visitFace(f);
        }
        else
        {
            for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
                visitFace(FaceHandle{static_cast<PropertyIndex>(fi)});
        }

        std::sort(hits.begin(), hits.end(), HitLess);
        return hits;
    }

    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeDegenerateTriangleMesh()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        const auto v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        const auto v2 = mesh.AddVertex(glm::vec3(2.0f, 0.0f, 0.0f));
        EXPECT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());
        return mesh;
    }

    [[nodiscard]] Geometry::HalfedgeMesh::Mesh MakeNonFiniteTriangleMesh()
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        const auto v0 = mesh.AddVertex(glm::vec3(0.0f, 0.0f, 0.0f));
        const auto v1 = mesh.AddVertex(glm::vec3(1.0f, 0.0f, 0.0f));
        const auto v2 = mesh.AddVertex(
            glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f));
        EXPECT_TRUE(mesh.AddTriangle(v0, v1, v2).has_value());
        return mesh;
    }
}

TEST(GeometryMeshClosestFace, MatchesBruteForce)
{
    const auto mesh = MakeGrid(8);
    Geometry::MeshClosestFaceIndex index;
    ASSERT_TRUE(index.Build(mesh));
    EXPECT_TRUE(index.IsBuilt());

    std::mt19937 rng(2024);
    std::uniform_real_distribution<float> xy(-0.3f, 1.3f);
    std::uniform_real_distribution<float> z(-0.5f, 0.5f);
    for (int k = 0; k < 300; ++k)
    {
        const glm::vec3 p{xy(rng), xy(rng), z(rng)};
        const auto r = index.Query(p);
        ASSERT_TRUE(r.Found);
        EXPECT_EQ(r.Status, Geometry::MeshClosestFaceStatus::Success);
        const std::vector<Geometry::MeshClosestFaceHit> brute =
            BruteForceHits(mesh, p);
        ASSERT_FALSE(brute.empty());
        EXPECT_EQ(r.Face.Index, brute.front().Face.Index) << "at k=" << k;
        EXPECT_EQ(r.PrimitiveIndex, brute.front().PrimitiveIndex)
            << "at k=" << k;
        EXPECT_NEAR(r.SquaredDistance, brute.front().SquaredDistance, 1e-4f)
            << "at k=" << k;
        // Returned closest point is consistent with the reported distance.
        const glm::vec3 d = p - r.Point;
        EXPECT_NEAR(glm::dot(d, d), r.SquaredDistance, 1e-4f);
        EXPECT_GT(r.Diagnostics.VisitedNodes, 0u);
        EXPECT_GT(r.Diagnostics.DistanceEvaluations, 0u);
    }
}

TEST(GeometryMeshClosestFace, PointOnSurfaceHasZeroDistance)
{
    const auto mesh = MakeGrid(4);
    Geometry::MeshClosestFaceIndex index;
    ASSERT_TRUE(index.Build(mesh));
    const glm::vec3 onSurface{0.37f, 0.62f, 0.0f};
    const auto r = index.Query(onSurface);
    ASSERT_TRUE(r.Found);
    EXPECT_NEAR(r.SquaredDistance, 0.0f, 1e-6f);
    EXPECT_NEAR(glm::length(r.Point - onSurface), 0.0f, 1e-3f);
    EXPECT_LT(r.Diagnostics.DistanceEvaluations, index.FaceCount());
}

TEST(GeometryMeshClosestFace, FailClosed)
{
    Geometry::MeshClosestFaceIndex empty;
    EXPECT_FALSE(empty.IsBuilt());
    const auto unbuilt = empty.Query(glm::vec3{0.0f});
    EXPECT_FALSE(unbuilt.Found);
    EXPECT_EQ(unbuilt.Status, Geometry::MeshClosestFaceStatus::UnbuiltIndex);

    const auto mesh = MakeGrid(2);
    Geometry::MeshClosestFaceIndex index;
    ASSERT_TRUE(index.Build(mesh));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const auto invalidPoint = index.Query(glm::vec3{nan, 0.0f, 0.0f});
    EXPECT_FALSE(invalidPoint.Found);
    EXPECT_EQ(invalidPoint.Status,
              Geometry::MeshClosestFaceStatus::InvalidQueryPoint);

    Geometry::MeshClosestFaceIndex degenerate;
    EXPECT_FALSE(degenerate.Build(MakeDegenerateTriangleMesh()));
    EXPECT_FALSE(degenerate.IsBuilt());

    Geometry::MeshClosestFaceIndex nonFinite;
    EXPECT_FALSE(nonFinite.Build(MakeNonFiniteTriangleMesh()));
    EXPECT_FALSE(nonFinite.IsBuilt());
}

TEST(GeometryMeshClosestFace, KNearestAndRadiusMatchBruteForce)
{
    const auto mesh = MakeGrid(6);
    Geometry::MeshClosestFaceIndex index;
    ASSERT_TRUE(index.Build(mesh));

    const glm::vec3 p{0.17f, 0.22f, 0.19f};
    const std::vector<Geometry::MeshClosestFaceHit> brute =
        BruteForceHits(mesh, p);
    ASSERT_GE(brute.size(), 8u);

    const Geometry::MeshClosestFaceKNearestResult nearest =
        index.QueryKNearest(p, 5u);
    ASSERT_TRUE(nearest.Succeeded());
    ASSERT_EQ(nearest.Hits.size(), 5u);
    EXPECT_EQ(nearest.Diagnostics.ReturnedCount, 5u);
    EXPECT_GT(nearest.Diagnostics.VisitedNodes, 0u);
    EXPECT_GT(nearest.Diagnostics.DistanceEvaluations, 0u);
    for (std::size_t i = 0u; i < nearest.Hits.size(); ++i)
    {
        EXPECT_EQ(nearest.Hits[i].Face.Index, brute[i].Face.Index);
        EXPECT_EQ(nearest.Hits[i].PrimitiveIndex, brute[i].PrimitiveIndex);
        EXPECT_NEAR(nearest.Hits[i].SquaredDistance,
                    brute[i].SquaredDistance,
                    1.0e-5f);
    }

    const float radius = 0.24f;
    const float radiusSq = radius * radius;
    std::vector<Geometry::MeshClosestFaceHit> bruteWithinRadius;
    for (const Geometry::MeshClosestFaceHit& hit : brute)
    {
        if (hit.SquaredDistance <= radiusSq)
            bruteWithinRadius.push_back(hit);
    }

    const Geometry::MeshClosestFaceRadiusResult radiusResult =
        index.QueryRadius(p, radius);
    ASSERT_TRUE(radiusResult.Succeeded());
    ASSERT_EQ(radiusResult.Hits.size(), bruteWithinRadius.size());
    EXPECT_EQ(radiusResult.Diagnostics.ReturnedCount,
              bruteWithinRadius.size());
    for (std::size_t i = 0u; i < radiusResult.Hits.size(); ++i)
    {
        EXPECT_EQ(radiusResult.Hits[i].Face.Index,
                  bruteWithinRadius[i].Face.Index);
        EXPECT_NEAR(radiusResult.Hits[i].SquaredDistance,
                    bruteWithinRadius[i].SquaredDistance,
                    1.0e-5f);
    }
}

TEST(GeometryMeshClosestFace, SubsetBuildRestrictsCandidates)
{
    const auto mesh = MakeGrid(3);
    const std::vector<FaceHandle> faces{
        FaceHandle{static_cast<PropertyIndex>(0u)},
        FaceHandle{static_cast<PropertyIndex>(mesh.FacesSize() - 1u)},
    };

    Geometry::MeshClosestFaceIndex index;
    ASSERT_TRUE(index.Build(mesh, faces));
    EXPECT_EQ(index.FaceCount(), faces.size());

    const glm::vec3 p{0.9f, 0.9f, 0.05f};
    const Geometry::MeshClosestFaceResult result = index.Query(p);
    ASSERT_TRUE(result.Found);
    const std::vector<Geometry::MeshClosestFaceHit> brute =
        BruteForceHits(mesh, p, faces);
    ASSERT_FALSE(brute.empty());

    EXPECT_EQ(result.Face.Index, brute.front().Face.Index);
    EXPECT_EQ(result.PrimitiveIndex, brute.front().PrimitiveIndex);
    EXPECT_NEAR(result.SquaredDistance,
                brute.front().SquaredDistance,
                1.0e-5f);
}
