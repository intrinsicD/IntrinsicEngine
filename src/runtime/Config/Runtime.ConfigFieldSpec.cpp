module;

#include <algorithm>
#include <cmath>
#include <limits>
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
        // An exclusive bound clamps to the nearest accepted value inside it: one step for integer
        // fields, the next representable double for Float ones.
        const bool real = field.Type == ConfigFieldType::Float;
        if (field.Min)
        {
            const double inside = !field.ExclusiveMin ? *field.Min
                : real ? std::nextafter(*field.Min, std::numeric_limits<double>::infinity()) : *field.Min + 1.0;
            value = std::max(value, inside);
        }
        if (field.Max)
        {
            const double inside = !field.ExclusiveMax ? *field.Max
                : real ? std::nextafter(*field.Max, -std::numeric_limits<double>::infinity()) : *field.Max - 1.0;
            value = std::min(value, inside);
        }
        return value;
    }
}
