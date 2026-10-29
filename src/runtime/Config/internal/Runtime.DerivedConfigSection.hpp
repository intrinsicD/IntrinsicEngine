// A config section's codec derived from its bound rows (RUNTIME-346, ADR 0031 Decision 2): parse,
// serialize, validation, get, set and registration. Rows are UInt and Float rows bound to number
// members, StringEnum rows bound to an enum (enumerator i is token EnumValues[i]) and PropertyRef
// rows bound to a GeometryPropertyRef. The payload is checked before conversion, in the stage order
// of the hand-written point codecs. Every Float row refuses a positive value that stores as zero,
// and the cross-field Check runs after the references: a section whose rules differ (outlier
// analysis' mode-dependent radius, density weights' order) needs that stated before it moves here.
// Include after Runtime.ConfigFieldJson.hpp, with the core config modules imported.
#pragma once

extern "C++"
{
    namespace Extrinsic::Runtime::ConfigDetail
    {
        template <class Cfg> struct DerivedConfigSection
        {
            std::string_view Name{}, SchemaId{}, Title{};
            std::uint32_t Version{1};
            std::span<const BoundField<Cfg>> Rows{};
            std::span<const ConfigFieldSpec> Fields{}; // SpecsOf(Rows), the section's exported table
            std::string_view ObjectError{}, UnknownFieldPrefix{};
            // The cross-field rule, checked on the converted record; empty when it holds.
            std::string_view (*Check)(const Cfg&){nullptr};
            // The exported validator: the registration stores it and Get validates with it.
            SectionValidatorFn Validate{nullptr};
        };

        // Every row has the accessor its type needs and matches its spec in Fields, so a codec never
        // calls a missing accessor or looks up a field another table declares.
        template <class Cfg> [[nodiscard]] constexpr bool IsDerivable(const DerivedConfigSection<Cfg>& section)
        {
            if (section.Rows.size() != section.Fields.size() || section.Validate == nullptr)
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

        // Reads a validated payload. A Float row reads the number as double first, so an integer
        // literal of more than 53 significant bits rounds twice on its way to a float member.
        template <class Cfg>
        [[nodiscard]] Cfg ParseDerivedConfig(const DerivedConfigSection<Cfg>& section, const nlohmann::json& data)
        {
            Cfg config{};
            for (const BoundField<Cfg>& row : section.Rows)
            {
                const nlohmann::json& value = data.at(std::string(row.Spec.Name));
                if (row.Ref != nullptr)
                    DecodePointPropertyRef(value, config.*row.Ref);
                else if (row.Spec.Type == ConfigFieldType::StringEnum)
                {
                    for (std::size_t i = 0; i < row.Spec.EnumValues.size(); ++i)
                        if (value.get_ref<const std::string&>() == row.Spec.EnumValues[i])
                            row.Set(config, static_cast<double>(i));
                }
                else
                    row.Set(config, value.get<double>());
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
            for (const BoundField<Cfg>& row : section.Rows)
            {
                if (row.Spec.Type != ConfigFieldType::Float)
                    continue;
                const double value = data[std::string(row.Spec.Name)].template get<double>();
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
        [[nodiscard]] std::optional<Cfg> GetDerivedConfig(
            const DerivedConfigSection<Cfg>& section, const Core::Config::EngineConfig& config)
        {
            const auto payload = FindValidatedCanonicalPayload(
                config, section.Name, section.SchemaId, section.Version, nullptr, section.Validate);
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
            const DerivedConfigSection<Cfg>& section)
        {
            const std::string defaults = SerializeDerivedConfig(section, Cfg{});
            return MakeSectionRegistration(section.Name,
                section.SchemaId,
                section.Version,
                defaults,
                section.Validate,
                BuildSectionSchemaJson(section.SchemaId,
                    section.Title,
                    "Validated " + std::string(section.Title) + " configuration.",
                    section.Fields,
                    ParseConfigJson(defaults, true)));
        }
    }
}
