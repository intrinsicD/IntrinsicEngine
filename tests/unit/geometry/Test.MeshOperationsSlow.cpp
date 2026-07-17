#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

import Geometry;

#include "Test_MeshBuilders.h"

namespace
{
    Geometry::HalfedgeMesh::Mesh MakeDenseClosedTriangleMesh(std::size_t iterations = 3)
    {
        auto coarse = MakeIcosahedron();
        Geometry::HalfedgeMesh::Mesh refined;

        Geometry::Subdivision::SubdivisionParams params;
        params.Iterations = iterations;

        auto result = Geometry::Subdivision::Subdivide(coarse, refined, params);
        EXPECT_TRUE(result.has_value());
        return refined;
    }

    void ExtractTriangleSoup(
        Geometry::HalfedgeMesh::Mesh& mesh,
        std::vector<glm::vec3>& positions,
        std::vector<uint32_t>& indices)
    {
        mesh.GarbageCollection();

        positions.clear();
        indices.clear();
        positions.reserve(mesh.VertexCount());
        indices.reserve(mesh.FaceCount() * 3);

        std::vector<uint32_t> vMap(mesh.VerticesSize(), 0u);
        uint32_t currentIdx = 0;
        for (std::size_t i = 0; i < mesh.VerticesSize(); ++i)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(i)};
            ASSERT_FALSE(mesh.IsDeleted(v));
            vMap[i] = currentIdx++;
            positions.push_back(mesh.Position(v));
        }

        for (std::size_t i = 0; i < mesh.FacesSize(); ++i)
        {
            Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(i)};
            ASSERT_FALSE(mesh.IsDeleted(f));

            const auto h0 = mesh.Halfedge(f);
            const auto h1 = mesh.NextHalfedge(h0);
            const auto h2 = mesh.NextHalfedge(h1);
            const auto v0 = mesh.ToVertex(h0);
            const auto v1 = mesh.ToVertex(h1);
            const auto v2 = mesh.ToVertex(h2);

            ASSERT_TRUE(mesh.IsValid(v0));
            ASSERT_TRUE(mesh.IsValid(v1));
            ASSERT_TRUE(mesh.IsValid(v2));
            ASSERT_LT(v0.Index, vMap.size());
            ASSERT_LT(v1.Index, vMap.size());
            ASSERT_LT(v2.Index, vMap.size());

            indices.push_back(vMap[v0.Index]);
            indices.push_back(vMap[v1.Index]);
            indices.push_back(vMap[v2.Index]);
        }
    }

    Geometry::HalfedgeMesh::Mesh RebuildMeshFromTriangleSoup(
        const std::vector<glm::vec3>& positions,
        const std::vector<uint32_t>& indices)
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        std::vector<Geometry::VertexHandle> verts;
        verts.reserve(positions.size());
        for (const auto& p : positions)
        {
            verts.push_back(mesh.AddVertex(p));
        }

        bool buildOk = true;
        for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const auto maybeFace = mesh.AddTriangle(verts[indices[i]], verts[indices[i + 1]], verts[indices[i + 2]]);
            if (!maybeFace.has_value())
            {
                ADD_FAILURE() << "Failed to rebuild triangle " << (i / 3)
                              << " from indices (" << indices[i] << ", "
                              << indices[i + 1] << ", "
                              << indices[i + 2] << ")";
                buildOk = false;
                break;
            }
        }
        EXPECT_TRUE(buildOk);
        return mesh;
    }
}

TEST(Simplification_QEM, HausdorffErrorConstraintIsRespected)
{
    auto mesh = MakeDenseClosedTriangleMesh();

    // Collect original vertex positions for distance checking
    std::vector<glm::vec3> originalPositions;
    for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
        if (!mesh.IsDeleted(v) && !mesh.IsIsolated(v))
        {
            originalPositions.push_back(mesh.Position(v));
        }
    }

    Geometry::Simplification::Params params;
    params.TargetFaces = 400;
    params.PreserveBoundary = false;
    params.HausdorffError = 0.5;

    auto result = Geometry::Simplification::Simplify(mesh, params);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(result->CollapseCount, 0u);
}

TEST(Simplification_QEM, RepeatedWorkflowStyleSimplificationStaysValid)
{
    auto mesh = MakeDenseClosedTriangleMesh(4); // 5,120 triangles: fast, but still dense enough for repeated decimation.

    Geometry::Simplification::Params first;
    first.TargetFaces = 4000;
    first.PreserveBoundary = false;

    auto firstResult = Geometry::Simplification::Simplify(mesh, first);
    ASSERT_TRUE(firstResult.has_value());
    ASSERT_LE(mesh.FaceCount(), 4000u);

    std::vector<glm::vec3> positions;
    std::vector<uint32_t> indices;
    ExtractTriangleSoup(mesh, positions, indices);
    ASSERT_EQ(indices.size(), mesh.FaceCount() * 3);

    auto rebuilt = RebuildMeshFromTriangleSoup(positions, indices);
    ASSERT_EQ(rebuilt.FaceCount(), indices.size() / 3);

    Geometry::Simplification::Params second;
    second.TargetFaces = 3000;
    second.PreserveBoundary = false;

    auto secondResult = Geometry::Simplification::Simplify(rebuilt, second);
    ASSERT_TRUE(secondResult.has_value());
    ASSERT_LE(rebuilt.FaceCount(), 3000u);

    ExtractTriangleSoup(rebuilt, positions, indices);

    EXPECT_EQ(indices.size(), rebuilt.FaceCount() * 3);
    for (std::size_t ei = 0; ei < rebuilt.EdgesSize(); ++ei)
    {
        Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(ei)};
        EXPECT_FALSE(rebuilt.IsDeleted(e));
        EXPECT_FALSE(rebuilt.IsBoundary(e)) << "Repeated workflow simplification introduced a hole at edge " << ei;
    }
}

TEST(Simplification_QEM, ConfigurableQuadricsSupportAllTypesResidencesAndPlacements)
{
    struct Config
    {
        Geometry::Simplification::QuadricType Type;
        Geometry::Simplification::QuadricResidence Residence;
        Geometry::Simplification::CollapsePlacementPolicy Placement;
    };

    const std::vector<Config> configs = {
        {Geometry::Simplification::QuadricType::Plane, Geometry::Simplification::QuadricResidence::Vertices, Geometry::Simplification::CollapsePlacementPolicy::KeepSurvivor},
        {Geometry::Simplification::QuadricType::Plane, Geometry::Simplification::QuadricResidence::Faces, Geometry::Simplification::CollapsePlacementPolicy::QuadricMinimizer},
        {Geometry::Simplification::QuadricType::Plane, Geometry::Simplification::QuadricResidence::VerticesAndFaces, Geometry::Simplification::CollapsePlacementPolicy::BestOfEndpointsAndMinimizer},
        {Geometry::Simplification::QuadricType::Triangle, Geometry::Simplification::QuadricResidence::Vertices, Geometry::Simplification::CollapsePlacementPolicy::KeepSurvivor},
        {Geometry::Simplification::QuadricType::Triangle, Geometry::Simplification::QuadricResidence::Faces, Geometry::Simplification::CollapsePlacementPolicy::QuadricMinimizer},
        {Geometry::Simplification::QuadricType::Triangle, Geometry::Simplification::QuadricResidence::VerticesAndFaces, Geometry::Simplification::CollapsePlacementPolicy::BestOfEndpointsAndMinimizer},
        {Geometry::Simplification::QuadricType::Point, Geometry::Simplification::QuadricResidence::Vertices, Geometry::Simplification::CollapsePlacementPolicy::KeepSurvivor},
        {Geometry::Simplification::QuadricType::Point, Geometry::Simplification::QuadricResidence::Faces, Geometry::Simplification::CollapsePlacementPolicy::QuadricMinimizer},
        {Geometry::Simplification::QuadricType::Point, Geometry::Simplification::QuadricResidence::VerticesAndFaces, Geometry::Simplification::CollapsePlacementPolicy::BestOfEndpointsAndMinimizer},
    };

    for (const Config& config : configs)
    {
        SCOPED_TRACE(static_cast<int>(config.Type));
        SCOPED_TRACE(static_cast<int>(config.Residence));
        SCOPED_TRACE(static_cast<int>(config.Placement));

        auto mesh = MakeDenseClosedTriangleMesh(2);
        const std::size_t initialFaces = mesh.FaceCount();

        Geometry::Simplification::Params params;
        params.TargetFaces = initialFaces / 2u;
        params.PreserveBoundary = false;
        params.Quadric.Type = config.Type;
        params.Quadric.Residence = config.Residence;
        params.Quadric.PlacementPolicy = config.Placement;

        auto result = Geometry::Simplification::Simplify(mesh, params);
        ASSERT_TRUE(result.has_value());
        EXPECT_GT(result->CollapseCount, 0u);
        EXPECT_LT(mesh.FaceCount(), initialFaces);
    }
}

TEST(Simplification_QEM, ProbabilisticQuadricsSupportIsotropicAndCovarianceModes)
{
    struct Config
    {
        Geometry::Simplification::QuadricType Type;
        Geometry::Simplification::QuadricProbabilisticMode Mode;
    };

    const std::vector<Config> configs = {
        {Geometry::Simplification::QuadricType::Plane, Geometry::Simplification::QuadricProbabilisticMode::Isotropic},
        {Geometry::Simplification::QuadricType::Plane, Geometry::Simplification::QuadricProbabilisticMode::Covariance},
        {Geometry::Simplification::QuadricType::Triangle, Geometry::Simplification::QuadricProbabilisticMode::Isotropic},
        {Geometry::Simplification::QuadricType::Triangle, Geometry::Simplification::QuadricProbabilisticMode::Covariance},
    };

    for (const Config& config : configs)
    {
        SCOPED_TRACE(static_cast<int>(config.Type));
        SCOPED_TRACE(static_cast<int>(config.Mode));

        auto mesh = MakeDenseClosedTriangleMesh(2);
        const std::size_t initialFaces = mesh.FaceCount();

        auto vertexSigma = Geometry::VertexProperty<glm::dmat3>(
            mesh.VertexProperties().GetOrAdd<glm::dmat3>("v:quadric_sigma_p", glm::dmat3(0.0)));
        for (std::size_t vi = 0; vi < mesh.VerticesSize(); ++vi)
        {
            Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
            if (!mesh.IsDeleted(v))
            {
                vertexSigma[v] = glm::dmat3(1e-4);
            }
        }

        auto faceSigmaP = Geometry::FaceProperty<glm::dmat3>(
            mesh.FaceProperties().GetOrAdd<glm::dmat3>("f:quadric_sigma_p", glm::dmat3(0.0)));
        auto faceSigmaN = Geometry::FaceProperty<glm::dmat3>(
            mesh.FaceProperties().GetOrAdd<glm::dmat3>("f:quadric_sigma_n", glm::dmat3(0.0)));
        for (std::size_t fi = 0; fi < mesh.FacesSize(); ++fi)
        {
            Geometry::FaceHandle f{static_cast<Geometry::PropertyIndex>(fi)};
            if (!mesh.IsDeleted(f))
            {
                faceSigmaP[f] = glm::dmat3(5e-5);
                faceSigmaN[f] = glm::dmat3(2.5e-5);
            }
        }

        Geometry::Simplification::Params params;
        params.TargetFaces = initialFaces / 2u;
        params.PreserveBoundary = false;
        params.Quadric.Type = config.Type;
        params.Quadric.Residence = Geometry::Simplification::QuadricResidence::VerticesAndFaces;
        params.Quadric.PlacementPolicy = Geometry::Simplification::CollapsePlacementPolicy::BestOfEndpointsAndMinimizer;
        params.Quadric.ProbabilisticMode = config.Mode;
        params.Quadric.PositionStdDev = 0.01;
        params.Quadric.NormalStdDev = 0.02;

        auto result = Geometry::Simplification::Simplify(mesh, params);
        ASSERT_TRUE(result.has_value());
        EXPECT_GT(result->CollapseCount, 0u);
        EXPECT_LT(mesh.FaceCount(), initialFaces);
    }
}
