module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#ifndef EIGEN_MPL2_ONLY
#define EIGEN_MPL2_ONLY
#endif

#ifndef EIGEN_DONT_PARALLELIZE
#define EIGEN_DONT_PARALLELIZE
#endif

#include <Eigen/Dense>

export module Geometry.Sparse;

namespace Geometry::Sparse::Detail
{
    struct SparseLDLTImpl;
    struct SparseLLTImpl;
}

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

    enum class SparseFactorizationStatus : std::uint8_t
    {
        Success = 0,
        NotFactored,
        NumericalIssue,
        NonSPD,
        ZeroPivot,
        DimensionMismatch,
        InvalidInput
    };

    enum class SparseIterativeStatus : std::uint8_t
    {
        Success = 0,
        NotConverged,
        NumericalIssue,
        DimensionMismatch,
        InvalidInput
    };

    enum class SparsePreconditioner : std::uint8_t
    {
        None = 0,
        Diagonal,
        IncompleteLUT
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

    struct SparseFactorizationDiagnostics
    {
        SparseFactorizationStatus Status{SparseFactorizationStatus::NotFactored};
        std::size_t PivotCount{0};
        double SmallestAbsolutePivot{0.0};
        double ConditionEstimate{0.0};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SparseFactorizationStatus::Success;
        }
    };

    struct SparseIterativeDiagnostics
    {
        SparseIterativeStatus Status{SparseIterativeStatus::InvalidInput};
        std::size_t Iterations{0};
        double FinalRelativeResidual{0.0};
        SparsePreconditioner Preconditioner{SparsePreconditioner::Diagonal};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return Status == SparseIterativeStatus::Success;
        }
    };

    struct CGParams
    {
        std::size_t MaxIterations{1000};
        double Tolerance{1e-8};
    };

    struct SparseBiCGSTABParams
    {
        std::size_t MaxIterations{1000};
        double RelativeTolerance{1e-8};
        SparsePreconditioner Preconditioner{SparsePreconditioner::Diagonal};
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

    using EigenDenseMatrixXd = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic>;
    using EigenDenseBlockRef = Eigen::Ref<EigenDenseMatrixXd>;
    using ConstEigenDenseBlockRef = Eigen::Ref<const EigenDenseMatrixXd>;

    class SparseLDLT
    {
    public:
        SparseLDLT();
        ~SparseLDLT();

        SparseLDLT(SparseLDLT&&) noexcept;
        SparseLDLT& operator=(SparseLDLT&&) noexcept;

        SparseLDLT(const SparseLDLT&) = delete;
        SparseLDLT& operator=(const SparseLDLT&) = delete;

        [[nodiscard]] SparseFactorizationDiagnostics factor(const SparseMatrix& matrix);
        [[nodiscard]] SparseFactorizationDiagnostics solve(
            std::span<const double> rhs,
            std::span<double> x) const;
        [[nodiscard]] SparseFactorizationDiagnostics solveInPlace(std::span<double> x) const;
        [[nodiscard]] SparseFactorizationDiagnostics solve(
            ConstEigenDenseBlockRef rhs,
            EigenDenseBlockRef x) const;
        [[nodiscard]] SparseFactorizationDiagnostics solveInPlace(EigenDenseBlockRef x) const;
        [[nodiscard]] const SparseFactorizationDiagnostics& diagnostics() const noexcept;

    private:
        std::unique_ptr<Detail::SparseLDLTImpl> Impl_;
        SparseFactorizationDiagnostics Diagnostics_{};
        std::size_t Dimension_{0};
    };

    class SparseLLT
    {
    public:
        SparseLLT();
        ~SparseLLT();

        SparseLLT(SparseLLT&&) noexcept;
        SparseLLT& operator=(SparseLLT&&) noexcept;

        SparseLLT(const SparseLLT&) = delete;
        SparseLLT& operator=(const SparseLLT&) = delete;

        [[nodiscard]] SparseFactorizationDiagnostics factor(const SparseMatrix& matrix);
        [[nodiscard]] SparseFactorizationDiagnostics solve(
            std::span<const double> rhs,
            std::span<double> x) const;
        [[nodiscard]] SparseFactorizationDiagnostics solveInPlace(std::span<double> x) const;
        [[nodiscard]] SparseFactorizationDiagnostics solve(
            ConstEigenDenseBlockRef rhs,
            EigenDenseBlockRef x) const;
        [[nodiscard]] SparseFactorizationDiagnostics solveInPlace(EigenDenseBlockRef x) const;
        [[nodiscard]] const SparseFactorizationDiagnostics& diagnostics() const noexcept;

    private:
        std::unique_ptr<Detail::SparseLLTImpl> Impl_;
        SparseFactorizationDiagnostics Diagnostics_{};
        std::size_t Dimension_{0};
    };

    class SparseBiCGSTAB
    {
    public:
        [[nodiscard]] SparseIterativeDiagnostics solve(
            const SparseMatrix& matrix,
            std::span<const double> rhs,
            std::span<double> x,
            const SparseBiCGSTABParams& params = {}) const;
        [[nodiscard]] SparseIterativeDiagnostics solve(
            const SparseMatrix& matrix,
            ConstEigenDenseBlockRef rhs,
            EigenDenseBlockRef x,
            const SparseBiCGSTABParams& params = {}) const;
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
