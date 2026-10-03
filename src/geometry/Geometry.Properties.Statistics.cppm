// Read-only statistics and row comparisons for typed properties on any geometry domain.
module;

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

export module Geometry.Properties.Statistics;
export import Geometry.Properties;

export namespace Geometry
{
    enum class PropertyStatisticsStatus : std::uint8_t
    {
        Ok,
        Empty,
        NoFiniteValues,
        PropertyNotFound,
        UnsupportedKind,
        InvalidParameters,
        InvalidDeletionMask,
        RowCountMismatch,
        KindMismatch,
        NumericOverflow
    };

    inline constexpr std::size_t MaxPropertyHistogramBins = 256;

    struct StatisticsParams
    {
        std::size_t Bins{32};
        bool HonorDeleted{true};
    };

    struct PropertyHistogram
    {
        // Bins are [Edges[i], Edges[i+1]), with the maximum included in the last.
        // A constant range has repeated edges and all samples in the first bin.
        std::vector<double> Edges;
        std::vector<std::size_t> Counts;
    };

    struct PropertyNumericStatistics
    {
        std::size_t Count{0};
        std::size_t FiniteCount{0};
        std::size_t NaNCount{0};
        std::size_t InfCount{0};
        std::size_t ZeroCount{0};
        // Finite-only summaries, rounded to double; zero when FiniteCount is zero.
        // StdDev uses the population denominator. Histograms can be disabled with Bins=0.
        double Min{0};
        double Max{0};
        double Mean{0};
        double RMS{0};
        double StdDev{0};
        PropertyHistogram Histogram;
    };

    struct PropertyStatisticsResult
    {
        PropertyStatisticsStatus Status{PropertyStatisticsStatus::Ok};
        std::string Diagnostic;
        PropertyValueKind Kind{PropertyValueKind::Unknown};
        std::size_t RowCount{0};
        std::size_t Count{0};
        std::size_t DeletedCount{0};
        // Whole-row classification: any NaN takes precedence over any infinity.
        std::size_t FiniteCount{0};
        std::size_t NaNCount{0};
        std::size_t InfCount{0};
        std::size_t ZeroCount{0};
        std::vector<PropertyNumericStatistics> Components;
        std::optional<PropertyNumericStatistics> Magnitude;
    };

    struct PropertyComparisonResult
    {
        PropertyStatisticsStatus Status{PropertyStatisticsStatus::Ok};
        std::string Diagnostic;
        PropertyValueKind KindA{PropertyValueKind::Unknown};
        PropertyValueKind KindB{PropertyValueKind::Unknown};
        std::size_t RowCount{0};
        std::size_t DeletedCount{0};
        std::size_t ComparableRows{0};
        std::size_t NonFiniteRows{0};
        std::size_t IdenticalRows{0};
        // Errors use absolute scalar difference or vector Euclidean distance per row.
        // They remain zero when NumericOverflow prevents a double representation.
        double MaxAbsError{0};
        double MeanAbsError{0};
        double RMSError{0};
        // Original storage index; ties select the first comparable row.
        std::optional<std::size_t> MaxErrorRow;
    };

    struct PropertyDeletionMasks
    {
        PropertyStatisticsStatus Status{PropertyStatisticsStatus::Ok};
        std::string Diagnostic;
        // Borrowed until the source property set is changed or destroyed.
        std::vector<ConstProperty<bool>> Masks;
        [[nodiscard]] bool IsDeleted(std::size_t row) const;
    };

    [[nodiscard]] PropertyDeletionMasks ResolvePropertyDeletionMasks(
        const ConstPropertySet& properties);

    // Bool *:deleted properties exclude rows by their union. Malformed masks fail
    // closed. Components remain useful even if every vector row is non-finite.
    [[nodiscard]] PropertyStatisticsResult ComputePropertyStatistics(
        const ConstPropertySet& properties,
        std::string_view name,
        const StatisticsParams& params = {});

    // Scalar kinds may mix; vectors require equal dimensions. Non-finite rows and
    // rows deleted in either set are excluded. Integer equality and subtraction
    // precede floating conversion; reported error aggregates are rounded to double.
    [[nodiscard]] PropertyComparisonResult ComparePropertyValues(
        const ConstPropertySet& a,
        std::string_view aName,
        const ConstPropertySet& b,
        std::string_view bName,
        bool honorDeleted = true);
}
