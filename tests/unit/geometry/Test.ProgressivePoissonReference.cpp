// Correctness tests for the geometry.progressive_poisson CPU reference backend
// (METHOD-012). Hermetic: depends only on the method header, no engine modules.

#include "ProgressivePoissonReference.hpp"

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <vector>

namespace ppr = Intrinsic::Methods::Geometry::ProgressivePoissonReference;

namespace
{
    // A point set is just a span of glm::vec3 — the same type any vec3 property
    // buffer (positions, normals, a `v:foo` property) exposes. No container type
    // is required by the method.
    std::vector<glm::vec3> UniformCube(std::uint32_t n, std::uint32_t seed, int dim)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> d(0.0f, 1.0f);
        std::vector<glm::vec3> pts(n);
        for (std::uint32_t i = 0; i < n; ++i)
            pts[i] = glm::vec3{d(rng), d(rng), (dim == 3) ? d(rng) : 0.0f};
        return pts;
    }

    // Independent O(k^2) ground truth — the reference must never disagree.
    float BruteMinDistance(const std::vector<glm::vec3>& pts, const std::vector<std::uint32_t>& order,
                           std::uint32_t count, int dim)
    {
        float best = std::numeric_limits<float>::max();
        for (std::uint32_t a = 0; a < count; ++a)
            for (std::uint32_t b = a + 1; b < count; ++b)
            {
                const glm::vec3& pi = pts[order[a]];
                const glm::vec3& pj = pts[order[b]];
                const float ex = pi.x - pj.x;
                const float ey = pi.y - pj.y;
                const float ez = (dim == 3) ? (pi.z - pj.z) : 0.0f;
                best = std::min(best, std::sqrt(ex * ex + ey * ey + ez * ez));
            }
        return best;
    }
} // namespace

class ProgressivePoissonReferenceDim : public ::testing::TestWithParam<int>
{
};

TEST_P(ProgressivePoissonReferenceDim, SmallDeterministicCloudHoldsAtEveryLevelBoundary)
{
    const int dim = GetParam();
    const float z = (dim == 3) ? 1.0f : 0.0f;
    const std::vector<glm::vec3> cloud{
        {0.00f, 0.00f, 0.00f},
        {0.04f, 0.02f, 0.03f * z},
        {0.08f, 0.01f, 0.06f * z},
        {0.25f, 0.20f, 0.17f * z},
        {0.29f, 0.21f, 0.22f * z},
        {0.52f, 0.45f, 0.50f * z},
        {0.56f, 0.47f, 0.54f * z},
        {0.75f, 0.80f, 0.70f * z},
        {0.79f, 0.82f, 0.75f * z},
        {1.00f, 1.00f, 1.00f * z},
    };
    ppr::Config cfg;
    cfg.Dimension = static_cast<std::uint32_t>(dim);

    const ppr::Result r = ppr::Compute(cloud, cfg);

    ASSERT_EQ(r.Diag.Code, ppr::ValidationCode::Valid);
    ASSERT_GT(r.LevelOffsets.size(), 2u);
    EXPECT_EQ(r.LevelOffsets.back(), r.Order.size());

    // Retain a cheap deterministic sentinel for the level-boundary theorem.
    for (std::size_t L = 0; L + 1 < r.LevelOffsets.size(); ++L)
    {
        const std::uint32_t le = r.LevelOffsets[L + 1];
        if (le < 2)
            continue;
        const float rL = r.Diag.LevelRadii[L];
        const float measured = BruteMinDistance(cloud, r.Order, le, dim);
        EXPECT_GE(measured, rL * 0.9999f)
            << "level " << L << " min_dist=" << measured << " r_L=" << rL;
    }
}

TEST_P(ProgressivePoissonReferenceDim, MinPairwiseHelperEqualsBruteForce)
{
    const int dim = GetParam();
    const std::vector<glm::vec3> cloud = UniformCube(8000, 11, dim);
    ppr::Config cfg;
    cfg.Dimension = static_cast<std::uint32_t>(dim);
    const ppr::Result r = ppr::Compute(cloud, cfg);
    ASSERT_GE(r.LevelOffsets.size(), 2u);

    // The helper is the exact measured minimum pairwise distance — it must agree
    // with the O(n^2) ground truth at the (sparse) level-0 prefix, not merely
    // report "no neighbor within one cell".
    const std::uint32_t le = r.LevelOffsets[1];
    const float helper = ppr::MinPairwiseDistance(cloud, r.Order, le, static_cast<std::uint32_t>(dim));
    const float brute = BruteMinDistance(cloud, r.Order, le, dim);
    ASSERT_NE(helper, std::numeric_limits<float>::max());
    EXPECT_NEAR(helper, brute, brute * 1e-5f);
}

TEST(ProgressivePoissonReference, MinPairwiseFindsPairsBeyondAdjacentCells)
{
    // The exact scenario from the review: two points 3 apart. A naive 3^d search
    // at any sub-3 cell size would miss them and report "no finite spacing"; the
    // shell search must return the true distance 3.
    const std::vector<glm::vec3> pts{glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{3.0f, 0.0f, 0.0f}};
    const std::vector<std::uint32_t> order{0u, 1u};
    const float d3 = ppr::MinPairwiseDistance(pts, order, 2u, 3u);
    EXPECT_NEAR(d3, 3.0f, 1e-5f);
    const float d2 = ppr::MinPairwiseDistance(pts, order, 2u, 2u);
    EXPECT_NEAR(d2, 3.0f, 1e-5f);

    // A wider, very sparse set: nearest pair is the 1.0 gap among far-flung points.
    const std::vector<glm::vec3> sparse{
        glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{50.0f, 0.0f, 0.0f},
        glm::vec3{50.0f, 1.0f, 0.0f}, glm::vec3{0.0f, 80.0f, 0.0f}};
    const std::vector<std::uint32_t> sorder{0u, 1u, 2u, 3u};
    const float ds = ppr::MinPairwiseDistance(sparse, sorder, 4u, 2u);
    EXPECT_NEAR(ds, 1.0f, 1e-5f);

    // Fewer than two points -> sentinel; coincident points -> 0.
    EXPECT_EQ(ppr::MinPairwiseDistance(pts, std::span<const std::uint32_t>{order.data(), 1u}, 1u, 2u),
              std::numeric_limits<float>::max());
    const std::vector<glm::vec3> dup(3, glm::vec3{2.0f, 2.0f, 2.0f});
    const std::vector<std::uint32_t> dorder{0u, 1u, 2u};
    EXPECT_EQ(ppr::MinPairwiseDistance(dup, dorder, 3u, 3u), 0.0f);
}

INSTANTIATE_TEST_SUITE_P(Dimensions, ProgressivePoissonReferenceDim, ::testing::Values(2, 3));

TEST(ProgressivePoissonReference, DeterministicForFixedSeeds)
{
    const std::vector<glm::vec3> cloud = UniformCube(5000, 7, 3);
    ppr::Config cfg;
    const ppr::Result a = ppr::Compute(cloud, cfg);
    const ppr::Result b = ppr::Compute(cloud, cfg);
    EXPECT_EQ(a.Order, b.Order);
    EXPECT_EQ(a.LevelOffsets, b.LevelOffsets);
    EXPECT_EQ(a.SplatRadii, b.SplatRadii);
}

TEST(ProgressivePoissonReference, ShuffleSeedPermutesWithinLevelButKeepsBoundaries)
{
    const std::vector<glm::vec3> cloud = UniformCube(5000, 7, 3);
    ppr::Config cfg;
    ppr::Config cfg2 = cfg;
    cfg2.ShuffleSeed = cfg.ShuffleSeed ^ 0x00abcdefu;

    const ppr::Result a = ppr::Compute(cloud, cfg);
    const ppr::Result b = ppr::Compute(cloud, cfg2);

    // Same accepted set and level boundaries; only intra-level order changes.
    EXPECT_EQ(a.LevelOffsets, b.LevelOffsets);
    EXPECT_EQ(a.Order.size(), b.Order.size());
}

TEST(ProgressivePoissonReference, EmptyInputIsValidAndEmpty)
{
    ppr::Config cfg;
    const ppr::Result r = ppr::Compute(std::span<const glm::vec3>{}, cfg);
    EXPECT_EQ(r.Diag.Code, ppr::ValidationCode::Valid);
    EXPECT_TRUE(r.Order.empty());
    ASSERT_EQ(r.LevelOffsets.size(), 1u);
    EXPECT_EQ(r.LevelOffsets[0], 0u);
}

TEST(ProgressivePoissonReference, CoincidentPairAcceptsExactlyOne)
{
    const std::vector<glm::vec3> pts(2, glm::vec3{0.5f, 0.5f, 0.5f});
    ppr::Config cfg;
    cfg.Dimension = 2;
    const ppr::Result r = ppr::Compute(pts, cfg);
    EXPECT_EQ(r.Order.size(), 1u);
    EXPECT_EQ(r.Diag.AcceptedCount, 1u);
}

TEST(ProgressivePoissonReference, NonFiniteInputFailsClosed)
{
    const std::vector<glm::vec3> pts{glm::vec3{0.0f, 0.0f, 0.0f},
                                     glm::vec3{std::nanf(""), 1.0f, 1.0f}};
    const ppr::Result r = ppr::Compute(pts, ppr::Config{});
    EXPECT_EQ(r.Diag.Code, ppr::ValidationCode::NonFiniteInput);
    EXPECT_TRUE(r.Order.empty());
}

TEST(ProgressivePoissonReference, InvalidDimensionFailsClosed)
{
    const std::vector<glm::vec3> pts{glm::vec3{0.0f, 0.0f, 0.0f}};
    ppr::Config cfg;
    cfg.Dimension = 4;
    const ppr::Result r = ppr::Compute(pts, cfg);
    EXPECT_EQ(r.Diag.Code, ppr::ValidationCode::InvalidDimension);
}

TEST(ProgressivePoissonReference, TwoDimensionalIgnoresZComponent)
{
    // Same x,y but wildly different z: in 2D the result must not depend on z.
    std::vector<glm::vec3> a = UniformCube(2000, 5, 2);
    std::vector<glm::vec3> b = a;
    for (std::size_t i = 0; i < b.size(); ++i)
        b[i].z = static_cast<float>(i) * 13.0f; // noise on the ignored axis
    ppr::Config cfg;
    cfg.Dimension = 2;
    const ppr::Result ra = ppr::Compute(a, cfg);
    const ppr::Result rb = ppr::Compute(b, cfg);
    EXPECT_EQ(ra.Order, rb.Order);
    EXPECT_EQ(ra.LevelOffsets, rb.LevelOffsets);
}

TEST(ProgressivePoissonReference, ZeroGridWidthAndLevelsAreClamped)
{
    const std::vector<glm::vec3> pts{glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{1.0f, 1.0f, 1.0f}};
    ppr::Config cfg;
    cfg.GridWidth = 0;
    cfg.MaxLevels = 0;
    const ppr::Result r = ppr::Compute(pts, cfg);
    EXPECT_TRUE(r.Diag.ClampedGridWidth);
    EXPECT_TRUE(r.Diag.ClampedMaxLevels);
    EXPECT_EQ(r.Diag.Code, ppr::ValidationCode::Valid);
}

TEST(ProgressivePoissonReference, AlphaOutOfRangeDefaultsToSqrtDOverTwo)
{
    const std::vector<glm::vec3> cloud = UniformCube(1000, 3, 2);
    ppr::Config cfg;
    cfg.Dimension = 2;
    cfg.RadiusAlpha = -1.0f; // out of (0,1)
    const ppr::Result r = ppr::Compute(cloud, cfg);
    EXPECT_TRUE(r.Diag.AlphaDefaulted);
    EXPECT_NEAR(r.Diag.UsedAlpha, 0.5f * 1.41421356f, 1e-5f);
}

// ── GEOM-112: the remaining CUDA sampler options ─────────────────────────────
namespace
{
    // The level-boundary guarantee: every prefix ending at a level boundary keeps r_L.
    void ExpectLevelGuarantee(const std::vector<glm::vec3>& pts, const ppr::Result& r, int dim, const char* label)
    {
        ASSERT_EQ(r.Diag.Code, ppr::ValidationCode::Valid) << label;
        for (std::size_t L = 0; L + 1 < r.LevelOffsets.size(); ++L)
        {
            const std::uint32_t le = r.LevelOffsets[L + 1];
            if (le < 2)
                continue;
            const float measured = ppr::MinPairwiseDistance(pts, r.Order, le, static_cast<std::uint32_t>(dim));
            EXPECT_GE(measured, r.Diag.LevelRadii[L] * 0.9999f) << label << " level " << L;
        }
    }

    std::vector<float> ScoresFromX(const std::vector<glm::vec3>& pts)
    {
        std::vector<float> scores;
        for (const auto& p : pts)
            scores.push_back(p.x);
        return scores;
    }
}

TEST_P(ProgressivePoissonReferenceDim, EveryCellPolicyKeepsTheLevelGuarantee)
{
    const int dim = GetParam();
    const auto pts = UniformCube(3000, 77u, dim);
    const auto scores = ScoresFromX(pts);
    ppr::Config base;
    base.Dimension = static_cast<std::uint32_t>(dim);
    for (const auto profile : {ppr::Profile::Fast, ppr::Profile::Balanced, ppr::Profile::Quality, ppr::Profile::Hapds})
        ExpectLevelGuarantee(pts, ppr::Compute(pts, ppr::WithProfile(base, profile)), dim, "profile");
    ppr::Config best = base;
    best.Selection = ppr::CellSelection::BestOfCandidates;
    best.CandidateBudget = 8;
    ExpectLevelGuarantee(pts, ppr::Compute(pts, best), dim, "best-of-candidates");
    for (const bool twoBands : {false, true})
    {
        ppr::Config priority = base;
        priority.Selection = ppr::CellSelection::FeaturePriority;
        priority.PriorityTwoBands = twoBands;
        priority.PriorityBandThreshold = 0.5f;
        ExpectLevelGuarantee(pts, ppr::Compute(pts, priority, scores), dim, twoBands ? "priority-2" : "priority-1");
    }
    ppr::Config phases = base;
    phases.RandomizePhaseOrder = true;
    const auto shuffledPhases = ppr::Compute(pts, phases);
    ExpectLevelGuarantee(pts, shuffledPhases, dim, "random-phase-order");
    EXPECT_NE(shuffledPhases.Order, ppr::Compute(pts, base).Order);
    ppr::Config coarse = ppr::WithProfile(base, ppr::Profile::Fast);
    coarse.ExhaustiveCoarseLevels = 2;
    ExpectLevelGuarantee(pts, ppr::Compute(pts, coarse), dim, "exhaustive-coarse");
}

TEST(ProgressivePoissonReference, BoundedCellsAreASubsetOfExhaustiveSaturationAtLevelZero)
{
    const auto pts = UniformCube(4000, 5u, 3);
    ppr::Config exhaustive;
    const auto full = ppr::Compute(pts, exhaustive);
    const auto fast = ppr::Compute(pts, ppr::WithProfile(exhaustive, ppr::Profile::Fast));
    ASSERT_GE(full.Diag.LevelCounts.size(), 1u);
    ASSERT_GE(fast.Diag.LevelCounts.size(), 1u);
    // Level 0 starts from the same empty state: a bounded cell can only stay empty where an
    // exhaustive cell finds a feasible candidate.
    EXPECT_LE(fast.Diag.LevelCounts[0], full.Diag.LevelCounts[0]);
    // A budget of one candidate is the bounded single contender.
    ppr::Config budgetOne;
    budgetOne.Selection = ppr::CellSelection::BestOfCandidates;
    budgetOne.CandidateBudget = 1;
    EXPECT_EQ(ppr::Compute(pts, budgetOne).Order, fast.Order);
}

TEST(ProgressivePoissonReference, FeaturePriorityPicksTheHighestFeasibleScorePerCell)
{
    // One level-0 cell holds every point: its winner is the highest score (ties: lowest index).
    std::vector<glm::vec3> pts{{0.10f, 0.1f, 0.1f}, {0.12f, 0.1f, 0.1f}, {0.14f, 0.1f, 0.1f}, {0.16f, 0.1f, 0.1f}};
    pts.push_back({1.0f, 1.0f, 1.0f}); // sets the extent; lands in another cell
    const std::vector<float> scores{0.2f, 0.9f, 0.9f, 0.5f, 0.0f};
    ppr::Config cfg;
    cfg.GridWidth = 1;
    cfg.RandomizeGridOrigin = false;
    cfg.ShuffleWithinLevels = false;
    cfg.Selection = ppr::CellSelection::FeaturePriority;
    const auto r = ppr::Compute(pts, cfg, scores);
    ASSERT_EQ(r.Diag.Code, ppr::ValidationCode::Valid);
    ASSERT_GE(r.LevelOffsets.size(), 2u);
    const std::vector<std::uint32_t> level0(r.Order.begin(), r.Order.begin() + r.LevelOffsets[1]);
    EXPECT_NE(std::find(level0.begin(), level0.end(), 1u), level0.end()) << "score 0.9 at the lowest index wins";
    EXPECT_EQ(std::find(level0.begin(), level0.end(), 2u), level0.end());
}

TEST(ProgressivePoissonReference, SpatiallyBalancedOrderingPermutesOnlyWithinLevels)
{
    const auto pts = UniformCube(5000, 9u, 3);
    ppr::Config shuffled;
    ppr::Config balanced;
    balanced.Ordering = ppr::WithinLevelOrdering::SpatiallyBalanced;
    const auto a = ppr::Compute(pts, shuffled);
    const auto b = ppr::Compute(pts, balanced);
    ASSERT_EQ(a.LevelOffsets, b.LevelOffsets);
    EXPECT_NE(a.Order, b.Order);
    for (std::size_t L = 0; L + 1 < a.LevelOffsets.size(); ++L)
    {
        std::vector<std::uint32_t> sa(a.Order.begin() + a.LevelOffsets[L], a.Order.begin() + a.LevelOffsets[L + 1]);
        std::vector<std::uint32_t> sb(b.Order.begin() + b.LevelOffsets[L], b.Order.begin() + b.LevelOffsets[L + 1]);
        std::sort(sa.begin(), sa.end());
        std::sort(sb.begin(), sb.end());
        EXPECT_EQ(sa, sb) << "level " << L;
    }
    // Reordering a cached result reproduces the direct run, and radii stay with their ids.
    const auto reordered = ppr::ReorderWithinLevels(a, pts, 3u, ppr::WithinLevelOrdering::SpatiallyBalanced);
    ASSERT_EQ(reordered.Diag.Code, ppr::ValidationCode::Valid);
    EXPECT_EQ(reordered.Order, b.Order);
    EXPECT_EQ(reordered.SplatRadii, b.SplatRadii);
    ppr::Result broken = a;
    broken.Order[0] = broken.Order[1];
    EXPECT_EQ(ppr::ReorderWithinLevels(broken, pts, 3u, ppr::WithinLevelOrdering::SpatiallyBalanced).Diag.Code,
              ppr::ValidationCode::InvalidConfig);
}

TEST(ProgressivePoissonReference, OrderOnlyModeSkipsRadiiWithTheSameOrder)
{
    const auto pts = UniformCube(2000, 13u, 3);
    ppr::Config full;
    ppr::Config orderOnly;
    orderOnly.ComputeSplatRadii = false;
    const auto a = ppr::Compute(pts, full);
    const auto b = ppr::Compute(pts, orderOnly);
    EXPECT_EQ(a.Order, b.Order);
    EXPECT_TRUE(b.SplatRadii.empty());
    EXPECT_TRUE(b.Diag.LevelMinDistance.empty());
}

TEST(ProgressivePoissonReference, PolicyMisuseFailsClosed)
{
    const auto pts = UniformCube(100, 3u, 3);
    const auto scores = ScoresFromX(pts);
    const auto code = [&](ppr::Config c, std::span<const float> s = {}) { return ppr::Compute(pts, c, s).Diag.Code; };
    ppr::Config priority;
    priority.Selection = ppr::CellSelection::FeaturePriority;
    EXPECT_EQ(code(priority), ppr::ValidationCode::InvalidConfig) << "scores missing";
    EXPECT_EQ(code(priority, std::span<const float>(scores).first(10)), ppr::ValidationCode::InvalidConfig);
    std::vector<float> nan = scores;
    nan[3] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(code(priority, nan), ppr::ValidationCode::InvalidConfig);
    ppr::Config coarse = priority;
    coarse.ExhaustiveCoarseLevels = 1;
    EXPECT_EQ(code(coarse, scores), ppr::ValidationCode::InvalidConfig);
    EXPECT_EQ(code(ppr::Config{}, scores), ppr::ValidationCode::InvalidConfig) << "scores without the policy";
    ppr::Config bands;
    bands.PriorityTwoBands = true;
    EXPECT_EQ(code(bands), ppr::ValidationCode::InvalidConfig);
    for (const std::uint32_t budget : {0u, 33u})
    {
        ppr::Config best;
        best.Selection = ppr::CellSelection::BestOfCandidates;
        best.CandidateBudget = budget;
        EXPECT_EQ(code(best), ppr::ValidationCode::InvalidConfig) << budget;
    }
    EXPECT_EQ(code(priority, scores), ppr::ValidationCode::Valid);
}

// Fixtures and expected sequences of the CUDA sampler's reorder_within_levels test
// (test_progressive_poisson.cu, "Cached reorder helper"): the CPU ordering matches exactly.
TEST(ProgressivePoissonReference, SpatiallyBalancedMatchesTheCudaSamplerSequences)
{
    const auto balanced = ppr::WithinLevelOrdering::SpatiallyBalanced;
    const auto reorder = [&](std::vector<std::uint32_t> order, std::vector<std::uint32_t> offsets,
                             const std::vector<glm::vec3>& pts, std::uint32_t dim) {
        ppr::Result cached;
        cached.Order = std::move(order);
        cached.LevelOffsets = std::move(offsets);
        return ppr::ReorderWithinLevels(cached, pts, dim, balanced);
    };
    {   // 2-D, nine collinear points: bit-reversed ranks.
        std::vector<glm::vec3> pts;
        std::vector<std::uint32_t> order;
        for (std::uint32_t i = 0; i < 9; ++i) { pts.push_back({0.1f * float(i + 1), 0.5f, 0.0f}); order.push_back(8 - i); }
        EXPECT_EQ(reorder(order, {0, 9}, pts, 2).Order, (std::vector<std::uint32_t>{0, 8, 4, 2, 6, 1, 5, 3, 7}));
    }
    {   // 3-D, two levels of 4 and 7 with a scrambled cached order.
        std::vector<glm::vec3> pts;
        for (std::uint32_t i = 0; i < 11; ++i) pts.push_back({0.05f * float(i), 0.3f, 0.7f});
        EXPECT_EQ(reorder({3, 1, 2, 0, 10, 5, 7, 9, 4, 6, 8}, {0, 4, 11}, pts, 3).Order,
                  (std::vector<std::uint32_t>{0, 2, 1, 3, 4, 8, 6, 10, 5, 9, 7}));
    }
    {   // Coincident points: Morton ties keep ascending ids.
        const std::vector<glm::vec3> pts(3, glm::vec3{0.4f, 0.6f, 0.0f});
        EXPECT_EQ(reorder({2, 0, 1}, {0, 3}, pts, 2).Order, (std::vector<std::uint32_t>{0, 2, 1}));
    }
    {   // Power-of-two count.
        std::vector<glm::vec3> pts;
        for (std::uint32_t i = 0; i < 8; ++i) pts.push_back({float(i), 0.0f, 0.0f});
        EXPECT_EQ(reorder({0, 1, 2, 3, 4, 5, 6, 7}, {0, 8}, pts, 2).Order,
                  (std::vector<std::uint32_t>{0, 4, 2, 6, 1, 5, 3, 7}));
    }
    {   // Empty and singleton results are returned unchanged.
        EXPECT_TRUE(reorder({}, {0}, {}, 2).Order.empty());
        const std::vector<glm::vec3> pts{{0.0f, 0.0f, 0.0f}, {0.5f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}};
        EXPECT_EQ(reorder({1}, {0, 1}, pts, 2).Order, (std::vector<std::uint32_t>{1}));
    }
}
