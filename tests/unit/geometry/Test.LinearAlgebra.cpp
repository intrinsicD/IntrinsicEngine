#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <utility>
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
                error = std::hypot(error, a(row, col) - b(row, col));
            }
        }
        return error;
    }

    [[nodiscard]] double FrobeniusNorm(const Geometry::Linalg::DenseMatrix& matrix)
    {
        double norm = 0.0;
        for (const double value : matrix.Values)
        {
            norm = std::hypot(norm, value);
        }
        return norm;
    }

    [[nodiscard]] double RelativeFrobeniusError(const Geometry::Linalg::DenseMatrix& a,
                                                const Geometry::Linalg::DenseMatrix& b)
    {
        return FrobeniusError(a, b) / std::max(FrobeniusNorm(b), 1.0);
    }

    [[nodiscard]] bool AllFinite(const Geometry::Linalg::DenseMatrix& matrix)
    {
        return std::ranges::all_of(matrix.Values, [](double value) { return std::isfinite(value); });
    }

    [[nodiscard]] Geometry::Linalg::DenseMatrix Scaled(Geometry::Linalg::DenseMatrix matrix, double scale)
    {
        for (double& value : matrix.Values)
        {
            value *= scale;
        }
        return matrix;
    }

    [[nodiscard]] Geometry::Linalg::DenseMatrix Identity2()
    {
        Geometry::Linalg::DenseMatrix identity(2, 2);
        identity(0, 0) = 1.0;
        identity(1, 1) = 1.0;
        return identity;
    }

    [[nodiscard]] Geometry::Linalg::DenseMatrix OuterProduct(std::span<const double> u, std::span<const double> v)
    {
        Geometry::Linalg::DenseMatrix result(u.size(), v.size());
        for (std::size_t row = 0; row < u.size(); ++row)
        {
            for (std::size_t col = 0; col < v.size(); ++col)
            {
                result(row, col) = u[row] * v[col];
            }
        }
        return result;
    }

    void ExpectFailsClosed(const Geometry::Linalg::RobustPCAResult& result, Geometry::Linalg::NumericStatus status)
    {
        EXPECT_EQ(result.Diagnostics.Status, status);
        EXPECT_TRUE(AllFinite(result.LowRank));
        EXPECT_TRUE(AllFinite(result.Sparse));
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
    EXPECT_TRUE(AllFinite(empty.LowRank));
    EXPECT_TRUE(AllFinite(empty.Sparse));

    Geometry::Linalg::DenseMatrix zero(2, 2);
    const Geometry::Linalg::RobustPCAResult zeroResult = Geometry::Linalg::RobustPCA(zero);
    EXPECT_FALSE(zeroResult.Diagnostics.Succeeded());
    EXPECT_TRUE(AllFinite(zeroResult.LowRank));
    EXPECT_TRUE(AllFinite(zeroResult.Sparse));

    Geometry::Linalg::DenseMatrix nonFinite(2, 2);
    nonFinite(0, 0) = 1.0;
    nonFinite(0, 1) = std::numeric_limits<double>::quiet_NaN();
    nonFinite(1, 0) = 2.0;
    nonFinite(1, 1) = 3.0;
    const Geometry::Linalg::RobustPCAResult nonFiniteResult = Geometry::Linalg::RobustPCA(nonFinite);
    EXPECT_FALSE(nonFiniteResult.Diagnostics.Succeeded());
    EXPECT_EQ(nonFiniteResult.Diagnostics.Status, Geometry::Linalg::NumericStatus::NonFinite);
    EXPECT_TRUE(AllFinite(nonFiniteResult.LowRank));
    EXPECT_TRUE(AllFinite(nonFiniteResult.Sparse));

    Geometry::Linalg::RobustPCAOptions invalidOptions;
    invalidOptions.Tolerance = 0.0;
    const Geometry::Linalg::RobustPCAResult invalidOptionResult =
        Geometry::Linalg::RobustPCA(MakeRobustPCASynthetic().Input, invalidOptions);
    EXPECT_FALSE(invalidOptionResult.Diagnostics.Succeeded());
    EXPECT_EQ(invalidOptionResult.Diagnostics.Status, Geometry::Linalg::NumericStatus::InvalidInput);
    EXPECT_TRUE(AllFinite(invalidOptionResult.LowRank));
    EXPECT_TRUE(AllFinite(invalidOptionResult.Sparse));
}

// M = [1], lambda = 0.5, mu = 2: the unique optimum is L = 0, S = 1 (objective
// 0.5). Iteration 2 reaches L = S = 0.5 with zero primal residual (objective
// 0.75); a primal-only stopping rule reported that point as Success.
TEST(LinearAlgebra, RobustPCAScalarRequiresDualConvergence)
{
    Geometry::Linalg::DenseMatrix scalar(1, 1);
    scalar(0, 0) = 1.0;
    Geometry::Linalg::RobustPCAOptions options;
    options.Lambda = 0.5;
    options.Mu = 2.0;

    const Geometry::Linalg::RobustPCAResult result = Geometry::Linalg::RobustPCA(scalar, options);
    ASSERT_TRUE(result.Diagnostics.Succeeded());
    EXPECT_NEAR(result.LowRank(0, 0), 0.0, 1.0e-12);
    EXPECT_NEAR(result.Sparse(0, 0), 1.0, 1.0e-12);
    EXPECT_EQ(result.Rank, 0u);

    options.MaxIterations = 2;
    const Geometry::Linalg::RobustPCAResult truncated = Geometry::Linalg::RobustPCA(scalar, options);
    EXPECT_EQ(truncated.Diagnostics.Status, Geometry::Linalg::NumericStatus::NoConvergence);
    EXPECT_EQ(truncated.Iterations, 2u);
    EXPECT_TRUE(AllFinite(truncated.LowRank));
    EXPECT_TRUE(AllFinite(truncated.Sparse));
}

// For M = I2 with default lambda = 1/sqrt(2), Y = lambda * I certifies L = 0, S = I.
TEST(LinearAlgebra, RobustPCAIdentityWithDefaultsIsAllSparse)
{
    const Geometry::Linalg::DenseMatrix identity = Identity2();
    const Geometry::Linalg::RobustPCAResult result = Geometry::Linalg::RobustPCA(identity);
    ASSERT_TRUE(result.Diagnostics.Succeeded());
    EXPECT_LT(FrobeniusNorm(result.LowRank), 1.0e-5);
    EXPECT_LT(FrobeniusError(result.Sparse, identity), 1.0e-5);
    EXPECT_EQ(result.Rank, 0u);
}

// Default mu = 1.25 / ||M||_2 makes PCP scale-equivariant; entries near 1e200
// and 1e-199 overflow/underflow a plain sum-of-squares norm.
TEST(LinearAlgebra, RobustPCAIsScaleEquivariantAtExtremeFiniteScales)
{
    const RobustPCASynthetic synthetic = MakeRobustPCASynthetic();
    Geometry::Linalg::RobustPCAOptions options;
    options.Lambda = 0.5;
    options.Tolerance = 1.0e-6;
    const Geometry::Linalg::RobustPCAResult reference = Geometry::Linalg::RobustPCA(synthetic.Input, options);
    ASSERT_TRUE(reference.Diagnostics.Succeeded());

    for (const double scale : {std::ldexp(1.0, 660), std::ldexp(1.0, -660)})
    {
        SCOPED_TRACE(scale);
        Geometry::Linalg::RobustPCAOptions scaledOptions = options;
        scaledOptions.RankTolerance = options.RankTolerance * scale; // RankTolerance is absolute.
        const Geometry::Linalg::RobustPCAResult result =
            Geometry::Linalg::RobustPCA(Scaled(synthetic.Input, scale), scaledOptions);
        ASSERT_TRUE(result.Diagnostics.Succeeded());
        EXPECT_EQ(result.Rank, reference.Rank);
        EXPECT_LT(RelativeFrobeniusError(Scaled(result.LowRank, 1.0 / scale), reference.LowRank), 1.0e-8);
        EXPECT_LT(RelativeFrobeniusError(Scaled(result.Sparse, 1.0 / scale), reference.Sparse), 1.0e-8);
        EXPECT_LT(RelativeFrobeniusError(Scaled(result.LowRank, 1.0 / scale), synthetic.LowRank), 1.0e-4);
    }
}

TEST(LinearAlgebra, RobustPCAKeepsRectangularRankOneInputLowRank)
{
    const std::array<double, 3> u{1.0, 2.0, 3.0};
    const std::array<double, 5> v{1.0, 1.0, 1.0, 1.0, 1.0};
    for (const Geometry::Linalg::DenseMatrix& input : {OuterProduct(u, v), OuterProduct(v, u)})
    {
        SCOPED_TRACE(input.Rows);
        const Geometry::Linalg::RobustPCAResult result = Geometry::Linalg::RobustPCA(input);
        ASSERT_TRUE(result.Diagnostics.Succeeded());
        EXPECT_EQ(result.LowRank.Rows, input.Rows);
        EXPECT_EQ(result.LowRank.Cols, input.Cols);
        EXPECT_EQ(result.Rank, 1u);
        EXPECT_LT(RelativeFrobeniusError(result.LowRank, input), 1.0e-4);
        EXPECT_LT(FrobeniusNorm(result.Sparse), 1.0e-4);
    }
}

TEST(LinearAlgebra, RobustPCARejectsMalformedAndOverflowingShapes)
{
    Geometry::Linalg::DenseMatrix mismatched;
    mismatched.Rows = 2;
    mismatched.Cols = 2;
    mismatched.Values = {1.0, 2.0, 3.0};
    ExpectFailsClosed(Geometry::Linalg::RobustPCA(mismatched), Geometry::Linalg::NumericStatus::InvalidInput);

    // Rows * Cols wraps to 0 and to 1; both satisfy IsShapeValid() but must be
    // rejected before any Rows/Cols-sized allocation.
    constexpr std::size_t kMax = std::numeric_limits<std::size_t>::max();
    Geometry::Linalg::DenseMatrix wrapsToZero;
    wrapsToZero.Rows = kMax / 2 + 1;
    wrapsToZero.Cols = 2;
    ASSERT_TRUE(wrapsToZero.IsShapeValid());
    const Geometry::Linalg::RobustPCAResult zeroResult = Geometry::Linalg::RobustPCA(wrapsToZero);
    ExpectFailsClosed(zeroResult, Geometry::Linalg::NumericStatus::InvalidInput);
    EXPECT_TRUE(zeroResult.LowRank.Values.empty());

    Geometry::Linalg::DenseMatrix wrapsToOne;
    wrapsToOne.Rows = kMax;
    wrapsToOne.Cols = kMax;
    wrapsToOne.Values = {1.0};
    ASSERT_TRUE(wrapsToOne.IsShapeValid());
    const Geometry::Linalg::RobustPCAResult oneResult = Geometry::Linalg::RobustPCA(wrapsToOne);
    ExpectFailsClosed(oneResult, Geometry::Linalg::NumericStatus::InvalidInput);
    EXPECT_TRUE(oneResult.LowRank.Values.empty());
}

TEST(LinearAlgebra, RobustPCARejectsNonFiniteInputAndInvalidOptions)
{
    for (const double bad : {std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()})
    {
        Geometry::Linalg::DenseMatrix input = Identity2();
        input(1, 0) = bad;
        ExpectFailsClosed(Geometry::Linalg::RobustPCA(input), Geometry::Linalg::NumericStatus::NonFinite);
    }

    const Geometry::Linalg::DenseMatrix input = MakeRobustPCASynthetic().Input;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const auto expectInvalid = [&](Geometry::Linalg::RobustPCAOptions options) {
        ExpectFailsClosed(Geometry::Linalg::RobustPCA(input, options), Geometry::Linalg::NumericStatus::InvalidInput);
    };
    expectInvalid({.Lambda = -1.0});
    expectInvalid({.Lambda = nan});
    expectInvalid({.Mu = -1.0});
    expectInvalid({.Mu = inf});
    expectInvalid({.MaxIterations = 0});
    expectInvalid({.Tolerance = nan});
    expectInvalid({.RankTolerance = 0.0});
    // Valid finite options whose derived thresholds 1/mu or lambda/mu overflow.
    expectInvalid({.Mu = std::numeric_limits<double>::denorm_min()});
    expectInvalid({.Lambda = 1.0e300, .Mu = 1.0e-300});
}

TEST(LinearAlgebra, RobustPCANearOverflowInputNeverPublishesNonFiniteValues)
{
    Geometry::Linalg::DenseMatrix input(2, 2);
    input(0, 0) = 1.0e307;
    input(0, 1) = -1.0e307;
    input(1, 0) = 1.0e307;
    input(1, 1) = 1.0e307;
    const Geometry::Linalg::RobustPCAResult result = Geometry::Linalg::RobustPCA(input);
    EXPECT_TRUE(AllFinite(result.LowRank));
    EXPECT_TRUE(AllFinite(result.Sparse));
    EXPECT_TRUE(std::isfinite(result.ResidualNorm));
}

TEST(LinearAlgebra, RobustPCAAnalyticCasesAreDeterministic)
{
    Geometry::Linalg::DenseMatrix scalar(1, 1);
    scalar(0, 0) = 1.0;
    const Geometry::Linalg::RobustPCAOptions scalarOptions{.Lambda = 0.5, .Mu = 2.0};
    const Geometry::Linalg::DenseMatrix identity = Identity2();

    for (const auto& [input, options] : {std::pair{scalar, scalarOptions},
                                         std::pair{identity, Geometry::Linalg::RobustPCAOptions{}}})
    {
        const Geometry::Linalg::RobustPCAResult a = Geometry::Linalg::RobustPCA(input, options);
        const Geometry::Linalg::RobustPCAResult b = Geometry::Linalg::RobustPCA(input, options);
        ASSERT_TRUE(a.Diagnostics.Succeeded());
        EXPECT_EQ(a.LowRank.Values, b.LowRank.Values);
        EXPECT_EQ(a.Sparse.Values, b.Sparse.Values);
        EXPECT_EQ(a.Iterations, b.Iterations);
        EXPECT_EQ(a.Rank, b.Rank);
    }
}

// M = [3 * 2^1022], Mu = 2^-1022: step 1 gives L = 2^1023, S = 0, Y = 1, so the
// step-2 SVD argument M - S + Y / Mu = 2^1024 overflows. That must report
// NonFinite with zeroed outputs, not keep the stale non-zero L.
TEST(LinearAlgebra, RobustPCAOverflowingSvdArgumentReportsNonFinite)
{
    Geometry::Linalg::DenseMatrix input(1, 1);
    input(0, 0) = 3.0 * std::ldexp(1.0, 1022);
    const Geometry::Linalg::RobustPCAResult result =
        Geometry::Linalg::RobustPCA(input, {.Mu = std::ldexp(1.0, -1022)});
    EXPECT_EQ(result.Diagnostics.Status, Geometry::Linalg::NumericStatus::NonFinite);
    EXPECT_EQ(result.Iterations, 2u);
    EXPECT_EQ(result.Diagnostics.Iterations, 2u);
    EXPECT_EQ(result.LowRank.Values, std::vector<double>{0.0});
    EXPECT_EQ(result.Sparse.Values, std::vector<double>{0.0});
}
