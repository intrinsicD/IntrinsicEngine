#pragma once

// =============================================================================
// Shared test mesh builders for geometry test suites.
//
// Usage: #include "TestMeshBuilders.h" AFTER `import Geometry;` in each test
// file. All functions are inline to avoid ODR issues across translation units.
// =============================================================================

#include <cmath>
#include <vector>

#include <glm/glm.hpp>

// Single equilateral triangle in the XY plane:
//   v0=(0,0,0)  v1=(1,0,0)  v2=(0.5, sqrt(3)/2, 0)
// Area = sqrt(3)/4 ≈ 0.4330.  1 face, 3 vertices, 3 boundary edges.
inline Geometry::Halfedge::Mesh MakeSingleTriangle()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.5f, std::sqrt(3.0f) / 2.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    return mesh;
}

// Unit square split into two right triangles: 4 vertices, 2 faces.
//   v0=(0,0,0)  v1=(1,0,0)  v2=(1,1,0)  v3=(0,1,0)
//   Face 0: v0-v1-v2,  Face 1: v0-v2-v3
inline Geometry::Halfedge::Mesh MakeTwoTriangleSquare()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v0, v2, v3);
    return mesh;
}

// Regular tetrahedron (closed mesh, no boundary).
//   v0=(1,1,1)  v1=(1,-1,-1)  v2=(-1,1,-1)  v3=(-1,-1,1)
// All edges have equal length sqrt(8), all faces equilateral.
inline Geometry::Halfedge::Mesh MakeTetrahedron()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({1.0f,  1.0f,  1.0f});
    auto v1 = mesh.AddVertex({1.0f, -1.0f, -1.0f});
    auto v2 = mesh.AddVertex({-1.0f, 1.0f, -1.0f});
    auto v3 = mesh.AddVertex({-1.0f,-1.0f,  1.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v0, v2, v3);
    (void)mesh.AddTriangle(v0, v3, v1);
    (void)mesh.AddTriangle(v1, v3, v2);
    return mesh;
}

// Equilateral triangle subdivided once: 6 vertices, 4 faces.
//   v3=mid(v0,v1), v4=mid(v1,v2), v5=mid(v0,v2)
// v3 is an interior vertex with valence 6 — good for Laplacian testing.
inline Geometry::Halfedge::Mesh MakeSubdividedTriangle()
{
    const float s = std::sqrt(3.0f);
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({2.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, s,    0.0f});
    auto v3 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v4 = mesh.AddVertex({1.5f, s / 2.0f, 0.0f});
    auto v5 = mesh.AddVertex({0.5f, s / 2.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v3, v5);
    (void)mesh.AddTriangle(v3, v1, v4);
    (void)mesh.AddTriangle(v5, v4, v2);
    (void)mesh.AddTriangle(v3, v4, v5);
    return mesh;
}

// Regular icosahedron (closed, 12 vertices, 20 faces, 30 edges).
// All vertices lie on the unit sphere.
inline Geometry::Halfedge::Mesh MakeIcosahedron()
{
    Geometry::Halfedge::Mesh mesh;
    const float phi   = (1.0f + std::sqrt(5.0f)) / 2.0f;
    const float scale = 1.0f / std::sqrt(1.0f + phi * phi);

    auto v0  = mesh.AddVertex(glm::vec3( 0,  1,  phi) * scale);
    auto v1  = mesh.AddVertex(glm::vec3( 0, -1,  phi) * scale);
    auto v2  = mesh.AddVertex(glm::vec3( 0,  1, -phi) * scale);
    auto v3  = mesh.AddVertex(glm::vec3( 0, -1, -phi) * scale);
    auto v4  = mesh.AddVertex(glm::vec3( 1,  phi,  0) * scale);
    auto v5  = mesh.AddVertex(glm::vec3(-1,  phi,  0) * scale);
    auto v6  = mesh.AddVertex(glm::vec3( 1, -phi,  0) * scale);
    auto v7  = mesh.AddVertex(glm::vec3(-1, -phi,  0) * scale);
    auto v8  = mesh.AddVertex(glm::vec3( phi,  0,  1) * scale);
    auto v9  = mesh.AddVertex(glm::vec3(-phi,  0,  1) * scale);
    auto v10 = mesh.AddVertex(glm::vec3( phi,  0, -1) * scale);
    auto v11 = mesh.AddVertex(glm::vec3(-phi,  0, -1) * scale);

    (void)mesh.AddTriangle(v0, v1, v8);
    (void)mesh.AddTriangle(v0, v8, v4);
    (void)mesh.AddTriangle(v0, v4, v5);
    (void)mesh.AddTriangle(v0, v5, v9);
    (void)mesh.AddTriangle(v0, v9, v1);
    (void)mesh.AddTriangle(v1, v6, v8);
    (void)mesh.AddTriangle(v1, v7, v6);
    (void)mesh.AddTriangle(v1, v9, v7);
    (void)mesh.AddTriangle(v2, v3, v11);
    (void)mesh.AddTriangle(v2, v10, v3);
    (void)mesh.AddTriangle(v2, v4, v10);
    (void)mesh.AddTriangle(v2, v5, v4);
    (void)mesh.AddTriangle(v2, v11, v5);
    (void)mesh.AddTriangle(v3, v6, v7);
    (void)mesh.AddTriangle(v3, v10, v6);
    (void)mesh.AddTriangle(v3, v7, v11);
    (void)mesh.AddTriangle(v4, v8, v10);
    (void)mesh.AddTriangle(v5, v11, v9);
    (void)mesh.AddTriangle(v6, v10, v8);
    (void)mesh.AddTriangle(v7, v9, v11);

    return mesh;
}

// ---- Name aliases used in some test files ----

// Same geometry as MakeSingleTriangle (used in Test_MeshQuality).
inline Geometry::Halfedge::Mesh MakeEquilateralTriangle()
{
    return MakeSingleTriangle();
}

// Same geometry as MakeTwoTriangleSquare (used in Test_AdaptiveRemeshing).
inline Geometry::Halfedge::Mesh MakeTwoTriangles()
{
    return MakeTwoTriangleSquare();
}
