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

    // Returns exactly `numPoints` points, each on `sphere`, for any valid sphere
    // (finite center, finite non-negative radius). The result is deterministic:
    // equal arguments always yield equal output.
    //
    // Small counts share one contract across every lattice value, because below
    // three samples there are not enough interior slots for the lattice formula:
    //   0 -> empty
    //   1 -> the north pole
    //   2 -> the north pole then the south pole
    //
    // For three or more samples, FLTHIRD and FLOFFSET place the north pole at
    // index 0 and the south pole at index `numPoints - 1`, and fill only the
    // interior indices from the lattice formula. The remaining lattices fill
    // every index from the formula.
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
