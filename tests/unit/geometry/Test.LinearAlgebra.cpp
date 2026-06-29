#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

#include <glm/glm.hpp>

import Geometry.Linalg;

namespace
{
    [[nodiscard]] double FrobeniusError(const Geometry::Linalg::DenseMatrix& a,
                                        const Geometry::Linalg::DenseMatrix& b)
    {
        double error = 0.0;
        for (std::size_t row = 0; row < a.Rows; ++row)
        {
            for (std::size_t col = 0; col < a.Cols; ++col)
            {
                const double delta = a(row, col) - b(row, col);
                error += delta * delta;
            }
        }
        return std::sqrt(error);
    }

    [[nodiscard]] double FrobeniusNorm(const Geometry::Linalg::DenseMatrix& matrix)
    {
        double norm = 0.0;
        for (const double value : matrix.Values)
        {
            norm += value * value;
        }
        return std::sqrt(norm);
    }

    [[nodiscard]] double RelativeFrobeniusError(const Geometry::Linalg::DenseMatrix& a,
                                                const Geometry::Linalg::DenseMatrix& b)
    {
        return FrobeniusError(a, b) / std::max(FrobeniusNorm(b), 1.0);
    }

    [[nodiscard]] bool HasNoNaNs(const Geometry::Linalg::DenseMatrix& matrix)
    {
        for (const double value : matrix.Values)
        {
            if (std::isnan(value))
            {
                return false;
            }
        }
        return true;
    }

    struct RobustPCASynthetic
    {
        Geometry::Linalg::DenseMatrix LowRank;
        Geometry::Linalg::DenseMatrix Sparse;
        Geometry::Linalg::DenseMatrix Input;
    };

    [[nodiscard]] RobustPCASynthetic MakeRobustPCASynthetic()
    {
        constexpr std::size_t rows = 6;
        constexpr std::size_t cols = 5;
        const std::array<double, rows> u{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
        const std::array<double, cols> v{1.0, -0.5, 0.25, 2.0, -1.5};

        RobustPCASynthetic synthetic{
            .LowRank = Geometry::Linalg::DenseMatrix(rows, cols),
            .Sparse = Geometry::Linalg::DenseMatrix(rows, cols),
            .Input = Geometry::Linalg::DenseMatrix(rows, cols)};

        for (std::size_t row = 0; row < rows; ++row)
        {
            for (std::size_t col = 0; col < cols; ++col)
            {
                synthetic.LowRank(row, col) = u[row] * v[col];
            }
        }

        synthetic.Sparse(0, 4) = 20.0;
        synthetic.Sparse(2, 1) = -18.0;
        synthetic.Sparse(4, 0) = -14.0;
        synthetic.Sparse(5, 3) = 16.0;

        for (std::size_t i = 0; i < synthetic.Input.Values.size(); ++i)
        {
            synthetic.Input.Values[i] = synthetic.LowRank.Values[i] + synthetic.Sparse.Values[i];
        }
        return synthetic;
    }

    [[nodiscard]] double SparseSupportAgreement(const Geometry::Linalg::DenseMatrix& recovered,
                                                const Geometry::Linalg::DenseMatrix& expected,
                                                double threshold)
    {
        std::size_t matches = 0;
        for (std::size_t i = 0; i < recovered.Values.size(); ++i)
        {
            const bool recoveredNonZero = std::abs(recovered.Values[i]) > threshold;
            const bool expectedNonZero = std::abs(expected.Values[i]) > threshold;
            if (recoveredNonZero == expectedNonZero)
            {
                ++matches;
            }
        }
        return static_cast<double>(matches) / static_cast<double>(recovered.Values.size());
    }

    [[nodiscard]] Geometry::Linalg::DenseMatrix Multiply(const Geometry::Linalg::DenseMatrix& a,
                                                         const Geometry::Linalg::DenseMatrix& b)
    {
        Geometry::Linalg::DenseMatrix result(a.Rows, b.Cols);
        for (std::size_t row = 0; row < a.Rows; ++row)
        {
            for (std::size_t col = 0; col < b.Cols; ++col)
            {
                double sum = 0.0;
                for (std::size_t k = 0; k < a.Cols; ++k)
                {
                    sum += a(row, k) * b(k, col);
                }
                result(row, col) = sum;
            }
        }
        return result;
    }

    [[nodiscard]] Geometry::Linalg::DenseMatrix DiagonalMatrix(const std::vector<double>& diagonal,
                                                               std::size_t rows,
                                                               std::size_t cols)
    {
        Geometry::Linalg::DenseMatrix result(rows, cols);
        for (std::size_t i = 0; i < diagonal.size() && i < rows && i < cols; ++i)
        {
            result(i, i) = diagonal[i];
        }
        return result;
    }
}

TEST(LinearAlgebra, GlmEigenAdaptersRoundTripFixedSizeValues)
{
    const glm::dvec3 v{1.25, -2.5, 4.0};
    const auto eigenV = Geometry::Linalg::ToEigen(v);
    const glm::dvec3 roundTripV = Geometry::Linalg::ToGlm(eigenV);
    EXPECT_DOUBLE_EQ(roundTripV.x, v.x);
    EXPECT_DOUBLE_EQ(roundTripV.y, v.y);
    EXPECT_DOUBLE_EQ(roundTripV.z, v.z);

    glm::dmat3 m{1.0};
    m[0][1] = 2.0;
    m[1][2] = -3.0;
    m[2][0] = 4.5;
    const auto eigenM = Geometry::Linalg::ToEigen(m);
    const glm::dmat3 roundTripM = Geometry::Linalg::ToGlm(eigenM);
    for (int col = 0; col < 3; ++col)
    {
        for (int row = 0; row < 3; ++row)
        {
            EXPECT_DOUBLE_EQ(roundTripM[col][row], m[col][row]);
        }
    }
}

TEST(LinearAlgebra, MapRowMajorMatrixViewsContiguousBufferWithoutCopy)
{
    std::array<double, 6> values{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    auto map = Geometry::Linalg::MapRowMajorMatrix(std::span<double>{values}, 2, 3);
    EXPECT_DOUBLE_EQ(map(0, 0), 1.0);
    EXPECT_DOUBLE_EQ(map(1, 2), 6.0);

    map(1, 1) = 42.0;
    EXPECT_DOUBLE_EQ(values[4], 42.0);

    const auto constMap = Geometry::Linalg::MapRowMajorMatrix(std::span<const double>{values}, 2, 3);
    EXPECT_DOUBLE_EQ(constMap(1, 1), 42.0);
}

TEST(LinearAlgebra, SvdReconstructsDeterministicMatrix)
{
    Geometry::Linalg::DenseMatrix a(3, 2);
    a(0, 0) = 3.0;
    a(0, 1) = 1.0;
    a(1, 0) = 0.0;
    a(1, 1) = 2.0;
    a(2, 0) = 0.0;
    a(2, 1) = 0.0;

    const Geometry::Linalg::SVDResult svd = Geometry::Linalg::ComputeSVD(a);
    EXPECT_TRUE(svd.Diagnostics.Succeeded());
    EXPECT_EQ(svd.Diagnostics.Rank, 2u);

    const Geometry::Linalg::DenseMatrix sigma = DiagonalMatrix(svd.SingularValues, svd.U.Cols, svd.Vt.Rows);
    const Geometry::Linalg::DenseMatrix reconstructed = Multiply(Multiply(svd.U, sigma), svd.Vt);
    EXPECT_LT(FrobeniusError(a, reconstructed), 1.0e-10);
}

TEST(LinearAlgebra, QrAndSymmetricEigenSolveSmallSystems)
{
    Geometry::Linalg::DenseMatrix a(3, 2);
    a(0, 0) = 1.0;
    a(0, 1) = 1.0;
    a(1, 0) = 1.0;
    a(1, 1) = -1.0;
    a(2, 0) = 2.0;
    a(2, 1) = 0.0;

    const Geometry::Linalg::QRResult qr = Geometry::Linalg::ComputeQR(a);
    EXPECT_TRUE(qr.Diagnostics.Succeeded());
    EXPECT_EQ(qr.Diagnostics.Rank, 2u);

    Geometry::Linalg::DenseMatrix symmetric(2, 2);
    symmetric(0, 0) = 2.0;
    symmetric(0, 1) = 1.0;
    symmetric(1, 0) = 1.0;
    symmetric(1, 1) = 2.0;

    const Geometry::Linalg::SymmetricEigenResult eigen = Geometry::Linalg::ComputeSymmetricEigen(symmetric);
    EXPECT_TRUE(eigen.Diagnostics.Succeeded());
    ASSERT_EQ(eigen.Eigenvalues.size(), 2u);
    EXPECT_NEAR(eigen.Eigenvalues[0], 1.0, 1.0e-12);
    EXPECT_NEAR(eigen.Eigenvalues[1], 3.0, 1.0e-12);
}

TEST(LinearAlgebra, LeastSquaresPolarAndCovarianceReturnDiagnostics)
{
    Geometry::Linalg::DenseMatrix a(3, 2);
    a(0, 0) = 1.0;
    a(0, 1) = 0.0;
    a(1, 0) = 1.0;
    a(1, 1) = 1.0;
    a(2, 0) = 1.0;
    a(2, 1) = 2.0;
    const std::array<double, 3> b{1.0, 2.0, 3.0};

    const Geometry::Linalg::LeastSquaresResult leastSquares = Geometry::Linalg::SolveLeastSquares(a, b);
    EXPECT_TRUE(leastSquares.Diagnostics.Succeeded());
    ASSERT_EQ(leastSquares.X.size(), 2u);
    EXPECT_NEAR(leastSquares.X[0], 1.0, 1.0e-12);
    EXPECT_NEAR(leastSquares.X[1], 1.0, 1.0e-12);
    EXPECT_LT(leastSquares.Diagnostics.ResidualNorm, 1.0e-12);

    Geometry::Linalg::DenseMatrix deformation(2, 2);
    deformation(0, 0) = 0.0;
    deformation(0, 1) = -2.0;
    deformation(1, 0) = 1.0;
    deformation(1, 1) = 0.0;
    const Geometry::Linalg::PolarDecompositionResult polar = Geometry::Linalg::ComputePolarDecomposition(deformation);
    EXPECT_TRUE(polar.Diagnostics.Succeeded());
    EXPECT_LT(polar.Diagnostics.RelativeResidual, 1.0e-12);

    Geometry::Linalg::CovarianceAccumulator covariance;
    covariance.Add({0.0, 0.0, 0.0});
    covariance.Add({2.0, 0.0, 0.0});
    covariance.Add({4.0, 0.0, 0.0});
    const Geometry::Linalg::CovarianceResult covarianceResult = covariance.Result();
    EXPECT_TRUE(covarianceResult.Valid);
    EXPECT_EQ(covarianceResult.Count, 3u);
    EXPECT_NEAR(covarianceResult.Mean.x, 2.0, 1.0e-12);
    EXPECT_NEAR(covarianceResult.Covariance[0][0], 4.0, 1.0e-12);
}

TEST(LinearAlgebra, RobustPCARecoversLowRankPlusSparseMatrix)
{
    const RobustPCASynthetic synthetic = MakeRobustPCASynthetic();
    Geometry::Linalg::RobustPCAOptions options;
    options.Lambda = 0.5;
    options.MaxIterations = 1000;
    options.Tolerance = 1.0e-6;

    const Geometry::Linalg::RobustPCAResult result = Geometry::Linalg::RobustPCA(synthetic.Input, options);
    ASSERT_TRUE(result.Diagnostics.Succeeded());
    EXPECT_EQ(result.Rank, 1u);
    EXPECT_LE(result.Iterations, options.MaxIterations);
    EXPECT_LT(result.RelativeResidual, options.Tolerance);
    EXPECT_LT(RelativeFrobeniusError(result.LowRank, synthetic.LowRank), 1.0e-4);
    EXPECT_LT(RelativeFrobeniusError(result.Sparse, synthetic.Sparse), 1.0e-4);
    EXPECT_GE(SparseSupportAgreement(result.Sparse, synthetic.Sparse, 1.0e-3), 1.0);
}

TEST(LinearAlgebra, RobustPCAIsDeterministicForIdenticalInput)
{
    const RobustPCASynthetic synthetic = MakeRobustPCASynthetic();
    Geometry::Linalg::RobustPCAOptions options;
    options.Lambda = 0.5;
    options.MaxIterations = 1000;
    options.Tolerance = 1.0e-6;

    const Geometry::Linalg::RobustPCAResult a = Geometry::Linalg::RobustPCA(synthetic.Input, options);
    const Geometry::Linalg::RobustPCAResult b = Geometry::Linalg::RobustPCA(synthetic.Input, options);

    ASSERT_TRUE(a.Diagnostics.Succeeded());
    ASSERT_TRUE(b.Diagnostics.Succeeded());
    EXPECT_EQ(a.LowRank.Values, b.LowRank.Values);
    EXPECT_EQ(a.Sparse.Values, b.Sparse.Values);
    EXPECT_EQ(a.Rank, b.Rank);
    EXPECT_EQ(a.Iterations, b.Iterations);
    EXPECT_DOUBLE_EQ(a.RelativeResidual, b.RelativeResidual);
}

TEST(LinearAlgebra, RobustPCAFailsClosedForDegenerateInput)
{
    const Geometry::Linalg::RobustPCAResult empty = Geometry::Linalg::RobustPCA(Geometry::Linalg::DenseMatrix{});
    EXPECT_FALSE(empty.Diagnostics.Succeeded());
    EXPECT_TRUE(HasNoNaNs(empty.LowRank));
    EXPECT_TRUE(HasNoNaNs(empty.Sparse));

    Geometry::Linalg::DenseMatrix zero(2, 2);
    const Geometry::Linalg::RobustPCAResult zeroResult = Geometry::Linalg::RobustPCA(zero);
    EXPECT_FALSE(zeroResult.Diagnostics.Succeeded());
    EXPECT_TRUE(HasNoNaNs(zeroResult.LowRank));
    EXPECT_TRUE(HasNoNaNs(zeroResult.Sparse));

    Geometry::Linalg::DenseMatrix nonFinite(2, 2);
    nonFinite(0, 0) = 1.0;
    nonFinite(0, 1) = std::numeric_limits<double>::quiet_NaN();
    nonFinite(1, 0) = 2.0;
    nonFinite(1, 1) = 3.0;
    const Geometry::Linalg::RobustPCAResult nonFiniteResult = Geometry::Linalg::RobustPCA(nonFinite);
    EXPECT_FALSE(nonFiniteResult.Diagnostics.Succeeded());
    EXPECT_EQ(nonFiniteResult.Diagnostics.Status, Geometry::Linalg::NumericStatus::NonFinite);
    EXPECT_TRUE(HasNoNaNs(nonFiniteResult.LowRank));
    EXPECT_TRUE(HasNoNaNs(nonFiniteResult.Sparse));

    Geometry::Linalg::RobustPCAOptions invalidOptions;
    invalidOptions.Tolerance = 0.0;
    const Geometry::Linalg::RobustPCAResult invalidOptionResult =
        Geometry::Linalg::RobustPCA(MakeRobustPCASynthetic().Input, invalidOptions);
    EXPECT_FALSE(invalidOptionResult.Diagnostics.Succeeded());
    EXPECT_EQ(invalidOptionResult.Diagnostics.Status, Geometry::Linalg::NumericStatus::InvalidInput);
    EXPECT_TRUE(HasNoNaNs(invalidOptionResult.LowRank));
    EXPECT_TRUE(HasNoNaNs(invalidOptionResult.Sparse));
}
