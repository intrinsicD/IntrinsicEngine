#pragma once

// =============================================================================
// Shared test mesh builders for geometry test suites.
//
// Usage: #include "TestMeshBuilders.h" AFTER `import Geometry;` in each test
// file. All functions are inline to avoid ODR issues across translation units.
// =============================================================================

#include <cmath>
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
    return Geometry::Halfedge::MakeMeshTetrahedron();
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
    return Geometry::Halfedge::MakeMeshIcosahedron();
}

// Two quads sharing an edge (butterfly configuration): 5 vertices, 2 faces (quads).
//   v0=(0,0,0)  v1=(1,0,0)  v2=(1,1,0)  v3=(0,1,0)  v4=(2,0,0)  v5=(2,1,0)
//   Quad 0: v0-v1-v2-v3,  Quad 1: v1-v4-v5-v2
inline Geometry::Halfedge::Mesh MakeQuadPair()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    auto v4 = mesh.AddVertex({2.0f, 0.0f, 0.0f});
    auto v5 = mesh.AddVertex({2.0f, 1.0f, 0.0f});
    (void)mesh.AddQuad(v0, v1, v2, v3);
    (void)mesh.AddQuad(v1, v4, v5, v2);
    return mesh;
}

// Axis-aligned cube centered at `center` with half-extent `h`.
// 8 vertices, 12 triangles (2 per face). Closed manifold mesh.
inline Geometry::Halfedge::Mesh MakeCube(float h = 1.0f, glm::vec3 center = glm::vec3(0.0f))
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex(center + glm::vec3(-h, -h, -h));
    auto v1 = mesh.AddVertex(center + glm::vec3( h, -h, -h));
    auto v2 = mesh.AddVertex(center + glm::vec3( h,  h, -h));
    auto v3 = mesh.AddVertex(center + glm::vec3(-h,  h, -h));
    auto v4 = mesh.AddVertex(center + glm::vec3(-h, -h,  h));
    auto v5 = mesh.AddVertex(center + glm::vec3( h, -h,  h));
    auto v6 = mesh.AddVertex(center + glm::vec3( h,  h,  h));
    auto v7 = mesh.AddVertex(center + glm::vec3(-h,  h,  h));
    // -Z
    (void)mesh.AddTriangle(v0, v2, v1);
    (void)mesh.AddTriangle(v0, v3, v2);
    // +Z
    (void)mesh.AddTriangle(v4, v5, v6);
    (void)mesh.AddTriangle(v4, v6, v7);
    // -X
    (void)mesh.AddTriangle(v0, v4, v7);
    (void)mesh.AddTriangle(v0, v7, v3);
    // +X
    (void)mesh.AddTriangle(v1, v2, v6);
    (void)mesh.AddTriangle(v1, v6, v5);
    // -Y
    (void)mesh.AddTriangle(v0, v1, v5);
    (void)mesh.AddTriangle(v0, v5, v4);
    // +Y
    (void)mesh.AddTriangle(v2, v3, v7);
    (void)mesh.AddTriangle(v2, v7, v6);
    return mesh;
}

// Right triangle in XY plane: v0=(0,0,0), v1=(1,0,0), v2=(0,1,0).
// 1 face, 3 vertices, 3 boundary edges.
// Used by property access and per-face attribute tests.
inline Geometry::Halfedge::Mesh MakeRightTriangle()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    return mesh;
}

// Two-triangle mesh sharing edge v1-v2:
//   v0=(0,0,0) v1=(1,0,0) v2=(0,1,0) v3=(1,1,0)
//   Face 0: v0-v1-v2, Face 1: v2-v1-v3
// 4 vertices, 2 faces, 1 shared interior edge, 4 boundary edges.
inline Geometry::Halfedge::Mesh MakeTwoTriangleDiamond()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    (void)mesh.AddTriangle(v2, v1, v3);
    return mesh;
}

// Single quad face: v0=(0,0,0), v1=(1,0,0), v2=(1,1,0), v3=(0,1,0).
// Tests polygon (non-triangle) face support.
inline Geometry::Halfedge::Mesh MakeSingleQuad()
{
    Geometry::Halfedge::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    (void)mesh.AddQuad(v0, v1, v2, v3);
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
