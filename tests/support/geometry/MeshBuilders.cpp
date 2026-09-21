#include <cmath>
#include <vector>
#include <glm/glm.hpp>

import Geometry.HalfedgeMesh;
import Geometry.HalfedgeMesh.Builder;
import Geometry.Properties;

#include "Test_MeshBuilders.h"

Geometry::HalfedgeMesh::Mesh MakeSingleTriangle()
{
    Geometry::HalfedgeMesh::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.5f, std::sqrt(3.0f) / 2.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    return mesh;
}

Geometry::HalfedgeMesh::Mesh MakeTwoTriangleSquare()
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

Geometry::HalfedgeMesh::Mesh MakeTetrahedron()
{
    return Geometry::HalfedgeMesh::MakeMeshTetrahedron();
}

Geometry::HalfedgeMesh::Mesh MakeSubdividedTriangle()
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

Geometry::HalfedgeMesh::Mesh MakeIcosahedron()
{
    return Geometry::HalfedgeMesh::MakeMeshIcosahedron();
}

Geometry::HalfedgeMesh::Mesh MakeQuadPair()
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

Geometry::HalfedgeMesh::Mesh MakeCube(float h, glm::vec3 center)
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

Geometry::HalfedgeMesh::Mesh MakePuncturedTorus()
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

Geometry::HalfedgeMesh::Mesh MakeRightTriangle()
{
    Geometry::HalfedgeMesh::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    (void)mesh.AddTriangle(v0, v1, v2);
    return mesh;
}

Geometry::HalfedgeMesh::Mesh MakeTwoTriangleDiamond()
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

Geometry::HalfedgeMesh::Mesh MakeSingleQuad()
{
    Geometry::HalfedgeMesh::Mesh mesh;
    auto v0 = mesh.AddVertex({0.0f, 0.0f, 0.0f});
    auto v1 = mesh.AddVertex({1.0f, 0.0f, 0.0f});
    auto v2 = mesh.AddVertex({1.0f, 1.0f, 0.0f});
    auto v3 = mesh.AddVertex({0.0f, 1.0f, 0.0f});
    (void)mesh.AddQuad(v0, v1, v2, v3);
    return mesh;
}

Geometry::HalfedgeMesh::Mesh MakeTriangleStrip(int columns)
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

Geometry::HalfedgeMesh::Mesh MakeQuadStrip(int columns)
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

Geometry::HalfedgeMesh::Mesh MakeEquilateralTriangle()
{
    return MakeSingleTriangle();
}

Geometry::HalfedgeMesh::Mesh MakeTwoTriangles()
{
    return MakeTwoTriangleSquare();
}

Geometry::HalfedgeMesh::Mesh MakeDiskAndClosedComponent()
{
    auto mesh = MakeTetrahedron();
    const auto a = mesh.AddVertex({3.0f, 0.0f, 0.0f});
    const auto b = mesh.AddVertex({4.0f, 0.0f, 0.0f});
    const auto c = mesh.AddVertex({3.0f, 1.0f, 0.0f});
    (void)mesh.AddTriangle(a, b, c);
    return mesh;
}

Geometry::HalfedgeMesh::Mesh MakeBowtieTriangles()
{
    auto mesh = MakeSingleTriangle();
    const auto a = mesh.AddVertex({-1.0f, 0.0f, 0.0f});
    const auto b = mesh.AddVertex({0.0f, -1.0f, 0.0f});
    (void)mesh.AddTriangle(Geometry::VertexHandle{0u}, a, b);
    return mesh;
}
