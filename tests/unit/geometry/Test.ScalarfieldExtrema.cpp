#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <vector>
import Geometry.HalfedgeMesh;
import Geometry.Properties;
import Geometry.HalfedgeMesh.ScalarfieldExtrema;
import Geometry.Curvature;
namespace
{
    namespace C = Geometry::ScalarfieldExtrema;
    using Mesh = Geometry::HalfedgeMesh::Mesh;
    Mesh Grid(int n = 40, bool flat = false, bool alternate = false, bool reverse = false,
              double scale = 1, glm::vec3 offset = {})
    {
        Mesh mesh;
        std::vector<Geometry::VertexHandle> vertices;
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i)
            {
                double x = -1 + 2.0 * i / n, y = -1 + 2.0 * j / n;
                double z = flat ? 0 : 0.1 * std::exp(-x * x / (2 * 0.2 * 0.2));
                vertices.push_back(mesh.AddVertex(offset + float(scale) * glm::vec3{x, y, z}));
            }
        auto add = [&](auto a, auto b, auto c)
        {
            if (reverse)
                std::swap(b, c);
            EXPECT_TRUE(mesh.AddTriangle(a, b, c));
        };
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i)
            {
                auto a = vertices[j * (n + 1) + i], b = vertices[j * (n + 1) + i + 1],
                     c = vertices[(j + 1) * (n + 1) + i + 1], d = vertices[(j + 1) * (n + 1) + i];
                if (alternate)
                {
                    add(a, b, d);
                    add(b, c, d);
                }
                else
                {
                    add(a, b, c);
                    add(a, c, d);
                }
            }
        return mesh;
    }
    C::Params Parameters()
    {
        C::Params p;
        p.RadiusRatio = 0.08;
        return p;
    }
    std::vector<glm::vec3> Midpoints(const C::Result& r, C::Kind kind, int scale = 1)
    {
        std::vector<glm::vec3> out;
        for (const auto& s : r.Segments)
            if (s.Signal == kind && s.Scale == scale)
                out.push_back((r.Points[s.PointA].Position + r.Points[s.PointB].Position) / 2.0f);
        return out;
    }
    void CheckCenter(const std::vector<glm::vec3>& points)
    {
        ASSERT_GT(points.size(), 10u);
        float low = 1, high = -1;
        for (auto p : points)
        {
            EXPECT_LT(std::abs(p.x), 0.04f);
            low = std::min(low, p.y);
            high = std::max(high, p.y);
        }
        EXPECT_LT(low, -0.6f);
        EXPECT_GT(high, 0.6f);
    }
} // namespace
TEST(ScalarfieldExtrema, GaussianExtrusionFindsGeometricTransverseExtremum)
{
    auto mesh = Grid();
    auto result = C::ExtractCurvatureExtrema(mesh, Parameters());
    ASSERT_TRUE(result.Succeeded());
    CheckCenter(Midpoints(result, C::Kind::PrincipalValley));
    CheckCenter(Midpoints(result, C::Kind::MeanValley));
    for (const auto& point : result.Points)
    {
        ASSERT_LE(point.VertexA, point.VertexB);
        ASSERT_GE(point.Fraction, 0);
        ASSERT_LE(point.Fraction, 1);
        auto a = mesh.Position(Geometry::VertexHandle{point.VertexA}),
             b = mesh.Position(Geometry::VertexHandle{point.VertexB});
        EXPECT_LT(glm::length(glm::dvec3{point.Position} - ((1 - point.Fraction) * glm::dvec3{a} +
                                                            point.Fraction * glm::dvec3{b})),
                  1e-6);
    }
    for (const auto& segment : result.Segments)
    {
        EXPECT_NE(segment.PointA, segment.PointB);
        EXPECT_GE(segment.Confidence, 0);
        EXPECT_LE(segment.Confidence, 1);
        EXPECT_NE(segment.PersistentScaleMask & (1u << segment.Scale), 0);
    }
}
TEST(ScalarfieldExtrema, PlaneHasNoArtificialCurves)
{
    auto mesh = Grid(32, true);
    auto result = C::ExtractCurvatureExtrema(mesh, Parameters());
    ASSERT_TRUE(result.Succeeded());
    EXPECT_TRUE(result.Segments.empty());
    EXPECT_TRUE(result.Curves.empty());
}
TEST(ScalarfieldExtrema, OrientationReversalExchangesRidgeValley)
{
    auto a = Grid(), b = Grid(40, false, false, true);
    auto p = Parameters();
    auto ra = C::ExtractCurvatureExtrema(a, p), rb = C::ExtractCurvatureExtrema(b, p);
    ASSERT_TRUE(ra.Succeeded());
    ASSERT_TRUE(rb.Succeeded());
    auto pa = Midpoints(ra, C::Kind::PrincipalValley), pb = Midpoints(rb, C::Kind::PrincipalRidge);
    ASSERT_EQ(pa.size(), pb.size());
    for (auto x : pa)
    {
        double best = std::numeric_limits<double>::infinity();
        for (auto y : pb)
            best = std::min(best, double(glm::length(x - y)));
        EXPECT_LT(best, 1e-5);
    }
    CheckCenter(Midpoints(rb, C::Kind::MeanRidge));
}
TEST(ScalarfieldExtrema, RetriangulationRetainsCenterCurve)
{
    auto mesh = Grid(40, false, true);
    auto r = C::ExtractCurvatureExtrema(mesh, Parameters());
    ASSERT_TRUE(r.Succeeded());
    CheckCenter(Midpoints(r, C::Kind::PrincipalValley));
    CheckCenter(Midpoints(r, C::Kind::MeanValley));
}
TEST(ScalarfieldExtrema, RefinementRetainsCenterCurve)
{
    auto mesh = Grid(60, false, false);
    auto r = C::ExtractCurvatureExtrema(mesh, Parameters());
    ASSERT_TRUE(r.Succeeded());
    CheckCenter(Midpoints(r, C::Kind::PrincipalValley));
    CheckCenter(Midpoints(r, C::Kind::MeanValley));
}
TEST(ScalarfieldExtrema, ScaleTranslationAndSourcePreservation)
{
    auto mesh = Grid(32, false, false, false, 5, {10, -4, 3});
    auto positions = mesh.VertexProperties().Get<glm::vec3>("v:point").Vector();
    auto custom = mesh.FaceProperties().GetOrAdd<int>("f:user", 17);
    auto faces = mesh.FaceCount();
    auto edges = mesh.EdgeCount();
    auto r = C::ExtractCurvatureExtrema(mesh, Parameters());
    ASSERT_TRUE(r.Succeeded());
    auto centers = Midpoints(r, C::Kind::PrincipalValley);
    for (auto& center : centers)
        center = (center - glm::vec3{10, -4, 3}) / 5.0f;
    CheckCenter(centers);
    EXPECT_EQ(mesh.FaceCount(), faces);
    EXPECT_EQ(mesh.EdgeCount(), edges);
    EXPECT_EQ(positions, mesh.VertexProperties().Get<glm::vec3>("v:point").Vector());
    EXPECT_TRUE(
        std::all_of(custom.Vector().begin(), custom.Vector().end(), [](int x) { return x == 17; }));
}
TEST(ScalarfieldExtrema, InvalidInputsAndWorkLimitHaveNoPartialCurves)
{
    Mesh empty;
    EXPECT_EQ(C::ExtractCurvatureExtrema(empty).Diagnostic.State, C::Status::EmptyMesh);
    auto mesh = Grid(20);
    auto params = Parameters();
    params.MaximumWorkItems = 5;
    auto r = C::ExtractCurvatureExtrema(mesh, params);
    EXPECT_EQ(r.Diagnostic.State, C::Status::WorkLimit);
    EXPECT_TRUE(r.Points.empty());
    EXPECT_TRUE(r.Segments.empty());
    EXPECT_TRUE(r.Curves.empty());
    params = Parameters();
    params.ScaleFactors = {1, 1, 2};
    EXPECT_EQ(C::ExtractCurvatureExtrema(mesh, params).Diagnostic.State, C::Status::InvalidParameters);
    mesh.Position(Geometry::VertexHandle{0}) = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
    EXPECT_EQ(C::ExtractCurvatureExtrema(mesh).Diagnostic.State, C::Status::InvalidGeometry);
}

namespace
{
    Mesh Cylinder()
    {
        Mesh mesh;
        std::vector<Geometry::VertexHandle> vertices;
        constexpr int around = 40, rows = 16;
        for (int j = 0; j <= rows; ++j)
            for (int i = 0; i < around; ++i)
            {
                double angle = 2 * std::numbers::pi * i / around;
                vertices.push_back(
                    mesh.AddVertex({std::cos(angle), 1 - 2.0 * j / rows, std::sin(angle)}));
            }
        for (int j = 0; j < rows; ++j)
            for (int i = 0; i < around; ++i)
            {
                auto a = vertices[j * around + i], b = vertices[j * around + (i + 1) % around],
                     c = vertices[(j + 1) * around + (i + 1) % around],
                     d = vertices[(j + 1) * around + i];
                EXPECT_TRUE(mesh.AddTriangle(a, b, c));
                EXPECT_TRUE(mesh.AddTriangle(a, c, d));
            }
        return mesh;
    }
    Mesh Sphere()
    {
        Mesh mesh;
        constexpr int around = 40, rows = 20;
        auto top = mesh.AddVertex({0, 1, 0});
        std::vector<Geometry::VertexHandle> vertices;
        for (int j = 1; j < rows; ++j)
            for (int i = 0; i < around; ++i)
            {
                double theta = std::numbers::pi * j / rows, phi = 2 * std::numbers::pi * i / around;
                vertices.push_back(mesh.AddVertex({std::sin(theta) * std::cos(phi), std::cos(theta),
                                                   std::sin(theta) * std::sin(phi)}));
            }
        auto bottom = mesh.AddVertex({0, -1, 0});
        for (int i = 0; i < around; ++i)
        {
            EXPECT_TRUE(mesh.AddTriangle(top, vertices[(i + 1) % around], vertices[i]));
            auto a = vertices[(rows - 2) * around + i],
                 b = vertices[(rows - 2) * around + (i + 1) % around];
            EXPECT_TRUE(mesh.AddTriangle(bottom, a, b));
        }
        for (int j = 0; j < rows - 2; ++j)
            for (int i = 0; i < around; ++i)
            {
                auto a = vertices[j * around + i], b = vertices[j * around + (i + 1) % around],
                     c = vertices[(j + 1) * around + (i + 1) % around],
                     d = vertices[(j + 1) * around + i];
                EXPECT_TRUE(mesh.AddTriangle(a, b, c));
                EXPECT_TRUE(mesh.AddTriangle(a, c, d));
            }
        return mesh;
    }
} // namespace
TEST(ScalarfieldExtrema, ConstantCurvatureCylinderHasNoInteriorCreases)
{
    auto mesh = Cylinder();
    auto r = C::ExtractCurvatureExtrema(mesh, Parameters());
    ASSERT_TRUE(r.Succeeded());
    for (const auto& segment : r.Segments)
    {
        auto p = (r.Points[segment.PointA].Position + r.Points[segment.PointB].Position) / 2.0f;
        EXPECT_GT(std::abs(p.y), 0.6f) << C::ToString(segment.Signal);
    }
}
TEST(ScalarfieldExtrema, SphereRejectsUmbilicPrincipalDirections)
{
    auto mesh = Sphere();
    auto r = C::ExtractCurvatureExtrema(mesh, Parameters());
    ASSERT_TRUE(r.Succeeded());
    EXPECT_TRUE(r.Segments.empty()) << r.Segments.size() << " false constant-curvature segments";
}
TEST(ScalarfieldExtrema, NoisyExtrusionRetainsDominantCenter)
{
    auto mesh = Grid();
    unsigned seed = 941;
    for (auto v : mesh.LiveVertices())
    {
        seed = 1664525 * seed + 1013904223;
        mesh.Position(v).z += 0.0005f * (2.0f * float(seed & 65535) / 65535 - 1);
    }
    auto r = C::ExtractCurvatureExtrema(mesh, Parameters());
    ASSERT_TRUE(r.Succeeded());
    auto points = Midpoints(r, C::Kind::PrincipalValley);
    std::erase_if(points, [](auto p) { return std::abs(p.y) > 0.7f; });
    CheckCenter(points);
}

TEST(ScalarfieldExtrema, SharpFoldDoesNotCreateParallelSmoothCurves)
{
    auto mesh = Grid(32, true);
    for (auto v : mesh.LiveVertices())
        if (mesh.Position(v).x > 0)
        {
            float x = mesh.Position(v).x;
            mesh.Position(v).x = 0.5f * x;
            mesh.Position(v).z = std::sqrt(0.75f) * x;
        }
    auto r = C::ExtractCurvatureExtrema(mesh, Parameters());
    ASSERT_TRUE(r.Succeeded());
    ASSERT_EQ(r.Segments.size(), 32u);
    for (const auto& s : r.Segments)
        EXPECT_EQ(s.Signal, C::Kind::SharpEdge);
    EXPECT_EQ(r.Curves.size(), 1u);
    EXPECT_EQ(r.Curves[0].Endpoints, 2u);
}

namespace
{
    C::Params ScalarParameters()
    {
        C::Params p = C::kScalarDefaults;
        p.RadiusRatio = 0.08;
        return p;
    }
    // Publishes f(x) = amplitude * exp(-x^2 / (2 * 0.2^2)) on a flat grid: a
    // straight scalar ridge (valley for negative amplitude) along x = 0.
    template <class T>
    void PublishBump(Mesh& mesh, const std::string& name, double amplitude, double offset = 0)
    {
        auto property = mesh.VertexProperties().GetOrAdd<T>(name, T{});
        for (auto v : mesh.LiveVertices())
        {
            const double x = mesh.Position(v).x;
            property[v.Index] = static_cast<T>(offset + amplitude * std::exp(-x * x / 0.08));
        }
    }
    // True when midpoints of `kind` near x = 0 span most of the grid in y.
    bool CoversCenter(const C::Result& r, C::Kind kind)
    {
        float low = 1, high = -1;
        for (auto p : Midpoints(r, kind))
            if (std::abs(p.x) < 0.04f)
            {
                low = std::min(low, p.y);
                high = std::max(high, p.y);
            }
        return low < -0.6f && high > 0.6f;
    }
}

TEST(ScalarfieldExtrema, ScalarBumpHasCenterRidgeAndNoValley)
{
    auto mesh = Grid(40, true);
    PublishBump<double>(mesh, "v:field", 3.0, -7.0);
    auto r = C::Extract(mesh, "v:field", ScalarParameters());
    ASSERT_TRUE(r.Succeeded()) << C::ToString(r.Diagnostic.State);
    CheckCenter(Midpoints(r, C::Kind::ScalarRidge));
    EXPECT_TRUE(Midpoints(r, C::Kind::ScalarValley).empty());
    for (const auto& s : r.Segments)
    {
        EXPECT_TRUE(s.Signal == C::Kind::ScalarRidge || s.Signal == C::Kind::ScalarValley);
        EXPECT_GE(s.Strength, 0);
        EXPECT_LE(s.Strength, 1);
    }
    EXPECT_GT(r.Diagnostic.Scales[1].SegmentCounts[static_cast<unsigned>(C::Kind::ScalarRidge)],
              0u);
}

TEST(ScalarfieldExtrema, ScalarNegatedFloatFieldIsValleyAndScaleInvariant)
{
    auto mesh = Grid(40, true);
    PublishBump<float>(mesh, "v:field", -1.0);
    PublishBump<double>(mesh, "v:scaled", -250.0, 12.0);
    auto r = C::Extract(mesh, "v:field", ScalarParameters());
    ASSERT_TRUE(r.Succeeded());
    CheckCenter(Midpoints(r, C::Kind::ScalarValley));
    EXPECT_TRUE(Midpoints(r, C::Kind::ScalarRidge).empty());
    // Heights are range-normalized: an affine rescale of the field is invisible.
    auto scaled = C::Extract(mesh, "v:scaled", ScalarParameters());
    ASSERT_TRUE(scaled.Succeeded());
    EXPECT_EQ(scaled.Segments.size(), r.Segments.size());
}

TEST(ScalarfieldExtrema, ScalarMeanCurvaturePropertyHasCenterExtremum)
{
    auto mesh = Grid();
    ASSERT_TRUE(Geometry::Curvature::ComputeMeanCurvature(mesh).has_value());
    auto r = C::Extract(mesh, "v:mean_curvature", ScalarParameters());
    ASSERT_TRUE(r.Succeeded()) << C::ToString(r.Diagnostic.State);
    // The bump crest is the mean-curvature extremum; the sign convention of H
    // decides whether it is a ridge or a valley of the field.
    EXPECT_TRUE(CoversCenter(r, C::Kind::ScalarRidge) || CoversCenter(r, C::Kind::ScalarValley));
}

TEST(ScalarfieldExtrema, ScalarRejectsMissingOrNonScalarPropertyAndIgnoresConstantField)
{
    auto mesh = Grid(16, true);
    EXPECT_EQ(C::Extract(mesh, "v:absent", ScalarParameters()).Diagnostic.State,
              C::Status::MissingProperty);
    EXPECT_EQ(C::Extract(mesh, "v:point", ScalarParameters()).Diagnostic.State,
              C::Status::MissingProperty);
    PublishBump<double>(mesh, "v:field", 1.0);
    auto invalid = ScalarParameters();
    invalid.RadiusRatio = 0;
    EXPECT_EQ(C::Extract(mesh, "v:field", invalid).Diagnostic.State,
              C::Status::InvalidParameters);

    PublishBump<double>(mesh, "v:constant", 0.0, 4.0);
    auto flat = C::Extract(mesh, "v:constant", ScalarParameters());
    ASSERT_TRUE(flat.Succeeded());
    EXPECT_TRUE(flat.Segments.empty());
    EXPECT_EQ(flat.Diagnostic.Scales[1].SupportedVertices, 0u);

    // Non-finite samples drop their vertex instead of poisoning the range.
    auto field = mesh.VertexProperties().Get<double>("v:field");
    field[0] = std::numeric_limits<double>::quiet_NaN();
    auto r = C::Extract(mesh, "v:field", ScalarParameters());
    ASSERT_TRUE(r.Succeeded());
    EXPECT_LT(r.Diagnostic.Scales[1].SupportedVertices, mesh.VertexCount());
}

namespace
{
    // Two radial pits at (+-0.5, 0) with an optional shallow third pit far from
    // both; the gradient-flow separatrix between the main pits is x = 0.
    void PublishPits(Mesh& mesh, double shallow)
    {
        auto property = mesh.VertexProperties().GetOrAdd<double>("v:pits", 0.0);
        auto pit = [](glm::dvec2 p, glm::dvec2 c, double sigma2)
        { return std::exp(-glm::dot(p - c, p - c) / (2 * sigma2)); };
        for (auto v : mesh.LiveVertices())
        {
            const glm::dvec2 p{mesh.Position(v).x, mesh.Position(v).y};
            property[v.Index] = -pit(p, {0.5, 0}, 0.025) - pit(p, {-0.5, 0}, 0.025) -
                                shallow * pit(p, {0.5, -0.75}, 0.005);
        }
    }
    C::Params WatershedParameters(double persistence = 0.05)
    {
        C::Params p = C::kScalarDefaults;
        p.Algorithm = C::Method::Watershed;
        p.MinimumPersistence = persistence;
        return p;
    }
}

TEST(ScalarfieldExtrema, WatershedRidgeSeparatesTwoPitsOnMeshEdges)
{
    auto mesh = Grid(40, true);
    PublishPits(mesh, 0.0);
    auto r = C::Extract(mesh, "v:pits", WatershedParameters());
    ASSERT_TRUE(r.Succeeded()) << C::ToString(r.Diagnostic.State);
    EXPECT_EQ(r.Diagnostic.DescendingBasins, 2u);
    ASSERT_EQ(r.DescendingBasin.size(), mesh.VerticesSize());
    ASSERT_EQ(r.AscendingBasin.size(), mesh.VerticesSize());
    // The two pit centers drain into different basins.
    auto nearest = [&](glm::vec3 target)
    {
        std::uint32_t best = 0;
        for (auto v : mesh.LiveVertices())
            if (glm::length(mesh.Position(v) - target) <
                glm::length(mesh.Position(Geometry::VertexHandle{best}) - target))
                best = v.Index;
        return best;
    };
    EXPECT_NE(r.DescendingBasin[nearest({0.5f, 0, 0})], r.DescendingBasin[nearest({-0.5f, 0, 0})]);

    std::vector<std::uint32_t> ridges;
    float low = 1, high = -1;
    for (std::uint32_t i = 0; i < r.Segments.size(); ++i)
    {
        const auto& s = r.Segments[i];
        EXPECT_EQ(s.Scale, C::kScaleFree);
        EXPECT_GE(s.Strength, 0);
        EXPECT_LE(s.Strength, 1);
        // Watershed curves run through source vertices only.
        EXPECT_EQ(r.Points[s.PointA].VertexA, r.Points[s.PointA].VertexB);
        EXPECT_EQ(r.Points[s.PointB].VertexA, r.Points[s.PointB].VertexB);
        if (s.Signal != C::Kind::ScalarRidge)
            continue;
        ridges.push_back(i);
        for (auto point : {s.PointA, s.PointB})
        {
            const auto p = r.Points[point].Position;
            EXPECT_LE(std::abs(p.x), 0.051f);
            low = std::min(low, p.y);
            high = std::max(high, p.y);
        }
    }
    EXPECT_LT(low, -0.9f);
    EXPECT_GT(high, 0.9f);

    const auto features = C::SnapToMesh(mesh, r, ridges);
    ASSERT_EQ(features.Vertices.size(), mesh.VerticesSize());
    ASSERT_EQ(features.Edges.size(), mesh.EdgesSize());
    EXPECT_EQ(features.EdgeCount, ridges.size());
    for (auto e : mesh.LiveEdges())
        if (features.Edges[e.Index])
        {
            auto h = mesh.Halfedge(e, 0);
            EXPECT_TRUE(features.Vertices[mesh.FromVertex(h).Index]);
            EXPECT_TRUE(features.Vertices[mesh.ToVertex(h).Index]);
        }
}

TEST(ScalarfieldExtrema, WatershedPersistenceMergesShallowBasins)
{
    auto mesh = Grid(40, true);
    PublishPits(mesh, 0.03);
    auto merged = C::Extract(mesh, "v:pits", WatershedParameters(0.05));
    ASSERT_TRUE(merged.Succeeded());
    EXPECT_EQ(merged.Diagnostic.DescendingBasins, 2u);
    EXPECT_GE(merged.Diagnostic.Minima, 3u);
    auto kept = C::Extract(mesh, "v:pits", WatershedParameters(0.01));
    ASSERT_TRUE(kept.Succeeded());
    EXPECT_EQ(kept.Diagnostic.DescendingBasins, 3u);
    EXPECT_GT(kept.Segments.size(), merged.Segments.size());
    // Deterministic: an identical call reproduces labels and curves exactly.
    auto again = C::Extract(mesh, "v:pits", WatershedParameters(0.01));
    EXPECT_EQ(again.DescendingBasin, kept.DescendingBasin);
    EXPECT_EQ(again.Segments.size(), kept.Segments.size());
}

TEST(ScalarfieldExtrema, HessianCurvesSnapToConnectedMeshFeatures)
{
    auto mesh = Grid(40, true);
    PublishBump<double>(mesh, "v:field", 1.0);
    const std::vector<double> values = mesh.VertexProperties().Get<double>("v:field").Vector();
    auto r = C::Extract(mesh, std::span<const double>{values}, ScalarParameters());
    ASSERT_TRUE(r.Succeeded());
    std::vector<std::uint32_t> selected;
    for (std::uint32_t i = 0; i < r.Segments.size(); ++i)
        if (r.Segments[i].Signal == C::Kind::ScalarRidge && r.Segments[i].Scale == 1)
            selected.push_back(i);
    ASSERT_FALSE(selected.empty());
    const auto features = C::SnapToMesh(mesh, r, selected);
    EXPECT_GT(features.VertexCount, 20u);
    EXPECT_GT(features.EdgeCount, 20u);
    for (auto v : mesh.LiveVertices())
        if (features.Vertices[v.Index])
            EXPECT_LE(std::abs(mesh.Position(v).x), 0.051f);
    // Out-of-range segment indices are ignored rather than trusted.
    const std::uint32_t bogus[] = {static_cast<std::uint32_t>(r.Segments.size())};
    EXPECT_EQ(C::SnapToMesh(mesh, r, bogus).VertexCount, 0u);
}

TEST(ScalarfieldExtrema, FieldEntryPointsRejectMismatchedInputsAndParameters)
{
    auto mesh = Grid(16, true);
    const std::vector<double> shortField(mesh.VerticesSize() - 1, 0.0);
    EXPECT_EQ(C::Extract(mesh, std::span<const double>{shortField}).Diagnostic.State,
              C::Status::MissingProperty);
    PublishBump<float>(mesh, "v:field", 1.0);
    EXPECT_EQ(C::Extract(mesh, "v:field", WatershedParameters(1.5)).Diagnostic.State,
              C::Status::InvalidParameters);
    auto curvature = Parameters();
    curvature.Algorithm = C::Method::Watershed;
    EXPECT_EQ(C::ExtractCurvatureExtrema(mesh, curvature).Diagnostic.State,
              C::Status::InvalidParameters);
    // A constant field succeeds with no curves or basins.
    PublishBump<double>(mesh, "v:constant", 0.0, 2.0);
    auto flat = C::Extract(mesh, "v:constant", WatershedParameters());
    ASSERT_TRUE(flat.Succeeded());
    EXPECT_TRUE(flat.Segments.empty());
    EXPECT_TRUE(flat.DescendingBasin.empty());
}
