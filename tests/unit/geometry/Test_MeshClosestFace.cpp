#include <gtest/gtest.h>

#include <cmath>
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

    // Brute-force exact nearest face distance over all non-deleted faces.
    double BruteForceMinSq(const Geometry::HalfedgeMesh::Mesh& mesh, const glm::vec3& p)
    {
        double best = std::numeric_limits<double>::max();
        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            const FaceHandle f{static_cast<PropertyIndex>(fi)};
            if (mesh.IsDeleted(f)) continue;
            std::vector<glm::vec3> c;
            for (const Geometry::VertexHandle v : mesh.VerticesAroundFace(f)) c.push_back(mesh.Position(v));
            for (std::size_t i = 1; i + 1 < c.size(); ++i)
            {
                const Triangle tri{c[0], c[i], c[i + 1]};
                best = std::min(best, Geometry::SquaredDistance(tri, p));
            }
        }
        return best;
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
        const double brute = BruteForceMinSq(mesh, p);
        EXPECT_NEAR(r.SquaredDistance, static_cast<float>(brute), 1e-4f) << "at k=" << k;
        // Returned closest point is consistent with the reported distance.
        const glm::vec3 d = p - r.Point;
        EXPECT_NEAR(glm::dot(d, d), r.SquaredDistance, 1e-4f);
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
}

TEST(GeometryMeshClosestFace, FailClosed)
{
    Geometry::MeshClosestFaceIndex empty;
    EXPECT_FALSE(empty.IsBuilt());
    EXPECT_FALSE(empty.Query(glm::vec3{0.0f}).Found); // unbuilt

    const auto mesh = MakeGrid(2);
    Geometry::MeshClosestFaceIndex index;
    ASSERT_TRUE(index.Build(mesh));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(index.Query(glm::vec3{nan, 0.0f, 0.0f}).Found); // non-finite probe
}
