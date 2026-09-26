#include <algorithm>
#include <cmath>
#include <numbers>
#include <random>
#include <vector>
#include <Eigen/Dense>
#include <gtest/gtest.h>
import Geometry.Sparse;
import Geometry.DEC;
import Geometry.HalfedgeMesh;
import Geometry.Properties;
namespace Sp = Geometry::Sparse;
namespace
{
    // Path-graph Laplacian with Dirichlet (dirichlet = true: diagonal 2) or Neumann ends.
    Sp::SparseMatrix PathLaplacian(std::size_t n, bool dirichlet)
    {
        Sp::SparseBuilder builder(n, n);
        for (std::size_t i = 0; i < n; ++i)
        {
            double diagonal = 0;
            if (i > 0) { builder.Add(i, i - 1, -1.0); diagonal += 1; }
            if (i + 1 < n) { builder.Add(i, i + 1, -1.0); diagonal += 1; }
            builder.Add(i, i, dirichlet ? 2.0 : diagonal);
        }
        return builder.Build().Matrix;
    }
    Sp::DiagonalMatrix Identity(std::size_t n) { return {n, std::vector<double>(n, 1.0)}; }

    Eigen::MatrixXd Dense(const Sp::SparseMatrix& m)
    {
        Eigen::MatrixXd d = Eigen::MatrixXd::Zero(Eigen::Index(m.Rows), Eigen::Index(m.Cols));
        for (std::size_t r = 0; r < m.Rows; ++r)
            for (auto k = m.RowOffsets[r]; k < m.RowOffsets[r + 1]; ++k) d(Eigen::Index(r), Eigen::Index(m.ColIndices[k])) += m.Values[k];
        return d;
    }

    Geometry::HalfedgeMesh::Mesh Grid(int size)
    {
        std::mt19937 random(24);
        std::uniform_real_distribution<float> jitter(-0.2f, 0.2f);
        Geometry::HalfedgeMesh::Mesh mesh;
        std::vector<Geometry::VertexHandle> v;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                v.push_back(mesh.AddVertex({float(x) + jitter(random), float(y) + jitter(random), 0.2f * std::sin(float(x * y))}));
        for (int y = 0; y + 1 < size; ++y)
            for (int x = 0; x + 1 < size; ++x)
            {
                const auto at = [&](int i, int j) { return v[std::size_t(j * size + i)]; };
                (void)mesh.AddTriangle(at(x, y), at(x + 1, y), at(x + 1, y + 1));
                (void)mesh.AddTriangle(at(x, y), at(x + 1, y + 1), at(x, y + 1));
            }
        return mesh;
    }
}

TEST(SparseEigensolver, DirichletPathMatchesAnalyticEigenvalues)
{
    const std::size_t n = 60;
    const auto result = Sp::SolveSymmetricGeneralizedEigen(PathLaplacian(n, true), Identity(n), {.Count = 6});
    ASSERT_TRUE(result.Succeeded()) << result.Diagnostic;
    ASSERT_EQ(result.Eigenvalues.size(), 6u);
    for (std::size_t j = 0; j < 6; ++j)
    {
        const double expected = 2.0 - 2.0 * std::cos(double(j + 1) * std::numbers::pi / double(n + 1));
        EXPECT_NEAR(result.Eigenvalues[j], expected, 1e-10 * std::max(1.0, expected));
        EXPECT_LE(result.RelativeResiduals[j], 1e-10);
    }
}

TEST(SparseEigensolver, NeumannPathFindsTheConstantNullspaceWithTheDefaultShift)
{
    const std::size_t n = 40;
    const auto result = Sp::SolveSymmetricGeneralizedEigen(PathLaplacian(n, false), Identity(n), {.Count = 4});
    ASSERT_TRUE(result.Succeeded()) << result.Diagnostic;
    EXPECT_LT(result.Shift, 0.0);
    for (std::size_t j = 0; j < 4; ++j)
        EXPECT_NEAR(result.Eigenvalues[j], 2.0 - 2.0 * std::cos(double(j) * std::numbers::pi / double(n)), 1e-10);
    // The first eigenvector is constant, M-normalized and positive by the sign convention.
    for (std::size_t r = 0; r < n; ++r) EXPECT_NEAR(result.Eigenvectors[r], 1.0 / std::sqrt(double(n)), 1e-8);
}

TEST(SparseEigensolver, CotanLaplacianWithLumpedMassMatchesADenseOracle)
{
    const auto mesh = Grid(9);
    const auto ops = Geometry::DEC::BuildOperators(mesh);
    ASSERT_TRUE(ops.IsValid());
    const std::size_t n = ops.Laplacian.Rows, k = 8;
    const auto result = Sp::SolveSymmetricGeneralizedEigen(ops.Laplacian, ops.Hodge0, {.Count = k});
    ASSERT_TRUE(result.Succeeded()) << result.Diagnostic;
    Eigen::MatrixXd A = Dense(ops.Laplacian), M = Eigen::MatrixXd::Zero(Eigen::Index(n), Eigen::Index(n));
    for (std::size_t i = 0; i < n; ++i) M(Eigen::Index(i), Eigen::Index(i)) = ops.Hodge0.Diagonal[i];
    const Eigen::GeneralizedSelfAdjointEigenSolver<Eigen::MatrixXd> oracle(A, M);
    ASSERT_EQ(oracle.info(), Eigen::Success);
    Eigen::MatrixXd Z(static_cast<Eigen::Index>(n), static_cast<Eigen::Index>(k));
    for (std::size_t j = 0; j < k; ++j)
    {
        EXPECT_NEAR(result.Eigenvalues[j], oracle.eigenvalues()(Eigen::Index(j)), 1e-8 * std::max(1.0, oracle.eigenvalues()(Eigen::Index(j))));
        for (std::size_t r = 0; r < n; ++r) Z(Eigen::Index(r), Eigen::Index(j)) = result.Eigenvectors[j * n + r];
    }
    // M-orthonormal eigenvectors, and each spans the oracle's direction for simple eigenvalues.
    const Eigen::MatrixXd gram = Z.transpose() * M * Z;
    EXPECT_LE((gram - Eigen::MatrixXd::Identity(Eigen::Index(k), Eigen::Index(k))).cwiseAbs().maxCoeff(), 1e-9);
    for (std::size_t j = 0; j < k; ++j)
    {
        const double gapBelow = j == 0 ? 1.0 : oracle.eigenvalues()(Eigen::Index(j)) - oracle.eigenvalues()(Eigen::Index(j - 1));
        const double gapAbove = oracle.eigenvalues()(Eigen::Index(j + 1)) - oracle.eigenvalues()(Eigen::Index(j));
        if (std::min(gapBelow, gapAbove) < 1e-3) continue;
        const double alignment = std::abs(Z.col(Eigen::Index(j)).dot(M * oracle.eigenvectors().col(Eigen::Index(j))));
        EXPECT_NEAR(alignment, 1.0, 1e-7) << j;
    }
}

TEST(SparseEigensolver, RejectsInvalidInputsAndIndefiniteShifts)
{
    const auto A = PathLaplacian(10, true);
    EXPECT_EQ(Sp::SolveSymmetricGeneralizedEigen(A, Identity(10), {.Count = 10}).Status, Sp::SymmetricEigenStatus::InvalidInput);
    EXPECT_EQ(Sp::SolveSymmetricGeneralizedEigen(A, Identity(10), {.Count = 0}).Status, Sp::SymmetricEigenStatus::InvalidInput);
    EXPECT_EQ(Sp::SolveSymmetricGeneralizedEigen(A, Identity(9), {.Count = 2}).Status, Sp::SymmetricEigenStatus::DimensionMismatch);
    EXPECT_EQ(Sp::SolveSymmetricGeneralizedEigen(A, Identity(10), {.Count = 2, .SubspaceDimension = 2}).Status,
              Sp::SymmetricEigenStatus::InvalidInput);
    Sp::DiagonalMatrix negative = Identity(10);
    negative.Diagonal[3] = -1.0;
    EXPECT_EQ(Sp::SolveSymmetricGeneralizedEigen(A, negative, {.Count = 2}).Status, Sp::SymmetricEigenStatus::InvalidInput)
        << "M must be SPD";
    Sp::SparseBuilder skew(3, 3);
    skew.Add(0, 0, 1); skew.Add(1, 1, 1); skew.Add(2, 2, 1); skew.Add(0, 1, 0.5);
    EXPECT_EQ(Sp::SolveSymmetricGeneralizedEigen(skew.Build().Matrix, Identity(3), {.Count = 1, .SubspaceDimension = 3}).Status,
              Sp::SymmetricEigenStatus::InvalidInput) << "A must be symmetric";
    // Indefinite A: the default shift is not below the spectrum, an explicit one is.
    Sp::SparseBuilder indefinite(6, 6);
    for (std::size_t i = 0; i < 6; ++i) indefinite.Add(i, i, double(i) - 1.0);
    const auto D = indefinite.Build().Matrix;
    EXPECT_EQ(Sp::SolveSymmetricGeneralizedEigen(D, Identity(6), {.Count = 2, .SubspaceDimension = 5}).Status,
              Sp::SymmetricEigenStatus::NumericalIssue);
    const auto shifted = Sp::SolveSymmetricGeneralizedEigen(D, Identity(6), {.Count = 2, .Shift = -2.0, .SubspaceDimension = 5});
    ASSERT_TRUE(shifted.Succeeded()) << shifted.Diagnostic;
    EXPECT_NEAR(shifted.Eigenvalues[0], -1.0, 1e-12);
    EXPECT_NEAR(shifted.Eigenvalues[1], 0.0, 1e-12);
}

TEST(SparseEigensolver, IsReproducibleAndReportsNonConvergence)
{
    const auto mesh = Grid(7);
    const auto ops = Geometry::DEC::BuildOperators(mesh);
    const auto first = Sp::SolveSymmetricGeneralizedEigen(ops.Laplacian, ops.Hodge0, {.Count = 5});
    const auto second = Sp::SolveSymmetricGeneralizedEigen(ops.Laplacian, ops.Hodge0, {.Count = 5});
    ASSERT_TRUE(first.Succeeded() && second.Succeeded());
    EXPECT_EQ(first.Eigenvalues, second.Eigenvalues);
    EXPECT_EQ(first.Eigenvectors, second.Eigenvectors);
    EXPECT_EQ(first.Iterations, second.Iterations);
    const auto stalled = Sp::SolveSymmetricGeneralizedEigen(ops.Laplacian, ops.Hodge0,
        {.Count = 5, .MaxIterations = 1, .Tolerance = 1e-14});
    EXPECT_EQ(stalled.Status, Sp::SymmetricEigenStatus::NotConverged);
    EXPECT_LT(stalled.ConvergedCount, 5u);
    EXPECT_EQ(stalled.Eigenvalues.size(), 5u) << "the best Ritz pairs are still returned";
}
