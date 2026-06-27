module;

#include <cstddef>
#include <optional>
#include <span>
#include <vector>
#include <algorithm>
#include <cmath>

module Geometry.Statistics;

namespace Geometry::Statistics
{
    double SafeAcos(double x) noexcept
    {
        if (!std::isfinite(x))
        {
            return 0.0;
        }
        return std::acos(std::clamp(x, -1.0, 1.0));
    }

    double SafeAsin(double x) noexcept
    {
        if (!std::isfinite(x))
        {
            return 0.0;
        }
        return std::asin(std::clamp(x, -1.0, 1.0));
    }

    void StreamingMoments::Reset() noexcept
    {
        count_ = 0;
        mean_ = 0.0;
        m2_ = 0.0;
        m3_ = 0.0;
        m4_ = 0.0;
    }

    void StreamingMoments::Add(double x) noexcept
    {
        if (!std::isfinite(x))
        {
            return; // fail-closed: never poison the accumulator with NaN/Inf.
        }

        const double n1 = static_cast<double>(count_);
        const std::size_t n = count_ + 1;
        const double nd = static_cast<double>(n);

        const double delta = x - mean_;
        const double deltaN = delta / nd;
        const double deltaN2 = deltaN * deltaN;
        const double term1 = delta * deltaN * n1;

        mean_ += deltaN;
        m4_ += term1 * deltaN2 * (nd * nd - 3.0 * nd + 3.0)
             + 6.0 * deltaN2 * m2_ - 4.0 * deltaN * m3_;
        m3_ += term1 * deltaN * (nd - 2.0) - 3.0 * deltaN * m2_;
        m2_ += term1;

        count_ = n;
    }

    std::optional<double> StreamingMoments::Mean() const noexcept
    {
        if (count_ == 0)
        {
            return std::nullopt;
        }
        return mean_;
    }

    std::optional<double> StreamingMoments::PopulationVariance() const noexcept
    {
        if (count_ == 0)
        {
            return std::nullopt;
        }
        return m2_ / static_cast<double>(count_);
    }

    std::optional<double> StreamingMoments::SampleVariance() const noexcept
    {
        if (count_ < 2)
        {
            return std::nullopt;
        }
        return m2_ / static_cast<double>(count_ - 1);
    }

    std::optional<double> StreamingMoments::Skewness() const noexcept
    {
        if (count_ < 2 || !(m2_ > 0.0))
        {
            return std::nullopt;
        }
        const double nd = static_cast<double>(count_);
        return (std::sqrt(nd) * m3_) / std::pow(m2_, 1.5);
    }

    std::optional<double> StreamingMoments::Kurtosis() const noexcept
    {
        if (count_ < 2 || !(m2_ > 0.0))
        {
            return std::nullopt;
        }
        const double nd = static_cast<double>(count_);
        return (nd * m4_) / (m2_ * m2_) - 3.0; // excess kurtosis
    }

    void StreamingMoments::Merge(const StreamingMoments& other) noexcept
    {
        if (other.count_ == 0)
        {
            return;
        }
        if (count_ == 0)
        {
            *this = other;
            return;
        }

        const double na = static_cast<double>(count_);
        const double nb = static_cast<double>(other.count_);
        const double nx = na + nb;
        const double delta = other.mean_ - mean_;
        const double delta2 = delta * delta;
        const double delta3 = delta2 * delta;
        const double delta4 = delta2 * delta2;

        const double combinedMean = mean_ + delta * nb / nx;

        const double combinedM2 = m2_ + other.m2_ + delta2 * na * nb / nx;

        const double combinedM3 = m3_ + other.m3_
            + delta3 * na * nb * (na - nb) / (nx * nx)
            + 3.0 * delta * (na * other.m2_ - nb * m2_) / nx;

        const double combinedM4 = m4_ + other.m4_
            + delta4 * na * nb * (na * na - na * nb + nb * nb) / (nx * nx * nx)
            + 6.0 * delta2 * (na * na * other.m2_ + nb * nb * m2_) / (nx * nx)
            + 4.0 * delta * (na * other.m3_ - nb * m3_) / nx;

        mean_ = combinedMean;
        m2_ = combinedM2;
        m3_ = combinedM3;
        m4_ = combinedM4;
        count_ = static_cast<std::size_t>(nx);
    }

    StreamingMoments StreamingMoments::operator+(const StreamingMoments& other) const noexcept
    {
        StreamingMoments result = *this;
        result.Merge(other);
        return result;
    }

    void RunningMedian::Reset() noexcept
    {
        lower_.clear();
        upper_.clear();
    }

    void RunningMedian::Add(double x)
    {
        if (!std::isfinite(x))
        {
            return; // fail-closed
        }

        // lower_ is a max-heap, upper_ is a min-heap. Keep |lower_| - |upper_| in {0, 1}.
        if (lower_.empty() || x <= lower_.front())
        {
            lower_.push_back(x);
            std::push_heap(lower_.begin(), lower_.end()); // max-heap (default)
        }
        else
        {
            upper_.push_back(x);
            std::push_heap(upper_.begin(), upper_.end(), std::greater<double>{}); // min-heap
        }

        // Rebalance so that lower_ holds the extra element when sizes are odd.
        if (lower_.size() > upper_.size() + 1)
        {
            std::pop_heap(lower_.begin(), lower_.end());
            const double moved = lower_.back();
            lower_.pop_back();
            upper_.push_back(moved);
            std::push_heap(upper_.begin(), upper_.end(), std::greater<double>{});
        }
        else if (upper_.size() > lower_.size())
        {
            std::pop_heap(upper_.begin(), upper_.end(), std::greater<double>{});
            const double moved = upper_.back();
            upper_.pop_back();
            lower_.push_back(moved);
            std::push_heap(lower_.begin(), lower_.end());
        }
    }

    std::optional<double> RunningMedian::Median() const
    {
        const std::size_t total = lower_.size() + upper_.size();
        if (total == 0)
        {
            return std::nullopt;
        }
        if (lower_.size() > upper_.size())
        {
            return lower_.front();
        }
        return 0.5 * (lower_.front() + upper_.front());
    }

    namespace
    {
        // Collect finite samples into a sorted scratch buffer.
        [[nodiscard]] std::vector<double> SortedFinite(std::span<const double> values)
        {
            std::vector<double> sorted;
            sorted.reserve(values.size());
            for (const double v : values)
            {
                if (std::isfinite(v))
                {
                    sorted.push_back(v);
                }
            }
            std::sort(sorted.begin(), sorted.end());
            return sorted;
        }
    }

    std::optional<double> Median(std::span<const double> values)
    {
        const std::vector<double> sorted = SortedFinite(values);
        if (sorted.empty())
        {
            return std::nullopt;
        }
        const std::size_t n = sorted.size();
        if (n % 2 == 1)
        {
            return sorted[n / 2];
        }
        return 0.5 * (sorted[n / 2 - 1] + sorted[n / 2]);
    }

    std::optional<double> Quantile(std::span<const double> values, double q)
    {
        if (!std::isfinite(q) || q < 0.0 || q > 1.0)
        {
            return std::nullopt;
        }
        const std::vector<double> sorted = SortedFinite(values);
        if (sorted.empty())
        {
            return std::nullopt;
        }
        const std::size_t n = sorted.size();
        if (n == 1)
        {
            return sorted[0];
        }

        // Type-7 (numpy "linear"): position h = q * (n - 1), interpolate.
        const double h = q * static_cast<double>(n - 1);
        const double hFloor = std::floor(h);
        const std::size_t lo = static_cast<std::size_t>(hFloor);
        if (lo >= n - 1)
        {
            return sorted[n - 1];
        }
        const double frac = h - hFloor;
        return sorted[lo] + frac * (sorted[lo + 1] - sorted[lo]);
    }
}
