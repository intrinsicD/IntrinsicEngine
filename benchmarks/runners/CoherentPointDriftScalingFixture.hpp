// Fixtures of the Coherent Point Drift scaling benchmarks (builtin.cpd_bumpy_ellipsoid_scaling.v1),
// shared by IntrinsicCoherentPointDriftScaling (METHOD-049) and the Vulkan E-step scaling
// profile (METHOD-056) so both measure the same point sets.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <random>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace CpdScaling
{
    struct Fixture
    {
        std::vector<glm::vec3> Source, Target, Truth;
    };

    inline glm::vec3 SurfacePoint(double u, double v)
    {
        // Bumpy ellipsoid: a closed 2-D surface, like a scanned object.
        const double r = 1.0 + 0.15 * std::sin(5.0 * u) * std::cos(4.0 * v);
        return glm::vec3(float(r * std::cos(u) * std::sin(v)), float(0.8 * r * std::sin(u) * std::sin(v)),
                         float(0.6 * r * std::cos(v)));
    }

    inline std::vector<glm::vec3> Surface(std::size_t count, std::uint32_t seed)
    {
        std::mt19937 random(seed);
        std::uniform_real_distribution<double> uniform(0.0, 1.0);
        std::vector<glm::vec3> points(count);
        for (auto& p : points)
            p = SurfacePoint(2.0 * std::numbers::pi * uniform(random), std::acos(1.0 - 2.0 * uniform(random)));
        return points;
    }

    inline Fixture RigidFixture(std::size_t count)
    {
        Fixture f;
        f.Source = Surface(count, 1234u + std::uint32_t(count));
        const glm::dmat3 rotation(glm::rotate(glm::dmat4(1.0), 0.35, glm::normalize(glm::dvec3(1.0, 2.0, 0.5))));
        const glm::dvec3 translation{0.3, -0.2, 0.1};
        std::mt19937 random(99u + std::uint32_t(count));
        std::normal_distribution<double> noise(0.0, 0.004);
        std::uniform_real_distribution<double> clutter(-1.5, 1.5);
        for (std::size_t i = 0; i < count; ++i)
        {
            const glm::dvec3 q = rotation * glm::dvec3(f.Source[i]) + translation;
            f.Truth.push_back(glm::vec3(q));
            f.Target.push_back(i % 20 == 0 ? glm::vec3(float(clutter(random)), float(clutter(random)), float(clutter(random)))
                                            : glm::vec3(q + glm::dvec3(noise(random), noise(random), noise(random))));
        }
        return f;
    }

    inline Fixture BentFixture(std::size_t count)
    {
        Fixture f;
        f.Source = Surface(count, 777u + std::uint32_t(count));
        for (const auto& p : f.Source)
        {
            const glm::vec3 q = p + glm::vec3(0.0f, 0.12f * std::sin(1.8f * p.x), 0.1f * std::cos(1.4f * p.y));
            f.Truth.push_back(q);
            f.Target.push_back(q);
        }
        return f;
    }

}
