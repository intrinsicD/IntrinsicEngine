// Shared import check selection and bounded reports crossing decode/publication stages.
module;
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
export module Extrinsic.Asset.ImportValidation;

export namespace Extrinsic::Assets
{
    enum class ImportCheck : std::uint8_t
    {
        Format, References, NumericData, Attributes, FaceGeometry, Topology,
        DuplicateVertices, Transforms, Resources, Materialization, Count,
    };
    enum class ImportCheckStatus : std::uint8_t
    {
        NotRun, NotApplicable, Skipped, Passed, Warning, Unsupported, Failed,
    };
    struct ImportValidationOptions
    {
        bool DuplicateVertices{false};
        bool FaceGeometry{true};
        bool Topology{true};
        bool Attributes{true};
        std::size_t MaxSourceBytes{std::numeric_limits<std::size_t>::max()};
        std::size_t MaxDecodedImageBytes{std::numeric_limits<std::size_t>::max()};
    };
    struct ImportCheckResult
    {
        ImportCheckStatus Status{ImportCheckStatus::NotRun};
        std::size_t WarningCount{0};
        std::size_t ErrorCount{0};
        std::size_t UnsupportedCount{0};
    };
    struct ImportValidationDiagnostic
    {
        ImportCheck Check{};
        ImportCheckStatus Status{};
        std::string Message{};
        std::size_t PrimitiveIndex{static_cast<std::size_t>(-1)};
        std::size_t ElementIndex{static_cast<std::size_t>(-1)};
    };
    struct ImportValidationReport
    {
        std::array<ImportCheckResult, static_cast<std::size_t>(ImportCheck::Count)> Checks{};
        std::vector<ImportValidationDiagnostic> Diagnostics{};
        std::size_t BoundaryEdgeCount{0};
        std::size_t DisconnectedFallbackCount{0};
        bool MaterializationAttempted{false};
        // Aggregate counts are complete; only diagnostic examples are capped.
        void Record(ImportCheck check, ImportCheckStatus status, std::string message = {},
                    std::size_t primitive = static_cast<std::size_t>(-1),
                    std::size_t element = static_cast<std::size_t>(-1), std::size_t count = 1);
        [[nodiscard]] bool HasErrors() const noexcept;
    };
    [[nodiscard]] std::string_view ToString(ImportCheck check) noexcept;
    [[nodiscard]] std::string_view ToString(ImportCheckStatus status) noexcept;
}
