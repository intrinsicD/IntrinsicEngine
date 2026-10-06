// Deterministic, fail-closed scalar statistics: streaming mean, medians and quantiles.
module;

#include <cstddef>
#include <optional>
#include <span>
#include <vector>
#include <algorithm>
#include <cmath>
#include <type_traits>

export module Geometry.Statistics;

// Numeric/fail-closed contract (GEOM-005 / GEOM-007):
//  - Non-finite samples are ignored by the accumulators (never poison state).
//  - Mean/median/quantile queries with insufficient data return std::nullopt
//    rather than producing NaN/Inf.
//  - SafeAcos clamps finite inputs into the valid domain and returns a defined
//    finite fall-back (0.0) for non-finite input.
export namespace Geometry::Statistics
{
    // Clamp the argument into [-1, 1] before std::acos so callers
    // never trip on tiny out-of-domain rounding error. Finite input always
    // yields a finite result; non-finite input returns 0.0 (defined fail-closed).
    [[nodiscard]] double SafeAcos(double x) noexcept;

    // Mergeable streaming moments (Pébay/Terriberry online M2/M3/M4 update); only
    // Count/Mean are exposed. Two accumulators combine exactly with Merge /
    // operator+ (parallel combine).
    class StreamingMoments
    {
    public:
        void Add(double x) noexcept;
        void Reset() noexcept;

        [[nodiscard]] std::size_t Count() const noexcept { return count_; }
        [[nodiscard]] bool Empty() const noexcept { return count_ == 0; }

        [[nodiscard]] std::optional<double> Mean() const noexcept;

        void Merge(const StreamingMoments& other) noexcept;
        [[nodiscard]] StreamingMoments operator+(const StreamingMoments& other) const noexcept;

    private:
        std::size_t count_{0};
        double mean_{0.0};
        double m2_{0.0};
        double m3_{0.0};
        double m4_{0.0};
    };

    // Streaming median via the classic two-heap method. Non-finite samples are
    // ignored. Median() returns nullopt while empty.
    class RunningMedian
    {
    public:
        void Add(double x);
        void Reset() noexcept;

        [[nodiscard]] std::size_t Count() const noexcept { return lower_.size() + upper_.size(); }
        [[nodiscard]] bool Empty() const noexcept { return Count() == 0; }
        [[nodiscard]] std::optional<double> Median() const;

    private:
        std::vector<double> lower_; // max-heap of the smaller half
        std::vector<double> upper_; // min-heap of the larger half
    };

    // Exact median over a copy of the samples. nullopt if no finite samples.
    [[nodiscard]] std::optional<double> Median(std::span<const double> values);

    // Linear-interpolation quantile (numpy "linear" / type-7) over a copy of the
    // finite samples. q is clamped-checked: q outside [0, 1] (or no finite
    // samples) returns nullopt.
    [[nodiscard]] std::optional<double> Quantile(std::span<const double> values, double q);

    template <typename T>
        requires(std::is_arithmetic_v<T>)
    [[nodiscard]] std::optional<double> Median(std::span<const T> values)
    {
        std::vector<double> finite;
        finite.reserve(values.size());
        for (const T value : values)
        {
            const double scalar = static_cast<double>(value);
            if (std::isfinite(scalar))
            {
                finite.push_back(scalar);
            }
        }
        return Median(std::span<const double>(finite));
    }

    template <typename T>
        requires(std::is_arithmetic_v<T>)
    [[nodiscard]] std::optional<double> Median(const std::vector<T>& values)
    {
        return Median(std::span<const T>(values));
    }

    template <typename T>
        requires(std::is_arithmetic_v<T>)
    [[nodiscard]] std::optional<double> Quantile(std::span<const T> values, double q)
    {
        std::vector<double> finite;
        finite.reserve(values.size());
        for (const T value : values)
        {
            const double scalar = static_cast<double>(value);
            if (std::isfinite(scalar))
            {
                finite.push_back(scalar);
            }
        }
        return Quantile(std::span<const double>(finite), q);
    }

    template <typename T>
        requires(std::is_arithmetic_v<T>)
    [[nodiscard]] std::optional<double> Quantile(const std::vector<T>& values, double q)
    {
        return Quantile(std::span<const T>(values), q);
    }
}
