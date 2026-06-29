module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#ifndef EIGEN_MPL2_ONLY
#define EIGEN_MPL2_ONLY
#endif

#ifndef EIGEN_DONT_PARALLELIZE
#define EIGEN_DONT_PARALLELIZE
#endif

#include <Eigen/IterativeLinearSolvers>
#include <Eigen/SparseCholesky>
#include <Eigen/SparseCore>

module Geometry.Sparse;

namespace Geometry::Sparse
{
    namespace
    {
        using EigenSparseMatrix = Eigen::SparseMatrix<double, Eigen::ColMajor, int>;
        using EigenIterativeSparseMatrix = Eigen::SparseMatrix<double, Eigen::RowMajor, int>;
        using EigenLDLT = Eigen::SimplicialLDLT<EigenSparseMatrix>;
        using EigenLLT = Eigen::SimplicialLLT<EigenSparseMatrix>;
        using EigenBiCGSTABIdentity = Eigen::BiCGSTAB<EigenIterativeSparseMatrix, Eigen::IdentityPreconditioner>;
        using EigenBiCGSTABDiagonal = Eigen::BiCGSTAB<EigenIterativeSparseMatrix, Eigen::DiagonalPreconditioner<double>>;
        using EigenBiCGSTABILUT = Eigen::BiCGSTAB<EigenIterativeSparseMatrix, Eigen::IncompleteLUT<double, int>>;

        constexpr double kSymmetryTolerance = 1.0e-10;
        constexpr double kPivotTolerance = 1.0e-12;

        [[nodiscard]] double Norm(std::span<const double> values)
        {
            double sum = 0.0;
            for (const double value : values)
            {
                sum += value * value;
            }
            return std::sqrt(sum);
        }

        [[nodiscard]] bool IsFiniteSpan(std::span<const double> values)
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

        [[nodiscard]] bool ValidateCsr(const SparseMatrix& matrix)
        {
            if (matrix.RowOffsets.size() != matrix.Rows + 1
                || matrix.ColIndices.size() != matrix.Values.size())
            {
                return false;
            }

            if (matrix.RowOffsets.empty())
            {
                return matrix.Rows == 0;
            }

            if (matrix.RowOffsets.front() != 0 || matrix.RowOffsets.back() != matrix.Values.size())
            {
                return false;
            }

            for (std::size_t row = 0; row < matrix.Rows; ++row)
            {
                if (matrix.RowOffsets[row] > matrix.RowOffsets[row + 1])
                {
                    return false;
                }
            }

            for (const std::size_t col : matrix.ColIndices)
            {
                if (col >= matrix.Cols)
                {
                    return false;
                }
            }

            return IsFiniteSpan(matrix.Values);
        }

        [[nodiscard]] double FindValue(const SparseMatrix& matrix, std::size_t row, std::size_t col)
        {
            if (row >= matrix.Rows)
            {
                return 0.0;
            }

            for (std::size_t k = matrix.RowOffsets[row]; k < matrix.RowOffsets[row + 1]; ++k)
            {
                if (matrix.ColIndices[k] == col)
                {
                    return matrix.Values[k];
                }
            }
            return 0.0;
        }

        [[nodiscard]] SparseFactorizationDiagnostics MakeDiagnostics(
            SparseFactorizationStatus status,
            std::size_t pivotCount = 0,
            double smallestAbsolutePivot = 0.0)
        {
            SparseFactorizationDiagnostics diagnostics;
            diagnostics.Status = status;
            diagnostics.PivotCount = pivotCount;
            diagnostics.SmallestAbsolutePivot = smallestAbsolutePivot;
            diagnostics.ConditionEstimate = 0.0;
            return diagnostics;
        }

        [[nodiscard]] SparseIterativeDiagnostics MakeIterativeDiagnostics(
            SparseIterativeStatus status,
            SparsePreconditioner preconditioner,
            std::size_t iterations = 0,
            double finalRelativeResidual = 0.0)
        {
            SparseIterativeDiagnostics diagnostics;
            diagnostics.Status = status;
            diagnostics.Iterations = iterations;
            diagnostics.FinalRelativeResidual = finalRelativeResidual;
            diagnostics.Preconditioner = preconditioner;
            return diagnostics;
        }

        [[nodiscard]] bool IsSupportedPreconditioner(SparsePreconditioner preconditioner)
        {
            switch (preconditioner)
            {
            case SparsePreconditioner::None:
            case SparsePreconditioner::Diagonal:
            case SparsePreconditioner::IncompleteLUT:
                return true;
            }
            return false;
        }

        [[nodiscard]] bool IsSymmetric(const SparseMatrix& matrix, double tolerance)
        {
            if (matrix.Rows != matrix.Cols)
            {
                return false;
            }

            for (std::size_t row = 0; row < matrix.Rows; ++row)
            {
                for (std::size_t k = matrix.RowOffsets[row]; k < matrix.RowOffsets[row + 1]; ++k)
                {
                    const std::size_t col = matrix.ColIndices[k];
                    const double mirror = FindValue(matrix, col, row);
                    if (std::abs(matrix.Values[k] - mirror) > tolerance)
                    {
                        return false;
                    }
                }
            }
            return true;
        }

        [[nodiscard]] SparseFactorizationDiagnostics ValidateFactorizationInput(const SparseMatrix& matrix)
        {
            if (matrix.Rows != matrix.Cols)
            {
                return MakeDiagnostics(SparseFactorizationStatus::DimensionMismatch);
            }
            if (matrix.Rows > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return MakeDiagnostics(SparseFactorizationStatus::InvalidInput);
            }
            if (!ValidateCsr(matrix))
            {
                return MakeDiagnostics(SparseFactorizationStatus::InvalidInput);
            }
            if (!IsSymmetric(matrix, kSymmetryTolerance))
            {
                return MakeDiagnostics(SparseFactorizationStatus::InvalidInput);
            }
            return MakeDiagnostics(SparseFactorizationStatus::Success);
        }

        [[nodiscard]] EigenSparseMatrix ToEigenSparseMatrix(const SparseMatrix& matrix)
        {
            std::vector<Eigen::Triplet<double, int>> triplets;
            triplets.reserve(matrix.Values.size());
            for (std::size_t row = 0; row < matrix.Rows; ++row)
            {
                for (std::size_t k = matrix.RowOffsets[row]; k < matrix.RowOffsets[row + 1]; ++k)
                {
                    triplets.emplace_back(
                        static_cast<int>(row),
                        static_cast<int>(matrix.ColIndices[k]),
                        matrix.Values[k]);
                }
            }

            EigenSparseMatrix result(static_cast<int>(matrix.Rows), static_cast<int>(matrix.Cols));
            result.setFromTriplets(triplets.begin(), triplets.end());
            result.makeCompressed();
            return result;
        }

        [[nodiscard]] EigenIterativeSparseMatrix ToEigenIterativeSparseMatrix(const SparseMatrix& matrix)
        {
            std::vector<Eigen::Triplet<double, int>> triplets;
            triplets.reserve(matrix.Values.size());
            for (std::size_t row = 0; row < matrix.Rows; ++row)
            {
                for (std::size_t k = matrix.RowOffsets[row]; k < matrix.RowOffsets[row + 1]; ++k)
                {
                    triplets.emplace_back(
                        static_cast<int>(row),
                        static_cast<int>(matrix.ColIndices[k]),
                        matrix.Values[k]);
                }
            }

            EigenIterativeSparseMatrix result(static_cast<int>(matrix.Rows), static_cast<int>(matrix.Cols));
            result.setFromTriplets(triplets.begin(), triplets.end());
            result.makeCompressed();
            return result;
        }

        [[nodiscard]] SparseFactorizationDiagnostics DiagnosticsFromLDLT(
            const EigenLDLT& solver,
            std::size_t dimension)
        {
            const auto diagonal = solver.vectorD();
            std::size_t pivotCount = 0;
            double minAbsPivot = std::numeric_limits<double>::infinity();
            bool hasNegativePivot = false;
            bool hasZeroPivot = false;

            for (Eigen::Index i = 0; i < diagonal.size(); ++i)
            {
                const double pivot = diagonal[i];
                if (!std::isfinite(pivot))
                {
                    return MakeDiagnostics(SparseFactorizationStatus::NumericalIssue, pivotCount, 0.0);
                }

                const double absPivot = std::abs(pivot);
                minAbsPivot = std::min(minAbsPivot, absPivot);
                if (absPivot <= kPivotTolerance)
                {
                    hasZeroPivot = true;
                }
                else
                {
                    ++pivotCount;
                }
                if (pivot < -kPivotTolerance)
                {
                    hasNegativePivot = true;
                }
            }

            if (!std::isfinite(minAbsPivot))
            {
                minAbsPivot = 0.0;
            }
            if (hasNegativePivot)
            {
                return MakeDiagnostics(SparseFactorizationStatus::NonSPD, pivotCount, minAbsPivot);
            }
            if (hasZeroPivot)
            {
                return MakeDiagnostics(SparseFactorizationStatus::ZeroPivot, pivotCount, minAbsPivot);
            }

            if (solver.info() == Eigen::Success)
            {
                return MakeDiagnostics(SparseFactorizationStatus::Success, dimension, minAbsPivot);
            }
            if (solver.info() == Eigen::InvalidInput)
            {
                return MakeDiagnostics(SparseFactorizationStatus::InvalidInput, pivotCount, minAbsPivot);
            }
            return MakeDiagnostics(SparseFactorizationStatus::NumericalIssue, pivotCount, minAbsPivot);
        }

        [[nodiscard]] SparseFactorizationDiagnostics DiagnosticsFromLLT(
            const EigenLLT& solver,
            const EigenSparseMatrix& matrix,
            std::size_t dimension)
        {
            if (solver.info() == Eigen::Success)
            {
                const EigenSparseMatrix lower = solver.matrixL();
                double minAbsPivot = std::numeric_limits<double>::infinity();
                for (Eigen::Index i = 0; i < lower.rows(); ++i)
                {
                    minAbsPivot = std::min(minAbsPivot, std::abs(lower.coeff(i, i)));
                }
                if (!std::isfinite(minAbsPivot))
                {
                    minAbsPivot = 0.0;
                }
                return MakeDiagnostics(SparseFactorizationStatus::Success, dimension, minAbsPivot);
            }
            if (solver.info() == Eigen::InvalidInput)
            {
                return MakeDiagnostics(SparseFactorizationStatus::InvalidInput);
            }

            EigenLDLT probe;
            probe.compute(matrix);
            SparseFactorizationDiagnostics diagnostics = DiagnosticsFromLDLT(probe, dimension);
            if (diagnostics.Status == SparseFactorizationStatus::Success)
            {
                diagnostics.Status = SparseFactorizationStatus::NumericalIssue;
            }
            return diagnostics;
        }

        [[nodiscard]] SparseFactorizationDiagnostics ValidateVectorSolveInput(
            const SparseFactorizationDiagnostics& factorDiagnostics,
            std::size_t dimension,
            std::span<const double> rhs,
            std::span<double> x)
        {
            if (!factorDiagnostics.Succeeded())
            {
                return MakeDiagnostics(SparseFactorizationStatus::NotFactored);
            }
            if (rhs.size() < dimension || x.size() < dimension)
            {
                return MakeDiagnostics(SparseFactorizationStatus::DimensionMismatch);
            }
            if (!IsFiniteSpan(rhs.first(dimension)))
            {
                return MakeDiagnostics(SparseFactorizationStatus::InvalidInput);
            }
            return factorDiagnostics;
        }

        [[nodiscard]] SparseFactorizationDiagnostics ValidateDenseSolveInput(
            const SparseFactorizationDiagnostics& factorDiagnostics,
            std::size_t dimension,
            ConstEigenDenseBlockRef rhs,
            EigenDenseBlockRef x)
        {
            if (!factorDiagnostics.Succeeded())
            {
                return MakeDiagnostics(SparseFactorizationStatus::NotFactored);
            }
            if (rhs.rows() != static_cast<Eigen::Index>(dimension)
                || x.rows() != static_cast<Eigen::Index>(dimension)
                || rhs.cols() != x.cols())
            {
                return MakeDiagnostics(SparseFactorizationStatus::DimensionMismatch);
            }
            if (!rhs.allFinite())
            {
                return MakeDiagnostics(SparseFactorizationStatus::InvalidInput);
            }
            return factorDiagnostics;
        }

        [[nodiscard]] SparseFactorizationDiagnostics FinalizeSolve(
            const SparseFactorizationDiagnostics& factorDiagnostics,
            Eigen::ComputationInfo info,
            bool outputFinite)
        {
            if (info == Eigen::InvalidInput)
            {
                return MakeDiagnostics(SparseFactorizationStatus::InvalidInput);
            }
            if (info != Eigen::Success || !outputFinite)
            {
                return MakeDiagnostics(
                    SparseFactorizationStatus::NumericalIssue,
                    factorDiagnostics.PivotCount,
                    factorDiagnostics.SmallestAbsolutePivot);
            }
            return factorDiagnostics;
        }

        [[nodiscard]] SparseIterativeDiagnostics ValidateBiCGSTABMatrixInput(
            const SparseMatrix& matrix,
            const SparseBiCGSTABParams& params)
        {
            if (matrix.Rows != matrix.Cols)
            {
                return MakeIterativeDiagnostics(
                    SparseIterativeStatus::DimensionMismatch,
                    params.Preconditioner);
            }
            if (matrix.Rows > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return MakeIterativeDiagnostics(SparseIterativeStatus::InvalidInput, params.Preconditioner);
            }
            if (!IsSupportedPreconditioner(params.Preconditioner))
            {
                return MakeIterativeDiagnostics(SparseIterativeStatus::InvalidInput, params.Preconditioner);
            }
            if (params.MaxIterations > static_cast<std::size_t>(std::numeric_limits<Eigen::Index>::max()))
            {
                return MakeIterativeDiagnostics(SparseIterativeStatus::InvalidInput, params.Preconditioner);
            }
            if (!ValidateCsr(matrix))
            {
                return MakeIterativeDiagnostics(SparseIterativeStatus::InvalidInput, params.Preconditioner);
            }
            if (params.MaxIterations == 0
                || !std::isfinite(params.RelativeTolerance)
                || params.RelativeTolerance <= 0.0)
            {
                return MakeIterativeDiagnostics(SparseIterativeStatus::InvalidInput, params.Preconditioner);
            }
            return MakeIterativeDiagnostics(SparseIterativeStatus::Success, params.Preconditioner);
        }

        [[nodiscard]] SparseIterativeDiagnostics ValidateBiCGSTABVectorInput(
            const SparseMatrix& matrix,
            std::span<const double> rhs,
            std::span<double> x,
            const SparseBiCGSTABParams& params)
        {
            SparseIterativeDiagnostics diagnostics = ValidateBiCGSTABMatrixInput(matrix, params);
            if (!diagnostics.Succeeded())
            {
                return diagnostics;
            }
            if (rhs.size() < matrix.Rows || x.size() < matrix.Rows)
            {
                return MakeIterativeDiagnostics(
                    SparseIterativeStatus::DimensionMismatch,
                    params.Preconditioner);
            }
            if (!IsFiniteSpan(rhs.first(matrix.Rows)))
            {
                return MakeIterativeDiagnostics(SparseIterativeStatus::InvalidInput, params.Preconditioner);
            }
            return diagnostics;
        }

        [[nodiscard]] SparseIterativeDiagnostics ValidateBiCGSTABDenseInput(
            const SparseMatrix& matrix,
            ConstEigenDenseBlockRef rhs,
            EigenDenseBlockRef x,
            const SparseBiCGSTABParams& params)
        {
            SparseIterativeDiagnostics diagnostics = ValidateBiCGSTABMatrixInput(matrix, params);
            if (!diagnostics.Succeeded())
            {
                return diagnostics;
            }
            if (rhs.rows() != static_cast<Eigen::Index>(matrix.Rows)
                || x.rows() != static_cast<Eigen::Index>(matrix.Rows)
                || rhs.cols() != x.cols())
            {
                return MakeIterativeDiagnostics(
                    SparseIterativeStatus::DimensionMismatch,
                    params.Preconditioner);
            }
            if (!rhs.allFinite())
            {
                return MakeIterativeDiagnostics(SparseIterativeStatus::InvalidInput, params.Preconditioner);
            }
            return diagnostics;
        }

        [[nodiscard]] SparseIterativeStatus StatusFromEigenInfo(Eigen::ComputationInfo info, bool outputFinite)
        {
            if (!outputFinite)
            {
                return SparseIterativeStatus::NumericalIssue;
            }
            if (info == Eigen::Success)
            {
                return SparseIterativeStatus::Success;
            }
            if (info == Eigen::NoConvergence)
            {
                return SparseIterativeStatus::NotConverged;
            }
            if (info == Eigen::InvalidInput)
            {
                return SparseIterativeStatus::InvalidInput;
            }
            return SparseIterativeStatus::NumericalIssue;
        }

        [[nodiscard]] SparseIterativeStatus MergeIterativeStatus(
            SparseIterativeStatus current,
            SparseIterativeStatus next)
        {
            if (current == SparseIterativeStatus::NumericalIssue || next == SparseIterativeStatus::NumericalIssue)
            {
                return SparseIterativeStatus::NumericalIssue;
            }
            if (current == SparseIterativeStatus::InvalidInput || next == SparseIterativeStatus::InvalidInput)
            {
                return SparseIterativeStatus::InvalidInput;
            }
            if (current == SparseIterativeStatus::DimensionMismatch || next == SparseIterativeStatus::DimensionMismatch)
            {
                return SparseIterativeStatus::DimensionMismatch;
            }
            if (current == SparseIterativeStatus::NotConverged || next == SparseIterativeStatus::NotConverged)
            {
                return SparseIterativeStatus::NotConverged;
            }
            return SparseIterativeStatus::Success;
        }

        template <typename Solver>
        [[nodiscard]] SparseIterativeDiagnostics ConfigureAndComputeBiCGSTAB(
            Solver& solver,
            const EigenIterativeSparseMatrix& matrix,
            const SparseBiCGSTABParams& params)
        {
            solver.setMaxIterations(static_cast<Eigen::Index>(params.MaxIterations));
            solver.setTolerance(params.RelativeTolerance);
            solver.compute(matrix);
            if (solver.info() == Eigen::Success)
            {
                return MakeIterativeDiagnostics(SparseIterativeStatus::Success, params.Preconditioner);
            }
            return MakeIterativeDiagnostics(
                StatusFromEigenInfo(solver.info(), true),
                params.Preconditioner);
        }

        [[nodiscard]] SparseIterativeDiagnostics ConfigureAndComputeBiCGSTAB(
            EigenBiCGSTABILUT& solver,
            const EigenIterativeSparseMatrix& matrix,
            const SparseBiCGSTABParams& params)
        {
            solver.preconditioner().setDroptol(1.0e-4);
            solver.preconditioner().setFillfactor(10);
            solver.setMaxIterations(static_cast<Eigen::Index>(params.MaxIterations));
            solver.setTolerance(params.RelativeTolerance);
            solver.compute(matrix);
            if (solver.info() == Eigen::Success)
            {
                return MakeIterativeDiagnostics(SparseIterativeStatus::Success, params.Preconditioner);
            }
            return MakeIterativeDiagnostics(
                StatusFromEigenInfo(solver.info(), true),
                params.Preconditioner);
        }

        template <typename Solver>
        [[nodiscard]] SparseIterativeDiagnostics SolveBiCGSTABWithSolver(
            Solver& solver,
            const EigenIterativeSparseMatrix& matrix,
            ConstEigenDenseBlockRef rhs,
            EigenDenseBlockRef x,
            const SparseBiCGSTABParams& params)
        {
            SparseIterativeDiagnostics diagnostics = ConfigureAndComputeBiCGSTAB(solver, matrix, params);
            if (!diagnostics.Succeeded())
            {
                return diagnostics;
            }

            if (rhs.rows() == 0)
            {
                return diagnostics;
            }

            EigenDenseMatrixXd solved(rhs.rows(), rhs.cols());
            SparseIterativeStatus status = SparseIterativeStatus::Success;
            std::size_t maxIterations = 0;
            double maxRelativeResidual = 0.0;
            for (Eigen::Index col = 0; col < rhs.cols(); ++col)
            {
                Eigen::VectorXd solution = solver.solve(rhs.col(col));
                const SparseIterativeStatus columnStatus = StatusFromEigenInfo(
                    solver.info(),
                    solution.allFinite());
                status = MergeIterativeStatus(status, columnStatus);
                maxIterations = std::max(maxIterations, static_cast<std::size_t>(solver.iterations()));
                const double relativeResidual = static_cast<double>(solver.error());
                if (std::isfinite(relativeResidual))
                {
                    maxRelativeResidual = std::max(maxRelativeResidual, relativeResidual);
                }
                else
                {
                    status = MergeIterativeStatus(status, SparseIterativeStatus::NumericalIssue);
                }
                solved.col(col) = solution;
            }

            diagnostics = MakeIterativeDiagnostics(
                status,
                params.Preconditioner,
                maxIterations,
                maxRelativeResidual);
            if (!diagnostics.Succeeded())
            {
                return diagnostics;
            }

            x = solved;
            return diagnostics;
        }

        [[nodiscard]] SparseIterativeDiagnostics SolveBiCGSTABDense(
            const SparseMatrix& matrix,
            ConstEigenDenseBlockRef rhs,
            EigenDenseBlockRef x,
            const SparseBiCGSTABParams& params)
        {
            SparseIterativeDiagnostics diagnostics = ValidateBiCGSTABDenseInput(matrix, rhs, x, params);
            if (!diagnostics.Succeeded())
            {
                return diagnostics;
            }

            if (matrix.Rows == 0)
            {
                return diagnostics;
            }

            const EigenIterativeSparseMatrix eigenMatrix = ToEigenIterativeSparseMatrix(matrix);
            switch (params.Preconditioner)
            {
            case SparsePreconditioner::None:
            {
                EigenBiCGSTABIdentity solver;
                return SolveBiCGSTABWithSolver(solver, eigenMatrix, rhs, x, params);
            }
            case SparsePreconditioner::Diagonal:
            {
                EigenBiCGSTABDiagonal solver;
                return SolveBiCGSTABWithSolver(solver, eigenMatrix, rhs, x, params);
            }
            case SparsePreconditioner::IncompleteLUT:
            {
                EigenBiCGSTABILUT solver;
                return SolveBiCGSTABWithSolver(solver, eigenMatrix, rhs, x, params);
            }
            }

            return MakeIterativeDiagnostics(SparseIterativeStatus::InvalidInput, params.Preconditioner);
        }
    }

    namespace Detail
    {
        struct SparseLDLTImpl
        {
            EigenLDLT Solver;
        };

        struct SparseLLTImpl
        {
            EigenLLT Solver;
        };
    }

    void SparseMatrix::Multiply(std::span<const double> x, std::span<double> y) const
    {
        assert(x.size() >= Cols);
        assert(y.size() >= Rows);

        for (std::size_t row = 0; row < Rows; ++row)
        {
            double sum = 0.0;
            for (std::size_t k = RowOffsets[row]; k < RowOffsets[row + 1]; ++k)
            {
                sum += Values[k] * x[ColIndices[k]];
            }
            y[row] = sum;
        }
    }

    void SparseMatrix::MultiplyTranspose(std::span<const double> x, std::span<double> y) const
    {
        assert(x.size() >= Rows);
        assert(y.size() >= Cols);

        std::fill(y.begin(), y.begin() + static_cast<std::ptrdiff_t>(Cols), 0.0);
        for (std::size_t row = 0; row < Rows; ++row)
        {
            for (std::size_t k = RowOffsets[row]; k < RowOffsets[row + 1]; ++k)
            {
                y[ColIndices[k]] += Values[k] * x[row];
            }
        }
    }

    void DiagonalMatrix::Multiply(std::span<const double> x, std::span<double> y) const
    {
        assert(x.size() >= Size);
        assert(y.size() >= Size);

        for (std::size_t i = 0; i < Size; ++i)
        {
            y[i] = Diagonal[i] * x[i];
        }
    }

    void DiagonalMatrix::MultiplyInverse(std::span<const double> x, std::span<double> y, double epsilon) const
    {
        assert(x.size() >= Size);
        assert(y.size() >= Size);

        for (std::size_t i = 0; i < Size; ++i)
        {
            y[i] = (std::abs(Diagonal[i]) > epsilon) ? (x[i] / Diagonal[i]) : 0.0;
        }
    }

    SparseBuilder::SparseBuilder(std::size_t rows, std::size_t cols)
        : Rows(rows)
        , Cols(cols)
    {
    }

    void SparseBuilder::Reserve(std::size_t count)
    {
        Entries.reserve(count);
    }

    void SparseBuilder::Add(std::size_t row, std::size_t col, double value)
    {
        Entries.push_back({row, col, value});
    }

    SparseBuildResult SparseBuilder::Build(double dropTolerance) const
    {
        SparseBuildResult result;
        result.Matrix.Rows = Rows;
        result.Matrix.Cols = Cols;
        result.Matrix.RowOffsets.assign(Rows + 1, 0);

        std::vector<SparseEntry> entries;
        entries.reserve(Entries.size());
        for (const SparseEntry& entry : Entries)
        {
            if (entry.Row >= Rows || entry.Col >= Cols)
            {
                ++result.DroppedOutOfBoundsEntries;
                result.Valid = false;
                continue;
            }
            if (!std::isfinite(entry.Value))
            {
                ++result.DroppedNearZeroEntries;
                result.Valid = false;
                continue;
            }
            if (std::abs(entry.Value) <= dropTolerance)
            {
                ++result.DroppedNearZeroEntries;
                continue;
            }
            entries.push_back(entry);
        }

        std::sort(entries.begin(), entries.end(), [](const SparseEntry& a, const SparseEntry& b) {
            if (a.Row != b.Row)
            {
                return a.Row < b.Row;
            }
            return a.Col < b.Col;
        });

        for (std::size_t i = 0; i < entries.size();)
        {
            SparseEntry merged = entries[i];
            std::size_t j = i + 1;
            while (j < entries.size() && entries[j].Row == merged.Row && entries[j].Col == merged.Col)
            {
                merged.Value += entries[j].Value;
                ++result.MergedDuplicateEntries;
                ++j;
            }

            if (std::abs(merged.Value) <= dropTolerance)
            {
                ++result.DroppedNearZeroEntries;
            }
            else
            {
                ++result.Matrix.RowOffsets[merged.Row + 1];
                result.Matrix.ColIndices.push_back(merged.Col);
                result.Matrix.Values.push_back(merged.Value);
            }
            i = j;
        }

        for (std::size_t row = 0; row < Rows; ++row)
        {
            result.Matrix.RowOffsets[row + 1] += result.Matrix.RowOffsets[row];
        }

        return result;
    }

    SparseDiagnostics AnalyzeSparseMatrix(const SparseMatrix& matrix, double tolerance)
    {
        SparseDiagnostics diagnostics;
        diagnostics.ValidShape = matrix.RowOffsets.size() == matrix.Rows + 1
            && matrix.ColIndices.size() == matrix.Values.size();

        if (!diagnostics.ValidShape)
        {
            return diagnostics;
        }

        diagnostics.RowOffsetsMonotonic = matrix.RowOffsets.empty()
            || (matrix.RowOffsets.front() == 0 && matrix.RowOffsets.back() == matrix.Values.size());
        for (std::size_t row = 0; diagnostics.RowOffsetsMonotonic && row < matrix.Rows; ++row)
        {
            diagnostics.RowOffsetsMonotonic = matrix.RowOffsets[row] <= matrix.RowOffsets[row + 1];
        }

        diagnostics.ColumnIndicesInBounds = true;
        for (const std::size_t col : matrix.ColIndices)
        {
            if (col >= matrix.Cols)
            {
                diagnostics.ColumnIndicesInBounds = false;
                break;
            }
        }

        diagnostics.ValuesFinite = IsFiniteSpan(matrix.Values);
        if (!diagnostics.StructurallyValid())
        {
            return diagnostics;
        }

        diagnostics.IsSymmetric = matrix.Rows == matrix.Cols;
        diagnostics.HasZeroRowSums = true;
        diagnostics.HasPositiveDiagonal = matrix.Rows == matrix.Cols;
        diagnostics.HasNonPositiveOffDiagonal = matrix.Rows == matrix.Cols;
        diagnostics.MinDiagonal = std::numeric_limits<double>::infinity();

        for (std::size_t row = 0; row < matrix.Rows; ++row)
        {
            double rowSum = 0.0;
            bool foundDiagonal = false;
            for (std::size_t k = matrix.RowOffsets[row]; k < matrix.RowOffsets[row + 1]; ++k)
            {
                const std::size_t col = matrix.ColIndices[k];
                const double value = matrix.Values[k];
                rowSum += value;

                if (matrix.Rows == matrix.Cols)
                {
                    if (row == col)
                    {
                        foundDiagonal = true;
                        diagnostics.MinDiagonal = std::min(diagnostics.MinDiagonal, value);
                        if (value < -tolerance)
                        {
                            diagnostics.HasPositiveDiagonal = false;
                        }
                    }
                    else if (value > tolerance)
                    {
                        diagnostics.HasNonPositiveOffDiagonal = false;
                        diagnostics.MaxPositiveOffDiagonal = std::max(diagnostics.MaxPositiveOffDiagonal, value);
                    }

                    const double symmetryError = std::abs(value - FindValue(matrix, col, row));
                    diagnostics.MaxSymmetryError = std::max(diagnostics.MaxSymmetryError, symmetryError);
                    if (symmetryError > tolerance)
                    {
                        diagnostics.IsSymmetric = false;
                    }
                }
            }

            const double rowSumError = std::abs(rowSum);
            diagnostics.MaxRowSumError = std::max(diagnostics.MaxRowSumError, rowSumError);
            if (rowSumError > tolerance)
            {
                diagnostics.HasZeroRowSums = false;
            }
            if (matrix.Rows == matrix.Cols && !foundDiagonal)
            {
                diagnostics.HasPositiveDiagonal = false;
                diagnostics.MinDiagonal = std::min(diagnostics.MinDiagonal, 0.0);
            }
        }

        if (!std::isfinite(diagnostics.MinDiagonal))
        {
            diagnostics.MinDiagonal = 0.0;
        }
        return diagnostics;
    }

    SparseLDLT::SparseLDLT()
        : Impl_(std::make_unique<Detail::SparseLDLTImpl>())
    {
    }

    SparseLDLT::~SparseLDLT() = default;

    SparseLDLT::SparseLDLT(SparseLDLT&&) noexcept = default;

    SparseLDLT& SparseLDLT::operator=(SparseLDLT&&) noexcept = default;

    SparseFactorizationDiagnostics SparseLDLT::factor(const SparseMatrix& matrix)
    {
        Diagnostics_ = ValidateFactorizationInput(matrix);
        Dimension_ = 0;
        Impl_ = std::make_unique<Detail::SparseLDLTImpl>();
        if (!Diagnostics_.Succeeded())
        {
            return Diagnostics_;
        }

        const EigenSparseMatrix eigenMatrix = ToEigenSparseMatrix(matrix);
        Impl_->Solver.compute(eigenMatrix);
        Diagnostics_ = DiagnosticsFromLDLT(Impl_->Solver, matrix.Rows);
        if (Diagnostics_.Succeeded())
        {
            Dimension_ = matrix.Rows;
        }
        return Diagnostics_;
    }

    SparseFactorizationDiagnostics SparseLDLT::solve(std::span<const double> rhs, std::span<double> x) const
    {
        SparseFactorizationDiagnostics diagnostics = ValidateVectorSolveInput(Diagnostics_, Dimension_, rhs, x);
        if (!diagnostics.Succeeded())
        {
            return diagnostics;
        }

        Eigen::Map<const Eigen::VectorXd> rhsVector(rhs.data(), static_cast<Eigen::Index>(Dimension_));
        const Eigen::VectorXd solved = Impl_->Solver.solve(rhsVector);
        diagnostics = FinalizeSolve(Diagnostics_, Impl_->Solver.info(), solved.allFinite());
        if (!diagnostics.Succeeded())
        {
            return diagnostics;
        }

        for (std::size_t i = 0; i < Dimension_; ++i)
        {
            x[i] = solved[static_cast<Eigen::Index>(i)];
        }
        return diagnostics;
    }

    SparseFactorizationDiagnostics SparseLDLT::solveInPlace(std::span<double> x) const
    {
        std::vector<double> rhs(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(std::min(x.size(), Dimension_)));
        return solve(rhs, x);
    }

    SparseFactorizationDiagnostics SparseLDLT::solve(ConstEigenDenseBlockRef rhs, EigenDenseBlockRef x) const
    {
        SparseFactorizationDiagnostics diagnostics = ValidateDenseSolveInput(Diagnostics_, Dimension_, rhs, x);
        if (!diagnostics.Succeeded())
        {
            return diagnostics;
        }

        const EigenDenseMatrixXd solved = Impl_->Solver.solve(rhs);
        diagnostics = FinalizeSolve(Diagnostics_, Impl_->Solver.info(), solved.allFinite());
        if (!diagnostics.Succeeded())
        {
            return diagnostics;
        }

        x = solved;
        return diagnostics;
    }

    SparseFactorizationDiagnostics SparseLDLT::solveInPlace(EigenDenseBlockRef x) const
    {
        const EigenDenseMatrixXd rhs = x;
        return solve(rhs, x);
    }

    const SparseFactorizationDiagnostics& SparseLDLT::diagnostics() const noexcept
    {
        return Diagnostics_;
    }

    SparseLLT::SparseLLT()
        : Impl_(std::make_unique<Detail::SparseLLTImpl>())
    {
    }

    SparseLLT::~SparseLLT() = default;

    SparseLLT::SparseLLT(SparseLLT&&) noexcept = default;

    SparseLLT& SparseLLT::operator=(SparseLLT&&) noexcept = default;

    SparseFactorizationDiagnostics SparseLLT::factor(const SparseMatrix& matrix)
    {
        Diagnostics_ = ValidateFactorizationInput(matrix);
        Dimension_ = 0;
        Impl_ = std::make_unique<Detail::SparseLLTImpl>();
        if (!Diagnostics_.Succeeded())
        {
            return Diagnostics_;
        }

        const EigenSparseMatrix eigenMatrix = ToEigenSparseMatrix(matrix);
        Impl_->Solver.compute(eigenMatrix);
        Diagnostics_ = DiagnosticsFromLLT(Impl_->Solver, eigenMatrix, matrix.Rows);
        if (Diagnostics_.Succeeded())
        {
            Dimension_ = matrix.Rows;
        }
        return Diagnostics_;
    }

    SparseFactorizationDiagnostics SparseLLT::solve(std::span<const double> rhs, std::span<double> x) const
    {
        SparseFactorizationDiagnostics diagnostics = ValidateVectorSolveInput(Diagnostics_, Dimension_, rhs, x);
        if (!diagnostics.Succeeded())
        {
            return diagnostics;
        }

        Eigen::Map<const Eigen::VectorXd> rhsVector(rhs.data(), static_cast<Eigen::Index>(Dimension_));
        const Eigen::VectorXd solved = Impl_->Solver.solve(rhsVector);
        diagnostics = FinalizeSolve(Diagnostics_, Impl_->Solver.info(), solved.allFinite());
        if (!diagnostics.Succeeded())
        {
            return diagnostics;
        }

        for (std::size_t i = 0; i < Dimension_; ++i)
        {
            x[i] = solved[static_cast<Eigen::Index>(i)];
        }
        return diagnostics;
    }

    SparseFactorizationDiagnostics SparseLLT::solveInPlace(std::span<double> x) const
    {
        std::vector<double> rhs(x.begin(), x.begin() + static_cast<std::ptrdiff_t>(std::min(x.size(), Dimension_)));
        return solve(rhs, x);
    }

    SparseFactorizationDiagnostics SparseLLT::solve(ConstEigenDenseBlockRef rhs, EigenDenseBlockRef x) const
    {
        SparseFactorizationDiagnostics diagnostics = ValidateDenseSolveInput(Diagnostics_, Dimension_, rhs, x);
        if (!diagnostics.Succeeded())
        {
            return diagnostics;
        }

        const EigenDenseMatrixXd solved = Impl_->Solver.solve(rhs);
        diagnostics = FinalizeSolve(Diagnostics_, Impl_->Solver.info(), solved.allFinite());
        if (!diagnostics.Succeeded())
        {
            return diagnostics;
        }

        x = solved;
        return diagnostics;
    }

    SparseFactorizationDiagnostics SparseLLT::solveInPlace(EigenDenseBlockRef x) const
    {
        const EigenDenseMatrixXd rhs = x;
        return solve(rhs, x);
    }

    const SparseFactorizationDiagnostics& SparseLLT::diagnostics() const noexcept
    {
        return Diagnostics_;
    }

    SparseIterativeDiagnostics SparseBiCGSTAB::solve(
        const SparseMatrix& matrix,
        std::span<const double> rhs,
        std::span<double> x,
        const SparseBiCGSTABParams& params) const
    {
        SparseIterativeDiagnostics diagnostics = ValidateBiCGSTABVectorInput(matrix, rhs, x, params);
        if (!diagnostics.Succeeded())
        {
            return diagnostics;
        }

        if (matrix.Rows == 0)
        {
            return diagnostics;
        }

        Eigen::Map<const EigenDenseMatrixXd> rhsVector(
            rhs.data(),
            static_cast<Eigen::Index>(matrix.Rows),
            1);
        EigenDenseMatrixXd solved = EigenDenseMatrixXd::Zero(static_cast<Eigen::Index>(matrix.Rows), 1);
        diagnostics = SolveBiCGSTABDense(matrix, rhsVector, solved, params);
        if (!diagnostics.Succeeded())
        {
            return diagnostics;
        }

        for (std::size_t i = 0; i < matrix.Rows; ++i)
        {
            x[i] = solved(static_cast<Eigen::Index>(i), 0);
        }
        return diagnostics;
    }

    SparseIterativeDiagnostics SparseBiCGSTAB::solve(
        const SparseMatrix& matrix,
        ConstEigenDenseBlockRef rhs,
        EigenDenseBlockRef x,
        const SparseBiCGSTABParams& params) const
    {
        return SolveBiCGSTABDense(matrix, rhs, x, params);
    }

    CGResult SolveCG(const SparseMatrix& A, std::span<const double> b, std::span<double> x, const CGParams& params)
    {
        CGResult result;
        if (A.Rows != A.Cols || b.size() < A.Rows || x.size() < A.Rows || !ValidateCsr(A)
            || !IsFiniteSpan(b) || !IsFiniteSpan(x))
        {
            result.Reason = CGConvergenceReason::InvalidInput;
            return result;
        }

        const std::size_t n = A.Rows;
        if (n == 0)
        {
            result.Converged = true;
            result.Reason = CGConvergenceReason::Converged;
            return result;
        }

        std::vector<double> diagInv(n, 1.0);
        for (std::size_t row = 0; row < n; ++row)
        {
            for (std::size_t k = A.RowOffsets[row]; k < A.RowOffsets[row + 1]; ++k)
            {
                if (A.ColIndices[k] == row)
                {
                    const double d = A.Values[k];
                    diagInv[row] = (std::abs(d) > 1e-15) ? (1.0 / d) : 1.0;
                    break;
                }
            }
        }

        std::vector<double> r(n);
        std::vector<double> Ax(n);
        A.Multiply(x, Ax);
        for (std::size_t i = 0; i < n; ++i)
        {
            r[i] = b[i] - Ax[i];
        }

        std::vector<double> z(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            z[i] = diagInv[i] * r[i];
        }
        std::vector<double> p(z);

        double rz = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            rz += r[i] * z[i];
        }

        const double bNorm = std::max(Norm(b.first(n)), 1.0);
        const double tolerance = params.Tolerance * bNorm;
        result.InitialResidualNorm = Norm(r);
        result.ResidualNorm = result.InitialResidualNorm;
        result.RelativeResidual = result.ResidualNorm / bNorm;

        if (result.ResidualNorm <= tolerance)
        {
            result.Converged = true;
            result.Reason = CGConvergenceReason::Converged;
            return result;
        }

        std::vector<double> Ap(n);
        for (std::size_t iter = 0; iter < params.MaxIterations; ++iter)
        {
            A.Multiply(p, Ap);

            double pAp = 0.0;
            for (std::size_t i = 0; i < n; ++i)
            {
                pAp += p[i] * Ap[i];
            }
            if (!std::isfinite(pAp) || !std::isfinite(rz))
            {
                result.Reason = CGConvergenceReason::NonFinite;
                return result;
            }
            if (std::abs(pAp) < 1e-30)
            {
                result.Reason = CGConvergenceReason::Breakdown;
                return result;
            }

            const double alpha = rz / pAp;
            for (std::size_t i = 0; i < n; ++i)
            {
                x[i] += alpha * p[i];
                r[i] -= alpha * Ap[i];
            }

            result.Iterations = iter + 1;
            result.ResidualNorm = Norm(r);
            result.RelativeResidual = result.ResidualNorm / bNorm;
            if (!std::isfinite(result.ResidualNorm))
            {
                result.Reason = CGConvergenceReason::NonFinite;
                return result;
            }
            if (result.ResidualNorm <= tolerance)
            {
                result.Converged = true;
                result.Reason = CGConvergenceReason::Converged;
                return result;
            }

            for (std::size_t i = 0; i < n; ++i)
            {
                z[i] = diagInv[i] * r[i];
            }

            double rzNew = 0.0;
            for (std::size_t i = 0; i < n; ++i)
            {
                rzNew += r[i] * z[i];
            }
            if (std::abs(rz) < 1e-30)
            {
                result.Reason = CGConvergenceReason::Breakdown;
                return result;
            }

            const double beta = rzNew / rz;
            rz = rzNew;
            for (std::size_t i = 0; i < n; ++i)
            {
                p[i] = z[i] + beta * p[i];
            }
        }

        result.Reason = CGConvergenceReason::MaxIterations;
        return result;
    }

    CGResult SolveCGShifted(
        const DiagonalMatrix& M, double alpha,
        const SparseMatrix& A, double beta,
        std::span<const double> b,
        std::span<double> x,
        const CGParams& params)
    {
        CGResult result;
        if (M.Size != A.Rows || M.Diagonal.size() < M.Size || A.Rows != A.Cols
            || b.size() < A.Rows || x.size() < A.Rows || !ValidateCsr(A)
            || !IsFiniteSpan(M.Diagonal) || !IsFiniteSpan(b) || !IsFiniteSpan(x))
        {
            result.Reason = CGConvergenceReason::InvalidInput;
            return result;
        }

        SparseBuilder builder(A.Rows, A.Cols);
        builder.Reserve(A.NonZeros() + M.Size);
        for (std::size_t row = 0; row < A.Rows; ++row)
        {
            if (alpha != 0.0)
            {
                builder.Add(row, row, alpha * M.Diagonal[row]);
            }
            for (std::size_t k = A.RowOffsets[row]; k < A.RowOffsets[row + 1]; ++k)
            {
                builder.Add(row, A.ColIndices[k], beta * A.Values[k]);
            }
        }
        const SparseBuildResult combined = builder.Build(0.0);
        if (!combined.Valid)
        {
            result.Reason = CGConvergenceReason::InvalidInput;
            return result;
        }
        return SolveCG(combined.Matrix, b, x, params);
    }
}
