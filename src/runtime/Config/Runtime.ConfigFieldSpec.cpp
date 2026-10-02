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

    bool AcceptsConfigFieldNumber(const ConfigFieldSpec& field, const double value) noexcept
    {
        if (!(value - value == 0.0)) return false; // NaN or infinity
        if (field.Min && (field.ExclusiveMin ? value <= *field.Min : value < *field.Min)) return false;
        if (field.Max && (field.ExclusiveMax ? value >= *field.Max : value > *field.Max)) return false;
        return true;
    }

    double ClampToConfigFieldRange(const ConfigFieldSpec& field, double value) noexcept
    {
        // An exclusive integer bound clamps one step inside it. An exclusive Float bound is left as
        // the bound itself: no representable neighbor is a meaningful value (a denormal sigma would
        // turn into NaN weights), so validation refuses it with its own diagnostic.
        const bool real = field.Type == ConfigFieldType::Float;
        if (field.Min)
            value = std::max(value, field.ExclusiveMin && !real ? *field.Min + 1.0 : *field.Min);
        if (field.Max)
            value = std::min(value, field.ExclusiveMax && !real ? *field.Max - 1.0 : *field.Max);
        return value;
    }
}
