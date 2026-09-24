#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <map>
#include <numbers>
#include <set>
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
    auto result = C::Extract(mesh, Parameters());
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
    auto result = C::Extract(mesh, Parameters());
    ASSERT_TRUE(result.Succeeded());
    EXPECT_TRUE(result.Segments.empty());
    EXPECT_TRUE(result.Curves.empty());
}
TEST(ScalarfieldExtrema, OrientationReversalExchangesRidgeValley)
{
    auto a = Grid(), b = Grid(40, false, false, true);
    auto p = Parameters();
    auto ra = C::Extract(a, p), rb = C::Extract(b, p);
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
    auto r = C::Extract(mesh, Parameters());
    ASSERT_TRUE(r.Succeeded());
    CheckCenter(Midpoints(r, C::Kind::PrincipalValley));
    CheckCenter(Midpoints(r, C::Kind::MeanValley));
}
TEST(ScalarfieldExtrema, RefinementRetainsCenterCurve)
{
    auto mesh = Grid(60, false, false);
    auto r = C::Extract(mesh, Parameters());
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
    auto r = C::Extract(mesh, Parameters());
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
    EXPECT_EQ(C::Extract(empty).Diagnostic.State, C::Status::EmptyMesh);
    auto mesh = Grid(20);
    auto params = Parameters();
    params.MaximumWorkItems = 5;
    auto r = C::Extract(mesh, params);
    EXPECT_EQ(r.Diagnostic.State, C::Status::WorkLimit);
    EXPECT_TRUE(r.Points.empty());
    EXPECT_TRUE(r.Segments.empty());
    EXPECT_TRUE(r.Curves.empty());
    params = Parameters();
    params.ScaleFactors = {1, 1, 2};
    EXPECT_EQ(C::Extract(mesh, params).Diagnostic.State, C::Status::InvalidParameters);
    mesh.Position(Geometry::VertexHandle{0}) = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
    EXPECT_EQ(C::Extract(mesh).Diagnostic.State, C::Status::InvalidGeometry);
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
    auto r = C::Extract(mesh, Parameters());
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
    auto r = C::Extract(mesh, Parameters());
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
    auto r = C::Extract(mesh, Parameters());
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
    auto r = C::Extract(mesh, Parameters());
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
    auto r = C::ExtractScalarExtrema(mesh, "v:field", ScalarParameters());
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
    auto r = C::ExtractScalarExtrema(mesh, "v:field", ScalarParameters());
    ASSERT_TRUE(r.Succeeded());
    CheckCenter(Midpoints(r, C::Kind::ScalarValley));
    EXPECT_TRUE(Midpoints(r, C::Kind::ScalarRidge).empty());
    // Heights are range-normalized: an affine rescale of the field is invisible.
    auto scaled = C::ExtractScalarExtrema(mesh, "v:scaled", ScalarParameters());
    ASSERT_TRUE(scaled.Succeeded());
    EXPECT_EQ(scaled.Segments.size(), r.Segments.size());
}

TEST(ScalarfieldExtrema, ScalarMeanCurvaturePropertyHasCenterExtremum)
{
    auto mesh = Grid();
    ASSERT_TRUE(Geometry::Curvature::ComputeMeanCurvature(mesh).has_value());
    auto r = C::ExtractScalarExtrema(mesh, "v:mean_curvature", ScalarParameters());
    ASSERT_TRUE(r.Succeeded()) << C::ToString(r.Diagnostic.State);
    // The bump crest is the mean-curvature extremum; the sign convention of H
    // decides whether it is a ridge or a valley of the field.
    EXPECT_TRUE(CoversCenter(r, C::Kind::ScalarRidge) || CoversCenter(r, C::Kind::ScalarValley));
}

TEST(ScalarfieldExtrema, ScalarRejectsMissingOrNonScalarPropertyAndIgnoresConstantField)
{
    auto mesh = Grid(16, true);
    EXPECT_EQ(C::ExtractScalarExtrema(mesh, "v:absent", ScalarParameters()).Diagnostic.State,
              C::Status::MissingProperty);
    EXPECT_EQ(C::ExtractScalarExtrema(mesh, "v:point", ScalarParameters()).Diagnostic.State,
              C::Status::MissingProperty);
    PublishBump<double>(mesh, "v:field", 1.0);
    auto invalid = ScalarParameters();
    invalid.RadiusRatio = 0;
    EXPECT_EQ(C::ExtractScalarExtrema(mesh, "v:field", invalid).Diagnostic.State,
              C::Status::InvalidParameters);

    PublishBump<double>(mesh, "v:constant", 0.0, 4.0);
    auto flat = C::ExtractScalarExtrema(mesh, "v:constant", ScalarParameters());
    ASSERT_TRUE(flat.Succeeded());
    EXPECT_TRUE(flat.Segments.empty());
    EXPECT_EQ(flat.Diagnostic.Scales[1].SupportedVertices, 0u);

    // Non-finite samples drop their vertex instead of poisoning the range.
    auto field = mesh.VertexProperties().Get<double>("v:field");
    field[0] = std::numeric_limits<double>::quiet_NaN();
    auto r = C::ExtractScalarExtrema(mesh, "v:field", ScalarParameters());
    ASSERT_TRUE(r.Succeeded());
    EXPECT_LT(r.Diagnostic.Scales[1].SupportedVertices, mesh.VertexCount());
}
