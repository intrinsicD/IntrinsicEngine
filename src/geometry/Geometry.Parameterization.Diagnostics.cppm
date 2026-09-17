// Evaluates UV distortion on a mesh; copied diagnostics have a mesh-independent owner.
module;

#include <cstddef>
#include <span>

#include <glm/fwd.hpp>

export module Geometry.Parameterization.Diagnostics;

import Geometry.HalfedgeMesh;
export import Geometry.Parameterization.Types;

export namespace Geometry::Parameterization
{
    struct ParameterizationDiagnosticsOptions
    {
        double DegeneratePositionAreaEpsilon{1.0e-12};
        double DegenerateUvAreaEpsilon{1.0e-12};
        double SingularValueEpsilon{1.0e-12};
        double BoundaryPositionLengthEpsilon{1.0e-12};
        double BoundaryUvLengthEpsilon{1.0e-12};
    };

    [[nodiscard]] const char* ToString(ParameterizationDiagnosticsStatus status) noexcept;

    [[nodiscard]] ParameterizationDiagnostics EvaluateParameterizationDiagnostics(
        const HalfedgeMesh::Mesh& mesh,
        std::span<const glm::vec2> uvs,
        const ParameterizationDiagnosticsOptions& options = {});
} // namespace Geometry::Parameterization
