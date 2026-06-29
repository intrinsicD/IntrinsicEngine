module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <random>

#include <glm/glm.hpp>

module Geometry.Sampling;

namespace Geometry::Sampling
{
    namespace
    {
        inline constexpr std::uint64_t kElementSalt = 0x9e3779b97f4a7c15ULL;
    }

    std::uint64_t MixSeed(std::uint64_t value) noexcept
    {
        value ^= value >> 30U;
        value *= 0xbf58476d1ce4e5b9ULL;
        value ^= value >> 27U;
        value *= 0x94d049bb133111ebULL;
        value ^= value >> 31U;
        return value;
    }

    glm::vec3 GaussianDisplacement(std::uint64_t seed,
                                   std::uint32_t elementIndex,
                                   float standardDeviation)
    {
        std::mt19937_64 rng(MixSeed(seed ^ (static_cast<std::uint64_t>(elementIndex) + kElementSalt)));
        std::normal_distribution<float> normal(0.0F, standardDeviation);
        return glm::vec3(normal(rng), normal(rng), normal(rng));
    }

    glm::vec3 UniformUnitVectorFromUnitSamples(double zSample, double azimuthSample)
    {
        const double z = 1.0 - 2.0 * zSample;
        const double r = std::sqrt(std::max(0.0, 1.0 - z * z));
        const double theta = 2.0 * std::numbers::pi_v<double> * azimuthSample;

        return glm::vec3{
            static_cast<float>(r * std::cos(theta)),
            static_cast<float>(r * std::sin(theta)),
            static_cast<float>(z)};
    }

    glm::vec3 UniformUnitBallPointFromUnitSamples(double radiusSample,
                                                  double zSample,
                                                  double azimuthSample)
    {
        return static_cast<float>(std::cbrt(radiusSample))
            * UniformUnitVectorFromUnitSamples(zSample, azimuthSample);
    }

    glm::vec3 UniformUnitVector(std::uint64_t seed, std::uint32_t elementIndex)
    {
        std::mt19937_64 rng(MixSeed(seed ^ (static_cast<std::uint64_t>(elementIndex) + kElementSalt)));
        std::uniform_real_distribution<double> unit(0.0, 1.0);

        const double zSample = unit(rng);
        const double azimuthSample = unit(rng);
        return UniformUnitVectorFromUnitSamples(zSample, azimuthSample);
    }

    glm::vec3 UniformUnitBallPoint(std::uint64_t seed, std::uint32_t elementIndex)
    {
        std::mt19937_64 rng(MixSeed(seed ^ (static_cast<std::uint64_t>(elementIndex) + kElementSalt)));
        std::uniform_real_distribution<double> unit(0.0, 1.0);

        const double radiusSample = unit(rng);
        const double zSample = unit(rng);
        const double azimuthSample = unit(rng);
        return UniformUnitBallPointFromUnitSamples(radiusSample, zSample, azimuthSample);
    }
}
