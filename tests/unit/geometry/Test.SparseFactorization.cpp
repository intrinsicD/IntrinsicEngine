#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <span>
#include <vector>

#include <Eigen/Dense>

import Geometry.Sparse;

namespace
{
    using Geometry::Sparse::SparseBuilder;
    using Geometry::Sparse::SparseFactorizationDiagnostics;
    using Geometry::Sparse::SparseFactorizationStatus;
    using Geometry::Sparse::SparseMatrix;

    [[nodiscard]] SparseMatrix BuildPoisson1D(std::size_t count)
    {
        SparseBuilder builder(count, count);
        for (std::size_t i = 0; i < count; ++i)
        {
            builder.Add(i, i, 2.0);
            if (i > 0)
            {
                builder.Add(i, i - 1, -1.0);
            }
            if (i + 1 < count)
            {
                builder.Add(i, i + 1, -1.0);
            }
        }
        return builder.Build().Matrix;
    }

    [[nodiscard]] SparseMatrix BuildTriangleFanMassPlusLaplacian()
    {
        SparseBuilder builder(4, 4);
        const std::array<double, 4> mass{1.0, 0.5, 0.5, 0.5};
        constexpr double t = 0.25;

        for (std::size_t i = 0; i < mass.size(); ++i)
        {
            builder.Add(i, i, mass[i]);
        }

        builder.Add(0, 0, 3.0 * t);
        builder.Add(1, 1, 1.0 * t);
        builder.Add(2, 2, 1.0 * t);
        builder.Add(3, 3, 1.0 * t);
        for (std::size_t leaf = 1; leaf < 4; ++leaf)
        {
            builder.Add(0, leaf, -t);
            builder.Add(leaf, 0, -t);
        }

        return builder.Build().Matrix;
    }

    [[nodiscard]] SparseMatrix BuildDiagonal(std::span<const double> diagonal)
    {
        SparseBuilder builder(diagonal.size(), diagonal.size());
        for (std::size_t i = 0; i < diagonal.size(); ++i)
        {
            builder.Add(i, i, diagonal[i]);
        }
        return builder.Build().Matrix;
    }

    [[nodiscard]] SparseMatrix BuildSingularLaplacian()
    {
        SparseBuilder builder(2, 2);
        builder.Add(0, 0, 1.0);
        builder.Add(0, 1, -1.0);
        builder.Add(1, 0, -1.0);
        builder.Add(1, 1, 1.0);
        return builder.Build().Matrix;
    }

    [[nodiscard]] std::vector<double> Multiply(const SparseMatrix& matrix, std::span<const double> x)
    {
        std::vector<double> y(matrix.Rows, 0.0);
        matrix.Multiply(x, y);
        return y;
    }

    void ExpectNear(std::span<const double> actual, std::span<const double> expected, double tolerance = 1.0e-10)
    {
        ASSERT_EQ(actual.size(), expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i)
        {
            EXPECT_NEAR(actual[i], expected[i], tolerance) << "index " << i;
        }
    }

    void ExpectSuccess(const SparseFactorizationDiagnostics& diagnostics)
    {
        EXPECT_EQ(diagnostics.Status, SparseFactorizationStatus::Success);
        EXPECT_TRUE(diagnostics.Succeeded());
        EXPECT_GT(diagnostics.PivotCount, 0u);
        EXPECT_GT(diagnostics.SmallestAbsolutePivot, 0.0);
        EXPECT_EQ(diagnostics.ConditionEstimate, 0.0);
    }
}

TEST(SparseFactorization, LDLTSolvesOneDimensionalPoissonSystem)
{
    const SparseMatrix matrix = BuildPoisson1D(3);
    Geometry::Sparse::SparseLDLT solver;
    ExpectSuccess(solver.factor(matrix));
    EXPECT_EQ(solver.diagnostics().Status, SparseFactorizationStatus::Success);

    const std::array<double, 3> rhs{1.0, 0.0, 1.0};
    std::array<double, 3> x{};
    ExpectSuccess(solver.solve(rhs, x));
    ExpectNear(x, std::array<double, 3>{1.0, 1.0, 1.0});
}

TEST(SparseFactorization, LLTSolvesTriangleFanMassPlusLaplacianShape)
{
    const SparseMatrix matrix = BuildTriangleFanMassPlusLaplacian();
    const std::array<double, 4> expected{1.0, 2.0, -1.0, 0.5};
    const std::vector<double> rhs = Multiply(matrix, expected);

    Geometry::Sparse::SparseLLT solver;
    ExpectSuccess(solver.factor(matrix));

    std::array<double, 4> x{};
    ExpectSuccess(solver.solve(rhs, x));
    ExpectNear(x, expected);
}

TEST(SparseFactorization, LDLTAndLLTClassifyIndefiniteAndSingularSystems)
{
    const SparseMatrix indefinite = BuildDiagonal(std::array<double, 2>{1.0, -1.0});
    Geometry::Sparse::SparseLDLT ldltIndefinite;
    SparseFactorizationDiagnostics diagnostics = ldltIndefinite.factor(indefinite);
    EXPECT_EQ(diagnostics.Status, SparseFactorizationStatus::NonSPD);
    EXPECT_FALSE(diagnostics.Succeeded());

    Geometry::Sparse::SparseLLT lltIndefinite;
    diagnostics = lltIndefinite.factor(indefinite);
    EXPECT_EQ(diagnostics.Status, SparseFactorizationStatus::NonSPD);
    EXPECT_FALSE(diagnostics.Succeeded());

    const SparseMatrix singular = BuildSingularLaplacian();
    Geometry::Sparse::SparseLDLT ldltSingular;
    diagnostics = ldltSingular.factor(singular);
    EXPECT_EQ(diagnostics.Status, SparseFactorizationStatus::ZeroPivot);
    EXPECT_FALSE(diagnostics.Succeeded());

    Geometry::Sparse::SparseLLT lltSingular;
    diagnostics = lltSingular.factor(singular);
    EXPECT_EQ(diagnostics.Status, SparseFactorizationStatus::ZeroPivot);
    EXPECT_FALSE(diagnostics.Succeeded());
}

TEST(SparseFactorization, MultiRhsSolveMatchesKnownSolutions)
{
    const SparseMatrix matrix = BuildPoisson1D(3);
    Eigen::MatrixXd expected(3, 2);
    expected << 1.0, -1.0,
        1.0, 0.5,
        1.0, 2.0;

    Eigen::MatrixXd rhs(3, 2);
    for (Eigen::Index col = 0; col < expected.cols(); ++col)
    {
        const std::vector<double> column = Multiply(
            matrix,
            std::span<const double>(expected.col(col).data(), static_cast<std::size_t>(expected.rows())));
        for (Eigen::Index row = 0; row < expected.rows(); ++row)
        {
            rhs(row, col) = column[static_cast<std::size_t>(row)];
        }
    }

    Geometry::Sparse::SparseLDLT solver;
    ExpectSuccess(solver.factor(matrix));

    Eigen::MatrixXd solved = Eigen::MatrixXd::Zero(3, 2);
    ExpectSuccess(solver.solve(rhs, solved));
    for (Eigen::Index row = 0; row < expected.rows(); ++row)
    {
        for (Eigen::Index col = 0; col < expected.cols(); ++col)
        {
            EXPECT_NEAR(solved(row, col), expected(row, col), 1.0e-10);
        }
    }
}

TEST(SparseFactorization, SolveInPlaceReusesRhsStorage)
{
    const SparseMatrix matrix = BuildPoisson1D(3);
    Geometry::Sparse::SparseLLT solver;
    ExpectSuccess(solver.factor(matrix));

    std::array<double, 3> rhs{1.0, 0.0, 1.0};
    ExpectSuccess(solver.solveInPlace(rhs));
    ExpectNear(rhs, std::array<double, 3>{1.0, 1.0, 1.0});

    Eigen::MatrixXd denseRhs(3, 1);
    denseRhs << 1.0, 0.0, 1.0;
    ExpectSuccess(solver.solveInPlace(denseRhs));
    EXPECT_NEAR(denseRhs(0, 0), 1.0, 1.0e-10);
    EXPECT_NEAR(denseRhs(1, 0), 1.0, 1.0e-10);
    EXPECT_NEAR(denseRhs(2, 0), 1.0, 1.0e-10);
}

TEST(SparseFactorization, RejectsInvalidInputAndDimensionMismatches)
{
    Geometry::Sparse::SparseLDLT notFactored;
    std::array<double, 2> rhs{1.0, 2.0};
    std::array<double, 2> x{};
    EXPECT_EQ(notFactored.solve(rhs, x).Status, SparseFactorizationStatus::NotFactored);

    SparseBuilder nonSquareBuilder(2, 3);
    nonSquareBuilder.Add(0, 0, 1.0);
    nonSquareBuilder.Add(1, 1, 1.0);
    Geometry::Sparse::SparseLDLT solver;
    EXPECT_EQ(solver.factor(nonSquareBuilder.Build().Matrix).Status, SparseFactorizationStatus::DimensionMismatch);

    SparseMatrix nonFinite;
    nonFinite.Rows = 2;
    nonFinite.Cols = 2;
    nonFinite.RowOffsets = {0, 1, 2};
    nonFinite.ColIndices = {0, 1};
    nonFinite.Values = {1.0, std::numeric_limits<double>::infinity()};
    EXPECT_EQ(solver.factor(nonFinite).Status, SparseFactorizationStatus::InvalidInput);

    const SparseMatrix matrix = BuildPoisson1D(3);
    ExpectSuccess(solver.factor(matrix));
    std::array<double, 2> tooSmall{};
    EXPECT_EQ(solver.solve(tooSmall, tooSmall).Status, SparseFactorizationStatus::DimensionMismatch);

    std::array<double, 3> nonFiniteRhs{1.0, std::numeric_limits<double>::quiet_NaN(), 0.0};
    std::array<double, 3> validX{};
    EXPECT_EQ(solver.solve(nonFiniteRhs, validX).Status, SparseFactorizationStatus::InvalidInput);
}

TEST(SparseFactorization, FactorOnceSolveManyIsBitStable)
{
    const SparseMatrix matrix = BuildPoisson1D(4);
    Geometry::Sparse::SparseLDLT solver;
    ExpectSuccess(solver.factor(matrix));

    const std::array<std::array<double, 4>, 3> rhsVectors{{
        {1.0, 0.0, 0.0, 1.0},
        {0.0, 1.0, -2.0, 0.5},
        {3.0, -1.0, 4.0, -1.0},
    }};

    for (const std::array<double, 4>& rhs : rhsVectors)
    {
        std::array<double, 4> first{};
        std::array<double, 4> second{};
        ExpectSuccess(solver.solve(rhs, first));
        ExpectSuccess(solver.solve(rhs, second));
        EXPECT_EQ(first, second);
    }
}
