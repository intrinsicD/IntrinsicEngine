// Copied, read-only property inspection results shared by editor panels and agents.
module;
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>
export module Extrinsic.Runtime.PropertyInspectionOperations;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.GeometryAvailability;
import Geometry.Properties.Statistics;

export namespace Extrinsic::Runtime
{
    inline constexpr std::size_t MaxEditorPropertyValues = 65536;
    inline constexpr std::size_t MaxEditorPropertyHistogramBins = Geometry::MaxPropertyHistogramBins;
    using EditorPropertyNumericStatistics = Geometry::PropertyNumericStatistics;
    using EditorPropertyStatisticsStatus = Geometry::PropertyStatisticsStatus;

    struct EditorPropertyCatalogResult
    {
        bool Success{};
        GeometryPropertyCatalogSnapshot Catalog{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };
    struct EditorPropertyStatisticsResult
    {
        bool Success{};
        GeometryPropertyRef Property{};
        Geometry::PropertyStatisticsResult Statistics{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };
    struct EditorPropertyComparisonResult
    {
        bool Success{};
        GeometryPropertyRef A{}, B{};
        Geometry::PropertyComparisonResult Comparison{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };
    // UInt64 values are decimal strings; floating special values are "NaN",
    // "+Infinity", or "-Infinity", so transport never silently rounds or nulls them.
    using EditorPropertyValue = std::variant<bool, std::int64_t, double, std::string>;
    struct EditorPropertyValueRow
    {
        std::size_t Index{};
        bool Deleted{};
        std::vector<EditorPropertyValue> Components{};
    };
    struct EditorPropertyValuesResult
    {
        bool Success{};
        GeometryPropertyRef Property{};
        std::size_t TotalCount{}, Offset{};
        bool HasMore{};
        std::vector<EditorPropertyValueRow> Rows{};
        std::vector<EditorDiagnostic> Diagnostics{};
    };

    [[nodiscard]] EditorPropertyCatalogResult GetEditorPropertyCatalog(
        const EditorProcessingCommands&, std::uint32_t stableId);
    [[nodiscard]] EditorPropertyStatisticsResult GetEditorPropertyStatistics(
        const EditorProcessingCommands&, std::uint32_t stableId,
        const GeometryPropertyRef&, std::size_t bins = 32);
    // Comparison requires the same element domain: equal cardinality alone does
    // not establish correspondence between, for example, faces and vertices.
    [[nodiscard]] EditorPropertyComparisonResult CompareEditorProperties(
        const EditorProcessingCommands&, std::uint32_t stableId,
        const GeometryPropertyRef& a, const GeometryPropertyRef& b);
    // Pages use storage indices, include flagged deleted slots, and never mutate
    // properties/history. Offsets at or past the end return a successful empty page.
    [[nodiscard]] EditorPropertyValuesResult ReadEditorPropertyValues(
        const EditorProcessingCommands&, std::uint32_t stableId,
        const GeometryPropertyRef&, std::size_t offset = 0, std::size_t limit = 256);
}
