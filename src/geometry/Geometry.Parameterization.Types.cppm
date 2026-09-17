// Copied parameterization status and quality diagnostics shared with editor results.
module;

#include <cstddef>
#include <cstdint>
#include <vector>

export module Geometry.Parameterization.Types;

export namespace Geometry::Parameterization
{
    enum class ParameterizationDiagnosticsStatus : std::uint8_t
    {
        Success = 0,
        EmptyInput,
        MissingUvCoordinates,
        PartialInvalidInput,
        NoEvaluatedFaces,
    };

    struct ParameterizationDiagnostics
    {
        ParameterizationDiagnosticsStatus Status{ParameterizationDiagnosticsStatus::EmptyInput};

        std::size_t VertexStorageCount{0};
        std::size_t FaceStorageCount{0};
        std::size_t LiveFaceCount{0};
        std::size_t EvaluatedFaceCount{0};
        std::size_t SkippedFaceCount{0};
        std::size_t DeletedFaceCount{0};
        std::size_t NonTriangleFaceCount{0};
        std::size_t DegeneratePositionFaceCount{0};
        std::size_t DegenerateUvFaceCount{0};
        std::size_t NonFinitePositionFaceCount{0};
        std::size_t NonFiniteUvFaceCount{0};
        std::size_t FlippedElementCount{0};

        double MeanConformalDistortion{0.0};
        double MaxConformalDistortion{0.0};
        double MeanConformalError{0.0};
        double RootMeanSquareConformalError{0.0};
        double MaxConformalError{0.0};

        /// Face-storage-aligned conformal distortion values. The vector size
        /// equals FaceStorageCount and each entry is indexed by
        /// FaceHandle::Index. Evaluated triangle entries contain the
        /// dimensionless singular-value ratio sigma_max / sigma_min (>= 1);
        /// deleted, skipped, and invalid faces contain quiet NaN.
        std::vector<float> FaceConformalDistortion{};

        double MeanAreaRatio{0.0};
        double MinAreaRatio{0.0};
        double MaxAreaRatio{0.0};
        double MeanAreaDistortion{0.0};
        double MaxAreaDistortion{0.0};
        double MeanAreaError{0.0};
        double MaxAreaError{0.0};

        double MeanSymmetricDirichletEnergy{0.0};
        double MaxSymmetricDirichletEnergy{0.0};
        double MeanSymmetricDirichletExcess{0.0};
        double MaxSymmetricDirichletExcess{0.0};

        double MeanStretch{0.0};
        double MaxStretch{0.0};
        double MeanStretchError{0.0};
        double MaxStretchError{0.0};

        std::size_t BoundaryLoopCount{0};
        std::size_t BoundaryEdgeCount{0};
        std::size_t SkippedBoundaryEdgeCount{0};
        double MeanBoundaryLengthRatio{0.0};
        double MinBoundaryLengthRatio{0.0};
        double MaxBoundaryLengthRatio{0.0};
        double MeanBoundaryLengthDistortion{0.0};
        double MaxBoundaryLengthDistortion{0.0};

        std::size_t SeamDiscontinuityCount{0};
        double MeanSeamDiscontinuity{0.0};
        double MaxSeamDiscontinuity{0.0};

        [[nodiscard]] bool HasUsableFaces() const noexcept { return EvaluatedFaceCount > 0u; }
        [[nodiscard]] bool HasInvalidInput() const noexcept
        {
            return NonTriangleFaceCount > 0u
                || DegeneratePositionFaceCount > 0u
                || DegenerateUvFaceCount > 0u
                || NonFinitePositionFaceCount > 0u
                || NonFiniteUvFaceCount > 0u;
        }
    };

    enum class ParameterizationStatus : std::uint8_t
    {
        Success,
        InvalidInput,
        SolverFailed,
    };

    // Unevaluated when no solver rejection was inspected or counts were unavailable.
    struct ParameterizationRejection
    {
        bool Evaluated{false};
        std::size_t ConnectedComponentCount{0u};
        std::size_t BoundaryLoopCount{0u};

        // True when the counts alone explain the rejection.
        [[nodiscard]] bool ViolatesDiskTopology() const noexcept
        {
            return Evaluated &&
                   (ConnectedComponentCount != 1u || BoundaryLoopCount != 1u);
        }
    };

}
