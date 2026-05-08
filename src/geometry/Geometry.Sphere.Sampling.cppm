module;

#include <vector>
#include <glm/glm.hpp>

export module Geometry.SphereSampling;

import Geometry.Sphere;

export namespace Geometry::Sampling
{
    [[nodiscard]] std::vector<glm::vec3> SampleSurfaceRandom(const Sphere& sphere, uint32_t numPoints);

    enum FibonacciLattice {
        FLNAIVE,    // No offset; samples are placed directly from i = 0 to N−1.
        FLFIRST,    // Offset by 0.5 to avoid including exact poles, improving uniformity.
        FLSECOND,   // Offset by 1.5 and adjust total count by 2*offset for a different distribution.
        FLTHIRD,    // Offset by 3.5, skip first and last index to place poles explicitly.
        FLOFFSET    // Use a small epsilon (~0.36) to fine‐tune pole spacing.
    };

    [[nodiscard]] std::vector<glm::vec3> SampleSurfaceFibonacciLattice(const Sphere& sphere, uint32_t numPoints, FibonacciLattice lattice);

    [[nodiscard]] std::vector<glm::vec3> SampleVolumeRandom(const Sphere& sphere, uint32_t numPoints);

    [[nodiscard]] std::vector<glm::vec3> SampleVolumeUniform(const Sphere& sphere, uint32_t numPoints);


}