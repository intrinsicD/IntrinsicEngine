module;

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

module Extrinsic.Runtime.ConfigFieldSpec;

namespace Extrinsic::Runtime
{
    const ConfigFieldSpec* FindConfigFieldSpec(const std::span<const ConfigFieldSpec> fields,
                                               const std::string_view name) noexcept
    {
        for (const auto& field : fields)
            if (field.Name == name) return &field;
        return nullptr;
    }

    std::string DescribeConfigFieldRange(const ConfigFieldSpec& field)
    {
        if (field.Type == ConfigFieldType::Enum)
        {
            std::string text = "one of: ";
            for (std::size_t i = 0; i < field.EnumNames.size(); ++i)
                text += (i ? ", " : "") + std::string(field.EnumNames[i]);
            return text;
        }
        if (field.Type == ConfigFieldType::String)
            return field.NonEmpty ? "a nonempty name" : std::string{};
        if (!field.Min && !field.Max) return {};
        const auto number = [](double value) { return std::format("{}", value); };
        if (field.Min && field.Max && !field.ExclusiveMin && !field.ExclusiveMax)
            return number(*field.Min) + " to " + number(*field.Max);
        std::string text;
        if (field.Min) text = (field.ExclusiveMin ? "greater than " : "at least ") + number(*field.Min);
        if (field.Max)
            text += (text.empty() ? "" : ", ") + std::string(field.ExclusiveMax ? "less than " : "at most ") +
                    number(*field.Max);
        return text;
    }

    double ClampToConfigFieldRange(const ConfigFieldSpec& field, double value) noexcept
    {
        if (field.Min) value = std::max(value, *field.Min);
        if (field.Max) value = std::min(value, *field.Max);
        return value;
    }
}
