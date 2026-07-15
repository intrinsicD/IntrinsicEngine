module;

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

export module Geometry.Parameterization.Bff;

import Geometry.HalfedgeMesh;
import Geometry.Parameterization.Diagnostics;

export namespace Geometry::Parameterization
{
    enum class BffBoundaryMode : std::uint8_t
    {
        AutomaticConformal,
        TargetLengths,
        TargetAngles,
    };

    enum class BffStatus : std::uint8_t
    {
        Success,
        EmptyMesh,
        InsufficientVertices,
        NotTriangleMesh,
        NotDiskTopology,
        DegenerateBoundary,
        NonFiniteGeometry,
        DegenerateGeometry,
        InvalidBoundaryMode,
        InvalidTolerance,
        MismatchedBoundaryArray,
        NonFiniteBoundaryData,
        NonPositiveTargetLength,
        InconsistentAngleSum,
        CurveClosureFailed,
        NonPositiveScaledLength,
        NonPositiveAdjustedLength,
        SingularSystem,
        SolverFailed,
        NonFiniteResult,
        UnusableDiagnostics,
    };

    struct BffParams
    {
        BffBoundaryMode Mode{BffBoundaryMode::AutomaticConformal};

        // Canonical boundary-loop order from MeshUtils::CollectBoundaryLoops.
        // TargetLengths: entry i belongs to edge (v_i, v_(i+1)).
        // TargetAngles: entry i is the orientation-normalized exterior angle
        // at v_i, in radians; a valid disk boundary sums to +2*pi.
        std::vector<double> BoundaryData{};

        double AngleSumTolerance{1.0e-8};
        // Dimensionless relative tolerance applied against local length/area
        // scales; automatic mode is invariant under uniform input scaling.
        double DegeneracyTolerance{1.0e-12};
    };

    struct BffDiagnostics
    {
        std::string_view Backend{"cpu_reference"};
        std::size_t BoundaryVertexCount{0};
        std::size_t InteriorVertexCount{0};
        std::size_t DirichletSolveCount{0};
        std::size_t NeumannSolveCount{0};

        double RequestedLengthRmsRelativeError{0.0};
        double RequestedLengthMaxRelativeError{0.0};
        double ClosureAdjustmentRmsRelative{0.0};
        double ClosureAdjustmentMaxRelative{0.0};

        double TargetAngleSum{0.0};
        double TargetAngleRmsError{0.0};
        double TargetAngleMaxError{0.0};

        ParameterizationDiagnostics Quality{};
    };

    struct BffResult
    {
        BffStatus Status{BffStatus::EmptyMesh};
        std::vector<glm::vec2> UVs{};
        BffDiagnostics Diagnostics{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == BffStatus::Success;
        }
    };

    [[nodiscard]] const char* ToString(BffStatus status) noexcept;

    [[nodiscard]] BffResult ComputeBFF(
        const HalfedgeMesh::Mesh& mesh,
        const BffParams& params = {});
} // namespace Geometry::Parameterization
