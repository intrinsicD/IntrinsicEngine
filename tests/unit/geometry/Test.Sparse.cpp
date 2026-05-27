#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <vector>

import Geometry.Sparse;

TEST(Sparse, BuilderSortsMergesDropsAndMultiplies)
{
    Geometry::Sparse::SparseBuilder builder(3, 3);
    builder.Add(1, 2, 4.0);
    builder.Add(0, 0, 1.0);
    builder.Add(1, 2, 2.0);
    builder.Add(0, 1, 0.0);
    builder.Add(9, 9, 1.0);
    builder.Add(2, 2, std::numeric_limits<double>::infinity());

    const Geometry::Sparse::SparseBuildResult built = builder.Build(1.0e-15);
    EXPECT_FALSE(built.Valid);
    EXPECT_EQ(built.DroppedOutOfBoundsEntries, 1u);
    EXPECT_EQ(built.DroppedNearZeroEntries, 2u);
    EXPECT_EQ(built.MergedDuplicateEntries, 1u);

    const Geometry::Sparse::SparseMatrix& matrix = built.Matrix;
    ASSERT_EQ(matrix.RowOffsets, (std::vector<std::size_t>{0u, 1u, 2u, 2u}));
    ASSERT_EQ(matrix.ColIndices, (std::vector<std::size_t>{0u, 2u}));
    ASSERT_EQ(matrix.Values, (std::vector<double>{1.0, 6.0}));

    const std::array<double, 3> x{2.0, 3.0, 5.0};
    std::array<double, 3> y{};
    matrix.Multiply(x, y);
    EXPECT_DOUBLE_EQ(y[0], 2.0);
    EXPECT_DOUBLE_EQ(y[1], 30.0);
    EXPECT_DOUBLE_EQ(y[2], 0.0);

    std::array<double, 3> yt{};
    matrix.MultiplyTranspose(y, yt);
    EXPECT_DOUBLE_EQ(yt[0], 2.0);
    EXPECT_DOUBLE_EQ(yt[1], 0.0);
    EXPECT_DOUBLE_EQ(yt[2], 180.0);
}

TEST(Sparse, DiagnosticsClassifyLaplacianLikeMatrix)
{
    Geometry::Sparse::SparseBuilder builder(2, 2);
    builder.Add(0, 0, 1.0);
    builder.Add(0, 1, -1.0);
    builder.Add(1, 0, -1.0);
    builder.Add(1, 1, 1.0);
    const Geometry::Sparse::SparseBuildResult built = builder.Build();
    ASSERT_TRUE(built.Valid);

    const Geometry::Sparse::SparseDiagnostics diagnostics = Geometry::Sparse::AnalyzeSparseMatrix(built.Matrix);
    EXPECT_TRUE(diagnostics.StructurallyValid());
    EXPECT_TRUE(diagnostics.IsSymmetric);
    EXPECT_TRUE(diagnostics.HasZeroRowSums);
    EXPECT_TRUE(diagnostics.HasPositiveDiagonal);
    EXPECT_TRUE(diagnostics.HasNonPositiveOffDiagonal);
    EXPECT_NEAR(diagnostics.MaxSymmetryError, 0.0, 1.0e-12);
    EXPECT_NEAR(diagnostics.MaxRowSumError, 0.0, 1.0e-12);
}

TEST(Sparse, ConjugateGradientReportsConvergenceAndBreakdown)
{
    Geometry::Sparse::SparseBuilder spdBuilder(2, 2);
    spdBuilder.Add(0, 0, 4.0);
    spdBuilder.Add(0, 1, 1.0);
    spdBuilder.Add(1, 0, 1.0);
    spdBuilder.Add(1, 1, 3.0);
    const Geometry::Sparse::SparseMatrix spd = spdBuilder.Build().Matrix;

    const std::array<double, 2> b{1.0, 2.0};
    std::array<double, 2> x{0.0, 0.0};
    const Geometry::Sparse::CGResult result = Geometry::Sparse::SolveCG(spd, b, x);
    EXPECT_TRUE(result.Converged);
    EXPECT_EQ(result.Reason, Geometry::Sparse::CGConvergenceReason::Converged);
    EXPECT_LT(result.ResidualNorm, 1.0e-8);
    EXPECT_NEAR(x[0], 1.0 / 11.0, 1.0e-10);
    EXPECT_NEAR(x[1], 7.0 / 11.0, 1.0e-10);

    Geometry::Sparse::SparseMatrix zero;
    zero.Rows = 2;
    zero.Cols = 2;
    zero.RowOffsets = {0, 0, 0};
    std::array<double, 2> singularX{0.0, 0.0};
    const Geometry::Sparse::CGResult singular = Geometry::Sparse::SolveCG(zero, b, singularX);
    EXPECT_FALSE(singular.Converged);
    EXPECT_EQ(singular.Reason, Geometry::Sparse::CGConvergenceReason::Breakdown);
}

TEST(Sparse, ShiftedConjugateGradientUsesDiagonalMassTerm)
{
    Geometry::Sparse::SparseBuilder stiffnessBuilder(2, 2);
    stiffnessBuilder.Add(0, 0, 1.0);
    stiffnessBuilder.Add(0, 1, -1.0);
    stiffnessBuilder.Add(1, 0, -1.0);
    stiffnessBuilder.Add(1, 1, 1.0);

    Geometry::Sparse::DiagonalMatrix mass;
    mass.Size = 2;
    mass.Diagonal = {2.0, 2.0};

    const Geometry::Sparse::SparseMatrix stiffness = stiffnessBuilder.Build().Matrix;
    const std::array<double, 2> b{1.0, 3.0};
    std::array<double, 2> x{0.0, 0.0};
    const Geometry::Sparse::CGResult result = Geometry::Sparse::SolveCGShifted(mass, 1.0, stiffness, 1.0, b, x);
    EXPECT_TRUE(result.Converged);
    EXPECT_NEAR(x[0], 0.75, 1.0e-10);
    EXPECT_NEAR(x[1], 1.25, 1.0e-10);
}

