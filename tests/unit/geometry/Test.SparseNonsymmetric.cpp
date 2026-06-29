#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <span>
#include <vector>

#include <Eigen/Dense>

import Geometry.Sparse;

namespace
{
    using Geometry::Sparse::SparseBiCGSTABParams;
    using Geometry::Sparse::SparseIterativeDiagnostics;
    using Geometry::Sparse::SparseIterativeStatus;
    using Geometry::Sparse::SparseMatrix;
    using Geometry::Sparse::SparsePreconditioner;

    [[nodiscard]] SparseMatrix BuildAdvectionDiffusion()
    {
        Geometry::Sparse::SparseBuilder builder(3, 3);
        builder.Add(0, 0, 4.0);
        builder.Add(0, 1, -1.0);
        builder.Add(1, 0, -2.0);
        builder.Add(1, 1, 4.0);
        builder.Add(1, 2, -1.0);
        builder.Add(2, 1, -2.0);
        builder.Add(2, 2, 3.0);
        return builder.Build().Matrix;
    }

    [[nodiscard]] SparseMatrix BuildPoisson1D(std::size_t count)
    {
        Geometry::Sparse::SparseBuilder builder(count, count);
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

    SparseBiCGSTABParams DefaultParams(SparsePreconditioner preconditioner = SparsePreconditioner::Diagonal)
    {
        SparseBiCGSTABParams params;
        params.MaxIterations = 100;
        params.RelativeTolerance = 1.0e-12;
        params.Preconditioner = preconditioner;
        return params;
    }

    void ExpectSuccess(const SparseIterativeDiagnostics& diagnostics, SparsePreconditioner preconditioner)
    {
        EXPECT_EQ(diagnostics.Status, SparseIterativeStatus::Success);
        EXPECT_TRUE(diagnostics.Succeeded());
        EXPECT_GT(diagnostics.Iterations, 0u);
        EXPECT_LT(diagnostics.FinalRelativeResidual, 1.0e-10);
        EXPECT_EQ(diagnostics.Preconditioner, preconditioner);
    }
}

TEST(SparseNonsymmetric, BiCGSTABSolvesAdvectionDiffusionSystem)
{
    const SparseMatrix matrix = BuildAdvectionDiffusion();
    const std::array<double, 3> expected{1.0, -2.0, 0.5};
    const std::vector<double> rhs = Multiply(matrix, expected);

    Geometry::Sparse::SparseBiCGSTAB solver;
    std::array<double, 3> x{};
    const SparseBiCGSTABParams params = DefaultParams();
    const SparseIterativeDiagnostics diagnostics = solver.solve(matrix, rhs, x, params);

    ExpectSuccess(diagnostics, SparsePreconditioner::Diagonal);
    ExpectNear(x, expected, 1.0e-10);
}

TEST(SparseNonsymmetric, BiCGSTABMatchesCGOnSPDSystem)
{
    const SparseMatrix matrix = BuildPoisson1D(4);
    const std::array<double, 4> rhs{1.0, 0.0, 2.0, -1.0};

    std::array<double, 4> cgX{};
    const Geometry::Sparse::CGResult cg = Geometry::Sparse::SolveCG(matrix, rhs, cgX);
    ASSERT_TRUE(cg.Converged);

    Geometry::Sparse::SparseBiCGSTAB solver;
    std::array<double, 4> bicgstabX{};
    const SparseIterativeDiagnostics diagnostics = solver.solve(matrix, rhs, bicgstabX, DefaultParams());
    ExpectSuccess(diagnostics, SparsePreconditioner::Diagonal);
    ExpectNear(bicgstabX, cgX, 1.0e-9);
}

TEST(SparseNonsymmetric, SingularSystemReportsFailureWithoutMutatingOutput)
{
    SparseMatrix singular;
    singular.Rows = 2;
    singular.Cols = 2;
    singular.RowOffsets = {0, 0, 0};

    Geometry::Sparse::SparseBiCGSTAB solver;
    const std::array<double, 2> rhs{1.0, 0.0};
    std::array<double, 2> x{42.0, -7.0};
    SparseBiCGSTABParams params = DefaultParams(SparsePreconditioner::None);
    params.MaxIterations = 4;

    const SparseIterativeDiagnostics diagnostics = solver.solve(singular, rhs, x, params);
    EXPECT_FALSE(diagnostics.Succeeded());
    EXPECT_TRUE(diagnostics.Status == SparseIterativeStatus::NotConverged
                || diagnostics.Status == SparseIterativeStatus::NumericalIssue);
    EXPECT_EQ(x, (std::array<double, 2>{42.0, -7.0}));
}

TEST(SparseNonsymmetric, RejectsInvalidInputs)
{
    Geometry::Sparse::SparseBiCGSTAB solver;

    Geometry::Sparse::SparseBuilder nonSquareBuilder(2, 3);
    nonSquareBuilder.Add(0, 0, 1.0);
    nonSquareBuilder.Add(1, 1, 1.0);
    const std::array<double, 2> rhs{1.0, 2.0};
    std::array<double, 2> x{};
    EXPECT_EQ(
        solver.solve(nonSquareBuilder.Build().Matrix, rhs, x, DefaultParams()).Status,
        SparseIterativeStatus::DimensionMismatch);

    SparseMatrix nonFinite;
    nonFinite.Rows = 2;
    nonFinite.Cols = 2;
    nonFinite.RowOffsets = {0, 1, 2};
    nonFinite.ColIndices = {0, 1};
    nonFinite.Values = {1.0, std::numeric_limits<double>::infinity()};
    EXPECT_EQ(solver.solve(nonFinite, rhs, x, DefaultParams()).Status, SparseIterativeStatus::InvalidInput);

    const SparseMatrix matrix = BuildAdvectionDiffusion();
    std::array<double, 3> nonFiniteRhs{1.0, std::numeric_limits<double>::quiet_NaN(), 0.0};
    std::array<double, 3> validX{};
    EXPECT_EQ(solver.solve(matrix, nonFiniteRhs, validX, DefaultParams()).Status, SparseIterativeStatus::InvalidInput);

    std::array<double, 2> tooSmall{};
    EXPECT_EQ(solver.solve(matrix, rhs, tooSmall, DefaultParams()).Status, SparseIterativeStatus::DimensionMismatch);

    SparseBiCGSTABParams invalidParams = DefaultParams();
    invalidParams.RelativeTolerance = 0.0;
    const std::array<double, 3> finiteRhs{1.0, 2.0, 3.0};
    EXPECT_EQ(solver.solve(matrix, finiteRhs, validX, invalidParams).Status, SparseIterativeStatus::InvalidInput);

    invalidParams = DefaultParams();
    invalidParams.Preconditioner = static_cast<SparsePreconditioner>(255);
    EXPECT_EQ(solver.solve(matrix, finiteRhs, validX, invalidParams).Status, SparseIterativeStatus::InvalidInput);
}

TEST(SparseNonsymmetric, MultiRhsSolveMatchesKnownSolutions)
{
    const SparseMatrix matrix = BuildAdvectionDiffusion();
    Eigen::MatrixXd expected(3, 2);
    expected << 1.0, -1.0,
        -2.0, 0.5,
        0.5, 2.0;

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

    Geometry::Sparse::SparseBiCGSTAB solver;
    Eigen::MatrixXd solved = Eigen::MatrixXd::Zero(3, 2);
    const SparseIterativeDiagnostics diagnostics = solver.solve(matrix, rhs, solved, DefaultParams());
    ExpectSuccess(diagnostics, SparsePreconditioner::Diagonal);

    for (Eigen::Index row = 0; row < expected.rows(); ++row)
    {
        for (Eigen::Index col = 0; col < expected.cols(); ++col)
        {
            EXPECT_NEAR(solved(row, col), expected(row, col), 1.0e-10);
        }
    }
}

TEST(SparseNonsymmetric, PreconditionersAgreeWithinTolerance)
{
    const SparseMatrix matrix = BuildAdvectionDiffusion();
    const std::array<double, 3> expected{1.0, -2.0, 0.5};
    const std::vector<double> rhs = Multiply(matrix, expected);

    Geometry::Sparse::SparseBiCGSTAB solver;
    std::array<double, 3> none{};
    std::array<double, 3> diagonal{};
    std::array<double, 3> ilut{};

    ExpectSuccess(
        solver.solve(matrix, rhs, none, DefaultParams(SparsePreconditioner::None)),
        SparsePreconditioner::None);
    ExpectSuccess(
        solver.solve(matrix, rhs, diagonal, DefaultParams(SparsePreconditioner::Diagonal)),
        SparsePreconditioner::Diagonal);
    ExpectSuccess(
        solver.solve(matrix, rhs, ilut, DefaultParams(SparsePreconditioner::IncompleteLUT)),
        SparsePreconditioner::IncompleteLUT);

    ExpectNear(none, diagonal, 1.0e-10);
    ExpectNear(ilut, diagonal, 1.0e-10);
}

TEST(SparseNonsymmetric, IdenticalInputsAreBitStable)
{
    const SparseMatrix matrix = BuildAdvectionDiffusion();
    const std::array<double, 3> expected{1.0, -2.0, 0.5};
    const std::vector<double> rhs = Multiply(matrix, expected);
    const SparseBiCGSTABParams params = DefaultParams(SparsePreconditioner::IncompleteLUT);

    Geometry::Sparse::SparseBiCGSTAB solver;
    std::array<double, 3> first{};
    std::array<double, 3> second{};
    const SparseIterativeDiagnostics firstDiagnostics = solver.solve(matrix, rhs, first, params);
    const SparseIterativeDiagnostics secondDiagnostics = solver.solve(matrix, rhs, second, params);

    EXPECT_EQ(firstDiagnostics.Status, secondDiagnostics.Status);
    EXPECT_EQ(firstDiagnostics.Iterations, secondDiagnostics.Iterations);
    EXPECT_DOUBLE_EQ(firstDiagnostics.FinalRelativeResidual, secondDiagnostics.FinalRelativeResidual);
    EXPECT_EQ(firstDiagnostics.Preconditioner, secondDiagnostics.Preconditioner);
    EXPECT_EQ(first, second);
}
