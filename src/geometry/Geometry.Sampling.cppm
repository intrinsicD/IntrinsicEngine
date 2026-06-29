module;

#include <cstdint>

#include <glm/glm.hpp>

export module Geometry.Sampling;

export namespace Geometry::Sampling
{
    [[nodiscard]] std::uint64_t MixSeed(std::uint64_t value) noexcept;

    [[nodiscard]] glm::vec3 GaussianDisplacement(std::uint64_t seed,
                                                 std::uint32_t elementIndex,
                                                 float standardDeviation);

    [[nodiscard]] glm::vec3 UniformUnitVectorFromUnitSamples(double zSample,
                                                             double azimuthSample);

    [[nodiscard]] glm::vec3 UniformUnitBallPointFromUnitSamples(double radiusSample,
                                                                double zSample,
                                                                double azimuthSample);

    [[nodiscard]] glm::vec3 UniformUnitVector(std::uint64_t seed,
                                              std::uint32_t elementIndex);

    [[nodiscard]] glm::vec3 UniformUnitBallPoint(std::uint64_t seed,
                                                 std::uint32_t elementIndex);
}
