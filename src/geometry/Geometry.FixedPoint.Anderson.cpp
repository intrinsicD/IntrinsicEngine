module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>

module Geometry.FixedPoint.Anderson;

namespace Geometry::FixedPoint
{
    namespace
    {
        [[nodiscard]] bool IsFinite(
            const std::span<const double> values) noexcept
        {
            return std::all_of(
                values.begin(), values.end(),
                [](const double value)
                { return std::isfinite(value); });
        }

        [[nodiscard]] double Norm(
            const std::span<const double> values) noexcept
        {
            double squaredNorm = 0.0;
            for (const double value : values)
                squaredNorm += value * value;
            return std::sqrt(squaredNorm);
        }

        [[nodiscard]] double Dot(
            const std::span<const double> lhs,
            const std::span<const double> rhs) noexcept
        {
            double result = 0.0;
            for (std::size_t i = 0u; i < lhs.size(); ++i)
                result += lhs[i] * rhs[i];
            return result;
        }

        [[nodiscard]] bool SolveConstrainedMix(
            const std::vector<std::vector<double>>& residuals,
            const double regularization,
            std::vector<double>& coefficients)
        {
            const std::size_t count = residuals.size();
            const std::size_t dimension = count + 1u;
            std::vector<double> augmented(
                dimension * (dimension + 1u), 0.0);
            const auto cell = [dimension, &augmented](
                                  const std::size_t row,
                                  const std::size_t column) -> double&
            {
                return augmented[
                    row * (dimension + 1u) + column];
            };

            for (std::size_t row = 0u; row < count; ++row)
            {
                for (std::size_t column = 0u;
                     column < count;
                     ++column)
                {
                    cell(row, column) = Dot(
                        residuals[row], residuals[column]);
                }
                cell(row, row) += regularization;
                cell(row, count) = 1.0;
                cell(count, row) = 1.0;
            }
            cell(count, dimension) = 1.0;

            constexpr double kPivotTolerance = 1.0e-15;
            for (std::size_t pivot = 0u;
                 pivot < dimension;
                 ++pivot)
            {
                std::size_t bestRow = pivot;
                double bestMagnitude =
                    std::abs(cell(pivot, pivot));
                for (std::size_t row = pivot + 1u;
                     row < dimension;
                     ++row)
                {
                    const double magnitude =
                        std::abs(cell(row, pivot));
                    if (magnitude > bestMagnitude)
                    {
                        bestMagnitude = magnitude;
                        bestRow = row;
                    }
                }
                if (!(bestMagnitude > kPivotTolerance) ||
                    !std::isfinite(bestMagnitude))
                {
                    return false;
                }
                if (bestRow != pivot)
                {
                    for (std::size_t column = pivot;
                         column <= dimension;
                         ++column)
                    {
                        std::swap(
                            cell(pivot, column),
                            cell(bestRow, column));
                    }
                }

                const double diagonal = cell(pivot, pivot);
                for (std::size_t column = pivot;
                     column <= dimension;
                     ++column)
                {
                    cell(pivot, column) /= diagonal;
                }
                for (std::size_t row = 0u;
                     row < dimension;
                     ++row)
                {
                    if (row == pivot)
                        continue;
                    const double factor = cell(row, pivot);
                    for (std::size_t column = pivot;
                         column <= dimension;
                         ++column)
                    {
                        cell(row, column) -=
                            factor * cell(pivot, column);
                    }
                }
            }

            coefficients.resize(count);
            for (std::size_t i = 0u; i < count; ++i)
            {
                coefficients[i] = cell(i, dimension);
                if (!std::isfinite(coefficients[i]))
                    return false;
            }
            return true;
        }
    }

    AndersonStepResult AndersonStep(
        AndersonState& state,
        const std::span<const double> current,
        const std::span<const double> mapped)
    {
        AndersonStepResult result{};
        const AndersonParams& params = state.Params;
        if (current.empty() ||
            current.size() != mapped.size() ||
            !IsFinite(current) ||
            !IsFinite(mapped) ||
            params.Window == 0u ||
            !std::isfinite(params.Damping) ||
            params.Damping < 0.0 ||
            params.Damping > 1.0 ||
            !std::isfinite(params.Regularization) ||
            params.Regularization < 0.0 ||
            !std::isfinite(params.SafeguardFactor) ||
            params.SafeguardFactor < 1.0)
        {
            return result;
        }

        std::vector<double> residual(current.size(), 0.0);
        for (std::size_t i = 0u; i < current.size(); ++i)
            residual[i] = mapped[i] - current[i];
        result.PlainResidualNorm = Norm(residual);
        result.MixedResidualNorm = result.PlainResidualNorm;
        result.Value.assign(mapped.begin(), mapped.end());

        if ((!state.MappedHistory.empty() &&
             state.MappedHistory.front().size() != current.size()) ||
            (!state.ResidualHistory.empty() &&
             state.ResidualHistory.front().size() != current.size()))
        {
            ResetAnderson(state);
        }

        state.MappedHistory.emplace_back(
            mapped.begin(), mapped.end());
        state.ResidualHistory.push_back(std::move(residual));
        while (state.MappedHistory.size() > params.Window)
        {
            state.MappedHistory.erase(
                state.MappedHistory.begin());
            state.ResidualHistory.erase(
                state.ResidualHistory.begin());
        }

        if (state.MappedHistory.size() < 2u ||
            !(result.PlainResidualNorm > 0.0))
        {
            result.Status = AndersonStepStatus::Plain;
            return result;
        }

        std::vector<double> coefficients{};
        if (!SolveConstrainedMix(
                state.ResidualHistory,
                params.Regularization,
                coefficients))
        {
            result.Status = AndersonStepStatus::Safeguarded;
            return result;
        }

        std::vector<double> mixed(mapped.size(), 0.0);
        std::vector<double> mixedResidual(
            mapped.size(), 0.0);
        for (std::size_t historyIndex = 0u;
             historyIndex < coefficients.size();
             ++historyIndex)
        {
            for (std::size_t valueIndex = 0u;
                 valueIndex < mapped.size();
                 ++valueIndex)
            {
                mixed[valueIndex] +=
                    coefficients[historyIndex] *
                    state.MappedHistory[historyIndex]
                                              [valueIndex];
                mixedResidual[valueIndex] +=
                    coefficients[historyIndex] *
                    state.ResidualHistory[historyIndex]
                                                [valueIndex];
            }
        }
        result.MixedResidualNorm = Norm(mixedResidual);
        if (!IsFinite(mixed) ||
            !std::isfinite(result.MixedResidualNorm))
        {
            result.Status = AndersonStepStatus::NumericalFailure;
            return result;
        }
        if (result.MixedResidualNorm >
            params.SafeguardFactor *
                result.PlainResidualNorm)
        {
            result.Status = AndersonStepStatus::Safeguarded;
            return result;
        }

        for (std::size_t i = 0u; i < mixed.size(); ++i)
        {
            result.Value[i] =
                (1.0 - params.Damping) * mapped[i] +
                params.Damping * mixed[i];
        }
        if (!IsFinite(result.Value))
        {
            result.Value.assign(mapped.begin(), mapped.end());
            result.Status = AndersonStepStatus::NumericalFailure;
            return result;
        }
        result.Status = AndersonStepStatus::Accelerated;
        return result;
    }

    void ResetAnderson(AndersonState& state) noexcept
    {
        state.MappedHistory.clear();
        state.ResidualHistory.clear();
    }
}
