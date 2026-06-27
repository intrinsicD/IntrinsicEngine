#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>
#include <glm/glm.hpp>

import Geometry.Rotation;

namespace
{
    using namespace Geometry::Rotation;
    constexpr float kTol = 1e-4f;

    void ExpectVecNear(const glm::vec3& a, const glm::vec3& b, float tol = kTol)
    {
        EXPECT_NEAR(a.x, b.x, tol);
        EXPECT_NEAR(a.y, b.y, tol);
        EXPECT_NEAR(a.z, b.z, tol);
    }

    bool IsRotation(const glm::mat3& r, float tol = 1e-3f)
    {
        const glm::mat3 shouldBeI = glm::transpose(r) * r;
        for (int c = 0; c < 3; ++c)
            for (int rr = 0; rr < 3; ++rr)
            {
                const float expected = (c == rr) ? 1.0f : 0.0f;
                if (std::abs(shouldBeI[c][rr] - expected) > tol) return false;
            }
        return std::abs(glm::determinant(r) - 1.0f) < tol;
    }
}

TEST(GeometryRotation, HatVeeInverse)
{
    const glm::vec3 w{0.3f, -1.2f, 0.7f};
    ExpectVecNear(Vee(Hat(w)), w);
    // Hat(w) * v == cross(w, v)
    const glm::vec3 v{2.0f, 0.5f, -1.0f};
    ExpectVecNear(Hat(w) * v, glm::cross(w, v));
}

TEST(GeometryRotation, ExpLogRoundTrip)
{
    const std::vector<glm::vec3> ws{
        {0.0f, 0.0f, 0.0f},
        {0.01f, 0.0f, 0.0f},
        {0.5f, -0.3f, 0.9f},
        {1.5f, 0.0f, 0.0f},
        {0.0f, 2.0f, 0.0f}};
    for (const glm::vec3& w : ws)
    {
        const glm::mat3 r = Exp(w);
        EXPECT_TRUE(IsRotation(r)) << "Exp(w) not a rotation for w=(" << w.x << "," << w.y << "," << w.z << ")";
        ExpectVecNear(Log(r), w, 1e-3f);
    }
}

TEST(GeometryRotation, LogExpRoundTripFromRandom)
{
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        const glm::mat3 r = RandomRotation(seed);
        ASSERT_TRUE(IsRotation(r));
        const glm::mat3 r2 = Exp(Log(r));
        // r2 should equal r (chordal distance ~ 0).
        EXPECT_LT(ChordalDistance(r, r2), 1e-3f);
    }
}

TEST(GeometryRotation, AngularDistanceMatchesAngle)
{
    const glm::vec3 axis = glm::normalize(glm::vec3{1.0f, 2.0f, -1.0f});
    for (float theta : {0.0f, 0.4f, 1.0f, 2.5f, 3.0f})
    {
        const glm::mat3 r = Exp(axis * theta);
        EXPECT_NEAR(AngularDistance(glm::mat3(1.0f), r), theta, 1e-3f);
    }
    // Distance to self is zero (acos near angle 0 is ill-conditioned with
    // float input, so allow the sqrt(2*eps) floor).
    const glm::mat3 r = RandomRotation(7);
    EXPECT_NEAR(AngularDistance(r, r), 0.0f, 1e-3f);
}

TEST(GeometryRotation, ChordalDistanceRelation)
{
    // ||I - R||_F = 2*sqrt(2)*sin(theta/2).
    const glm::vec3 axis = glm::normalize(glm::vec3{0.0f, 0.0f, 1.0f});
    const float theta = 1.2f;
    const glm::mat3 r = Exp(axis * theta);
    const float expected = 2.0f * std::sqrt(2.0f) * std::sin(theta / 2.0f);
    EXPECT_NEAR(ChordalDistance(glm::mat3(1.0f), r), expected, 1e-3f);
}

TEST(GeometryRotation, RandomRotationDeterministicAndValid)
{
    const glm::mat3 a = RandomRotation(42);
    const glm::mat3 b = RandomRotation(42);
    const glm::mat3 c = RandomRotation(43);
    EXPECT_LT(ChordalDistance(a, b), 1e-6f); // same seed -> identical
    EXPECT_GT(ChordalDistance(a, c), 1e-3f); // different seed -> different
    EXPECT_TRUE(IsRotation(a));
    EXPECT_TRUE(IsRotation(c));
}

TEST(GeometryRotation, ProjectOnSO3)
{
    const glm::mat3 r = RandomRotation(11);
    // Projecting a rotation returns (essentially) itself.
    EXPECT_LT(ChordalDistance(ProjectOnSO3(r), r), 1e-4f);
    // Projecting a scaled/perturbed matrix yields a valid rotation.
    glm::mat3 m = r;
    m[0] *= 1.3f;
    m[1] += glm::vec3(0.05f, -0.02f, 0.01f);
    EXPECT_TRUE(IsRotation(ProjectOnSO3(m)));
}

TEST(GeometryRotation, OptimalRotationRecoversKnown)
{
    const glm::mat3 r = RandomRotation(99);
    std::vector<glm::vec3> from{
        {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},
        {1.0f, 2.0f, 3.0f}, {-2.0f, 0.5f, 1.0f}, {0.3f, -1.0f, 2.0f}};
    std::vector<glm::vec3> to;
    for (const glm::vec3& f : from) to.push_back(r * f);

    const glm::mat3 recovered = OptimalRotation(from, to);
    EXPECT_TRUE(IsRotation(recovered));
    EXPECT_LT(AngularDistance(recovered, r), 1e-3f);
}

TEST(GeometryRotation, FailClosed)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    // Exp of non-finite -> identity.
    EXPECT_TRUE(IsRotation(Exp(glm::vec3{nan, 0.0f, 0.0f})));
    EXPECT_LT(ChordalDistance(Exp(glm::vec3{nan, 0.0f, 0.0f}), glm::mat3(1.0f)), 1e-5f);
    // Log of non-finite -> zero vector.
    glm::mat3 bad(1.0f);
    bad[0][0] = nan;
    ExpectVecNear(Log(bad), glm::vec3{0.0f});
    // OptimalRotation with empty / mismatched -> identity.
    std::vector<glm::vec3> a{{1.0f, 0.0f, 0.0f}};
    std::vector<glm::vec3> empty{};
    std::vector<glm::vec3> two{{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};
    EXPECT_LT(ChordalDistance(OptimalRotation(empty, empty), glm::mat3(1.0f)), 1e-5f);
    EXPECT_LT(ChordalDistance(OptimalRotation(a, two), glm::mat3(1.0f)), 1e-5f);
}
