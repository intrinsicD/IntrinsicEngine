module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <glm/glm.hpp>

module Geometry.Properties.Statistics;

namespace Geometry
{
    namespace
    {
        using Kind = PropertyValueKind;
        using Status = PropertyStatisticsStatus;
        using PropertyData = std::variant<ConstProperty<bool>, ConstProperty<std::int32_t>,
            ConstProperty<std::uint32_t>, ConstProperty<std::uint64_t>, ConstProperty<float>,
            ConstProperty<double>, ConstProperty<glm::vec2>, ConstProperty<glm::vec3>,
            ConstProperty<glm::vec4>>;

        std::optional<PropertyDescriptor> FindDescriptor(
            const ConstPropertySet& properties, std::string_view name)
        {
            for (const auto& descriptor : properties.Descriptors())
                if (descriptor.Name == name)
                    return descriptor;
            return std::nullopt;
        }

        std::optional<PropertyData> GetData(
            const ConstPropertySet& properties, const PropertyDescriptor& descriptor)
        {
            switch (descriptor.ValueKind)
            {
            case Kind::Bool: return properties.Get<bool>(descriptor.Name);
            case Kind::Int32: return properties.Get<std::int32_t>(descriptor.Name);
            case Kind::UInt32: return properties.Get<std::uint32_t>(descriptor.Name);
            case Kind::UInt64: return properties.Get<std::uint64_t>(descriptor.Name);
            case Kind::Float: return properties.Get<float>(descriptor.Name);
            case Kind::Double: return properties.Get<double>(descriptor.Name);
            case Kind::Vec2: return properties.Get<glm::vec2>(descriptor.Name);
            case Kind::Vec3: return properties.Get<glm::vec3>(descriptor.Name);
            case Kind::Vec4: return properties.Get<glm::vec4>(descriptor.Name);
            case Kind::Unknown: return std::nullopt;
            }
            return std::nullopt;
        }

        std::size_t Width(Kind kind)
        {
            switch (kind)
            {
            case Kind::Vec2: return 2;
            case Kind::Vec3: return 3;
            case Kind::Vec4: return 4;
            default: return 1;
            }
        }

        struct NumericValue
        {
            bool Integral{false};
            bool Negative{false};
            std::uint64_t Magnitude{0};
            double Real{0};

            long double Wide() const
            {
                if (!Integral)
                    return static_cast<long double>(Real);
                const auto magnitude = static_cast<long double>(Magnitude);
                return Negative ? -magnitude : magnitude;
            }
        };

        template <class T>
        NumericValue ToValue(T value)
        {
            if constexpr (std::is_integral_v<T>)
            {
                if constexpr (std::is_signed_v<T>)
                {
                    const auto signedValue = static_cast<std::int64_t>(value);
                    return {.Integral = true, .Negative = signedValue < 0,
                            .Magnitude = static_cast<std::uint64_t>(signedValue < 0 ? -signedValue : signedValue)};
                }
                else
                    return {.Integral = true, .Magnitude = static_cast<std::uint64_t>(value)};
            }
            else
                return {.Real = static_cast<double>(value)};
        }

        std::array<NumericValue, 4> ReadRow(const PropertyData& data, std::size_t row)
        {
            return std::visit([row](const auto& property)
            {
                std::array<NumericValue, 4> result{};
                const auto value = property[row];
                if constexpr (std::is_arithmetic_v<decltype(value)>)
                    result[0] = ToValue(value);
                else
                    for (int component = 0; component < value.length(); ++component)
                        result[component] = ToValue(value[component]);
                return result;
            }, data);
        }

        long double AbsoluteDifference(const NumericValue& a, const NumericValue& b)
        {
            if (a.Integral && b.Integral)
            {
                if (a.Negative != b.Negative)
                    return static_cast<long double>(a.Magnitude) + static_cast<long double>(b.Magnitude);
                return static_cast<long double>(a.Magnitude >= b.Magnitude
                    ? a.Magnitude - b.Magnitude : b.Magnitude - a.Magnitude);
            }
            if (!a.Integral && !b.Integral)
                return std::abs(a.Wide() - b.Wide());

            const auto& integer = a.Integral ? a : b;
            const auto& real = a.Integral ? b : a;
            const double magnitude = std::abs(real.Real);
            if (integer.Negative != (real.Real < 0))
                return static_cast<long double>(integer.Magnitude) + static_cast<long double>(magnitude);

            // Decompose before converting the integer: U64_MAX versus double(2^64)
            // must differ by one, even on platforms where long double equals double.
            constexpr double uint64Limit = 0x1p64;
            if (magnitude >= uint64Limit)
                return static_cast<long double>(magnitude - uint64Limit) +
                    static_cast<long double>(std::numeric_limits<std::uint64_t>::max() - integer.Magnitude) + 1;
            const auto whole = static_cast<std::uint64_t>(magnitude);
            const double fraction = magnitude - std::floor(magnitude);
            if (integer.Magnitude >= whole)
                return std::abs(static_cast<long double>(integer.Magnitude - whole) - fraction);
            return static_cast<long double>(whole - integer.Magnitude) + fraction;
        }

        struct Accumulator
        {
            PropertyNumericStatistics Result;
            long double Minimum{0};
            long double Maximum{0};
            long double Origin{0};
            long double CenteredMean{0};
            long double M2{0};
            long double Sum{0};
            long double SumCorrection{0};
            long double SquareScale{0};
            long double ScaledSquares{0};

            void Add(long double value)
            {
                ++Result.Count;
                if (std::isnan(value))
                {
                    ++Result.NaNCount;
                    return;
                }
                if (!std::isfinite(value))
                {
                    ++Result.InfCount;
                    return;
                }
                if (value == 0)
                    ++Result.ZeroCount;
                if (Result.FiniteCount++ == 0)
                    Origin = Minimum = Maximum = value;
                Minimum = std::min(Minimum, value);
                Maximum = std::max(Maximum, value);

                // Compensation retains small residuals when large values cancel.
                const long double nextSum = Sum + value;
                SumCorrection += std::abs(Sum) >= std::abs(value)
                    ? (Sum - nextSum) + value : (value - nextSum) + Sum;
                Sum = nextSum;

                // Center first so close, large values retain their small variance.
                // Unlike general higher moments, only the second moment is needed.
                const long double centered = value - Origin;
                const long double delta = centered - CenteredMean;
                CenteredMean += delta / static_cast<long double>(Result.FiniteCount);
                M2 += delta * (centered - CenteredMean);

                const long double magnitude = std::abs(value);
                if (magnitude > SquareScale)
                {
                    const long double ratio = SquareScale / magnitude;
                    ScaledSquares = 1 + ScaledSquares * ratio * ratio;
                    SquareScale = magnitude;
                }
                else if (magnitude != 0)
                {
                    const long double ratio = magnitude / SquareScale;
                    ScaledSquares += ratio * ratio;
                }
            }

            bool Finish(std::size_t bins)
            {
                if (Result.FiniteCount == 0)
                    return true;
                // long double may have no wider range than double. Check before
                // clamping: std::max(0, NaN) would otherwise hide moment overflow.
                const std::array<long double, 6> intermediates{
                    CenteredMean, M2, Sum, SumCorrection, SquareScale, ScaledSquares};
                for (const long double value : intermediates)
                    if (!std::isfinite(value))
                        return false;
                const long double count = static_cast<long double>(Result.FiniteCount);
                const long double mean = (Sum + SumCorrection) / count;
                if (!std::isfinite(mean))
                    return false;
                const std::array<long double, 5> summaries{
                    Minimum, Maximum, std::clamp(mean, Minimum, Maximum),
                    SquareScale * std::sqrt(ScaledSquares / count),
                    std::sqrt(std::max(0.0L, M2 / count))};
                for (const long double value : summaries)
                    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<double>::max())
                        return false;
                Result.Min = static_cast<double>(summaries[0]);
                Result.Max = static_cast<double>(summaries[1]);
                Result.Mean = static_cast<double>(summaries[2]);
                Result.RMS = static_cast<double>(summaries[3]);
                Result.StdDev = static_cast<double>(summaries[4]);
                if (bins != 0)
                {
                    Result.Histogram.Counts.assign(bins, 0);
                    Result.Histogram.Edges.resize(bins + 1);
                    for (std::size_t i = 0; i <= bins; ++i)
                    {
                        const long double fraction = static_cast<long double>(i) / bins;
                        // Convex interpolation avoids overflowing Max-Min.
                        Result.Histogram.Edges[i] = static_cast<double>(
                            (1 - fraction) * Minimum + fraction * Maximum);
                    }
                }
                return true;
            }

            void AddHistogram(long double value)
            {
                auto& histogram = Result.Histogram;
                if (histogram.Counts.empty() || !std::isfinite(value))
                    return;
                // Histogram coordinates, like its published edges, use double.
                const double sample = static_cast<double>(value);
                std::size_t bin = 0;
                if (histogram.Edges.front() != histogram.Edges.back())
                {
                    const auto edge = std::upper_bound(histogram.Edges.begin() + 1,
                                                       histogram.Edges.end() - 1, sample);
                    bin = static_cast<std::size_t>(edge - histogram.Edges.begin() - 1);
                }
                ++histogram.Counts[bin];
            }
        };

        long double RowMagnitude(const std::array<NumericValue, 4>& values, std::size_t width)
        {
            long double magnitude = 0;
            for (std::size_t component = 0; component < width; ++component)
            {
                const long double value = values[component].Wide();
                if (std::isnan(value))
                    return std::numeric_limits<long double>::quiet_NaN();
                magnitude = std::hypot(magnitude, value);
            }
            return magnitude;
        }
    }

    bool PropertyDeletionMasks::IsDeleted(std::size_t row) const
    {
        for (const auto& mask : Masks)
            if (row < mask.Size() && mask[row])
                return true;
        return false;
    }

    PropertyDeletionMasks ResolvePropertyDeletionMasks(const ConstPropertySet& properties)
    {
        PropertyDeletionMasks result;
        for (const auto& descriptor : properties.Descriptors())
        {
            if (!std::string_view(descriptor.Name).ends_with(":deleted"))
                continue;
            const auto mask = properties.Get<bool>(descriptor.Name);
            if (!mask || mask.Size() != properties.Size())
            {
                result.Status = Status::InvalidDeletionMask;
                result.Diagnostic = "Deletion mask '" + descriptor.Name + "' must be a bool property with domain row count.";
                result.Masks.clear();
                return result;
            }
            result.Masks.push_back(mask);
        }
        return result;
    }

    PropertyStatisticsResult ComputePropertyStatistics(
        const ConstPropertySet& properties, std::string_view name, const StatisticsParams& params)
    {
        PropertyStatisticsResult result;
        result.RowCount = properties.Size();
        if (params.Bins > MaxPropertyHistogramBins)
        {
            result.Status = Status::InvalidParameters;
            result.Diagnostic = "Histogram bins must be between 0 and 256.";
            return result;
        }
        const auto descriptor = FindDescriptor(properties, name);
        if (!descriptor)
        {
            result.Status = Status::PropertyNotFound;
            result.Diagnostic = "Property was not found.";
            return result;
        }
        result.Kind = descriptor->ValueKind;
        const auto data = GetData(properties, *descriptor);
        if (!data)
        {
            result.Status = Status::UnsupportedKind;
            result.Diagnostic = "Property kind does not support numeric statistics.";
            return result;
        }
        if (descriptor->ElementCount != properties.Size())
        {
            result.Status = Status::RowCountMismatch;
            result.Diagnostic = "Property row count differs from its domain.";
            return result;
        }
        const auto masks = params.HonorDeleted ? ResolvePropertyDeletionMasks(properties) : PropertyDeletionMasks{};
        if (masks.Status != Status::Ok)
        {
            result.Status = masks.Status;
            result.Diagnostic = masks.Diagnostic;
            return result;
        }
        const std::size_t width = Width(result.Kind);
        std::array<Accumulator, 4> components;
        Accumulator magnitude;
        for (std::size_t row = 0; row < result.RowCount; ++row)
        {
            if (masks.IsDeleted(row))
            {
                ++result.DeletedCount;
                continue;
            }
            ++result.Count;
            const auto values = ReadRow(*data, row);
            bool hasNaN = false;
            bool hasInf = false;
            bool allZero = true;
            for (std::size_t component = 0; component < width; ++component)
            {
                const long double value = values[component].Wide();
                components[component].Add(value);
                hasNaN |= std::isnan(value);
                hasInf |= std::isinf(value);
                allZero &= value == 0;
            }
            if (hasNaN) ++result.NaNCount;
            else if (hasInf) ++result.InfCount;
            else ++result.FiniteCount;
            if (allZero) ++result.ZeroCount;
            if (width > 1)
                magnitude.Add(RowMagnitude(values, width));
        }
        bool representable = true;
        for (std::size_t component = 0; component < width; ++component)
            representable &= components[component].Finish(params.Bins);
        if (width > 1)
            representable &= magnitude.Finish(params.Bins);
        if (params.Bins != 0 && representable)
        {
            for (std::size_t row = 0; row < result.RowCount; ++row)
            {
                if (masks.IsDeleted(row))
                    continue;
                const auto values = ReadRow(*data, row);
                for (std::size_t component = 0; component < width; ++component)
                    components[component].AddHistogram(values[component].Wide());
                if (width > 1)
                    magnitude.AddHistogram(RowMagnitude(values, width));
            }
        }
        for (std::size_t component = 0; component < width; ++component)
            result.Components.push_back(std::move(components[component].Result));
        if (width > 1)
            result.Magnitude = std::move(magnitude.Result);
        if (!representable)
        {
            result.Status = Status::NumericOverflow;
            result.Diagnostic = "Statistics exceed the available numeric range.";
        }
        else if (result.Count == 0)
        {
            result.Status = Status::Empty;
            result.Diagnostic = "Property has no included rows.";
        }
        else if (result.FiniteCount == 0)
        {
            result.Status = Status::NoFiniteValues;
            result.Diagnostic = "Property has no fully finite rows; individual components may still contain finite values.";
        }
        return result;
    }

    PropertyComparisonResult ComparePropertyValues(
        const ConstPropertySet& a, std::string_view aName,
        const ConstPropertySet& b, std::string_view bName, bool honorDeleted)
    {
        PropertyComparisonResult result;
        result.RowCount = a.Size();
        const auto descriptorA = FindDescriptor(a, aName);
        const auto descriptorB = FindDescriptor(b, bName);
        if (!descriptorA || !descriptorB)
        {
            result.Status = Status::PropertyNotFound;
            result.Diagnostic = "One or both comparison properties were not found.";
            return result;
        }
        result.KindA = descriptorA->ValueKind;
        result.KindB = descriptorB->ValueKind;
        const auto dataA = GetData(a, *descriptorA);
        const auto dataB = GetData(b, *descriptorB);
        if (!dataA || !dataB)
        {
            result.Status = Status::UnsupportedKind;
            result.Diagnostic = "One or both property kinds do not support numeric comparison.";
            return result;
        }
        if (a.Size() != b.Size() || descriptorA->ElementCount != a.Size() || descriptorB->ElementCount != b.Size())
        {
            result.Status = Status::RowCountMismatch;
            result.Diagnostic = "Comparison properties must match their domains and have equal row counts.";
            return result;
        }
        const auto width = Width(result.KindA);
        if (width != Width(result.KindB))
        {
            result.Status = Status::KindMismatch;
            result.Diagnostic = "Comparison requires two scalars or vectors with equal dimensions.";
            return result;
        }
        const auto masksA = honorDeleted ? ResolvePropertyDeletionMasks(a) : PropertyDeletionMasks{};
        const auto masksB = honorDeleted ? ResolvePropertyDeletionMasks(b) : PropertyDeletionMasks{};
        if (masksA.Status != Status::Ok || masksB.Status != Status::Ok)
        {
            result.Status = Status::InvalidDeletionMask;
            result.Diagnostic = masksA.Status != Status::Ok ? masksA.Diagnostic : masksB.Diagnostic;
            return result;
        }
        Accumulator errors;
        long double maxError = -1;
        bool overflow = false;
        for (std::size_t row = 0; row < result.RowCount; ++row)
        {
            if (masksA.IsDeleted(row) || masksB.IsDeleted(row))
            {
                ++result.DeletedCount;
                continue;
            }
            const auto valuesA = ReadRow(*dataA, row);
            const auto valuesB = ReadRow(*dataB, row);
            bool finite = true;
            for (std::size_t component = 0; component < width; ++component)
                finite &= std::isfinite(valuesA[component].Wide()) && std::isfinite(valuesB[component].Wide());
            if (!finite)
            {
                ++result.NonFiniteRows;
                continue;
            }
            long double error = 0;
            for (std::size_t component = 0; component < width; ++component)
                error = std::hypot(error, AbsoluteDifference(valuesA[component], valuesB[component]));
            ++result.ComparableRows;
            if (error == 0) ++result.IdenticalRows;
            if (error > maxError)
            {
                maxError = error;
                result.MaxErrorRow = row;
            }
            overflow |= !std::isfinite(error) || error > std::numeric_limits<double>::max();
            errors.Add(error);
        }
        if (overflow || !errors.Finish(0))
        {
            result.Status = Status::NumericOverflow;
            result.Diagnostic = "Comparison error exceeds the available numeric range.";
        }
        else if (result.ComparableRows == 0)
        {
            result.Status = result.NonFiniteRows == 0 ? Status::Empty : Status::NoFiniteValues;
            result.Diagnostic = "Comparison has no included finite rows.";
        }
        else
        {
            result.MaxAbsError = errors.Result.Max;
            result.MeanAbsError = errors.Result.Mean;
            result.RMSError = errors.Result.RMS;
        }
        return result;
    }
}
