module;

#include <optional>
#include <span>
#include <glm/glm.hpp>

export module Geometry.Curve;

// Geometry.Curve — parametric Bézier curve evaluation.
//
// A Bézier curve of degree n-1 is defined by n control points and parameter
// t in [0, 1]. Evaluation is offered via de Casteljau (numerically stable
// recursive interpolation) and the Bernstein basis (closed form), plus the
// analytic first derivative (tangent).
//
// Fail-closed contract (GEOM-005 / GEOM-007): an empty control-point span, a
// non-finite t, or t outside [0, 1] returns std::nullopt; no NaN/Inf.
export namespace Geometry
{
    [[nodiscard]] std::optional<glm::vec3> EvaluateBezier(std::span<const glm::vec3> controlPoints, float t);

    [[nodiscard]] std::optional<glm::vec3> EvaluateBezierBernstein(std::span<const glm::vec3> controlPoints, float t);

    // Unnormalized first derivative (tangent) of the curve at t. For a single
    // control point the derivative is the zero vector.
    [[nodiscard]] std::optional<glm::vec3> EvaluateBezierDerivative(std::span<const glm::vec3> controlPoints, float t);
}
