module;

#ifndef EIGEN_MPL2_ONLY
#define EIGEN_MPL2_ONLY
#endif

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

#include <Eigen/Core>
#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <Eigen/QR>
#include <Eigen/SVD>
#include <glm/glm.hpp>

module Geometry.Linalg;

namespace Geometry::Linalg
{
    namespace
    {
        using EigenColMajorMatrixXd = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic>;

        [[nodiscard]] bool IsFinite(const DenseMatrix& matrix)
        {
            if (!matrix.IsShapeValid())
            {
                return false;
            }
            for (const double value : matrix.Values)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool IsFinite(std::span<const double> values)
        {
            for (const double value : values)
            {
                if (!std::isfinite(value))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] EigenRowMajorMatrixXd ToEigenMatrix(const DenseMatrix& matrix)
        {
            EigenRowMajorMatrixXd result(matrix.Rows, matrix.Cols);
            for (std::size_t row = 0; row < matrix.Rows; ++row)
            {
                for (std::size_t col = 0; col < matrix.Cols; ++col)
                {
                    result(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(col)) = matrix(row, col);
                }
            }
            return result;
        }

        [[nodiscard]] DenseMatrix FromEigenMatrix(const Eigen::Ref<const EigenColMajorMatrixXd>& matrix)
        {
            DenseMatrix result(static_cast<std::size_t>(matrix.rows()), static_cast<std::size_t>(matrix.cols()));
            for (Eigen::Index row = 0; row < matrix.rows(); ++row)
            {
                for (Eigen::Index col = 0; col < matrix.cols(); ++col)
                {
                    result(static_cast<std::size_t>(row), static_cast<std::size_t>(col)) = matrix(row, col);
                }
            }
            return result;
        }


        [[nodiscard]] double ConditionFromSingularValues(const Eigen::VectorXd& singularValues, double tolerance)
        {
            if (singularValues.size() == 0)
            {
                return 0.0;
            }
            const double maxValue = singularValues.array().abs().maxCoeff();
            double minPositive = std::numeric_limits<double>::infinity();
            for (Eigen::Index i = 0; i < singularValues.size(); ++i)
            {
                const double value = std::abs(singularValues[i]);
                if (value > tolerance)
                {
                    minPositive = std::min(minPositive, value);
                }
            }
            if (!std::isfinite(minPositive) || minPositive <= tolerance)
            {
                return std::numeric_limits<double>::infinity();
            }
            return maxValue / minPositive;
        }

        [[nodiscard]] std::size_t RankFromSingularValues(const Eigen::VectorXd& singularValues, double tolerance)
        {
            std::size_t rank = 0;
            for (Eigen::Index i = 0; i < singularValues.size(); ++i)
            {
                if (std::abs(singularValues[i]) > tolerance)
                {
                    ++rank;
                }
            }
            return rank;
        }

        [[nodiscard]] bool IsHardSvdFailure(NumericStatus status) noexcept
        {
            return status == NumericStatus::InvalidInput ||
                   status == NumericStatus::IllConditioned ||
                   status == NumericStatus::NonFinite ||
                   status == NumericStatus::NoConvergence;
        }

        [[nodiscard]] double FrobeniusNorm(const DenseMatrix& matrix)
        {
            double sum = 0.0;
            for (const double value : matrix.Values)
            {
                sum += value * value;
            }
            return std::sqrt(sum);
        }

        [[nodiscard]] DenseMatrix MakeResidual(const DenseMatrix& matrix,
                                               const DenseMatrix& lowRank,
                                               const DenseMatrix& sparse)
        {
            DenseMatrix residual(matrix.Rows, matrix.Cols);
            for (std::size_t i = 0; i < matrix.Values.size(); ++i)
            {
                residual.Values[i] = matrix.Values[i] - lowRank.Values[i] - sparse.Values[i];
            }
            return residual;
        }

        [[nodiscard]] DenseMatrix MakeLowRankArgument(const DenseMatrix& matrix,
                                                      const DenseMatrix& sparse,
                                                      const DenseMatrix& dual,
                                                      double mu)
        {
            DenseMatrix result(matrix.Rows, matrix.Cols);
            const double invMu = 1.0 / mu;
            for (std::size_t i = 0; i < matrix.Values.size(); ++i)
            {
                result.Values[i] = matrix.Values[i] - sparse.Values[i] + dual.Values[i] * invMu;
            }
            return result;
        }

        [[nodiscard]] DenseMatrix MakeSparseArgument(const DenseMatrix& matrix,
                                                     const DenseMatrix& lowRank,
                                                     const DenseMatrix& dual,
                                                     double mu)
        {
            DenseMatrix result(matrix.Rows, matrix.Cols);
            const double invMu = 1.0 / mu;
            for (std::size_t i = 0; i < matrix.Values.size(); ++i)
            {
                result.Values[i] = matrix.Values[i] - lowRank.Values[i] + dual.Values[i] * invMu;
            }
            return result;
        }

        [[nodiscard]] double SoftThreshold(double value, double threshold) noexcept
        {
            if (value > threshold)
            {
                return value - threshold;
            }
            if (value < -threshold)
            {
                return value + threshold;
            }
            return 0.0;
        }

        [[nodiscard]] DenseMatrix SoftThresholdElements(const DenseMatrix& matrix, double threshold)
        {
            DenseMatrix result(matrix.Rows, matrix.Cols);
            for (std::size_t i = 0; i < matrix.Values.size(); ++i)
            {
                result.Values[i] = SoftThreshold(matrix.Values[i], threshold);
            }
            return result;
        }

        [[nodiscard]] DenseMatrix SoftThresholdSingularValues(const SVDResult& svd,
                                                              double threshold,
                                                              double rankTolerance,
                                                              std::size_t& rank)
        {
            DenseMatrix result(svd.U.Rows, svd.Vt.Cols);
            rank = 0;
            for (std::size_t k = 0; k < svd.SingularValues.size(); ++k)
            {
                const double sigma = svd.SingularValues[k] - threshold;
                if (!(sigma > 0.0))
                {
                    continue;
                }
                if (sigma > rankTolerance)
                {
                    ++rank;
                }
                for (std::size_t row = 0; row < result.Rows; ++row)
                {
                    const double u = svd.U(row, k);
                    for (std::size_t col = 0; col < result.Cols; ++col)
                    {
                        result(row, col) += sigma * u * svd.Vt(k, col);
                    }
                }
            }
            return result;
        }

        void AddScaled(DenseMatrix& target, const DenseMatrix& source, double scale)
        {
            for (std::size_t i = 0; i < target.Values.size(); ++i)
            {
                target.Values[i] += source.Values[i] * scale;
            }
        }

    }

    DenseMatrix::DenseMatrix(std::size_t rows, std::size_t cols)
        : Rows(rows)
        , Cols(cols)
        , Values(rows * cols, 0.0)
    {
    }

    double DenseMatrix::operator()(std::size_t row, std::size_t col) const
    {
        assert(row < Rows && col < Cols);
        return Values[row * Cols + col];
    }

    double& DenseMatrix::operator()(std::size_t row, std::size_t col)
    {
        assert(row < Rows && col < Cols);
        return Values[row * Cols + col];
    }

    void CovarianceAccumulator::Reset() noexcept
    {
        Count_ = 0;
        Mean_ = glm::dvec3{0.0};
        M2_ = glm::dmat3{0.0};
    }

    void CovarianceAccumulator::Add(glm::dvec3 sample) noexcept
    {
        ++Count_;
        const glm::dvec3 delta = sample - Mean_;
        Mean_ += delta / static_cast<double>(Count_);
        const glm::dvec3 delta2 = sample - Mean_;
        for (int col = 0; col < 3; ++col)
        {
            for (int row = 0; row < 3; ++row)
            {
                M2_[col][row] += delta[row] * delta2[col];
            }
        }
    }

    CovarianceResult CovarianceAccumulator::Result() const noexcept
    {
        CovarianceResult result;
        result.Mean = Mean_;
        result.Count = Count_;
        result.Valid = Count_ > 1;
        if (result.Valid)
        {
            result.Covariance = M2_ / static_cast<double>(Count_ - 1);
        }
        return result;
    }

    EigenVector2d ToEigen(glm::dvec2 value) noexcept { return {value.x, value.y}; }
    EigenVector3d ToEigen(glm::dvec3 value) noexcept { return {value.x, value.y, value.z}; }
    EigenVector4d ToEigen(glm::dvec4 value) noexcept { return {value.x, value.y, value.z, value.w}; }

    EigenMatrix3d ToEigen(const glm::dmat3& value) noexcept
    {
        EigenMatrix3d result;
        for (int col = 0; col < 3; ++col)
        {
            for (int row = 0; row < 3; ++row)
            {
                result(row, col) = value[col][row];
            }
        }
        return result;
    }

    EigenMatrix4d ToEigen(const glm::dmat4& value) noexcept
    {
        EigenMatrix4d result;
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                result(row, col) = value[col][row];
            }
        }
        return result;
    }

    glm::dvec2 ToGlm(const EigenVector2d& value) noexcept { return {value.x(), value.y()}; }
    glm::dvec3 ToGlm(const EigenVector3d& value) noexcept { return {value.x(), value.y(), value.z()}; }
    glm::dvec4 ToGlm(const EigenVector4d& value) noexcept { return {value.x(), value.y(), value.z(), value.w()}; }

    glm::dmat3 ToGlm(const EigenMatrix3d& value) noexcept
    {
        glm::dmat3 result{1.0};
        for (int col = 0; col < 3; ++col)
        {
            for (int row = 0; row < 3; ++row)
            {
                result[col][row] = value(row, col);
            }
        }
        return result;
    }

    glm::dmat4 ToGlm(const EigenMatrix4d& value) noexcept
    {
        glm::dmat4 result{1.0};
        for (int col = 0; col < 4; ++col)
        {
            for (int row = 0; row < 4; ++row)
            {
                result[col][row] = value(row, col);
            }
        }
        return result;
    }

    EigenDenseMap MapRowMajorMatrix(std::span<double> values, std::size_t rows, std::size_t cols)
    {
        assert(values.size() >= rows * cols);
        return EigenDenseMap(values.data(), static_cast<Eigen::Index>(rows), static_cast<Eigen::Index>(cols));
    }

    ConstEigenDenseMap MapRowMajorMatrix(std::span<const double> values, std::size_t rows, std::size_t cols)
    {
        assert(values.size() >= rows * cols);
        return ConstEigenDenseMap(values.data(), static_cast<Eigen::Index>(rows), static_cast<Eigen::Index>(cols));
    }

    StridedEigenMap<double> MapAsMatrix(std::span<double> values,
                                        std::size_t rows,
                                        std::size_t cols,
                                        std::ptrdiff_t strideElements)
    {
        if (!Detail::IsValidStridedExtent(values.size(), rows, cols, strideElements))
        {
            return Detail::EmptyStridedMap<double>();
        }
        return StridedEigenMap<double>(values.data(),
                                       static_cast<Eigen::Index>(rows),
                                       static_cast<Eigen::Index>(cols),
                                       EigenDynamicStride(static_cast<Eigen::Index>(strideElements), 1));
    }

    ConstStridedEigenMap<double> MapAsMatrix(std::span<const double> values,
                                             std::size_t rows,
                                             std::size_t cols,
                                             std::ptrdiff_t strideElements)
    {
        if (!Detail::IsValidStridedExtent(values.size(), rows, cols, strideElements))
        {
            return Detail::EmptyConstStridedMap<double>();
        }
        return ConstStridedEigenMap<double>(values.data(),
                                            static_cast<Eigen::Index>(rows),
                                            static_cast<Eigen::Index>(cols),
                                            EigenDynamicStride(static_cast<Eigen::Index>(strideElements), 1));
    }

    EigenDenseMap MapVec3Array(std::span<glm::dvec3> values)
    {
        return EigenDenseMap(reinterpret_cast<double*>(values.data()),
                             static_cast<Eigen::Index>(values.size()), 3);
    }

    ConstEigenDenseMap MapVec3Array(std::span<const glm::dvec3> values)
    {
        return ConstEigenDenseMap(reinterpret_cast<const double*>(values.data()),
                                  static_cast<Eigen::Index>(values.size()), 3);
    }

    SVDResult ComputeSVD(const DenseMatrix& matrix, double tolerance)
    {
        SVDResult result;
        if (!IsFinite(matrix))
        {
            result.Diagnostics.Status = NumericStatus::InvalidInput;
            return result;
        }

        const EigenRowMajorMatrixXd A = ToEigenMatrix(matrix);
        Eigen::JacobiSVD<EigenRowMajorMatrixXd> svd(A, Eigen::ComputeFullU | Eigen::ComputeFullV);
        const Eigen::VectorXd singularValues = svd.singularValues();

        result.U = FromEigenMatrix(svd.matrixU());
        result.Vt = FromEigenMatrix(svd.matrixV().transpose());
        result.SingularValues.assign(singularValues.data(), singularValues.data() + singularValues.size());
        result.Diagnostics.Status = NumericStatus::Success;
        result.Diagnostics.Rank = RankFromSingularValues(singularValues, tolerance);
        result.Diagnostics.ConditionEstimate = ConditionFromSingularValues(singularValues, tolerance);
        if (result.Diagnostics.Rank < std::min(matrix.Rows, matrix.Cols))
        {
            result.Diagnostics.Status = NumericStatus::RankDeficient;
        }
        return result;
    }

    QRResult ComputeQR(const DenseMatrix& matrix, double tolerance)
    {
        QRResult result;
        if (!IsFinite(matrix))
        {
            result.Diagnostics.Status = NumericStatus::InvalidInput;
            return result;
        }

        const EigenRowMajorMatrixXd A = ToEigenMatrix(matrix);
        Eigen::HouseholderQR<EigenRowMajorMatrixXd> qr(A);
        const EigenColMajorMatrixXd q = qr.householderQ() * EigenColMajorMatrixXd::Identity(A.rows(), A.rows());
        const EigenColMajorMatrixXd r = qr.matrixQR().template triangularView<Eigen::Upper>();

        result.Q = FromEigenMatrix(q);
        result.R = FromEigenMatrix(r.topRows(A.rows()));
        result.Diagnostics.Status = NumericStatus::Success;
        result.Diagnostics.Rank = static_cast<std::size_t>((r.diagonal().array().abs() > tolerance).count());
        if (result.Diagnostics.Rank < std::min(matrix.Rows, matrix.Cols))
        {
            result.Diagnostics.Status = NumericStatus::RankDeficient;
        }
        return result;
    }

    SymmetricEigenResult ComputeSymmetricEigen(const DenseMatrix& matrix, double tolerance)
    {
        SymmetricEigenResult result;
        if (!IsFinite(matrix) || matrix.Rows != matrix.Cols)
        {
            result.Diagnostics.Status = NumericStatus::InvalidInput;
            return result;
        }

        const EigenRowMajorMatrixXd A = ToEigenMatrix(matrix);
        const double symmetryError = (A - A.transpose()).norm();
        if (symmetryError > tolerance)
        {
            result.Diagnostics.Status = NumericStatus::InvalidInput;
            result.Diagnostics.ResidualNorm = symmetryError;
            return result;
        }

        Eigen::SelfAdjointEigenSolver<EigenRowMajorMatrixXd> solver(A);
        if (solver.info() != Eigen::Success)
        {
            result.Diagnostics.Status = NumericStatus::NoConvergence;
            return result;
        }

        result.Eigenvectors = FromEigenMatrix(solver.eigenvectors());
        result.Eigenvalues.assign(solver.eigenvalues().data(), solver.eigenvalues().data() + solver.eigenvalues().size());
        result.Diagnostics.Status = NumericStatus::Success;
        result.Diagnostics.Rank = static_cast<std::size_t>((solver.eigenvalues().array().abs() > tolerance).count());
        return result;
    }

    PolarDecompositionResult ComputePolarDecomposition(const DenseMatrix& matrix, double tolerance)
    {
        PolarDecompositionResult result;
        if (!IsFinite(matrix) || matrix.Rows != matrix.Cols)
        {
            result.Diagnostics.Status = NumericStatus::InvalidInput;
            return result;
        }

        const EigenRowMajorMatrixXd A = ToEigenMatrix(matrix);
        Eigen::JacobiSVD<EigenRowMajorMatrixXd> svd(A, Eigen::ComputeFullU | Eigen::ComputeFullV);
        const EigenColMajorMatrixXd U = svd.matrixU() * svd.matrixV().transpose();
        const EigenColMajorMatrixXd P = svd.matrixV() * svd.singularValues().asDiagonal() * svd.matrixV().transpose();

        result.Orthogonal = FromEigenMatrix(U);
        result.Symmetric = FromEigenMatrix(P);
        result.Diagnostics.Status = NumericStatus::Success;
        result.Diagnostics.Rank = RankFromSingularValues(svd.singularValues(), tolerance);
        result.Diagnostics.ConditionEstimate = ConditionFromSingularValues(svd.singularValues(), tolerance);
        result.Diagnostics.ResidualNorm = (A - U * P).norm();
        result.Diagnostics.RelativeResidual = result.Diagnostics.ResidualNorm / std::max(A.norm(), 1.0);
        if (result.Diagnostics.Rank < matrix.Rows)
        {
            result.Diagnostics.Status = NumericStatus::RankDeficient;
        }
        return result;
    }

    LeastSquaresResult SolveLeastSquares(const DenseMatrix& matrix, std::span<const double> rhs, double tolerance)
    {
        LeastSquaresResult result;
        if (!IsFinite(matrix) || rhs.size() < matrix.Rows || !IsFinite(rhs.first(matrix.Rows)))
        {
            result.Diagnostics.Status = NumericStatus::InvalidInput;
            return result;
        }

        const EigenRowMajorMatrixXd A = ToEigenMatrix(matrix);
        Eigen::VectorXd b(static_cast<Eigen::Index>(matrix.Rows));
        for (std::size_t row = 0; row < matrix.Rows; ++row)
        {
            b[static_cast<Eigen::Index>(row)] = rhs[row];
        }

        Eigen::CompleteOrthogonalDecomposition<EigenRowMajorMatrixXd> cod(A);
        cod.setThreshold(tolerance);
        const Eigen::VectorXd x = cod.solve(b);
        result.X.assign(x.data(), x.data() + x.size());
        result.Diagnostics.Rank = static_cast<std::size_t>(cod.rank());
        result.Diagnostics.ResidualNorm = (A * x - b).norm();
        result.Diagnostics.RelativeResidual = result.Diagnostics.ResidualNorm / std::max(b.norm(), 1.0);
        result.Diagnostics.Status = (result.Diagnostics.Rank < std::min(matrix.Rows, matrix.Cols))
            ? NumericStatus::RankDeficient
            : NumericStatus::Success;
        return result;
    }

    RobustPCAResult RobustPCA(const DenseMatrix& matrix, const RobustPCAOptions& options)
    {
        RobustPCAResult result;
        if (!matrix.IsShapeValid() || matrix.Rows == 0 || matrix.Cols == 0)
        {
            result.Diagnostics.Status = NumericStatus::InvalidInput;
            return result;
        }

        result.LowRank = DenseMatrix(matrix.Rows, matrix.Cols);
        result.Sparse = DenseMatrix(matrix.Rows, matrix.Cols);

        if (!IsFinite(matrix))
        {
            result.Diagnostics.Status = NumericStatus::NonFinite;
            return result;
        }
        if (!std::isfinite(options.Lambda) || options.Lambda < 0.0 ||
            !std::isfinite(options.Mu) || options.Mu < 0.0 ||
            options.MaxIterations == 0 ||
            !std::isfinite(options.Tolerance) || options.Tolerance <= 0.0 ||
            !std::isfinite(options.RankTolerance) || options.RankTolerance <= 0.0)
        {
            result.Diagnostics.Status = NumericStatus::InvalidInput;
            return result;
        }

        const double matrixNorm = FrobeniusNorm(matrix);
        if (!(matrixNorm > 0.0) || !std::isfinite(matrixNorm))
        {
            result.Diagnostics.Status = NumericStatus::InvalidInput;
            return result;
        }

        const SVDResult inputSvd = ComputeSVD(matrix, options.RankTolerance);
        if (IsHardSvdFailure(inputSvd.Diagnostics.Status) || inputSvd.SingularValues.empty())
        {
            result.Diagnostics = inputSvd.Diagnostics;
            if (result.Diagnostics.Status == NumericStatus::Success)
            {
                result.Diagnostics.Status = NumericStatus::InvalidInput;
            }
            return result;
        }

        const double lambda = options.Lambda > 0.0
            ? options.Lambda
            : 1.0 / std::sqrt(static_cast<double>(std::max(matrix.Rows, matrix.Cols)));
        const double spectralNorm = inputSvd.SingularValues.front();
        const double mu = options.Mu > 0.0
            ? options.Mu
            : (spectralNorm > 0.0 ? 1.25 / spectralNorm : 1.0);
        if (!(lambda > 0.0) || !std::isfinite(lambda) || !(mu > 0.0) || !std::isfinite(mu))
        {
            result.Diagnostics.Status = NumericStatus::InvalidInput;
            return result;
        }

        DenseMatrix dual(matrix.Rows, matrix.Cols);
        double residualNorm = matrixNorm;
        double relativeResidual = 1.0;
        std::size_t recoveredRank = 0;

        for (std::size_t iteration = 1; iteration <= options.MaxIterations; ++iteration)
        {
            const DenseMatrix lowRankArgument = MakeLowRankArgument(matrix, result.Sparse, dual, mu);
            const SVDResult svd = ComputeSVD(lowRankArgument, options.RankTolerance);
            if (IsHardSvdFailure(svd.Diagnostics.Status))
            {
                result.Diagnostics = svd.Diagnostics;
                result.Iterations = iteration;
                result.Diagnostics.Iterations = iteration;
                return result;
            }

            result.LowRank = SoftThresholdSingularValues(svd, 1.0 / mu, options.RankTolerance, recoveredRank);
            const DenseMatrix sparseArgument = MakeSparseArgument(matrix, result.LowRank, dual, mu);
            result.Sparse = SoftThresholdElements(sparseArgument, lambda / mu);

            const DenseMatrix residual = MakeResidual(matrix, result.LowRank, result.Sparse);
            residualNorm = FrobeniusNorm(residual);
            relativeResidual = residualNorm / matrixNorm;
            AddScaled(dual, residual, mu);

            result.Iterations = iteration;
            if (relativeResidual <= options.Tolerance)
            {
                result.Rank = recoveredRank;
                result.ResidualNorm = residualNorm;
                result.RelativeResidual = relativeResidual;
                result.Diagnostics.Status = NumericStatus::Success;
                result.Diagnostics.ResidualNorm = residualNorm;
                result.Diagnostics.RelativeResidual = relativeResidual;
                result.Diagnostics.Rank = recoveredRank;
                result.Diagnostics.Iterations = iteration;
                result.Diagnostics.ConditionEstimate = inputSvd.Diagnostics.ConditionEstimate;
                return result;
            }
        }

        result.Rank = recoveredRank;
        result.ResidualNorm = residualNorm;
        result.RelativeResidual = relativeResidual;
        result.Diagnostics.Status = NumericStatus::NoConvergence;
        result.Diagnostics.ResidualNorm = residualNorm;
        result.Diagnostics.RelativeResidual = relativeResidual;
        result.Diagnostics.Rank = recoveredRank;
        result.Diagnostics.Iterations = result.Iterations;
        result.Diagnostics.ConditionEstimate = inputSvd.Diagnostics.ConditionEstimate;
        return result;
    }
}
