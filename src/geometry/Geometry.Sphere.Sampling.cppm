module;

#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

export module Geometry.Sphere.Sampling;

import Geometry.Sphere;

export namespace Geometry::Sampling
{
    inline constexpr std::uint64_t kDefaultSphereSamplingSeed = 0;

    [[nodiscard]] std::vector<glm::vec3> SampleSurfaceRandom(const Sphere& sphere, std::uint32_t numPoints);
    [[nodiscard]] std::vector<glm::vec3> SampleSurfaceRandom(const Sphere& sphere,
                                                             std::uint32_t numPoints,
                                                             std::uint64_t seed);

    enum FibonacciLattice {
        FLNAIVE,    // No offset; samples are placed directly from i = 0 to N−1.
        FLFIRST,    // Offset by 0.5 to avoid including exact poles, improving uniformity.
        FLSECOND,   // Offset by 1.5 and adjust total count by 2*offset for a different distribution.
        FLTHIRD,    // Offset by 3.5, skip first and last index to place poles explicitly.
        FLOFFSET    // Use a small epsilon (~0.36) to fine‐tune pole spacing.
    };

    [[nodiscard]] std::vector<glm::vec3> SampleSurfaceFibonacciLattice(const Sphere& sphere,
                                                                       std::uint32_t numPoints,
                                                                       FibonacciLattice lattice);

    [[nodiscard]] std::vector<glm::vec3> SampleVolumeRandom(const Sphere& sphere, std::uint32_t numPoints);
    [[nodiscard]] std::vector<glm::vec3> SampleVolumeRandom(const Sphere& sphere,
                                                            std::uint32_t numPoints,
                                                            std::uint64_t seed);

    [[nodiscard]] std::vector<glm::vec3> SampleVolumeUniform(const Sphere& sphere, std::uint32_t numPoints);
    [[nodiscard]] std::vector<glm::vec3> SampleVolumeUniform(const Sphere& sphere,
                                                             std::uint32_t numPoints,
                                                             std::uint64_t seed);


}
