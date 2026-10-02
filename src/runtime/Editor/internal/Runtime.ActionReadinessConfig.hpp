// Readiness reasons from a typed config section (RUNTIME-277): every out-of-range declared field
// as its own InvalidConfig reason carrying the field's schema key, else the section validator's
// cross-field verdict. Include after Runtime.ConfigFieldJson.hpp in a unit that imports
// Extrinsic.Runtime.EditorProcessing and Extrinsic.Core.Config.Engine.
#pragma once

extern "C++"
{
namespace Extrinsic::Runtime
{
    // Appends nothing for a usable config. A cross-field rejection is reported only when every
    // field passes, because the validator stops at the first field error.
    inline void AppendConfigReadinessReasons(
        std::vector<ActionReadinessReason>& reasons, const std::string& payload,
        const std::span<const ConfigFieldSpec> fields,
        const Core::Config::EngineConfigSectionValidationResult& validation)
    {
        const auto before = reasons.size();
        for (auto& error : ConfigDetail::CollectDeclaredFieldErrors(nlohmann::json::parse(payload, nullptr, false), fields))
            reasons.push_back({.Code = ActionReadinessCode::InvalidConfig, .Field = std::move(error.Field),
                               .Message = std::move(error.Message)});
        if (reasons.size() == before && !validation.Usable())
            reasons.push_back({.Code = ActionReadinessCode::ConflictingOptions, .Field = {},
                               .Message = validation.Diagnostics.empty() ? std::string{"The settings are invalid."}
                                                                         : validation.Diagnostics.front().Message});
    }

    // True when a reason already names `field`; checks that need a valid field skip it then.
    [[nodiscard]] inline bool ReadinessNamesField(
        const std::span<const ActionReadinessReason> reasons, const std::string_view field) noexcept
    {
        for (const auto& reason : reasons)
            if (reason.Field == field) return true;
        return false;
    }
}
}
