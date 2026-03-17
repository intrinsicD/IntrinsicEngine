#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

import Geometry;

namespace
{
    int EulerCharacteristic(const Geometry::Halfedge::Mesh& mesh)
    {
        return static_cast<int>(mesh.VertexCount())
             - static_cast<int>(mesh.EdgeCount())
             + static_cast<int>(mesh.FaceCount());
    }

    void ExpectClosed(const Geometry::Halfedge::Mesh& mesh)
    {
        for (std::size_t ei = 0; ei < mesh.EdgesSize(); ++ei)
        {
            Geometry::EdgeHandle e{static_cast<Geometry::PropertyIndex>(ei)};
            if (mesh.IsDeleted(e))
            {
                continue;
            }
            EXPECT_FALSE(mesh.IsBoundary(e)) << "edge " << ei << " should not be boundary";
        }
    }
}

TEST(MeshBuilder, AABBProducesQuadBox)
{
    const auto mesh = Geometry::Halfedge::MakeMesh(Geometry::AABB{
        .Min = {-1.0f, -2.0f, -3.0f},
        .Max = { 4.0f,  5.0f,  6.0f},
    });

    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->VertexCount(), 8u);
    EXPECT_EQ(mesh->FaceCount(), 6u);
    EXPECT_EQ(mesh->EdgeCount(), 12u);
    EXPECT_EQ(EulerCharacteristic(*mesh), 2);
    ExpectClosed(*mesh);
}

TEST(MeshBuilder, DegenerateAABBReturnsNullopt)
{
    EXPECT_FALSE(Geometry::Halfedge::MakeMesh(Geometry::AABB{
        .Min = {0.0f, 0.0f, 0.0f},
        .Max = {0.0f, 1.0f, 1.0f},
    }).has_value());
}

TEST(MeshBuilder, OBBProducesQuadBox)
{
    Geometry::OBB obb;
    obb.Center = {1.0f, -2.0f, 0.5f};
    obb.Extents = {2.0f, 1.0f, 0.75f};
    obb.Rotation = glm::normalize(glm::angleAxis(0.7f, glm::vec3{0.0f, 1.0f, 0.0f}));

    const auto mesh = Geometry::Halfedge::MakeMesh(obb);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->VertexCount(), 8u);
    EXPECT_EQ(mesh->FaceCount(), 6u);
    EXPECT_EQ(mesh->EdgeCount(), 12u);
    EXPECT_EQ(EulerCharacteristic(*mesh), 2);
    ExpectClosed(*mesh);
}

TEST(MeshBuilder, SphereSubdivisionZeroMatchesIcosahedronCounts)
{
    const Geometry::Sphere sphere{
        .Center = {0.0f, 0.0f, 0.0f},
        .Radius = 2.5f,
    };

    const auto mesh = Geometry::Halfedge::MakeMesh(sphere, 0);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->VertexCount(), 12u);
    EXPECT_EQ(mesh->FaceCount(), 20u);
    EXPECT_EQ(mesh->EdgeCount(), 30u);
    ExpectClosed(*mesh);

    float minRadius = std::numeric_limits<float>::max();
    float maxRadius = 0.0f;
    for (std::size_t vi = 0; vi < mesh->VerticesSize(); ++vi)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
        if (mesh->IsDeleted(v))
        {
            continue;
        }
        const float radius = glm::length(mesh->Position(v) - sphere.Center);
        minRadius = std::min(minRadius, radius);
        maxRadius = std::max(maxRadius, radius);
    }
    EXPECT_NEAR(minRadius, sphere.Radius, 1e-4f);
    EXPECT_NEAR(maxRadius, sphere.Radius, 1e-4f);
}

TEST(MeshBuilder, DegenerateSphereReturnsNullopt)
{
    EXPECT_FALSE(Geometry::Halfedge::MakeMesh(Geometry::Sphere{
        .Center = {0.0f, 0.0f, 0.0f},
        .Radius = 0.0f,
    }).has_value());
}

TEST(MeshBuilder, CylinderProducesClosedMesh)
{
    const auto mesh = Geometry::Halfedge::MakeMesh(Geometry::Cylinder{
        .PointA = {0.0f, -1.0f, 0.0f},
        .PointB = {0.0f,  1.0f, 0.0f},
        .Radius = 0.5f,
    });

    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->FaceCount(), 26u);
    EXPECT_EQ(EulerCharacteristic(*mesh), 2);
    ExpectClosed(*mesh);
}

TEST(MeshBuilder, CapsuleZeroAxisFallsBackToSphere)
{
    const auto mesh = Geometry::Halfedge::MakeMesh(Geometry::Capsule{
        .PointA = {1.0f, 2.0f, 3.0f},
        .PointB = {1.0f, 2.0f, 3.0f},
        .Radius = 1.25f,
    });

    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->VertexCount(), 642u);
    EXPECT_EQ(mesh->FaceCount(), 1280u);
    EXPECT_EQ(mesh->EdgeCount(), 1920u);
    ExpectClosed(*mesh);
}

TEST(MeshBuilder, CapsuleProducesClosedMesh)
{
    const auto mesh = Geometry::Halfedge::MakeMesh(Geometry::Capsule{
        .PointA = {0.0f, -1.0f, 0.0f},
        .PointB = {0.0f,  1.0f, 0.0f},
        .Radius = 0.5f,
    });

    ASSERT_TRUE(mesh.has_value());
    EXPECT_GT(mesh->VertexCount(), 0u);
    EXPECT_GT(mesh->FaceCount(), 0u);
    EXPECT_EQ(EulerCharacteristic(*mesh), 2);
    ExpectClosed(*mesh);
}

TEST(MeshBuilder, EllipsoidProducesFiniteTransformedSphere)
{
    Geometry::Ellipsoid ellipsoid;
    ellipsoid.Center = {1.0f, -3.0f, 2.0f};
    ellipsoid.Radii = {2.0f, 1.0f, 0.5f};
    ellipsoid.Rotation = glm::normalize(glm::angleAxis(0.45f, glm::vec3{0.0f, 0.0f, 1.0f}));

    const auto mesh = Geometry::Halfedge::MakeMesh(ellipsoid);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(mesh->VertexCount(), 642u);
    EXPECT_EQ(mesh->FaceCount(), 1280u);
    ExpectClosed(*mesh);

    for (std::size_t vi = 0; vi < mesh->VerticesSize(); ++vi)
    {
        Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(vi)};
        if (mesh->IsDeleted(v))
        {
            continue;
        }
        const glm::vec3 p = mesh->Position(v);
        EXPECT_TRUE(std::isfinite(p.x));
        EXPECT_TRUE(std::isfinite(p.y));
        EXPECT_TRUE(std::isfinite(p.z));
    }
}

TEST(MeshBuilder, PlatonicSolidCounts)
{
    const auto tetra = Geometry::Halfedge::MakeMeshTetrahedron();
    EXPECT_EQ(tetra.VertexCount(), 4u);
    EXPECT_EQ(tetra.EdgeCount(), 6u);
    EXPECT_EQ(tetra.FaceCount(), 4u);
    EXPECT_EQ(EulerCharacteristic(tetra), 2);

    const auto octa = Geometry::Halfedge::MakeMeshOctahedron();
    EXPECT_EQ(octa.VertexCount(), 6u);
    EXPECT_EQ(octa.EdgeCount(), 12u);
    EXPECT_EQ(octa.FaceCount(), 8u);
    EXPECT_EQ(EulerCharacteristic(octa), 2);

    const auto icosa = Geometry::Halfedge::MakeMeshIcosahedron();
    EXPECT_EQ(icosa.VertexCount(), 12u);
    EXPECT_EQ(icosa.EdgeCount(), 30u);
    EXPECT_EQ(icosa.FaceCount(), 20u);
    EXPECT_EQ(EulerCharacteristic(icosa), 2);

    const auto dodeca = Geometry::Halfedge::MakeMeshDodecahedron();
    EXPECT_EQ(dodeca.VertexCount(), 20u);
    EXPECT_EQ(dodeca.EdgeCount(), 30u);
    EXPECT_EQ(dodeca.FaceCount(), 12u);
    EXPECT_EQ(EulerCharacteristic(dodeca), 2);
    ExpectClosed(dodeca);
}

