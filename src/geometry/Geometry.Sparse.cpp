module;

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>

module Geometry.Sparse;

namespace Geometry::Sparse
{
    namespace
    {
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

