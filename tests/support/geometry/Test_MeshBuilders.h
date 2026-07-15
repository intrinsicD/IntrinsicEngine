#pragma once

// =============================================================================
// Shared test mesh builders for geometry test suites.
//
// Usage: #include "Test_MeshBuilders.h" AFTER `import Geometry;` in each test
// file. All functions are inline to avoid ODR issues across translation units.
// =============================================================================

#include <cmath>
#include <vector>
#include <glm/glm.hpp>

// Single equilateral triangle in the XY plane:
//   v0=(0,0,0)  v1=(1,0,0)  v2=(0.5, sqrt(3)/2, 0)
// Area = sqrt(3)/4 ≈ 0.4330.  1 face, 3 vertices, 3 boundary edges.
inline Geometry::HalfedgeMesh::Mesh MakeSingleTriangle()
{
    Geometry::HalfedgeMesh::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.5f, std::sqrt(3.0f) / 2.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    return mesh;
}

// Unit square split into two right triangles: 4 vertices, 2 faces.
//   v0=(0,0,0)  v1=(1,0,0)  v2=(1,1,0)  v3=(0,1,0)
//   Face 0: v0-v1-v2,  Face 1: v0-v2-v3
inline Geometry::HalfedgeMesh::Mesh MakeTwoTriangleSquare()
{
    Geometry::HalfedgeMesh::Mesh mesh;
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
inline Geometry::HalfedgeMesh::Mesh MakeTetrahedron()
{
    return Geometry::HalfedgeMesh::MakeMeshTetrahedron();
}

// Equilateral triangle subdivided once: 6 vertices, 4 faces.
//   v3=mid(v0,v1), v4=mid(v1,v2), v5=mid(v0,v2)
// v3 is an interior vertex with valence 6 — good for Laplacian testing.
inline Geometry::HalfedgeMesh::Mesh MakeSubdividedTriangle()
{
    const float s = std::sqrt(3.0f);
    Geometry::HalfedgeMesh::Mesh mesh;
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
inline Geometry::HalfedgeMesh::Mesh MakeIcosahedron()
{
    return Geometry::HalfedgeMesh::MakeMeshIcosahedron();
}

// Two quads sharing an edge (butterfly configuration): 5 vertices, 2 faces (quads).
//   v0=(0,0,0)  v1=(1,0,0)  v2=(1,1,0)  v3=(0,1,0)  v4=(2,0,0)  v5=(2,1,0)
//   Quad 0: v0-v1-v2-v3,  Quad 1: v1-v4-v5-v2
inline Geometry::HalfedgeMesh::Mesh MakeQuadPair()
{
    Geometry::HalfedgeMesh::Mesh mesh;
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
inline Geometry::HalfedgeMesh::Mesh MakeCube(float h = 1.0f, glm::vec3 center = glm::vec3(0.0f))
{
    Geometry::HalfedgeMesh::Mesh mesh;
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

// Periodic 4x4 triangulation of a torus with one triangle removed. It is
// connected and has exactly one boundary loop, but chi = -1 (genus one), so a
// boundary-count-only "disk" check accepts it incorrectly.
inline Geometry::HalfedgeMesh::Mesh MakePuncturedTorus()
{
    constexpr int kMajorSegments = 4;
    constexpr int kMinorSegments = 4;
    constexpr float kMajorRadius = 2.0f;
    constexpr float kMinorRadius = 0.5f;
    constexpr float kTwoPi = 6.28318530717958647692f;

    Geometry::HalfedgeMesh::Mesh mesh;
    std::vector<std::vector<Geometry::VertexHandle>> vertices(
        kMajorSegments,
        std::vector<Geometry::VertexHandle>(kMinorSegments));

    for (int i = 0; i < kMajorSegments; ++i)
    {
        const float theta =
            kTwoPi * static_cast<float>(i) / static_cast<float>(kMajorSegments);
        for (int j = 0; j < kMinorSegments; ++j)
        {
            const float phi =
                kTwoPi * static_cast<float>(j) / static_cast<float>(kMinorSegments);
            const float radial = kMajorRadius + kMinorRadius * std::cos(phi);
            vertices[i][j] = mesh.AddVertex({
                radial * std::cos(theta),
                radial * std::sin(theta),
                kMinorRadius * std::sin(phi),
            });
        }
    }

    Geometry::FaceHandle puncture;
    for (int i = 0; i < kMajorSegments; ++i)
    {
        const int nextI = (i + 1) % kMajorSegments;
        for (int j = 0; j < kMinorSegments; ++j)
        {
            const int nextJ = (j + 1) % kMinorSegments;
            const auto a = vertices[i][j];
            const auto b = vertices[nextI][j];
            const auto c = vertices[nextI][nextJ];
            const auto d = vertices[i][nextJ];
            const auto first = mesh.AddTriangle(a, b, c);
            (void)mesh.AddTriangle(a, c, d);
            if (i == 0 && j == 0 && first.has_value())
                puncture = *first;
        }
    }

    if (puncture.IsValid())
        mesh.DeleteFace(puncture);
    return mesh;
}

// Right triangle in XY plane: v0=(0,0,0), v1=(1,0,0), v2=(0,1,0).
// 1 face, 3 vertices, 3 boundary edges.
// Used by property access and per-face attribute tests.
inline Geometry::HalfedgeMesh::Mesh MakeRightTriangle()
{
    Geometry::HalfedgeMesh::Mesh mesh;
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
inline Geometry::HalfedgeMesh::Mesh MakeTwoTriangleDiamond()
{
    Geometry::HalfedgeMesh::Mesh mesh;
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
inline Geometry::HalfedgeMesh::Mesh MakeSingleQuad()
{
    Geometry::HalfedgeMesh::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    (void)mesh.AddQuad(v0, v1, v2, v3);
    return mesh;
}

// Triangle strip with N columns: a zig-zag strip of 2*N triangles.
//   Row 0: y=0 vertices at x=0,1,...,N
//   Row 1: y=1 vertices at x=0,1,...,N
// Produces (N+1)*2 vertices, 2*N faces, with well-defined edge loops/rings.
//
//   v3---v4---v5---v6          (row 1, y=1)
//   |  / |  / |  / |
//   | /  | /  | /  |
//   v0---v1---v2---v3_alias    (row 0, y=0)  [but v7=v3 alias not needed, these are separate vertices]
//
// Actually uses separate vertices:
//   v0=(0,0) v1=(1,0) v2=(2,0) ... v_N=(N,0)
//   v_{N+1}=(0,1) v_{N+2}=(1,1) ... v_{2N+1}=(N,1)
//   Faces: for each column i in [0,N-1]:
//     Lower: v_i, v_{i+1}, v_{N+1+i}
//     Upper: v_{i+1}, v_{N+2+i}, v_{N+1+i}
inline Geometry::HalfedgeMesh::Mesh MakeTriangleStrip(int columns = 3)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    // Bottom row
    std::vector<Geometry::VertexHandle> bot, top;
    for (int i = 0; i <= columns; ++i)
        bot.push_back(mesh.AddVertex({static_cast<float>(i), 0.0f, 0.0f}));
    // Top row
    for (int i = 0; i <= columns; ++i)
        top.push_back(mesh.AddVertex({static_cast<float>(i), 1.0f, 0.0f}));
    // Triangulate
    for (int i = 0; i < columns; ++i)
    {
        (void)mesh.AddTriangle(bot[i], bot[i + 1], top[i]);
        (void)mesh.AddTriangle(bot[i + 1], top[i + 1], top[i]);
    }
    return mesh;
}

// Quad strip with N columns: N quads in a row.
//   v0--v1--v2--v3     (bottom, y=0)
//   |   |   |   |
//   v4--v5--v6--v7     (top, y=1)
// Produces (N+1)*2 vertices, N faces (quads).
inline Geometry::HalfedgeMesh::Mesh MakeQuadStrip(int columns = 3)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    std::vector<Geometry::VertexHandle> bot, top;
    for (int i = 0; i <= columns; ++i)
        bot.push_back(mesh.AddVertex({static_cast<float>(i), 0.0f, 0.0f}));
    for (int i = 0; i <= columns; ++i)
        top.push_back(mesh.AddVertex({static_cast<float>(i), 1.0f, 0.0f}));
    for (int i = 0; i < columns; ++i)
        (void)mesh.AddQuad(bot[i], bot[i + 1], top[i + 1], top[i]);
    return mesh;
}

// ---- Name aliases used in some test files ----

// Same geometry as MakeSingleTriangle (used in Test_MeshQuality).
inline Geometry::HalfedgeMesh::Mesh MakeEquilateralTriangle()
{
    return MakeSingleTriangle();
}

// Same geometry as MakeTwoTriangleSquare (used in Test_AdaptiveRemeshing).
inline Geometry::HalfedgeMesh::Mesh MakeTwoTriangles()
{
    return MakeTwoTriangleSquare();
}
