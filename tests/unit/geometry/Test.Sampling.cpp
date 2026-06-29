#include <gtest/gtest.h>

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
