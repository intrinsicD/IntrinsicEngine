module;
#include <glm/glm.hpp>
#include <vector>
#include <optional>

export module Geometry:EPA;

import :GJK; // Requires access to Simplex
import :Support;
import :Primitives;

export namespace Geometry::Internal
{
    struct EPAResult
    {
        glm::vec3 Normal;
        float Depth;
        glm::vec3 ContactPoint; // On object A
    };

    // Computes penetration depth using Expanding Polytope Algorithm
    // simplex: The final simplex from GJK (must contain origin)
    template <typename ShapeA, typename ShapeB>
    std::optional<EPAResult> EPA_Solver(
        const ShapeA& a, 
        const ShapeB& b, 
        const Simplex& gjkSimplex);
}