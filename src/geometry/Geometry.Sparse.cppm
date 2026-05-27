module;

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

export module Geometry.Sparse;

export namespace Geometry::Sparse
{
    enum class CGConvergenceReason : std::uint8_t
    {
        NotRun = 0,
        Converged,
        MaxIterations,
        InvalidInput,
        Breakdown,
        NonFinite
    };

    struct SparseMatrix
    {
        std::size_t Rows{0};
        std::size_t Cols{0};
        std::vector<std::size_t> RowOffsets;
        std::vector<std::size_t> ColIndices;
        std::vector<double> Values;

        [[nodiscard]] std::size_t NonZeros() const noexcept { return Values.size(); }
        [[nodiscard]] bool IsEmpty() const noexcept { return Rows == 0 && Cols == 0; }

        void Multiply(std::span<const double> x, std::span<double> y) const;
        void MultiplyTranspose(std::span<const double> x, std::span<double> y) const;
    };

    struct DiagonalMatrix
    {
        std::size_t Size{0};
        std::vector<double> Diagonal;

        [[nodiscard]] bool IsEmpty() const noexcept { return Size == 0; }

        void Multiply(std::span<const double> x, std::span<double> y) const;
        void MultiplyInverse(std::span<const double> x, std::span<double> y,
                             double epsilon = 1e-12) const;
    };

    struct SparseEntry
    {
        std::size_t Row{0};
        std::size_t Col{0};
        double Value{0.0};
    };

    struct SparseBuildResult
    {
        SparseMatrix Matrix{};
        std::size_t DroppedOutOfBoundsEntries{0};
        std::size_t DroppedNearZeroEntries{0};
        std::size_t MergedDuplicateEntries{0};
        bool Valid{true};
    };

    struct SparseBuilder
    {
        std::size_t Rows{0};
        std::size_t Cols{0};
        std::vector<SparseEntry> Entries;

        SparseBuilder() = default;
        SparseBuilder(std::size_t rows, std::size_t cols);

        void Reserve(std::size_t count);
        void Add(std::size_t row, std::size_t col, double value);
        [[nodiscard]] SparseBuildResult Build(double dropTolerance = 0.0) const;
    };

    struct SparseDiagnostics
    {
        bool ValidShape{false};
        bool RowOffsetsMonotonic{false};
        bool ColumnIndicesInBounds{false};
        bool ValuesFinite{false};
        bool IsSymmetric{false};
        bool HasZeroRowSums{false};
        bool HasPositiveDiagonal{false};
        bool HasNonPositiveOffDiagonal{false};
        double MaxSymmetryError{0.0};
        double MaxRowSumError{0.0};
        double MinDiagonal{0.0};
        double MaxPositiveOffDiagonal{0.0};

        [[nodiscard]] bool StructurallyValid() const noexcept
        {
            return ValidShape && RowOffsetsMonotonic && ColumnIndicesInBounds && ValuesFinite;
        }
    };

    struct CGParams
    {
        std::size_t MaxIterations{1000};
        double Tolerance{1e-8};
    };

    struct CGResult
    {
        std::size_t Iterations{0};
        double InitialResidualNorm{0.0};
        double ResidualNorm{0.0};
        double RelativeResidual{0.0};
        bool Converged{false};
        CGConvergenceReason Reason{CGConvergenceReason::NotRun};
    };

    [[nodiscard]] SparseDiagnostics AnalyzeSparseMatrix(
        const SparseMatrix& matrix,
        double tolerance = 1e-10);

    [[nodiscard]] CGResult SolveCG(
        const SparseMatrix& A,
        std::span<const double> b,
        std::span<double> x,
        const CGParams& params = {});

    [[nodiscard]] CGResult SolveCGShifted(
        const DiagonalMatrix& M, double alpha,
        const SparseMatrix& A, double beta,
        std::span<const double> b,
        std::span<double> x,
        const CGParams& params = {});
}

