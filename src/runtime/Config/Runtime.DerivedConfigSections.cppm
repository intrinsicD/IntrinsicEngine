// A config section's codec derived from its bound rows (RUNTIME-346, ADR 0031 Decision 2), an exported
// module since RUNTIME-351: an operation's interface exports its section declaration and callers use
// the generic functions below (serialize, validate, get, set, registration). Rows are UInt and Float
// rows bound to number members, StringEnum rows bound to an enum (enumerator i is token
// EnumValues[i]) and PropertyRef rows bound to a GeometryPropertyRef. The payload is checked before
// conversion, in the stage order of the hand-written point codecs. A Float row refuses a positive
// value that stores as zero, unless its Positivity rule says when (outlier analysis' mode-dependent
// radius, RUNTIME-354); the cross-field Check runs after the references. A section whose rules
// differ otherwise (density weights' order) needs that stated before it moves here. The config helper headers are in the purview with their own linkage:
// reachable where a generic function is instantiated, not exported.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
export module Extrinsic.Runtime.DerivedConfigSections;
export import Extrinsic.Runtime.ConfigFieldSpec;
export import Extrinsic.Core.Config.Engine;
export import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.GeometryProperty.Types;
#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"

export extern "C++"
{
    namespace Extrinsic::Runtime
    {
        template <class Cfg> struct DerivedConfigSection
        {
            using Config = Cfg;
            std::string_view Name{}, SchemaId{}, Title{};
            std::uint32_t Version{1};
            std::span<const BoundField<Cfg>> Rows{};
            std::span<const ConfigFieldSpec> Fields{}; // SpecsOf(Rows), the section's exported table
            std::string_view ObjectError{}, UnknownFieldPrefix{};
        // The cross-field rule, checked on the converted record; empty when it holds.
            std::string_view (*Check)(const Cfg&){nullptr};
        };

    // Every row has the accessor its type needs and matches its spec in Fields, so a codec never
    // calls a missing accessor or looks up a field another table declares.
        template <class Cfg> [[nodiscard]] constexpr bool IsDerivable(const DerivedConfigSection<Cfg>& section)
        {
            if (section.Rows.size() != section.Fields.size())
                return false;
            for (std::size_t i = 0; i < section.Rows.size(); ++i)
            {
                const BoundField<Cfg>& row = section.Rows[i];
                const ConfigFieldType type = row.Spec.Type;
                if (row.Spec.Name != section.Fields[i].Name || type != section.Fields[i].Type)
                    return false;
                const bool number = type == ConfigFieldType::UInt || type == ConfigFieldType::Float ||
                    type == ConfigFieldType::StringEnum;
                if (number ? row.Get == nullptr || row.Set == nullptr
                           : type != ConfigFieldType::PropertyRef || row.Ref == nullptr)
                    return false;
            }
            return true;
        }
    }
}

extern "C++"
{
    namespace Extrinsic::Runtime::ConfigDetail
    {
        template <class Cfg>
        [[nodiscard]] std::string SerializeDerivedConfig(const DerivedConfigSection<Cfg>& section, const Cfg& config)
        {
            nlohmann::json out = nlohmann::json::object();
            for (const BoundField<Cfg>& row : section.Rows)
            {
                const std::string name(row.Spec.Name);
                if (row.Ref != nullptr)
                    out[name] = EncodePointPropertyRef(config.*row.Ref);
                else if (row.Spec.Type == ConfigFieldType::StringEnum)
                {
                    const double index = row.Get(config);
                    out[name] = index < static_cast<double>(row.Spec.EnumValues.size())
                        ? std::string(row.Spec.EnumValues[static_cast<std::size_t>(index)])
                        : std::string("invalid");
                }
                else if (row.Spec.Type == ConfigFieldType::UInt)
                    out[name] = static_cast<std::uint64_t>(row.Get(config));
                else
                    out[name] = row.Get(config);
            }
            return SerializeConfigJson(out);
        }

        // Reads a validated payload; without `references`, its numbers and tokens only. A number goes
        // straight into a float member, as the hand-written codecs converted it.
        template <class Cfg>
        [[nodiscard]] Cfg ParseDerivedConfig(
            const DerivedConfigSection<Cfg>& section, const nlohmann::json& data, const bool references = true)
        {
            Cfg config{};
            for (const BoundField<Cfg>& row : section.Rows)
            {
                const nlohmann::json& value = data.at(std::string(row.Spec.Name));
                if (row.Ref != nullptr)
                {
                    if (references)
                        DecodePointPropertyRef(value, config.*row.Ref);
                }
                else if (row.Spec.Type == ConfigFieldType::StringEnum)
                {
                    for (std::size_t i = 0; i < row.Spec.EnumValues.size(); ++i)
                        if (value.get_ref<const std::string&>() == row.Spec.EnumValues[i])
                            row.Set(config, static_cast<double>(i));
                }
                else
                    row.Set(config, row.Narrow ? double(value.get<float>()) : value.get<double>());
            }
            return config;
        }

        template <class Cfg>
        [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateDerivedConfig(
            const DerivedConfigSection<Cfg>& section, const std::string_view payload, const std::string_view subject)
        {
            const auto refuse = [&](const BoundField<Cfg>& row, std::string generic)
            {
                return RejectConfigSection(
                    subject, row.Spec.Refusal.empty() ? std::move(generic) : std::string(row.Spec.Refusal));
            };
            const nlohmann::json input = ParseConfigJson(payload, false);
            nlohmann::json data = ParseConfigJson(SerializeDerivedConfig(section, Cfg{}), true);
            if (auto error = ValidatePointConfigFields(
                    input, data, section.ObjectError, section.UnknownFieldPrefix, section.Fields))
                return RejectConfigSection(subject, std::move(*error));
            for (const BoundField<Cfg>& row : section.Rows)
                if (row.Spec.Type == ConfigFieldType::UInt &&
                    !AcceptsConfigFieldNumber(row.Spec, data[std::string(row.Spec.Name)].template get<double>()))
                    return refuse(
                        row, std::string(row.Spec.Name) + " must be " + DescribeConfigFieldRange(row.Spec) + ".");
            for (const BoundField<Cfg>& row : section.Rows)
                if (row.Spec.Type == ConfigFieldType::StringEnum &&
                    !IsConfigFieldToken(section.Fields, row.Spec.Name, data[std::string(row.Spec.Name)]))
                    return refuse(row,
                        "Unknown " + std::string(row.Spec.Name) + " value; " + DescribeConfigFieldRange(row.Spec) +
                            ".");
            if (auto error = ValidatePointConfigNonnegativeFloats(data, section.Fields))
                return RejectConfigSection(subject, std::move(*error));
            const Cfg numbers = ParseDerivedConfig(section, data, false);
            for (const BoundField<Cfg>& row : section.Rows)
            {
                if (row.Spec.Type != ConfigFieldType::Float)
                    continue;
                const double value = data[std::string(row.Spec.Name)].template get<double>();
                if (row.Positivity != nullptr)
                {
                    if (!row.Positivity(numbers))
                        continue;
                    if (!(value > 0.0))
                        return RejectConfigSection(subject, std::string(row.PositiveRefusal));
                }
                Cfg stored{};
                row.Set(stored, value);
                if (value > 0.0 && row.Get(stored) == 0.0)
                    return refuse(row, std::string(row.Spec.Name) + " must remain positive in storage.");
            }
            if (auto error = ValidatePointConfigPropertyRefs(data, section.Fields))
                return RejectConfigSection(subject, std::move(*error));
            const Cfg config = ParseDerivedConfig(section, data);
            if (section.Check != nullptr)
                if (const std::string_view refusal = section.Check(config); !refusal.empty())
                    return RejectConfigSection(subject, std::string(refusal));
            return AcceptConfigSection(SerializeDerivedConfig(section, config), input.size());
        }

        template <class Cfg>
        [[nodiscard]] std::optional<Cfg> GetDerivedConfig(const DerivedConfigSection<Cfg>& section,
            const Core::Config::EngineConfig& config,
            const SectionValidatorFn validate)
        {
            const auto payload = FindValidatedCanonicalPayload(
                config, section.Name, section.SchemaId, section.Version, nullptr, validate);
            if (!payload)
                return std::nullopt;
            return ParseDerivedConfig(section, ParseConfigJson(*payload, true));
        }

        // Stores the record unvalidated, as the hand-written setters did.
        template <class Cfg>
        void SetDerivedConfig(
            const DerivedConfigSection<Cfg>& section, Core::Config::EngineConfig& config, const Cfg& value)
        {
            Core::Config::UpsertEngineConfigSection(config.AppSections,
                MakeConfigSection(
                    section.Name, section.SchemaId, section.Version, SerializeDerivedConfig(section, value)));
        }

        template <class Cfg>
        [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeDerivedRegistration(
            const DerivedConfigSection<Cfg>& section, const SectionValidatorFn validate)
        {
            const std::string defaults = SerializeDerivedConfig(section, Cfg{});
            return MakeSectionRegistration(section.Name,
                section.SchemaId,
                section.Version,
                defaults,
                validate,
                BuildSectionSchemaJson(section.SchemaId,
                    section.Title,
                    "Validated " + std::string(section.Title) + " configuration.",
                    section.Fields,
                    ParseConfigJson(defaults, true)));
        }
    }
}

export extern "C++"
{
    namespace Extrinsic::Runtime
    {
    // The generic codec of one section declaration (an `inline constexpr DerivedConfigSection` with
    // static storage). ValidateDerivedSection<Section> has the exact section validator type, so the
    // registration stores it and Get validates with it.
        template <const auto& Section>
        using DerivedSectionConfig = typename std::remove_cvref_t<decltype(Section)>::Config;

        template <const auto& Section>
        [[nodiscard]] std::string SerializeDerivedSection(const DerivedSectionConfig<Section>& config)
        {
            return ConfigDetail::SerializeDerivedConfig(Section, config);
        }
        template <const auto& Section>
        [[nodiscard]] Core::Config::EngineConfigSectionValidationResult ValidateDerivedSection(
            const std::string_view payload, const std::string_view, const std::string_view subject)
        {
            return ConfigDetail::ValidateDerivedConfig(Section, payload, subject);
        }
        template <const auto& Section>
        [[nodiscard]] std::optional<DerivedSectionConfig<Section>> GetDerivedSection(
            const Core::Config::EngineConfig& config)
        {
            return ConfigDetail::GetDerivedConfig(Section, config, &ValidateDerivedSection<Section>);
        }
        template <const auto& Section>
        void SetDerivedSection(Core::Config::EngineConfig& config, const DerivedSectionConfig<Section>& value)
        {
            ConfigDetail::SetDerivedConfig(Section, config, value);
        }
        template <const auto& Section>
        [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeDerivedSectionRegistration()
        {
            return ConfigDetail::MakeDerivedRegistration(Section, &ValidateDerivedSection<Section>);
        }
    }
}
