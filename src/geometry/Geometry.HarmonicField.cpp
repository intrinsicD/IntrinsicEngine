// Assembles L (M^-1 L)^(k-1) for a weighted graph, eliminates hard rows, grounds pure-Neumann
// components, adds soft penalties and solves every channel with one sparse Cholesky factorization
// of the reduced SPD system.
module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <span>
#include <string>
#include <vector>
module Geometry.HarmonicField;
import Geometry.Sparse;

namespace Geometry::HarmonicField
{
    namespace
    {
        constexpr std::size_t kInvalid = std::numeric_limits<std::size_t>::max();

        std::size_t Find(std::vector<std::size_t>& parent, std::size_t x)
        {
            while (parent[x] != x)
            {
                parent[x] = parent[parent[x]];
                x = parent[x];
            }
            return x;
        }

        // Rows of the symmetric weighted Laplacian L = D - W as (column, value) lists, sorted by column.
        struct RowMatrix
        {
            std::vector<std::vector<std::pair<std::size_t, double>>> Rows;
        };

        RowMatrix BuildLaplacian(std::size_t count, std::span<const Smoothing::PropertyEdge> edges)
        {
            Sparse::SparseBuilder builder(count, count);
            builder.Reserve(4 * edges.size());
            for (const auto& edge : edges)
            {
                builder.Add(edge.A, edge.A, edge.Weight);
                builder.Add(edge.B, edge.B, edge.Weight);
                builder.Add(edge.A, edge.B, -edge.Weight);
                builder.Add(edge.B, edge.A, -edge.Weight);
            }
            const auto csr = builder.Build().Matrix;
            RowMatrix matrix;
            matrix.Rows.resize(count);
            for (std::size_t row = 0; row < count; ++row)
                for (auto k = csr.RowOffsets[row]; k < csr.RowOffsets[row + 1]; ++k)
                    matrix.Rows[row].emplace_back(csr.ColIndices[k], csr.Values[k]);
            return matrix;
        }

        // X M^-1 Y row by row through a dense accumulator. Powers of L interleaved with M^-1 are
        // symmetric up to rounding; the Cholesky factorization reads one triangle.
        RowMatrix MultiplyMassWeighted(const RowMatrix& left, std::span<const double> inverseMass, const RowMatrix& right)
        {
            const auto count = left.Rows.size();
            RowMatrix product;
            product.Rows.resize(count);
            std::vector<double> accumulator(count, 0.0);
            std::vector<bool> seen(count, false);
            std::vector<std::size_t> touched;
            for (std::size_t i = 0; i < count; ++i)
            {
                for (const auto& [k, lik] : left.Rows[i])
                    for (const auto& [j, lkj] : right.Rows[k])
                    {
                        if (!seen[j]) { seen[j] = true; touched.push_back(j); }
                        accumulator[j] += lik * inverseMass[k] * lkj;
                    }
                std::ranges::sort(touched);
                for (const auto j : touched)
                {
                    product.Rows[i].emplace_back(j, accumulator[j]);
                    accumulator[j] = 0.0;
                    seen[j] = false;
                }
                touched.clear();
            }
            return product;
        }
    }

    bool ValidateParams(const Params& p) noexcept
    {
        return p.Order >= FieldOrder::Harmonic && p.Order <= FieldOrder::Triharmonic &&
               p.Unconstrained <= UnconstrainedPolicy::ZeroMean;
    }

    Result Solve(std::span<const double> values, std::size_t channels,
                 std::span<const Smoothing::PropertyEdge> edges, const Params& params,
                 std::span<const std::size_t> hardRows, std::span<const SoftConstraint> softRows,
                 std::span<const double> mass, std::span<const double> source)
    {
        Result result;
        const auto fail = [&](std::string diagnostic) {
            result.Success = false;
            result.Values.clear();
            result.KeptInput.clear();
            result.Diagnostic = std::move(diagnostic);
            return result;
        };
        if (!ValidateParams(params) || channels == 0 || values.empty() || values.size() % channels)
            return fail("Invalid harmonic-field parameters or value shape.");
        const std::size_t count = values.size() / channels;
        result.Stats.Rows = count;
        const auto finiteRow = [&](std::size_t row) {
            for (std::size_t c = 0; c < channels; ++c)
                if (!std::isfinite(values[row * channels + c])) return false;
            return true;
        };
        if (!mass.empty() && (mass.size() != count ||
            !std::ranges::all_of(mass, [](double m) { return std::isfinite(m) && m > 0; })))
            return fail("Row masses must be positive and finite, one per row.");
        if (!source.empty() && (source.size() != values.size() ||
            !std::ranges::all_of(source, [](double b) { return std::isfinite(b); })))
            return fail("The source must be finite with the shape of the values.");
        const auto sourceAt = [&](std::size_t row, std::size_t c) {
            return source.empty() ? 0.0 : source[row * channels + c];
        };

        // 0 = free, 1 = hard, 2 = soft, 3 = ground of a zero-mean component.
        std::vector<std::uint8_t> role(count, 0);
        std::vector<double> softWeight(count, 0.0);
        for (const auto row : hardRows)
        {
            if (row >= count || role[row]) return fail("Invalid or duplicate hard constraint row.");
            if (!finiteRow(row)) return fail("Hard constraint values must be finite.");
            role[row] = 1;
        }
        for (const auto& soft : softRows)
        {
            if (soft.Row >= count || role[soft.Row]) return fail("Invalid, duplicate or hard-constrained soft constraint row.");
            if (!std::isfinite(soft.Weight) || soft.Weight <= 0) return fail("Soft constraint weights must be positive and finite.");
            if (!finiteRow(soft.Row)) return fail("Soft constraint targets must be finite.");
            role[soft.Row] = 2;
            softWeight[soft.Row] = soft.Weight;
        }
        result.Stats.HardRows = hardRows.size();
        result.Stats.SoftRows = softRows.size();

        std::vector<std::size_t> parent(count);
        std::iota(parent.begin(), parent.end(), std::size_t{0});
        for (const auto& edge : edges)
        {
            if (edge.A >= count || edge.B >= count || edge.A == edge.B || !std::isfinite(edge.Weight) || edge.Weight < 0)
                return fail("Invalid nonnegative graph edge.");
            if (edge.Weight > 0) parent[Find(parent, edge.A)] = Find(parent, edge.B);
        }
        std::vector<bool> constrainedComponent(count, false), isRoot(count, false);
        for (std::size_t row = 0; row < count; ++row)
        {
            const auto root = Find(parent, row);
            isRoot[root] = true;
            if (role[row]) constrainedComponent[root] = true;
        }
        for (std::size_t row = 0; row < count; ++row)
            if (isRoot[row])
            {
                ++result.Stats.Components;
                if (!constrainedComponent[row]) ++result.Stats.UnconstrainedComponents;
            }
        if (result.Stats.UnconstrainedComponents && params.Unconstrained == UnconstrainedPolicy::Fail)
            return fail(std::to_string(result.Stats.UnconstrainedComponents) +
                        " connected component(s) have no constraint; add constraints or keep their input values.");
        const bool grounded = params.Unconstrained == UnconstrainedPolicy::ZeroMean;

        // Pure-Neumann components: A is singular with the constants as null space, so A x = b needs
        // sum b = 0. The defect is removed in proportion to mass, the smallest row is grounded to
        // zero and the mass-weighted mean is subtracted after the solve.
        std::vector<double> projected;
        if (grounded && result.Stats.UnconstrainedComponents)
        {
            projected.resize(values.size());
            for (std::size_t i = 0; i < values.size(); ++i) projected[i] = sourceAt(i / channels, i % channels);
            std::vector<double> sum(count * channels, 0.0), absSum(count * channels, 0.0), massSum(count, 0.0);
            for (std::size_t row = 0; row < count; ++row)
            {
                const auto root = Find(parent, row);
                if (constrainedComponent[root]) continue;
                massSum[root] += mass.empty() ? 1.0 : mass[row];
                for (std::size_t c = 0; c < channels; ++c)
                {
                    sum[root * channels + c] += projected[row * channels + c];
                    absSum[root * channels + c] += std::abs(projected[row * channels + c]);
                }
            }
            for (std::size_t row = 0; row < count; ++row)
            {
                const auto root = Find(parent, row);
                if (constrainedComponent[root]) continue;
                if (root == row)
                {
                    ++result.Stats.GroundedComponents;
                    for (std::size_t c = 0; c < channels; ++c)
                        if (absSum[row * channels + c] > 0)
                            result.Stats.MaxCompatibilityDefect = std::max(result.Stats.MaxCompatibilityDefect,
                                std::abs(sum[row * channels + c]) / absSum[row * channels + c]);
                }
                const double share = (mass.empty() ? 1.0 : mass[row]) / massSum[root];
                for (std::size_t c = 0; c < channels; ++c) projected[row * channels + c] -= sum[root * channels + c] * share;
            }
            source = projected;
        }

        // Compact the free rows; kept rows belong to unconstrained components and couple to nothing.
        std::vector<std::size_t> freeIndex(count, kInvalid);
        std::vector<std::size_t> freeRows;
        std::vector<bool> groundedComponent(count, false);
        if (result.Stats.UnconstrainedComponents && !grounded) result.KeptInput.assign(count, false);
        for (std::size_t row = 0; row < count; ++row)
        {
            const auto root = Find(parent, row);
            if (!constrainedComponent[root] && !grounded)
            {
                if (!finiteRow(row)) return fail("Kept unconstrained rows must have finite input values.");
                result.KeptInput[row] = true;
                continue;
            }
            // Rows are visited in ascending order, so the first row of a component is its ground.
            if (!constrainedComponent[root] && !groundedComponent[root])
            {
                groundedComponent[root] = true;
                role[row] = 3;
                continue;
            }
            if (role[row] == 1) continue;
            freeIndex[row] = freeRows.size();
            freeRows.push_back(row);
        }
        result.Stats.FreeRows = freeRows.size();
        result.Values.assign(values.begin(), values.end());
        for (std::size_t row = 0; row < count; ++row)
            if (role[row] == 3)
                for (std::size_t c = 0; c < channels; ++c) result.Values[row * channels + c] = 0.0;
        const auto finish = [&]() -> Result {
            if (result.Stats.GroundedComponents)
            {
                std::vector<double> mean(count * channels, 0.0), massSum(count, 0.0);
                for (std::size_t row = 0; row < count; ++row)
                {
                    const auto root = Find(parent, row);
                    if (!groundedComponent[root]) continue;
                    const double m = mass.empty() ? 1.0 : mass[row];
                    massSum[root] += m;
                    for (std::size_t c = 0; c < channels; ++c) mean[root * channels + c] += m * result.Values[row * channels + c];
                }
                for (std::size_t row = 0; row < count; ++row)
                {
                    const auto root = Find(parent, row);
                    if (!groundedComponent[root]) continue;
                    for (std::size_t c = 0; c < channels; ++c)
                        result.Values[row * channels + c] -= mean[root * channels + c] / massSum[root];
                }
            }
            result.Success = true;
            result.Diagnostic = "cpu_reference_sparse_cholesky";
            return result;
        };
        if (freeRows.empty()) return finish();

        auto system = BuildLaplacian(count, edges);
        if (params.Order != FieldOrder::Harmonic)
        {
            const auto laplacian = system;
            std::vector<double> inverseMass(count, 1.0);
            for (std::size_t row = 0; row < mass.size(); ++row) inverseMass[row] = 1.0 / mass[row];
            for (auto order = FieldOrder::Harmonic; order != params.Order;
                 order = static_cast<FieldOrder>(static_cast<std::uint8_t>(order) + 1))
                system = MultiplyMassWeighted(system, inverseMass, laplacian);
        }

        // Reduced system (A_ff + W_f) x_f = W_f t_f + b_f - A_fc x_c, assembled once for every
        // channel. Grounded rows are zero and contribute nothing.
        Sparse::SparseBuilder builder(freeRows.size(), freeRows.size());
        struct Coupling { std::size_t Free, Hard; double Value; };
        std::vector<Coupling> couplings;
        for (std::size_t f = 0; f < freeRows.size(); ++f)
        {
            const auto row = freeRows[f];
            if (role[row] == 2) builder.Add(f, f, softWeight[row]);
            for (const auto& [col, value] : system.Rows[row])
            {
                if (freeIndex[col] != kInvalid) builder.Add(f, freeIndex[col], value);
                else if (role[col] == 1) couplings.push_back({f, col, value});
            }
        }
        const auto reduced = builder.Build();
        if (!reduced.Valid) return fail("Unable to assemble the reduced harmonic system.");
        Sparse::SparseLLT cholesky;
        if (!cholesky.factor(reduced.Matrix).Succeeded())
            return fail("Sparse Cholesky factorization of the reduced system failed; no values were changed.");

        std::vector<double> rhs(freeRows.size()), solved(freeRows.size()), check(freeRows.size());
        for (std::size_t c = 0; c < channels; ++c)
        {
            for (std::size_t f = 0; f < freeRows.size(); ++f)
            {
                const auto row = freeRows[f];
                rhs[f] = sourceAt(row, c) + (role[row] == 2 ? softWeight[row] * values[row * channels + c] : 0.0);
            }
            for (const auto& coupling : couplings)
                rhs[coupling.Free] -= coupling.Value * values[coupling.Hard * channels + c];
            if (!cholesky.solve(rhs, solved).Succeeded())
                return fail("Sparse Cholesky solve failed; no values were changed.");
            reduced.Matrix.Multiply(solved, check);
            double residual = 0, scale = 1;
            for (std::size_t f = 0; f < freeRows.size(); ++f)
            {
                residual = std::max(residual, std::abs(check[f] - rhs[f]));
                scale = std::max(scale, std::abs(rhs[f]));
            }
            result.Stats.MaxRelativeResidual = std::max(result.Stats.MaxRelativeResidual, residual / scale);
            for (std::size_t f = 0; f < freeRows.size(); ++f)
                result.Values[freeRows[f] * channels + c] = solved[f];
        }
        // Free-row inputs are ignored and overwritten, so non-finite output comes from the solve.
        for (const auto row : freeRows)
            for (std::size_t c = 0; c < channels; ++c)
                if (!std::isfinite(result.Values[row * channels + c]))
                    return fail("The harmonic solve produced non-finite values; no values were changed.");
        return finish();
    }

    LabelResult PropagateLabels(std::span<const std::int32_t> labels, std::int32_t unlabeled,
                                std::span<const Smoothing::PropertyEdge> edges, const Params& params,
                                std::span<const double> mass)
    {
        LabelResult result;
        if (params.Unconstrained == UnconstrainedPolicy::ZeroMean)
        {
            result.Diagnostic = "Label propagation cannot ground unseeded components; fail or keep their labels.";
            return result;
        }
        std::vector<std::size_t> seeds;
        for (std::size_t row = 0; row < labels.size(); ++row)
            if (labels[row] != unlabeled)
            {
                seeds.push_back(row);
                result.SeedLabels.push_back(labels[row]);
            }
        std::ranges::sort(result.SeedLabels);
        result.SeedLabels.erase(std::ranges::unique(result.SeedLabels).begin(), result.SeedLabels.end());
        if (labels.empty() || result.SeedLabels.empty())
        {
            result.Diagnostic = "Label propagation needs at least one seed label.";
            result.SeedLabels.clear();
            return result;
        }
        const std::size_t channels = result.SeedLabels.size();
        std::vector<double> indicator(labels.size() * channels, 0.0);
        for (const auto row : seeds)
        {
            const auto label = std::ranges::lower_bound(result.SeedLabels, labels[row]) - result.SeedLabels.begin();
            indicator[row * channels + static_cast<std::size_t>(label)] = 1.0;
        }
        const auto field = Solve(indicator, channels, edges, params, seeds, {}, mass);
        result.Stats = field.Stats;
        if (!field.Success)
        {
            result.Diagnostic = field.Diagnostic;
            result.SeedLabels.clear();
            return result;
        }
        result.Labels.resize(labels.size());
        result.Confidence.resize(labels.size());
        result.Weights = field.Values;
        for (std::size_t row = 0; row < labels.size(); ++row)
        {
            if (!field.KeptInput.empty() && field.KeptInput[row])
            {
                result.Labels[row] = labels[row];
                result.Confidence[row] = 0.0;
                std::fill_n(result.Weights.begin() + static_cast<std::ptrdiff_t>(row * channels), channels, 0.0);
                continue;
            }
            std::size_t best = 0;
            for (std::size_t c = 1; c < channels; ++c)
                if (field.Values[row * channels + c] > field.Values[row * channels + best]) best = c;
            result.Labels[row] = result.SeedLabels[best];
            result.Confidence[row] = field.Values[row * channels + best];
        }
        result.Success = true;
        result.Diagnostic = field.Diagnostic;
        return result;
    }
}
