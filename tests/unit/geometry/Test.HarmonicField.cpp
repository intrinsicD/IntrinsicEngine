#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>
#include <glm/glm.hpp>
#include <gtest/gtest.h>
import Geometry.HarmonicField;
import Geometry.HalfedgeMesh;
import Geometry.DEC;
import Geometry.Properties;
namespace H = Geometry::HarmonicField;
using Edge = Geometry::Smoothing::PropertyEdge;
namespace
{
    std::vector<Edge> Path(std::size_t n, double w = 1)
    {
        std::vector<Edge> edges;
        for (std::size_t i = 0; i + 1 < n; ++i) edges.push_back({i, i + 1, w});
        return edges;
    }

    // Independent dense oracle: minimizes x^T A x + sum w_s |x_s - t_s|^2 - 2 b^T x with hard rows
    // by Gauss-Jordan elimination on the stationarity equations of the free rows.
    std::vector<double> DenseOracle(const std::vector<double>& values, std::size_t channels, std::size_t n,
        const std::vector<Edge>& edges, H::FieldOrder order, const std::vector<std::size_t>& hard,
        const std::vector<H::SoftConstraint>& soft, const std::vector<double>& mass,
        const std::vector<double>& source = {})
    {
        std::vector<std::vector<double>> L(n, std::vector<double>(n, 0.0)), A;
        for (const auto& e : edges)
        {
            L[e.A][e.A] += e.Weight; L[e.B][e.B] += e.Weight;
            L[e.A][e.B] -= e.Weight; L[e.B][e.A] -= e.Weight;
        }
        A = L;
        for (int power = 1; power < int(order); ++power)
        {
            auto next = A;
            for (std::size_t i = 0; i < n; ++i)
                for (std::size_t j = 0; j < n; ++j)
                {
                    double sum = 0;
                    for (std::size_t k = 0; k < n; ++k) sum += A[i][k] * L[k][j] / (mass.empty() ? 1.0 : mass[k]);
                    next[i][j] = sum;
                }
            A = next;
        }
        std::vector<bool> isHard(n, false);
        for (auto h : hard) isHard[h] = true;
        std::vector<double> w(n, 0.0);
        for (auto s : soft) w[s.Row] = s.Weight;
        std::vector<std::size_t> freeRows;
        for (std::size_t i = 0; i < n; ++i) if (!isHard[i]) freeRows.push_back(i);
        std::vector<double> out = values;
        for (std::size_t c = 0; c < channels; ++c)
        {
            const auto m = freeRows.size();
            std::vector<std::vector<double>> M(m, std::vector<double>(m + 1, 0.0));
            for (std::size_t a = 0; a < m; ++a)
            {
                const auto i = freeRows[a];
                for (std::size_t b = 0; b < m; ++b) M[a][b] = A[i][freeRows[b]];
                M[a][a] += w[i];
                double rhs = w[i] * values[i * channels + c] + (source.empty() ? 0.0 : source[i * channels + c]);
                for (auto h : hard) rhs -= A[i][h] * values[h * channels + c];
                M[a][m] = rhs;
            }
            for (std::size_t p = 0; p < m; ++p)
            {
                std::size_t best = p;
                for (std::size_t r = p + 1; r < m; ++r) if (std::abs(M[r][p]) > std::abs(M[best][p])) best = r;
                std::swap(M[p], M[best]);
                for (std::size_t r = 0; r < m; ++r)
                {
                    if (r == p) continue;
                    const double f = M[r][p] / M[p][p];
                    for (std::size_t q = p; q <= m; ++q) M[r][q] -= f * M[p][q];
                }
            }
            for (std::size_t a = 0; a < m; ++a) out[freeRows[a] * channels + c] = M[a][m] / M[a][a];
        }
        return out;
    }
}

TEST(HarmonicField, HarmonicInterpolationIsLinearOnAPathForEveryChannel)
{
    constexpr std::size_t n = 11;
    std::vector<double> values(n * 2, std::numeric_limits<double>::quiet_NaN()); // free inputs are ignored
    values[0] = 0; values[1] = 10; values[(n - 1) * 2] = 10; values[(n - 1) * 2 + 1] = 0;
    const auto r = H::Solve(values, 2, Path(n, 3.0), {}, std::vector<std::size_t>{0, n - 1});
    ASSERT_TRUE(r.Success) << r.Diagnostic;
    EXPECT_EQ(r.Diagnostic, "cpu_reference_sparse_cholesky");
    for (std::size_t i = 0; i < n; ++i)
    {
        EXPECT_NEAR(r.Values[i * 2], double(i), 1e-12);
        EXPECT_NEAR(r.Values[i * 2 + 1], 10.0 - double(i), 1e-12);
    }
    EXPECT_EQ(r.Stats.FreeRows, n - 2);
    EXPECT_EQ(r.Stats.HardRows, 2u);
    EXPECT_EQ(r.Stats.Components, 1u);
    EXPECT_LT(r.Stats.MaxRelativeResidual, 1e-12);
}

TEST(HarmonicField, BiharmonicReproducesACubicWithTwoPinnedRowsPerEnd)
{
    // Interior rows of L M^-1 L on a uniform path are the fourth difference, which vanishes on
    // cubics; pinning two rows per end keeps every free row's stencil in the interior.
    constexpr std::size_t n = 12;
    const auto f = [](double i) { return i * i * i - 3 * i * i + 2; };
    std::vector<double> values(n, 0.0);
    const std::vector<std::size_t> pins{0, 1, n - 2, n - 1};
    for (auto p : pins) values[p] = f(double(p));
    H::Params params{.Order = H::FieldOrder::Biharmonic};
    const auto r = H::Solve(values, 1, Path(n), params, pins);
    ASSERT_TRUE(r.Success) << r.Diagnostic;
    for (std::size_t i = 0; i < n; ++i) EXPECT_NEAR(r.Values[i], f(double(i)), 1e-8 * std::max(1.0, std::abs(f(double(i)))));
}

TEST(HarmonicField, SoftConstraintMatchesClosedForm)
{
    // E = w (x1 - x0)^2 + lambda (x1 - 1)^2 with x0 = 0 hard: x1 = lambda / (w + lambda).
    const std::vector<double> values{0, 1};
    for (double lambda : {0.25, 1.0, 40.0})
    {
        const auto r = H::Solve(values, 1, std::vector<Edge>{{0, 1, 2.0}}, {}, std::vector<std::size_t>{0},
                                std::vector<H::SoftConstraint>{{1, lambda}});
        ASSERT_TRUE(r.Success) << r.Diagnostic;
        EXPECT_EQ(r.Values[0], 0.0);
        EXPECT_NEAR(r.Values[1], lambda / (2.0 + lambda), 1e-14);
    }
    // Soft constraints alone determine a component.
    const auto r = H::Solve(std::vector<double>{4, 0, 8}, 1, Path(3), {}, {},
                            std::vector<H::SoftConstraint>{{0, 1.0}, {2, 1.0}});
    ASSERT_TRUE(r.Success) << r.Diagnostic;
    EXPECT_NEAR(r.Values[1], 6.0, 1e-12);
}

TEST(HarmonicField, MatchesDenseOracleOnRandomGraphsForEveryOrderWithSources)
{
    std::mt19937 rng(1234);
    std::uniform_real_distribution<double> unit(0.1, 2.0), value(-5, 5);
    constexpr std::size_t n = 24, channels = 3;
    for (int trial = 0; trial < 6; ++trial)
    {
        std::vector<Edge> edges = Path(n);
        for (auto& e : edges) e.Weight = unit(rng);
        for (int extra = 0; extra < 30; ++extra)
        {
            const std::size_t a = rng() % n, b = rng() % n;
            if (a != b) edges.push_back({a, b, unit(rng)});
        }
        std::vector<double> values(n * channels), mass(n), source(n * channels);
        for (auto& v : values) v = value(rng);
        for (auto& b : source) b = value(rng);
        for (auto& m : mass) m = unit(rng);
        const std::vector<std::size_t> hard{0, 7, 15};
        const std::vector<H::SoftConstraint> soft{{3, unit(rng)}, {20, unit(rng)}};
        for (auto order : {H::FieldOrder::Harmonic, H::FieldOrder::Biharmonic, H::FieldOrder::Triharmonic})
            for (bool withSource : {false, true})
        {
            SCOPED_TRACE(trial * 100 + int(order) * 10 + int(withSource));
            const auto b = withSource ? source : std::vector<double>{};
            const auto r = H::Solve(values, channels, edges, {.Order = order}, hard, soft, mass, b);
            ASSERT_TRUE(r.Success) << r.Diagnostic;
            const auto oracle = DenseOracle(values, channels, n, edges, order, hard, soft, mass, b);
            for (std::size_t j = 0; j < values.size(); ++j)
                EXPECT_NEAR(r.Values[j], oracle[j], 1e-9 * std::max(1.0, std::abs(oracle[j]))) << j;
            for (auto h : hard)
                for (std::size_t c = 0; c < channels; ++c) EXPECT_EQ(r.Values[h * channels + c], values[h * channels + c]);
            EXPECT_LT(r.Stats.MaxRelativeResidual, 1e-10);
        }
    }
}

TEST(HarmonicField, CotangentHarmonicFieldReproducesLinearFunctionsOnAPlanarMesh)
{
    // Linear precision of the cotangent Laplacian: interior values of a planar mesh follow the
    // boundary's linear function exactly.
    // Jittered triangular lattice: angles stay near 60 degrees, so cotangent weights stay positive.
    Geometry::HalfedgeMesh::Mesh mesh;
    constexpr int side = 7;
    std::vector<Geometry::VertexHandle> v;
    for (int y = 0; y <= side; ++y)
        for (int x = 0; x <= side; ++x)
        {
            const bool interior = x > 0 && x < side && y > 0 && y < side;
            const float jx = interior ? 0.04f * float((x * 5 + y * 3) % 5 - 2) : 0.0f;
            const float jy = interior ? 0.04f * float((x * 3 + y * 7) % 5 - 2) : 0.0f;
            v.push_back(mesh.AddVertex({float(x) + 0.5f * float(y & 1) + jx, 0.8660254f * float(y) + jy, 0}));
        }
    const auto at = [&](int x, int y) { return v[y * (side + 1) + x]; };
    for (int y = 0; y < side; ++y)
        for (int x = 0; x < side; ++x)
            if ((y & 1) == 0)
            {
                ASSERT_TRUE(mesh.AddTriangle(at(x, y), at(x + 1, y), at(x, y + 1)));
                ASSERT_TRUE(mesh.AddTriangle(at(x + 1, y), at(x + 1, y + 1), at(x, y + 1)));
            }
            else
            {
                ASSERT_TRUE(mesh.AddTriangle(at(x, y), at(x + 1, y + 1), at(x, y + 1)));
                ASSERT_TRUE(mesh.AddTriangle(at(x, y), at(x + 1, y), at(x + 1, y + 1)));
            }
    const auto laplacian = Geometry::DEC::BuildLaplacian(mesh);
    std::vector<Edge> edges;
    for (std::size_t i = 0; i < laplacian.Rows; ++i)
        for (auto k = laplacian.RowOffsets[i]; k < laplacian.RowOffsets[i + 1]; ++k)
            if (laplacian.ColIndices[k] > i) edges.push_back({i, laplacian.ColIndices[k], -laplacian.Values[k]});
    for (const auto& e : edges) ASSERT_GE(e.Weight, 0.0) << "fixture must keep cotangent weights nonnegative";
    const auto linear = [&](Geometry::VertexHandle h) { const auto p = mesh.Position(h); return 2.0 * p.x - 0.5 * p.y + 1.0; };
    std::vector<double> values(mesh.VerticesSize(), 0.0);
    std::vector<std::size_t> boundary;
    for (auto h : v)
        if (mesh.IsBoundary(h)) { boundary.push_back(h.Index); values[h.Index] = linear(h); }
    const auto r = H::Solve(values, 1, edges, {}, boundary);
    ASSERT_TRUE(r.Success) << r.Diagnostic;
    for (auto h : v) EXPECT_NEAR(r.Values[h.Index], linear(h), 1e-9);
}

TEST(HarmonicField, UnconstrainedComponentsFailOrKeepTheirInput)
{
    const std::vector<Edge> edges{{0, 1, 1}, {2, 3, 1}};
    const std::vector<double> values{0, 0, 5, 6, 9};
    const std::vector<std::size_t> hard{0};
    const auto failed = H::Solve(values, 1, edges, {}, hard);
    EXPECT_FALSE(failed.Success);
    EXPECT_TRUE(failed.Values.empty());
    EXPECT_EQ(failed.Stats.UnconstrainedComponents, 2u); // {2,3} and the isolated row 4
    const auto kept = H::Solve(values, 1, edges, {.Unconstrained = H::UnconstrainedPolicy::KeepInput}, hard);
    ASSERT_TRUE(kept.Success) << kept.Diagnostic;
    EXPECT_EQ(kept.Values, values);
    EXPECT_EQ(kept.KeptInput, (std::vector<bool>{false, false, true, true, true}));
    EXPECT_EQ(kept.Stats.Components, 3u);
}

TEST(HarmonicField, InvalidInputsReturnNoPartialValues)
{
    const std::vector<double> values{0, 1, 2};
    const auto edges = Path(3);
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<std::size_t> duplicate{0, 0}, outside{3}, single{0};
    EXPECT_FALSE(H::Solve(values, 1, edges, {}, duplicate).Success);
    EXPECT_FALSE(H::Solve(values, 1, edges, {}, outside).Success);
    EXPECT_FALSE(H::Solve(values, 2, edges, {}, single).Success); // shape
    EXPECT_FALSE(H::Solve(values, 1, edges, {}, single, std::vector<H::SoftConstraint>{{0, 1}}).Success);
    EXPECT_FALSE(H::Solve(values, 1, edges, {}, single, std::vector<H::SoftConstraint>{{1, 0}}).Success);
    EXPECT_FALSE(H::Solve(values, 1, edges, {}, single, {}, std::vector<double>{1, 0, 1}).Success);
    EXPECT_FALSE(H::Solve(values, 1, std::vector<Edge>{{0, 1, -1}}, {}, single).Success);
    EXPECT_FALSE(H::Solve(std::vector<double>{nan, 1, 2}, 1, edges, {}, single).Success);
    EXPECT_FALSE(H::Solve(values, 1, edges, {.Order = H::FieldOrder(4)}, single).Success);
    EXPECT_FALSE(H::Solve(values, 1, edges, {.Unconstrained = H::UnconstrainedPolicy(3)}, single).Success);
    EXPECT_FALSE(H::Solve(values, 1, edges, {}, single, {}, {}, std::vector<double>{1, 1}).Success);
    EXPECT_FALSE(H::Solve(values, 1, edges, {}, single, {}, {}, std::vector<double>{1, nan, 1}).Success);
    const auto r = H::Solve(values, 1, edges, {}, duplicate);
    EXPECT_TRUE(r.Values.empty());
    EXPECT_FALSE(r.Diagnostic.empty());
}

TEST(HarmonicField, TriharmonicReproducesAQuinticWithThreePinnedRowsPerEnd)
{
    // Interior rows of L^3 on a uniform path are the sixth difference, which vanishes on quintics.
    constexpr std::size_t n = 14;
    const auto f = [](double i) { return 0.001 * i * i * i * i * i - 0.02 * i * i * i * i + i; };
    std::vector<double> values(n, 0.0);
    const std::vector<std::size_t> pins{0, 1, 2, n - 3, n - 2, n - 1};
    for (auto p : pins) values[p] = f(double(p));
    const auto r = H::Solve(values, 1, Path(n), {.Order = H::FieldOrder::Triharmonic}, pins);
    ASSERT_TRUE(r.Success) << r.Diagnostic;
    for (std::size_t i = 0; i < n; ++i) EXPECT_NEAR(r.Values[i], f(double(i)), 1e-7 * std::max(1.0, std::abs(f(double(i)))));
}

TEST(HarmonicField, PoissonSourceReproducesAQuadratic)
{
    // (L x)_i = 2 i^2 - (i-1)^2 - (i+1)^2 = -2 on interior rows of a unit path.
    constexpr std::size_t n = 9;
    std::vector<double> values(n, 0.0), source(n, -2.0);
    values[n - 1] = double((n - 1) * (n - 1));
    source[0] = 1e9; // ignored on hard rows
    const auto r = H::Solve(values, 1, Path(n), {}, std::vector<std::size_t>{0, n - 1}, {}, {}, source);
    ASSERT_TRUE(r.Success) << r.Diagnostic;
    for (std::size_t i = 0; i < n; ++i) EXPECT_NEAR(r.Values[i], double(i * i), 1e-10);
}

TEST(HarmonicField, ZeroMeanSolvesTheGroundedNeumannProblem)
{
    // Unit flux in at row 0 and out at row n-1 of a free path: x_i = c - i, zero mean.
    constexpr std::size_t n = 7;
    std::vector<double> source(n, 0.0);
    source[0] = 1; source[n - 1] = -1;
    const std::vector<double> values(n, 42.0); // ignored
    const H::Params params{.Unconstrained = H::UnconstrainedPolicy::ZeroMean};
    const auto r = H::Solve(values, 1, Path(n), params, {}, {}, {}, source);
    ASSERT_TRUE(r.Success) << r.Diagnostic;
    EXPECT_TRUE(r.KeptInput.empty());
    EXPECT_EQ(r.Stats.GroundedComponents, 1u);
    EXPECT_EQ(r.Stats.MaxCompatibilityDefect, 0.0);
    for (std::size_t i = 0; i < n; ++i) EXPECT_NEAR(r.Values[i], double(n - 1) / 2 - double(i), 1e-12);

    // An incompatible source is projected in proportion to mass and reported; the mass-weighted
    // mean is zero and the projected equations hold.
    const std::vector<double> mass{1, 2, 1, 3, 1, 2, 1};
    std::vector<double> biased = source;
    biased[3] += 0.5;
    const auto p = H::Solve(values, 1, Path(n), params, {}, {}, mass, biased);
    ASSERT_TRUE(p.Success) << p.Diagnostic;
    EXPECT_NEAR(p.Stats.MaxCompatibilityDefect, 0.5 / 2.5, 1e-15);
    double weighted = 0;
    for (std::size_t i = 0; i < n; ++i) weighted += mass[i] * p.Values[i];
    EXPECT_NEAR(weighted, 0.0, 1e-12);
    for (std::size_t i = 0; i < n; ++i)
    {
        const double left = i > 0 ? p.Values[i] - p.Values[i - 1] : 0.0;
        const double right = i + 1 < n ? p.Values[i] - p.Values[i + 1] : 0.0;
        EXPECT_NEAR(left + right, biased[i] - 0.5 * mass[i] / 11.0, 1e-12) << i;
    }

    // Constrained components are unaffected; isolated rows ground to zero.
    const std::vector<Edge> edges{{0, 1, 1}, {1, 2, 1}, {3, 4, 1}};
    const auto mixed = H::Solve(std::vector<double>{0, 9, 4, 9, 9, 9}, 1, edges, params,
                                std::vector<std::size_t>{0, 2}, {}, {}, std::vector<double>{0, 0, 0, 2, -2, 5});
    ASSERT_TRUE(mixed.Success) << mixed.Diagnostic;
    EXPECT_EQ(mixed.Stats.GroundedComponents, 2u);
    EXPECT_EQ(mixed.Stats.MaxCompatibilityDefect, 1.0); // the isolated row's source cannot be balanced
    EXPECT_NEAR(mixed.Values[1], 2.0, 1e-12);
    EXPECT_NEAR(mixed.Values[3], 1.0, 1e-12);
    EXPECT_NEAR(mixed.Values[4], -1.0, 1e-12);
    EXPECT_EQ(mixed.Values[5], 0.0);
}

TEST(HarmonicField, RandomWalkerLabelsFollowTheHarmonicSplit)
{
    constexpr std::size_t n = 10;
    std::vector<std::int32_t> labels(n, -1);
    labels[0] = 5; labels[n - 1] = 2;
    const auto r = H::PropagateLabels(labels, -1, Path(n), {});
    ASSERT_TRUE(r.Success) << r.Diagnostic;
    EXPECT_EQ(r.SeedLabels, (std::vector<std::int32_t>{2, 5}));
    ASSERT_EQ(r.Weights.size(), n * 2);
    for (std::size_t i = 0; i < n; ++i)
    {
        EXPECT_NEAR(r.Weights[i * 2] + r.Weights[i * 2 + 1], 1.0, 1e-12); // partition of unity
        EXPECT_NEAR(r.Weights[i * 2 + 1], 1.0 - double(i) / double(n - 1), 1e-12); // label 5 at row 0
    }
    for (std::size_t i = 0; i < n; ++i)
    {
        EXPECT_EQ(r.Labels[i], i <= 4 ? 5 : 2) << i;
        EXPECT_NEAR(r.Confidence[i], std::max(double(i), double(n - 1 - i)) / double(n - 1), 1e-12);
    }
    // Ties go to the smallest label.
    const auto tie = H::PropagateLabels(std::vector<std::int32_t>{7, 0, 3}, 0, Path(3), {});
    ASSERT_TRUE(tie.Success) << tie.Diagnostic;
    EXPECT_EQ(tie.Labels, (std::vector<std::int32_t>{7, 3, 3}));
    EXPECT_NEAR(tie.Confidence[1], 0.5, 1e-12);
}

TEST(HarmonicField, RandomWalkerKeepsUnseededComponentsAndRejectsMissingSeeds)
{
    const std::vector<Edge> edges{{0, 1, 1}, {2, 3, 1}};
    const std::vector<std::int32_t> labels{4, 0, 0, 0};
    EXPECT_FALSE(H::PropagateLabels(labels, 0, edges, {}).Success);
    const auto kept = H::PropagateLabels(labels, 0, edges, {.Unconstrained = H::UnconstrainedPolicy::KeepInput});
    ASSERT_TRUE(kept.Success) << kept.Diagnostic;
    EXPECT_EQ(kept.Labels, (std::vector<std::int32_t>{4, 4, 0, 0}));
    EXPECT_EQ(kept.Confidence[2], 0.0);
    EXPECT_EQ(kept.Weights, (std::vector<double>{1, 1, 0, 0}));
    EXPECT_FALSE(H::PropagateLabels(labels, 0, edges, {.Unconstrained = H::UnconstrainedPolicy::ZeroMean}).Success);
    EXPECT_FALSE(H::PropagateLabels(std::vector<std::int32_t>{0, 0}, 0, Path(2), {}).Success);
}
