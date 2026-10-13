module;
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>
module Extrinsic.Asset.ImportValidation;
namespace Extrinsic::Assets
{
    void ImportValidationReport::Record(ImportCheck check, ImportCheckStatus status,
                                       std::string message, std::size_t primitive,
                                       std::size_t element, std::size_t count)
    {
        auto& result = Checks[static_cast<std::size_t>(check)];
        result.Status = std::max(result.Status, status);
        if (status == ImportCheckStatus::Warning) result.WarningCount += count;
        if (status == ImportCheckStatus::Failed) result.ErrorCount += count;
        if (status == ImportCheckStatus::Unsupported) result.UnsupportedCount += count;
        const auto examples = std::ranges::count_if(Diagnostics, [check, status](const auto& diagnostic) {
            return diagnostic.Check == check && diagnostic.Status == status;
        });
        if (!message.empty() && examples < 8)
            Diagnostics.push_back({check, status, std::move(message), primitive, element});
    }
    bool ImportValidationReport::HasErrors() const noexcept
    {
        return std::ranges::any_of(Checks, [](const auto& check) {
            return check.Status == ImportCheckStatus::Failed;
        });
    }
    std::string_view ToString(ImportCheck check) noexcept
    {
        constexpr std::array names{"format", "references", "numeric_data", "attributes",
            "face_geometry", "topology", "duplicate_vertices", "transforms", "resources", "materialization"};
        const auto i = static_cast<std::size_t>(check);
        return i < names.size() ? names[i] : "unknown";
    }
    std::string_view ToString(ImportCheckStatus status) noexcept
    {
        constexpr std::array names{"not_run", "not_applicable", "skipped", "passed",
            "warning", "unsupported", "failed"};
        const auto i = static_cast<std::size_t>(status);
        return i < names.size() ? names[i] : "unknown";
    }
}
