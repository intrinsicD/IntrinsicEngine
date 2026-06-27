module;

#ifndef EIGEN_MPL2_ONLY
#define EIGEN_MPL2_ONLY
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Dense>
#include <glm/glm.hpp>

export module Geometry.Linalg;

export namespace Geometry::Linalg
{
    enum class NumericStatus : std::uint8_t
    {
        Success = 0,
        InvalidInput,
        RankDeficient,
        IllConditioned,
        NonFinite,
        NoConvergence
    };

    struct NumericDiagnostics
    {
        NumericStatus Status{NumericStatus::InvalidInput};
        double ResidualNorm{0.0};
        double RelativeResidual{0.0};
        double ConditionEstimate{0.0};
        std::size_t Rank{0};
        std::size_t Iterations{0};

        [[nodiscard]] bool Succeeded() const noexcept { return Status == NumericStatus::Success; }
    };

    struct DenseMatrix
    {
        std::size_t Rows{0};
        std::size_t Cols{0};
        std::vector<double> Values;

        DenseMatrix() = default;
        DenseMatrix(std::size_t rows, std::size_t cols);

        [[nodiscard]] bool IsShapeValid() const noexcept { return Values.size() == Rows * Cols; }
        [[nodiscard]] double operator()(std::size_t row, std::size_t col) const;
        [[nodiscard]] double& operator()(std::size_t row, std::size_t col);
    };

    struct SVDResult
    {
        DenseMatrix U{};
        DenseMatrix Vt{};
        std::vector<double> SingularValues;
        NumericDiagnostics Diagnostics{};
    };

    struct QRResult
    {
        DenseMatrix Q{};
        DenseMatrix R{};
        NumericDiagnostics Diagnostics{};
    };

    struct SymmetricEigenResult
    {
        DenseMatrix Eigenvectors{};
        std::vector<double> Eigenvalues;
        NumericDiagnostics Diagnostics{};
    };

    struct PolarDecompositionResult
    {
        DenseMatrix Orthogonal{};
        DenseMatrix Symmetric{};
        NumericDiagnostics Diagnostics{};
    };

    struct LeastSquaresResult
    {
        std::vector<double> X;
        NumericDiagnostics Diagnostics{};
    };

    struct CovarianceResult
    {
        glm::dvec3 Mean{0.0};
        glm::dmat3 Covariance{0.0};
        std::size_t Count{0};
        bool Valid{false};
    };

    struct CovarianceAccumulator
    {
        void Reset() noexcept;
        void Add(glm::dvec3 sample) noexcept;
        [[nodiscard]] CovarianceResult Result() const noexcept;

    private:
        std::size_t Count_{0};
        glm::dvec3 Mean_{0.0};
        glm::dmat3 M2_{0.0};
    };

    using EigenVector2d = Eigen::Matrix<double, 2, 1>;
    using EigenVector3d = Eigen::Matrix<double, 3, 1>;
    using EigenVector4d = Eigen::Matrix<double, 4, 1>;
    using EigenMatrix3d = Eigen::Matrix<double, 3, 3>;
    using EigenMatrix4d = Eigen::Matrix<double, 4, 4>;
    using EigenRowMajorMatrixXd = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>;
    using EigenDenseMap = Eigen::Map<EigenRowMajorMatrixXd, Eigen::Unaligned>;
    using ConstEigenDenseMap = Eigen::Map<const EigenRowMajorMatrixXd, Eigen::Unaligned>;

    [[nodiscard]] EigenVector2d ToEigen(glm::dvec2 value) noexcept;
    [[nodiscard]] EigenVector3d ToEigen(glm::dvec3 value) noexcept;
    [[nodiscard]] EigenVector4d ToEigen(glm::dvec4 value) noexcept;
    [[nodiscard]] EigenMatrix3d ToEigen(const glm::dmat3& value) noexcept;
    [[nodiscard]] EigenMatrix4d ToEigen(const glm::dmat4& value) noexcept;

    [[nodiscard]] glm::dvec2 ToGlm(const EigenVector2d& value) noexcept;
    [[nodiscard]] glm::dvec3 ToGlm(const EigenVector3d& value) noexcept;
    [[nodiscard]] glm::dvec4 ToGlm(const EigenVector4d& value) noexcept;
    [[nodiscard]] glm::dmat3 ToGlm(const EigenMatrix3d& value) noexcept;
    [[nodiscard]] glm::dmat4 ToGlm(const EigenMatrix4d& value) noexcept;

    // Maps are row-major, unaligned views over contiguous scalar buffers. The
    // caller owns the buffer and must keep it alive for the lifetime of the map.
    [[nodiscard]] EigenDenseMap MapRowMajorMatrix(std::span<double> values,
                                                  std::size_t rows,
                                                  std::size_t cols);
    [[nodiscard]] ConstEigenDenseMap MapRowMajorMatrix(std::span<const double> values,
                                                       std::size_t rows,
                                                       std::size_t cols);

    // View a contiguous array of glm::dvec3 as an N x 3 row-major Eigen matrix
    // without copying (glm::dvec3 is three tightly-packed doubles). The caller
    // owns the storage and must keep it alive for the lifetime of the map. An
    // empty span yields a 0 x 3 map.
    [[nodiscard]] EigenDenseMap MapVec3Array(std::span<glm::dvec3> values);
    [[nodiscard]] ConstEigenDenseMap MapVec3Array(std::span<const glm::dvec3> values);

    [[nodiscard]] SVDResult ComputeSVD(const DenseMatrix& matrix, double tolerance = 1e-12);
    [[nodiscard]] QRResult ComputeQR(const DenseMatrix& matrix, double tolerance = 1e-12);
    [[nodiscard]] SymmetricEigenResult ComputeSymmetricEigen(const DenseMatrix& matrix, double tolerance = 1e-12);
    [[nodiscard]] PolarDecompositionResult ComputePolarDecomposition(const DenseMatrix& matrix, double tolerance = 1e-12);
    [[nodiscard]] LeastSquaresResult SolveLeastSquares(const DenseMatrix& matrix,
                                                       std::span<const double> rhs,
                                                       double tolerance = 1e-12);
}

