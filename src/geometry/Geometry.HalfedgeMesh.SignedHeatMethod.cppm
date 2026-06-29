module;

#include <cstddef>
#include <cstdint>
#include <span>

export module Geometry.SignedHeatMethod;

import Geometry.Properties;
import Geometry.HalfedgeMesh;
import Geometry.Sparse;

export namespace Geometry::SignedHeatMethod
{
    enum class SignedHeatStatus : std::uint8_t
    {
        Success = 0,
        DegenerateBoundaryInput,
        InvalidInput,
        OperatorAssemblyFailed,
        HeatFactorizationFailed,
        HeatSolveFailed,
        PoissonFactorizationFailed,
        PoissonSolveFailed,
        NonFiniteResult
    };

    struct SignedHeatParams
    {
        // If <= 0, uses mean edge length squared.
        double TimeStep{0.0};

        // Small positive mass shift used to remove the Poisson null space.
        double PoissonRegularization{1.0e-8};
    };

    struct SignedHeatDiagnostics
    {
        SignedHeatStatus Status{SignedHeatStatus::InvalidInput};
        std::size_t BoundaryHalfedgeCount{0};
        std::size_t SourceVertexCount{0};
        std::size_t InvalidBoundaryHalfedgeCount{0};
        std::size_t DegenerateBoundaryVertexCount{0};
        double MeanEdgeLength{0.0};
        double TimeStep{0.0};
        double MeanBoundaryOffset{0.0};
        double MaxAbsDistance{0.0};
        Geometry::Sparse::SparseFactorizationDiagnostics HeatFactorization{};
        Geometry::Sparse::SparseFactorizationDiagnostics PoissonFactorization{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SignedHeatStatus::Success
                || Status == SignedHeatStatus::DegenerateBoundaryInput;
        }

        [[nodiscard]] bool BoundaryDegenerate() const noexcept
        {
            return Status == SignedHeatStatus::DegenerateBoundaryInput
                || DegenerateBoundaryVertexCount > 0
                || InvalidBoundaryHalfedgeCount > 0;
        }
    };

    struct SignedHeatResult
    {
        VertexProperty<double> SignedDistanceProperty{};
        VertexProperty<bool> IsBoundarySourceProperty{};
        SignedHeatDiagnostics Diagnostics{};
    };

    [[nodiscard]] SignedHeatResult ComputeSignedDistance(
        HalfedgeMesh::Mesh& mesh,
        std::span<const HalfedgeHandle> boundary,
        const SignedHeatParams& params = {});
}
