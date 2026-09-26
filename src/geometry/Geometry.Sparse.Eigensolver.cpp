// Shift-invert block subspace iteration for the smallest eigenpairs of A z = lambda M z
// (Bathe's subspace iteration with Rayleigh-Ritz in the M-inner product). Every iteration
// applies (A - sigma M)^-1 M to a block of q >= k vectors through one SparseLDLT
// factorization, M-orthonormalizes the block, and diagonalizes the projected q x q problem.
// Eigenvalue i converges at the rate (lambda_i - sigma) / (lambda_(q+1) - sigma) per step.
module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <Eigen/Dense>
module Geometry.Sparse;

namespace Geometry::Sparse
{
    namespace
    {
        // SplitMix64: a fixed, portable start block independent of global RNG state.
        double NextUniform(std::uint64_t& state)
        {
            std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            z ^= z >> 31;
            return double(z >> 11) * (1.0 / 9007199254740992.0) * 2.0 - 1.0;
        }

        double Dot(std::span<const double> a, std::span<const double> b)
        {
            double sum = 0;
            for (std::size_t i = 0; i < a.size(); ++i) sum += a[i] * b[i];
            return sum;
        }

        bool Finite(const SparseMatrix& m)
        {
            return std::ranges::all_of(m.Values, [](double v) { return std::isfinite(v); });
        }

        double Diagonal(const SparseMatrix& m, std::size_t row)
        {
            for (auto k = m.RowOffsets[row]; k < m.RowOffsets[row + 1]; ++k)
                if (m.ColIndices[k] == row) return m.Values[k];
            return 0.0;
        }
    }

    SymmetricEigenResult SolveSymmetricGeneralizedEigen(const SparseMatrix& A, const DiagonalMatrix& M,
                                                        const SymmetricEigenParams& params)
    {
        if (M.Size != A.Rows || M.Diagonal.size() != M.Size)
        {
            SymmetricEigenResult result;
            result.Status = SymmetricEigenStatus::DimensionMismatch;
            result.Diagnostic = "Mass diagonal size does not match the matrix.";
            return result;
        }
        SparseBuilder builder(M.Size, M.Size);
        builder.Reserve(M.Size);
        for (std::size_t i = 0; i < M.Size; ++i) builder.Add(i, i, M.Diagonal[i]);
        return SolveSymmetricGeneralizedEigen(A, builder.Build().Matrix, params);
    }

    SymmetricEigenResult SolveSymmetricGeneralizedEigen(const SparseMatrix& A, const SparseMatrix& M,
                                                        const SymmetricEigenParams& params)
    {
        SymmetricEigenResult result;
        const auto fail = [&](SymmetricEigenStatus status, std::string message) {
            result.Status = status;
            result.Eigenvalues.clear();
            result.Eigenvectors.clear();
            result.RelativeResiduals.clear();
            result.Diagnostic = std::move(message);
            return result;
        };
        const std::size_t n = A.Rows, k = params.Count;
        result.Rows = n;
        if (A.Rows != A.Cols || M.Rows != M.Cols || M.Rows != n)
            return fail(SymmetricEigenStatus::DimensionMismatch, "A and M must be square and of equal size.");
        if (!AnalyzeSparseMatrix(A).StructurallyValid() || !AnalyzeSparseMatrix(M).StructurallyValid() || !Finite(A) || !Finite(M))
            return fail(SymmetricEigenStatus::InvalidInput, "A and M must be valid CSR matrices with finite entries.");
        if (k == 0 || k >= n)
            return fail(SymmetricEigenStatus::InvalidInput, "The eigenpair count must satisfy 1 <= k < n.");
        const std::size_t q = params.SubspaceDimension ? params.SubspaceDimension : std::min(n, std::max(2 * k, k + 8));
        if (q <= k || q > n)
            return fail(SymmetricEigenStatus::InvalidInput, "The subspace dimension must satisfy k < q <= n.");
        if (params.MaxIterations == 0 || !(params.Tolerance > 0) || !(params.Tolerance < 1))
            return fail(SymmetricEigenStatus::InvalidInput, "Iterations must be positive and the tolerance in (0, 1).");
        double scaleA = 0, scaleM = 0;
        for (const double v : A.Values) scaleA = std::max(scaleA, std::abs(v));
        for (const double v : M.Values) scaleM = std::max(scaleM, std::abs(v));
        if (!AnalyzeSparseMatrix(A, 1e-10 * std::max(scaleA, 1.0)).IsSymmetric ||
            !AnalyzeSparseMatrix(M, 1e-10 * std::max(scaleM, 1.0)).IsSymmetric)
            return fail(SymmetricEigenStatus::InvalidInput, "A and M must be symmetric.");
        {
            // M must be SPD for the M-inner product; the SPD-only LDLT is the check.
            SparseLDLT mass;
            if (!mass.factor(M).Succeeded())
                return fail(SymmetricEigenStatus::InvalidInput, "The mass matrix M must be symmetric positive definite.");
        }

        // Infinity norms (max absolute row sums) for the backward-error residual.
        const auto rowNorm = [](const SparseMatrix& m) {
            double best = 0;
            for (std::size_t r = 0; r < m.Rows; ++r)
            {
                double sum = 0;
                for (auto e = m.RowOffsets[r]; e < m.RowOffsets[r + 1]; ++e) sum += std::abs(m.Values[e]);
                best = std::max(best, sum);
            }
            return best;
        };
        const double normA = rowNorm(A), normM = rowNorm(M);
        double traceA = 0, traceM = 0;
        for (std::size_t i = 0; i < n; ++i)
        {
            traceA += std::abs(Diagonal(A, i));
            traceM += Diagonal(M, i);
        }
        const double sigma = std::isnan(params.Shift) ? -1e-6 * (traceM > 0 ? traceA / traceM : 1.0) : params.Shift;
        if (!std::isfinite(sigma)) return fail(SymmetricEigenStatus::InvalidInput, "The shift must be finite.");
        result.Shift = sigma;

        // K = A - sigma M, factored once.
        SparseBuilder shifted(n, n);
        shifted.Reserve(A.NonZeros() + M.NonZeros());
        for (std::size_t row = 0; row < n; ++row)
        {
            for (auto e = A.RowOffsets[row]; e < A.RowOffsets[row + 1]; ++e) shifted.Add(row, A.ColIndices[e], A.Values[e]);
            for (auto e = M.RowOffsets[row]; e < M.RowOffsets[row + 1]; ++e) shifted.Add(row, M.ColIndices[e], -sigma * M.Values[e]);
        }
        const auto K = shifted.Build();
        SparseLDLT factor;
        result.ShiftFactorization = K.Valid ? factor.factor(K.Matrix) : SparseFactorizationDiagnostics{};
        if (!K.Valid || !result.ShiftFactorization.Succeeded())
            return fail(SymmetricEigenStatus::NumericalIssue,
                        "A - sigma M is not positive definite: the shift must lie below every eigenvalue "
                        "(pass an explicit shift for an indefinite A).");

        // Block columns are stored contiguously: column j at [j * n, (j + 1) * n).
        std::vector<double> X(q * n), Z(q * n), MZ(q * n), KZ(q * n), work(n);
        std::uint64_t state = params.Seed;
        for (auto& x : X) x = NextUniform(state);
        const auto column = [n](std::vector<double>& block, std::size_t j) { return std::span<double>(block).subspan(j * n, n); };
        std::vector<double> residuals(k, std::numeric_limits<double>::infinity()), eigenvalues(q);
        for (std::size_t iteration = 1; iteration <= params.MaxIterations; ++iteration)
        {
            result.Iterations = iteration;
            // Z = K^-1 M X.
            for (std::size_t j = 0; j < q; ++j)
            {
                M.Multiply(column(X, j), work);
                if (!factor.solve(work, column(Z, j)).Succeeded())
                    return fail(SymmetricEigenStatus::NumericalIssue, "Shifted solve failed.");
                ++result.Operations;
            }
            // M-orthonormalize Z by modified Gram-Schmidt, twice for stability; a collapsed
            // column is replaced by a fresh deterministic vector.
            for (std::size_t j = 0; j < q; ++j)
            {
                auto zj = column(Z, j);
                for (int attempt = 0; attempt < 3; ++attempt)
                {
                    for (int pass = 0; pass < 2; ++pass)
                        for (std::size_t i = 0; i < j; ++i)
                        {
                            const double projection = Dot(column(MZ, i), zj);
                            const auto zi = column(Z, i);
                            for (std::size_t r = 0; r < n; ++r) zj[r] -= projection * zi[r];
                        }
                    M.Multiply(zj, column(MZ, j));
                    const double norm = std::sqrt(std::max(Dot(zj, column(MZ, j)), 0.0));
                    if (norm > 1e-14 * std::sqrt(std::max(traceM, 1e-300)))
                    {
                        for (std::size_t r = 0; r < n; ++r) { zj[r] /= norm; MZ[j * n + r] /= norm; }
                        break;
                    }
                    if (attempt == 2) return fail(SymmetricEigenStatus::NumericalIssue, "The subspace lost rank.");
                    for (auto& z : zj) z = NextUniform(state);
                }
            }
            // Rayleigh-Ritz: Kr = Z^T K Z with K Z = A Z - sigma M Z; Mr = I.
            Eigen::MatrixXd Kr(q, q);
            for (std::size_t j = 0; j < q; ++j)
            {
                A.Multiply(column(Z, j), column(KZ, j));
                for (std::size_t r = 0; r < n; ++r) KZ[j * n + r] -= sigma * MZ[j * n + r];
            }
            for (std::size_t i = 0; i < q; ++i)
                for (std::size_t j = i; j < q; ++j)
                    Kr(i, j) = Kr(j, i) = 0.5 * (Dot(column(Z, i), column(KZ, j)) + Dot(column(Z, j), column(KZ, i)));
            const Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> projected(Kr);
            if (projected.info() != Eigen::Success)
                return fail(SymmetricEigenStatus::NumericalIssue, "The projected eigenproblem failed.");
            const Eigen::VectorXd& mu = projected.eigenvalues(); // ascending
            const Eigen::MatrixXd& Q = projected.eigenvectors();
            // X = Z Q; the Ritz vectors are M-orthonormal.
            for (std::size_t j = 0; j < q; ++j)
            {
                auto xj = column(X, j);
                std::fill(xj.begin(), xj.end(), 0.0);
                for (std::size_t i = 0; i < q; ++i)
                {
                    const double w = Q(Eigen::Index(i), Eigen::Index(j));
                    const auto zi = column(Z, i);
                    for (std::size_t r = 0; r < n; ++r) xj[r] += w * zi[r];
                }
                eigenvalues[j] = mu(Eigen::Index(j)) + sigma;
            }
            // Relative residuals of the k leading Ritz pairs.
            std::size_t converged = 0;
            bool leading = true;
            for (std::size_t j = 0; j < k; ++j)
            {
                A.Multiply(column(X, j), column(KZ, j));
                M.Multiply(column(X, j), column(MZ, j));
                double r2 = 0, x2 = 0;
                for (std::size_t r = 0; r < n; ++r)
                {
                    const double d = KZ[j * n + r] - eigenvalues[j] * MZ[j * n + r];
                    r2 += d * d;
                    x2 += X[j * n + r] * X[j * n + r];
                }
                // Normwise backward error; well defined at lambda = 0 (Laplacian nullspace).
                residuals[j] = std::sqrt(r2) /
                    std::max((normA + std::abs(eigenvalues[j]) * normM) * std::sqrt(x2), 1e-300);
                if (!std::isfinite(residuals[j])) return fail(SymmetricEigenStatus::NumericalIssue, "Non-finite Ritz residual.");
                leading = leading && residuals[j] <= params.Tolerance;
                if (leading) ++converged;
            }
            result.ConvergedCount = converged;
            if (converged == k) break;
        }

        result.Eigenvalues.assign(eigenvalues.begin(), eigenvalues.begin() + std::ptrdiff_t(k));
        result.RelativeResiduals = residuals;
        result.Eigenvectors.assign(X.begin(), X.begin() + std::ptrdiff_t(k * n));
        // Sign convention: the largest-magnitude entry (first on ties) is positive.
        for (std::size_t j = 0; j < k; ++j)
        {
            auto v = std::span<double>(result.Eigenvectors).subspan(j * n, n);
            std::size_t best = 0;
            for (std::size_t r = 1; r < n; ++r)
                if (std::abs(v[r]) > std::abs(v[best])) best = r;
            if (v[best] < 0)
                for (auto& x : v) x = -x;
        }
        if (result.ConvergedCount < k)
        {
            result.Status = SymmetricEigenStatus::NotConverged;
            result.Diagnostic = std::to_string(result.ConvergedCount) + " of " + std::to_string(k) +
                                " eigenpairs reached the tolerance in " + std::to_string(result.Iterations) + " iterations.";
            return result;
        }
        result.Status = SymmetricEigenStatus::Success;
        result.Diagnostic = "cpu_reference_subspace_iteration";
        return result;
    }
}
