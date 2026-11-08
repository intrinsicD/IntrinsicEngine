module;

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

module Extrinsic.Runtime.StepRunExecution;

import Extrinsic.Runtime.ConfigFieldSpec;

namespace Extrinsic::Runtime
{
    using Json = nlohmann::json;

    namespace
    {
        // The rows a method's arguments use: all five, or a one-shot's (cap 1) auto_accept and start.
        std::span<const ConfigFieldSpec> ExecutionArgumentRows(
            const std::array<ConfigFieldSpec, 5>& fields, const std::uint32_t maxIterationCap)
        {
            const std::span<const ConfigFieldSpec> rows{fields};
            return maxIterationCap == 1u ? rows.subspan(3u) : rows;
        }
    }

    std::string StepRunExecutionSchemaProperties(
        const std::uint32_t maxIterationCap, const std::uint32_t minIterationCap)
    {
        Json properties = Json::object();
        const auto fields = StepRunExecutionFields(maxIterationCap, minIterationCap);
        for (const ConfigFieldSpec& field : ExecutionArgumentRows(fields, maxIterationCap))
        {
            Json schema{{"description", field.Description}};
            if (field.Type == ConfigFieldType::UInt)
                schema.update({{"type", "integer"},
                    {"minimum", static_cast<std::uint64_t>(*field.Min)},
                    {"maximum", static_cast<std::uint64_t>(*field.Max)}});
            else
            {
                schema["type"] = "string";
                schema["enum"] = Json::array();
                for (const std::string_view token : field.EnumValues)
                    schema["enum"].push_back(token);
            }
            properties[std::string{field.Name}] = std::move(schema);
        }
        const std::string text = properties.dump();
        return text.substr(1, text.size() - 2); // without the braces
    }

    bool ReadStepRunExecutionArguments(const std::string_view argumentsJson,
        const std::uint32_t maxIterationCap,
        StepRunExecution& options,
        std::string& refusal,
        const std::uint32_t minIterationCap)
    {
        const Json args = Json::parse(argumentsJson, nullptr, false);
        if (!args.is_object())
        {
            refusal = "Expected an object.";
            return false;
        }
        StepRunExecution read = options;
        const auto fields = StepRunExecutionFields(maxIterationCap, minIterationCap);
        for (const ConfigFieldSpec& field : ExecutionArgumentRows(fields, maxIterationCap))
        {
            const auto found = args.find(std::string{field.Name});
            if (found == args.end())
                continue;
            const auto wrong = [&]
            {
                refusal = std::string{field.Name} + " must be " + DescribeConfigFieldRange(field) + ".";
                return false;
            };
            if (field.Type == ConfigFieldType::UInt)
            {
                if (!found->is_number_unsigned() || !AcceptsConfigFieldNumber(field, found->get<double>()))
                    return wrong();
                const auto value = found->get<std::uint32_t>();
                if (field.Name == "max_iterations")
                    read.IterationCap = value;
                else if (field.Name == "preview_every")
                    read.PreviewEvery = value;
                else
                    read.PreviewIntervalMs = value;
                continue;
            }
            if (!found->is_string() || !AcceptsConfigFieldToken(field, found->get<std::string>()))
                return wrong();
            const auto token = found->get<std::string>();
            if (field.Name == "auto_accept")
                read.AutoAccept = *ParseStepRunAutoAccept(token);
            else
                read.Start = *ParseStepRunStart(token);
        }
        options = read;
        return true;
    }

    bool NamesStepRunExecutionArguments(const std::string_view argumentsJson, const std::uint32_t maxIterationCap)
    {
        const Json args = Json::parse(argumentsJson, nullptr, false);
        if (!args.is_object())
            return false;
        const auto fields = StepRunExecutionFields(maxIterationCap);
        for (const ConfigFieldSpec& field : ExecutionArgumentRows(fields, maxIterationCap))
            if (args.contains(std::string{field.Name}))
                return true;
        return false;
    }
}
