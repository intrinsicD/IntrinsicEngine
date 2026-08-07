#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/glm.hpp>

import Geometry.Sampling;
import Geometry.Sphere;
import Geometry.Sphere.Sampling;

namespace
{
    [[nodiscard]] Geometry::Sphere MakeSphere()
    {
        return Geometry::Sphere{
            .Center = glm::vec3{1.0F, -2.0F, 3.0F},
            .Radius = 2.5F};
    }

    void ExpectPointOnSphere(const glm::vec3& point, const Geometry::Sphere& sphere)
    {
        EXPECT_NEAR(glm::length(point - sphere.Center), sphere.Radius, 1.0e-5F);
    }

    void ExpectPointInSphere(const glm::vec3& point, const Geometry::Sphere& sphere)
    {
        EXPECT_LE(glm::length(point - sphere.Center), sphere.Radius + 1.0e-5F);
    }

    constexpr Geometry::Sampling::FibonacciLattice kAllLattices[] = {
        Geometry::Sampling::FLNAIVE,
        Geometry::Sampling::FLFIRST,
        Geometry::Sampling::FLSECOND,
        Geometry::Sampling::FLTHIRD,
        Geometry::Sampling::FLOFFSET};

    [[nodiscard]] glm::vec3 NorthPole(const Geometry::Sphere& sphere)
    {
        return glm::vec3{0.0F, 0.0F, 1.0F} * sphere.Radius + sphere.Center;
    }

    [[nodiscard]] glm::vec3 SouthPole(const Geometry::Sphere& sphere)
    {
        return glm::vec3{0.0F, 0.0F, -1.0F} * sphere.Radius + sphere.Center;
    }

    void ExpectAllPointsFiniteAndOnSphere(const std::vector<glm::vec3>& points,
                                          const Geometry::Sphere& sphere)
    {
        for (const glm::vec3& point : points)
        {
            ASSERT_TRUE(std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z));
            ExpectPointOnSphere(point, sphere);
        }
    }
}

TEST(Sampling, GaussianDisplacementIsDeterministicPerSeedAndElement)
{
    const glm::vec3 a = Geometry::Sampling::GaussianDisplacement(42, 7, 0.25F);
    const glm::vec3 b = Geometry::Sampling::GaussianDisplacement(42, 7, 0.25F);
    const glm::vec3 c = Geometry::Sampling::GaussianDisplacement(42, 8, 0.25F);
    const glm::vec3 d = Geometry::Sampling::GaussianDisplacement(43, 7, 0.25F);

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_NE(a, d);
}

TEST(SphereSampling, SurfaceRandomIsSeededAndDefaultDeterministic)
{
    const Geometry::Sphere sphere = MakeSphere();

    const std::vector<glm::vec3> defaultA = Geometry::Sampling::SampleSurfaceRandom(sphere, 32);
    const std::vector<glm::vec3> defaultB = Geometry::Sampling::SampleSurfaceRandom(sphere, 32);
    const std::vector<glm::vec3> seededA = Geometry::Sampling::SampleSurfaceRandom(sphere, 32, 123);
    const std::vector<glm::vec3> seededB = Geometry::Sampling::SampleSurfaceRandom(sphere, 32, 123);
    const std::vector<glm::vec3> seededC = Geometry::Sampling::SampleSurfaceRandom(sphere, 32, 124);

    ASSERT_EQ(defaultA.size(), 32u);
    EXPECT_EQ(defaultA, defaultB);
    EXPECT_EQ(seededA, seededB);
    EXPECT_NE(seededA, seededC);

    for (const glm::vec3& point : seededA)
    {
        ExpectPointOnSphere(point, sphere);
    }
}

TEST(SphereSampling, VolumeRandomAndUniformAreSeededAndBounded)
{
    const Geometry::Sphere sphere = MakeSphere();

    const std::vector<glm::vec3> randomA = Geometry::Sampling::SampleVolumeRandom(sphere, 64, 7);
    const std::vector<glm::vec3> randomB = Geometry::Sampling::SampleVolumeRandom(sphere, 64, 7);
    const std::vector<glm::vec3> randomC = Geometry::Sampling::SampleVolumeRandom(sphere, 64, 8);

    const std::vector<glm::vec3> uniformA = Geometry::Sampling::SampleVolumeUniform(sphere, 64, 7);
    const std::vector<glm::vec3> uniformB = Geometry::Sampling::SampleVolumeUniform(sphere, 64, 7);
    const std::vector<glm::vec3> uniformC = Geometry::Sampling::SampleVolumeUniform(sphere, 64, 8);

    EXPECT_EQ(randomA, randomB);
    EXPECT_NE(randomA, randomC);
    EXPECT_EQ(uniformA, uniformB);
    EXPECT_NE(uniformA, uniformC);

    for (const glm::vec3& point : randomA)
    {
        ExpectPointInSphere(point, sphere);
    }
    for (const glm::vec3& point : uniformA)
    {
        ExpectPointInSphere(point, sphere);
    }
}

TEST(SphereSampling, FibonacciLatticeSmallCountsAreSafeAndDeterministic)
{
    const Geometry::Sphere sphere = MakeSphere();

    for (const Geometry::Sampling::FibonacciLattice lattice : kAllLattices)
    {
        SCOPED_TRACE(testing::Message() << "lattice=" << static_cast<int>(lattice));

        for (const std::uint32_t count : {0u, 1u, 2u})
        {
            SCOPED_TRACE(testing::Message() << "count=" << count);

            const std::vector<glm::vec3> a =
                Geometry::Sampling::SampleSurfaceFibonacciLattice(sphere, count, lattice);
            const std::vector<glm::vec3> b =
                Geometry::Sampling::SampleSurfaceFibonacciLattice(sphere, count, lattice);

            ASSERT_EQ(a.size(), count);
            EXPECT_EQ(a, b);
            ExpectAllPointsFiniteAndOnSphere(a, sphere);

            if (count >= 1u)
            {
                EXPECT_EQ(a.front(), NorthPole(sphere));
            }
            if (count == 2u)
            {
                EXPECT_EQ(a.back(), SouthPole(sphere));
            }
        }
    }
}

TEST(SphereSampling, FibonacciLatticeLargerCountsStayOnSphereAndAreDeterministic)
{
    const Geometry::Sphere sphere = MakeSphere();

    for (const Geometry::Sampling::FibonacciLattice lattice : kAllLattices)
    {
        SCOPED_TRACE(testing::Message() << "lattice=" << static_cast<int>(lattice));

        for (const std::uint32_t count : {3u, 4u, 33u})
        {
            SCOPED_TRACE(testing::Message() << "count=" << count);

            const std::vector<glm::vec3> a =
                Geometry::Sampling::SampleSurfaceFibonacciLattice(sphere, count, lattice);
            const std::vector<glm::vec3> b =
                Geometry::Sampling::SampleSurfaceFibonacciLattice(sphere, count, lattice);

            ASSERT_EQ(a.size(), count);
            EXPECT_EQ(a, b);
            ExpectAllPointsFiniteAndOnSphere(a, sphere);

            const bool explicit_poles = lattice == Geometry::Sampling::FLTHIRD
                || lattice == Geometry::Sampling::FLOFFSET;
            if (explicit_poles)
            {
                EXPECT_EQ(a.front(), NorthPole(sphere));
                EXPECT_EQ(a.back(), SouthPole(sphere));
            }
        }
    }
}

TEST(SphereSampling, FibonacciLatticeDegenerateSpheresStayOnSphere)
{
    const Geometry::Sphere point_sphere{
        .Center = glm::vec3{-4.0F, 0.5F, 11.0F},
        .Radius = 0.0F};

    for (const Geometry::Sampling::FibonacciLattice lattice : kAllLattices)
    {
        SCOPED_TRACE(testing::Message() << "lattice=" << static_cast<int>(lattice));

        for (const std::uint32_t count : {0u, 1u, 2u, 3u, 9u})
        {
            SCOPED_TRACE(testing::Message() << "count=" << count);

            const std::vector<glm::vec3> points =
                Geometry::Sampling::SampleSurfaceFibonacciLattice(point_sphere, count, lattice);

            ASSERT_EQ(points.size(), count);
            for (const glm::vec3& p : points)
            {
                EXPECT_EQ(p, point_sphere.Center);
            }
        }
    }
}
