// Stress correctness tests for the geometry.progressive_poisson CPU reference
// backend (METHOD-012). Hermetic: no runtime, ECS, or graphics dependencies.

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
    std::vector<glm::vec3> UniformCube(std::uint32_t n, std::uint32_t seed, int dim)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<float> d(0.0f, 1.0f);
        std::vector<glm::vec3> pts(n);
        for (std::uint32_t i = 0; i < n; ++i)
            pts[i] = glm::vec3{d(rng), d(rng), (dim == 3) ? d(rng) : 0.0f};
        return pts;
    }

    float BruteMinDistance(const std::vector<glm::vec3>& pts,
                           const std::vector<std::uint32_t>& order,
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

TEST_P(ProgressivePoissonReferenceDim, PoissonGuaranteeHoldsAtEveryLevelBoundary)
{
    const int dim = GetParam();
    constexpr std::uint32_t kPointCount = 3000u;
    const std::vector<glm::vec3> cloud = UniformCube(kPointCount, 42, dim);
    ppr::Config cfg;
    cfg.Dimension = static_cast<std::uint32_t>(dim);

    const ppr::Result r = ppr::Compute(cloud, cfg);

    ASSERT_EQ(r.Diag.Code, ppr::ValidationCode::Valid);
    ASSERT_FALSE(r.LevelOffsets.empty());
    EXPECT_EQ(r.LevelOffsets.back(), r.Order.size());
    EXPECT_EQ(r.SplatRadii.size(), r.Order.size());
    EXPECT_GT(r.Diag.AcceptedCount, 0u);
    EXPECT_LE(r.Diag.AcceptedCount, kPointCount);

    // Accepted ordering is a unique, in-range subset of the input.
    std::vector<char> seen(kPointCount, 0);
    for (const std::uint32_t idx : r.Order)
    {
        ASSERT_LT(idx, kPointCount);
        ASSERT_EQ(seen[idx], 0) << "duplicate index in order";
        seen[idx] = 1;
    }

    // The core theorem: every level-boundary prefix is Poisson-disk at r_L.
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

INSTANTIATE_TEST_SUITE_P(Dimensions, ProgressivePoissonReferenceDim, ::testing::Values(2, 3));

TEST(ProgressivePoissonReference, CoincidentPointsAcceptExactlyOne)
{
    const std::vector<glm::vec3> pts(16, glm::vec3{0.5f, 0.5f, 0.5f});
    const ppr::Result r = ppr::Compute(pts, ppr::Config{});
    EXPECT_EQ(r.Order.size(), 1u);
    EXPECT_EQ(r.Diag.AcceptedCount, 1u);
}
