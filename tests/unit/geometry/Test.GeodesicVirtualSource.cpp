// Analytic and failure-contract coverage for virtual-source surface distance.
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>
#include <glm/glm.hpp>
import Geometry;

namespace
{
    Geometry::HalfedgeMesh::Mesh Grid(int n, float scale = 1, bool folded = false)
    {
        Geometry::HalfedgeMesh::Mesh mesh;
        for (int y = 0; y <= n; ++y)
            for (int x = 0; x <= n; ++x)
                (void)mesh.AddVertex(folded && x > n / 2
                                         ? glm::vec3{float(n / 2) * scale, float(y) * scale,
                                                     float(x - n / 2) * scale}
                                         : glm::vec3{float(x) * scale, float(y) * scale, 0});
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                auto a = Geometry::VertexHandle{unsigned(y * (n + 1) + x)};
                auto b = Geometry::VertexHandle{a.Index + 1};
                auto c = Geometry::VertexHandle{a.Index + unsigned(n + 1)};
                auto d = Geometry::VertexHandle{c.Index + 1};
                EXPECT_TRUE(mesh.AddTriangle(a, b, d).has_value());
                EXPECT_TRUE(mesh.AddTriangle(a, d, c).has_value());
            }
        return mesh;
    }
}

TEST(GeodesicVirtualSource, BoundaryAndInteriorSourcesBoundPlanarError)
{
    for (const std::size_t source : {0u, 40u, 80u})
    {
        auto mesh = Grid(8);
        auto result = Geometry::Geodesic::ComputeVirtualSourceDistance(mesh, std::array{source});
        ASSERT_TRUE(result.Succeeded()) << Geometry::Geodesic::ToString(result.Status);
        EXPECT_EQ(result.UnreachableVertexCount, 0);
        // The published one-source-per-face visibility heuristic can bend
        // planar paths. The authors' supplemental runner using double arithmetic on
        // this grid with consistently normalized seeds has maximum error 0.2295
        // for the interior source. Keep an explicit 0.25-unit acceptance bound.
        for (std::size_t v = 0; v < mesh.VerticesSize(); ++v)
        {
            const double exact =
                glm::length(glm::dvec3(mesh.Positions()[v]) - glm::dvec3(mesh.Positions()[source]));
            EXPECT_NEAR(result.Distances[v], exact,
                        source == 40 ? .25 : std::max(1e-5, .01 * exact))
                << v;
        }
        EXPECT_FALSE(mesh.VertexProperties().Exists("v:geodesic_distance"));
    }
}

TEST(GeodesicVirtualSource, FoldedSurfaceUsesIntrinsicDistanceAndScales)
{
    // Binary-exact scales preserve the input edge metric under float storage.
    // Decimal coordinates perturb tied face winners in this approximation.
    for (float scale : {0.125f, 1.f, 1024.f})
    {
        auto mesh = Grid(8, scale, true);
        auto flat = Grid(8, scale);
        auto reference =
            Geometry::Geodesic::ComputeVirtualSourceDistance(flat, std::array<std::size_t, 1>{0});
        auto result =
            Geometry::Geodesic::ComputeVirtualSourceDistance(mesh, std::array<std::size_t, 1>{0});
        ASSERT_TRUE(result.Succeeded());
        for (int y = 0; y <= 8; ++y)
            for (int x = 0; x <= 8; ++x)
                EXPECT_NEAR(result.Distances[y * 9 + x], reference.Distances[y * 9 + x],
                            1e-5 * scale);
    }
}

TEST(GeodesicVirtualSource, MultipleSourcesAreOrderIndependentAndRepeatable)
{
    auto mesh = Grid(8);
    auto a = Geometry::Geodesic::ComputeVirtualSourceDistance(mesh,
                                                              std::array<std::size_t, 3>{0, 80, 0});
    auto b =
        Geometry::Geodesic::ComputeVirtualSourceDistance(mesh, std::array<std::size_t, 2>{80, 0});
    ASSERT_TRUE(a.Succeeded());
    ASSERT_TRUE(b.Succeeded());
    EXPECT_EQ(a.SourceCount, 2);
    EXPECT_EQ(a.Distances, b.Distances);
    EXPECT_EQ(a.HalfedgeExpansions, b.HalfedgeExpansions);
    EXPECT_EQ(a.Distances[0], 0);
    EXPECT_EQ(a.Distances[80], 0);
    for (int y = 0; y <= 8; ++y)
        for (int x = 0; x <= 8; ++x)
            EXPECT_NEAR(a.Distances[y * 9 + x],
                        std::min(std::hypot(x, y), std::hypot(8 - x, 8 - y)), .3);
}

TEST(GeodesicVirtualSource, UnreachableAndIsolatedSourcesRemainExplicit)
{
    auto mesh = Grid(1);
    auto a = mesh.AddVertex({10, 0, 0}), b = mesh.AddVertex({11, 0, 0}),
         c = mesh.AddVertex({10, 1, 0});
    ASSERT_TRUE(mesh.AddTriangle(a, b, c));
    auto isolated = mesh.AddVertex({20, 0, 0});
    auto result = Geometry::Geodesic::ComputeVirtualSourceDistance(
        mesh, std::array<std::size_t, 2>{0, isolated.Index});
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.UnreachableVertexCount, 3);
    EXPECT_TRUE(std::isinf(result.Distances[a.Index]));
    EXPECT_EQ(result.Distances[isolated.Index], 0);
}

TEST(GeodesicVirtualSource, RejectsInvalidInputsAndBudgetWithoutPublishing)
{
    using namespace Geometry::Geodesic;
    auto mesh = Grid(2);
    EXPECT_EQ(ComputeVirtualSourceDistance(mesh, std::array<std::size_t, 1>{99}).Status,
              VirtualSourceStatus::InvalidSources);
    EXPECT_EQ(ComputeVirtualSourceDistance(mesh, std::span<const std::size_t>{}).Status,
              VirtualSourceStatus::InvalidSources);
    auto capped =
        ComputeVirtualSourceDistance(mesh, std::array<std::size_t, 1>{0}, VirtualSourceParams{1});
    EXPECT_EQ(capped.Status, VirtualSourceStatus::ExpansionLimit);
    EXPECT_TRUE(capped.Distances.empty());
    auto positions = std::vector<glm::vec3>(mesh.Positions().begin(), mesh.Positions().end());
    positions[0].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(ComputeVirtualSourceDistance(mesh, positions, std::array<std::size_t, 1>{0}).Status,
              VirtualSourceStatus::InvalidPositions);
    mesh.Position(Geometry::VertexHandle{0}) = mesh.Position(Geometry::VertexHandle{1});
    EXPECT_EQ(ComputeVirtualSourceDistance(mesh, std::array<std::size_t, 1>{0}).Status,
              VirtualSourceStatus::DegenerateFace);
}

TEST(GeodesicVirtualSource, TypedPositionsAndDeletedSlotsPreserveInput)
{
    auto mesh = Grid(2);
    auto deleted = mesh.AddVertex({7, 7, 7});
    mesh.DeleteVertex(deleted);
    auto positions = std::vector<glm::vec3>(mesh.Positions().begin(), mesh.Positions().end());
    for (auto& p : positions)
        p *= 2.f;
    auto result = Geometry::Geodesic::ComputeVirtualSourceDistance(mesh, positions,
                                                                   std::array<std::size_t, 1>{0});
    ASSERT_TRUE(result.Succeeded());
    EXPECT_TRUE(std::isinf(result.Distances[deleted.Index]));
    EXPECT_NEAR(result.Distances[8], std::sqrt(32.), 1e-6);
    EXPECT_EQ(mesh.Position(Geometry::VertexHandle{8}), glm::vec3(2, 2, 0));
}

TEST(GeodesicVirtualSource, ConcaveBoundaryRoutesAroundMissingSurface)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    for (unsigned y = 0; y <= 4; ++y)
        for (unsigned x = 0; x <= 4; ++x)
            (void)mesh.AddVertex({float(x), float(y), 0});
    for (unsigned y = 0; y < 4; ++y)
        for (unsigned x = 0; x < 4; ++x)
        {
            if (x > 0 && y > 0)
                continue;
            auto a = Geometry::VertexHandle{y * 5 + x}, b = Geometry::VertexHandle{a.Index + 1};
            auto c = Geometry::VertexHandle{a.Index + 5}, d = Geometry::VertexHandle{c.Index + 1};
            ASSERT_TRUE(mesh.AddTriangle(a, b, d));
            ASSERT_TRUE(mesh.AddTriangle(a, d, c));
        }
    const auto result =
        Geometry::Geodesic::ComputeVirtualSourceDistance(mesh, std::array<std::size_t, 1>{4});
    ASSERT_TRUE(result.Succeeded());
    // The straight segment crosses the missing upper-right square. The
    // surface path bends at (1,1), with analytic length 2*sqrt(10).
    EXPECT_NEAR(result.Distances[20], 2 * std::sqrt(10.), .1);
    EXPECT_GT(result.Distances[20], std::sqrt(32.) + .5);
}

TEST(GeodesicVirtualSource, RejectsPolygonFacesAndSubmeshViews)
{
    auto mesh = Grid(2);
    const auto view = Geometry::HalfedgeMesh::Mesh::CreateView(
        mesh, {0, mesh.VerticesSize()}, {0, mesh.EdgesSize()}, {0, mesh.FacesSize()});
    EXPECT_EQ(Geometry::Geodesic::ComputeVirtualSourceDistance(view, std::array<std::size_t, 1>{0})
                  .Status,
              Geometry::Geodesic::VirtualSourceStatus::UnsupportedSubmeshView);
    Geometry::HalfedgeMesh::Mesh quad;
    auto a = quad.AddVertex({0, 0, 0}), b = quad.AddVertex({1, 0, 0});
    auto c = quad.AddVertex({1, 1, 0}), d = quad.AddVertex({0, 1, 0});
    ASSERT_TRUE(quad.AddQuad(a, b, c, d));
    EXPECT_EQ(Geometry::Geodesic::ComputeVirtualSourceDistance(quad, std::array<std::size_t, 1>{0})
                  .Status,
              Geometry::Geodesic::VirtualSourceStatus::NonTriangleFace);
}

TEST(GeodesicVirtualSource, ClosedOctahedronUsesUnfoldedSurfaceDistance)
{
    Geometry::HalfedgeMesh::Mesh mesh;
    for (auto p : std::array{glm::vec3{1,0,0},glm::vec3{-1,0,0},glm::vec3{0,1,0},
                            glm::vec3{0,-1,0},glm::vec3{0,0,1},glm::vec3{0,0,-1}})
        (void)mesh.AddVertex(p);
    for (auto tri : std::array{std::array{4u,0u,2u},std::array{4u,2u,1u},std::array{4u,1u,3u},std::array{4u,3u,0u},
                              std::array{5u,2u,0u},std::array{5u,1u,2u},std::array{5u,3u,1u},std::array{5u,0u,3u}})
        ASSERT_TRUE(mesh.AddTriangle(Geometry::VertexHandle{tri[0]},Geometry::VertexHandle{tri[1]},Geometry::VertexHandle{tri[2]}));
    const auto result=Geometry::Geodesic::ComputeVirtualSourceDistance(mesh,std::array<std::size_t,1>{4});
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(result.UnreachableVertexCount,0);
    EXPECT_NEAR(result.Distances[5],std::sqrt(6.),1e-6);
}
