// Shared feature config codecs and point-property JSON handling, compiled once
// in an ordinary translation unit without exposing JSON through module APIs.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <span>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <type_traits>
#include <vector>

#include <nlohmann/json.hpp>

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.CurvatureSegmentationConfig;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.PointCloudConsolidationConfig;
import Extrinsic.Runtime.ProgressivePoissonConfig;
import Extrinsic.Runtime.PointSamplingConfig;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.ConfigFieldSpec;
import Geometry.Properties.Types;

#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.PointSamplingConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"

namespace Extrinsic::Runtime::ConfigDetail
{
    nlohmann::json ParseConfigJson(const std::string_view payload, const bool allowExceptions)
    {
        // Keep the parser and its input adapter instantiated in one translation unit.
        return nlohmann::json::parse(payload, nullptr, allowExceptions);
    }

    std::string SerializeConfigJson(const nlohmann::json& value)
    {
        // Keep the default serializer instantiated in this translation unit.
        return value.dump();
    }

    Core::Config::EngineConfigSectionValidationResult RejectConfigSection(
        const std::string_view subject, std::string message)
    {
        Core::Config::EngineConfigSectionValidationResult result;
        result.Diagnostics.push_back({.Code = Core::Config::EngineConfigDiagnosticCode::InvalidValue,
            .Subject = std::string(subject), .Message = std::move(message)});
        return result;
    }

    Core::Config::EngineConfigSectionValidationResult AcceptConfigSection(
        std::string canonicalPayloadJson, const std::size_t parsedFieldCount)
    {
        Core::Config::EngineConfigSectionValidationResult result;
        result.State = Core::Config::EngineConfigState::Valid;
        result.CanonicalPayloadJson = std::move(canonicalPayloadJson);
        result.ParsedFieldCount = static_cast<std::uint32_t>(parsedFieldCount);
        return result;
    }

    Core::Config::EngineConfigSection MakeConfigSection(
        const std::string_view name,
        const std::string_view schemaId,
        const std::uint32_t schemaVersion,
        std::string payloadJson)
    {
        return Core::Config::EngineConfigSection{
            .Name = std::string{name},
            .SchemaId = std::string{schemaId},
            .SchemaVersion = schemaVersion,
            .PayloadJson = std::move(payloadJson),
        };
    }

    Core::Config::EngineConfigSectionRegistration
    MakeSectionRegistration(
        const std::string_view name,
        const std::string_view schemaId,
        const std::uint32_t schemaVersion,
        std::string defaultPayloadJson,
        const SectionValidatorFn validate,
        std::string schemaJson,
        Core::Config::EngineConfigSectionChangedCallback&& onChanged)
    {
        return Core::Config::EngineConfigSectionRegistration{
            .DefaultSection = MakeConfigSection(
                name,
                schemaId,
                schemaVersion,
                std::move(defaultPayloadJson)),
            .Validate = validate,
            .OnChanged = std::move(onChanged),
            .SchemaJson = std::move(schemaJson),
        };
    }

    [[nodiscard]] std::optional<std::string> FindValidatedCanonicalPayload(
        const Core::Config::EngineConfig& config,
        const std::string_view name,
        const std::string_view schemaId,
        const std::uint32_t schemaVersion,
        std::string (*serializeDefault)(),
        const SectionValidatorFn validate)
    {
        const Core::Config::EngineConfigSection* section =
            Core::Config::FindEngineConfigSection(config.AppSections, name);
        if (section == nullptr || section->SchemaId != schemaId ||
            section->SchemaVersion != schemaVersion)
        {
            return std::nullopt;
        }
        const std::string referencePayloadJson = serializeDefault ? serializeDefault() : std::string{};
        Core::Config::EngineConfigSectionValidationResult validated =
            validate(section->PayloadJson, referencePayloadJson, name);
        if (validated.State != Core::Config::EngineConfigState::Valid)
        {
            return std::nullopt;
        }
        return std::move(validated.CanonicalPayloadJson);
    }

    std::optional<std::string> ValidatePointConfigFields(
        const nlohmann::json& input, nlohmann::json& defaults,
        const std::string_view objectError, const std::string_view unknownFieldPrefix,
        const std::span<const ConfigFieldSpec> fields)
    {
        if (!input.is_object()) return std::string(objectError);
        for (auto it = input.begin(); it != input.end(); ++it)
        {
            if (!defaults.contains(it.key()) || !FindConfigFieldSpec(fields, it.key()))
                return std::string(unknownFieldPrefix) + it.key();
            defaults[it.key()] = it.value();
        }
        for (const auto& field : fields)
        {
            if (field.Type != ConfigFieldType::UInt && field.Type != ConfigFieldType::Enum) continue;
            const auto key = field.Name;
            const auto& value = defaults.at(std::string(key));
            if (!value.is_number_unsigned() ||
                value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
                return std::string(key) + " must be an unsigned 32-bit integer.";
        }
        return std::nullopt;
    }

    std::optional<std::string> ValidatePointConfigNonnegativeFloats(
        const nlohmann::json& values, const std::span<const ConfigFieldSpec> fields)
    {
        for (const auto& field : fields)
        {
            if (field.Type != ConfigFieldType::Float) continue;
            const auto key = field.Name;
            const auto& value = values.at(std::string(key));
            if (!value.is_number() || !AcceptsConfigFieldNumber(field, value.get<double>()))
                return std::string(key) + " must be a finite nonnegative float.";
        }
        return std::nullopt;
    }

    const char* PointPropertyKindToken(const Geometry::PropertyValueKind kind) noexcept
    {
        switch (kind)
        {
        case Geometry::PropertyValueKind::Vec2: return "vec2";
        case Geometry::PropertyValueKind::Vec3: return "vec3";
        case Geometry::PropertyValueKind::Vec4: return "vec4";
        case Geometry::PropertyValueKind::UInt32: return "uint32";
        case Geometry::PropertyValueKind::Float: return "float";
        case Geometry::PropertyValueKind::Double: return "double";
        case Geometry::PropertyValueKind::Bool: return "bool";
        case Geometry::PropertyValueKind::Int32: return "int32";
        case Geometry::PropertyValueKind::UInt64: return "uint64";
        default: return "invalid";
        }
    }
    nlohmann::json EncodePointPropertyRef(const GeometryPropertyRef& ref)
    {
        return {{"domain", ref.Domain >= GeometryElementDomain::Unknown &&
                               ref.Domain <= GeometryElementDomain::PointCloudPoint
                           ? ToString(ref.Domain) : "invalid"},
                {"name", ref.Name}, {"kind", PointPropertyKindToken(ref.ValueKind)}};
    }

    nlohmann::json EncodeVec3PointPropertyRef(const GeometryPropertyRef& ref)
    {
        return {{"domain", ref.Domain <= GeometryElementDomain::PointCloudPoint
                               ? ToString(ref.Domain) : "invalid"},
                {"name", ref.Name},
                {"kind", ref.ValueKind == Geometry::PropertyValueKind::Vec3 ? "vec3" : "invalid"}};
    }

    PointPropertyValidation ValidatePointPropertyRef(
        const nlohmann::json& ref, const Geometry::PropertyValueKind kind, const bool allowScalarConversion)
    {
        bool matchingKind = ref.is_object() && ref.contains("kind") && ref["kind"] == PointPropertyKindToken(kind);
        if (allowScalarConversion && GeometryPropertyComponentCount(kind) == 1 && ref.is_object() && ref.contains("kind"))
            for (unsigned i = unsigned(Geometry::PropertyValueKind::Bool); i <= unsigned(Geometry::PropertyValueKind::Double); ++i)
                matchingKind |= ref["kind"] == PointPropertyKindToken(Geometry::PropertyValueKind(i));
        if (!ref.is_object() || ref.size() != 3 || !ref.contains("domain") ||
            !ref.contains("name") || !ref.contains("kind") ||
            !matchingKind || !ref["name"].is_string() ||
            ref["name"].get<std::string>().empty())
            return PointPropertyValidation::InvalidReference;
        for (unsigned i = 0; i <= unsigned(GeometryElementDomain::PointCloudPoint); ++i)
            if (ref["domain"] == ToString(GeometryElementDomain(i)))
                return PointPropertyValidation::Valid;
        return PointPropertyValidation::UnknownDomain;
    }

    std::optional<std::string> ValidatePointConfigPropertyRefs(
        const nlohmann::json& values,
        const std::span<const ConfigFieldSpec> fields)
    {
        for (const auto& field : fields)
        {
            if (field.Type != ConfigFieldType::PropertyRef) continue;
            const auto key = field.Name;
            const auto validation = field.RefKinds.size() == 1u
                ? ValidatePointPropertyRef(values.at(std::string(key)), field.RefKinds.front(), field.AnyScalar)
                : PointPropertyValidation::InvalidReference;
            if (validation == PointPropertyValidation::InvalidReference)
                return std::string(key) + " needs a canonical typed property reference.";
            if (validation == PointPropertyValidation::UnknownDomain)
                return "Unknown element domain.";
        }
        return std::nullopt;
    }

    bool InConfigFieldRange(const std::span<const ConfigFieldSpec> fields,
                           const std::string_view name, const double value) noexcept
    {
        const auto* field = FindConfigFieldSpec(fields, name);
        return field && AcceptsConfigFieldNumber(*field, value);
    }

    bool IsConfigFieldToken(const std::span<const ConfigFieldSpec> fields,
                            const std::string_view name, const nlohmann::json& value)
    {
        const auto* field = FindConfigFieldSpec(fields, name);
        return field && value.is_string() && AcceptsConfigFieldToken(*field, value.get_ref<const std::string&>());
    }

    namespace
    {
        using Kind = Geometry::PropertyValueKind;
        constexpr Kind kFirstScalar = Kind::Bool, kLastScalar = Kind::Double;

        // Accepted kind tokens of a property-ref field, in enum order.
        std::vector<std::string> RefKindTokens(const ConfigFieldSpec& field)
        {
            std::vector<std::string> tokens;
            for (unsigned i = unsigned(Kind::Bool); i <= unsigned(Kind::Vec4); ++i)
            {
                const auto kind = Kind(i);
                bool accepted = std::ranges::find(field.RefKinds, kind) != field.RefKinds.end();
                if (field.AnyScalar && kind >= kFirstScalar && kind <= kLastScalar)
                    for (const auto listed : field.RefKinds)
                        accepted |= GeometryPropertyComponentCount(listed) == 1u;
                if (accepted) tokens.emplace_back(PointPropertyKindToken(kind));
            }
            return tokens;
        }
        std::vector<std::string> RefDomainTokens(const ConfigFieldSpec& field)
        {
            std::vector<std::string> tokens;
            if (field.RefDomains.empty())
                for (unsigned i = 0; i <= unsigned(GeometryElementDomain::PointCloudPoint); ++i)
                    tokens.emplace_back(ToString(GeometryElementDomain(i)));
            else
                for (const auto domain : field.RefDomains) tokens.emplace_back(ToString(domain));
            return tokens;
        }
        std::string JoinTokens(const std::vector<std::string>& tokens, std::string_view last)
        {
            std::string text;
            for (std::size_t i = 0; i < tokens.size(); ++i)
                text += (i == 0 ? "" : i + 1 == tokens.size() ? std::string(last) : std::string(", ")) + tokens[i];
            return text;
        }
        bool InRange(const ConfigFieldSpec& field, double value)
        {
            if (field.Min && (field.ExclusiveMin ? !(value > *field.Min) : !(value >= *field.Min))) return false;
            if (field.Max && (field.ExclusiveMax ? !(value < *field.Max) : !(value <= *field.Max))) return false;
            return true;
        }
        std::string RangeError(const ConfigFieldSpec& field)
        {
            return std::string(field.Name) + " must be " + DescribeConfigFieldRange(field) + ".";
        }
        std::optional<std::string> CheckField(const ConfigFieldSpec& field, const nlohmann::json& value)
        {
            const std::string name{field.Name};
            if (value.is_null() && field.Nullable) return std::nullopt;
            switch (field.Type)
            {
            case ConfigFieldType::Bool:
                if (!value.is_boolean()) return name + " must be boolean.";
                return std::nullopt;
            case ConfigFieldType::UInt:
                if (!value.is_number_unsigned() || value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
                    return name + " must be an unsigned 32-bit integer.";
                return InRange(field, double(value.get<std::uint64_t>())) ? std::nullopt : std::optional{RangeError(field)};
            case ConfigFieldType::Int:
                if (!value.is_number_integer() ||
                    (value.is_number_unsigned() && value.get<std::uint64_t>() > std::uint64_t(std::numeric_limits<std::int64_t>::max())))
                    return name + " must be an integer.";
                return InRange(field, double(value.get<std::int64_t>())) ? std::nullopt : std::optional{RangeError(field)};
            case ConfigFieldType::Float:
                if (!value.is_number() || !std::isfinite(value.get<double>())) return name + " must be a finite number.";
                return InRange(field, value.get<double>()) ? std::nullopt : std::optional{RangeError(field)};
            case ConfigFieldType::Enum:
            {
                const auto first = std::uint64_t(field.Min.value_or(0.0));
                if (!value.is_number_unsigned() || value.get<std::uint64_t>() < first ||
                    value.get<std::uint64_t>() >= first + field.EnumNames.size())
                    return "Unknown " + name + " value; " + DescribeConfigFieldRange(field) + " (numbered from " +
                           std::to_string(first) + ").";
                return std::nullopt;
            }
            case ConfigFieldType::String:
                if (value.is_null() && field.Nullable) return std::nullopt;
                if (!value.is_string() || (field.NonEmpty && value.get<std::string>().empty()))
                    return name + (field.Nullable ? " must be null or a" : " must be a") +
                           (field.NonEmpty ? " nonempty string." : " string.");
                return std::nullopt;
            case ConfigFieldType::StringEnum:
                if (!value.is_string() || !AcceptsConfigFieldToken(field, value.get_ref<const std::string&>()))
                    return "Unknown " + name + " value; " + DescribeConfigFieldRange(field) + ".";
                return std::nullopt;
            case ConfigFieldType::Array:
                if (!value.is_array() || (field.MinItems && value.size() < *field.MinItems) ||
                    (field.MaxItems && value.size() > *field.MaxItems))
                    return name + " has an invalid array length.";
                if (field.Item)
                    for (const auto& item : value)
                        if (auto error = CheckField(*field.Item, item)) return error;
                return std::nullopt;
            case ConfigFieldType::Object:
                // Nested fallback owners validate and merge their objects themselves.
                return value.is_object() ? std::nullopt : std::optional{name + " must be an object."};
            case ConfigFieldType::UIntArray:
                if (!value.is_array()) return name + " must be an array of unsigned 32-bit integers.";
                for (const auto& item : value)
                    if (!item.is_number_unsigned() || item.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
                        return name + " must be an array of unsigned 32-bit integers.";
                return std::nullopt;
            case ConfigFieldType::PropertyRef:
            {
                if (value.is_null() && field.Nullable) return std::nullopt;
                bool valid = false;
                for (const auto kind : field.RefKinds)
                    valid |= ValidatePointPropertyRef(value, kind, field.AnyScalar) == PointPropertyValidation::Valid;
                if (valid && !field.RefDomains.empty())
                {
                    bool domain = false;
                    for (const auto allowed : field.RefDomains) domain |= value["domain"] == ToString(allowed);
                    valid = domain;
                }
                if (!valid)
                    return name + (field.Nullable ? " must be null or a " : " needs a ") +
                           JoinTokens(RefKindTokens(field), " or ") + " property reference" +
                           (field.RefDomains.empty() ? "" : " on " + JoinTokens(RefDomainTokens(field), " or ")) + ".";
                return std::nullopt;
            }
            }
            return name + " has an undeclared type.";
        }
    }

    std::optional<std::string> ValidateDeclaredFields(
        const nlohmann::json& input, nlohmann::json& merged, const std::span<const ConfigFieldSpec> fields,
        const std::string_view objectError, const std::string_view unknownFieldPrefix)
    {
        if (!input.is_object()) return std::string(objectError);
        for (auto it = input.begin(); it != input.end(); ++it)
        {
            if (!merged.contains(it.key()) || FindConfigFieldSpec(fields, it.key()) == nullptr)
                return std::string(unknownFieldPrefix) + it.key();
            merged[it.key()] = it.value();
        }
        for (const auto& field : fields)
        {
            const auto it = merged.find(std::string(field.Name));
            if (it == merged.end()) return std::string(field.Name) + " is declared but has no default.";
            if (auto error = CheckField(field, *it)) return error;
        }
        return std::nullopt;
    }

    std::vector<ConfigFieldError> CollectDeclaredFieldErrors(
        const nlohmann::json& payload, const std::span<const ConfigFieldSpec> fields)
    {
        std::vector<ConfigFieldError> errors;
        if (!payload.is_object()) return errors;
        for (const auto& field : fields)
            if (const auto it = payload.find(std::string(field.Name)); it != payload.end())
                if (auto error = CheckField(field, *it)) errors.push_back({std::string(field.Name), std::move(*error)});
        return errors;
    }

    namespace
    {
        using OJson = nlohmann::ordered_json;
        OJson BuildFieldSchema(const ConfigFieldSpec& field, const nlohmann::json* defaultValue)
        {
            OJson p = OJson::object();
            const auto bound = [&](const char* inclusive, const char* exclusive, const std::optional<double>& v, bool excl) {
                if (!v) return;
                if (field.Type == ConfigFieldType::Float) p[excl ? exclusive : inclusive] = *v;
                else p[excl ? exclusive : inclusive] = std::int64_t(*v);
            };
            switch (field.Type)
            {
            case ConfigFieldType::Bool: p["type"] = "boolean"; break;
            case ConfigFieldType::UInt:
                p["type"] = "integer";
                p["minimum"] = std::int64_t(field.Min.value_or(0.0));
                p["maximum"] = std::int64_t(field.Max.value_or(double(std::numeric_limits<std::uint32_t>::max())));
                if (field.ExclusiveMin) { p.erase("minimum"); bound("minimum", "exclusiveMinimum", field.Min, true); }
                if (field.ExclusiveMax) { p.erase("maximum"); bound("maximum", "exclusiveMaximum", field.Max, true); }
                break;
            case ConfigFieldType::Int:
                p["type"] = "integer";
                bound("minimum", "exclusiveMinimum", field.Min, field.ExclusiveMin);
                bound("maximum", "exclusiveMaximum", field.Max, field.ExclusiveMax);
                break;
            case ConfigFieldType::Float:
                p["type"] = "number";
                bound("minimum", "exclusiveMinimum", field.Min, field.ExclusiveMin);
                bound("maximum", "exclusiveMaximum", field.Max, field.ExclusiveMax);
                break;
            case ConfigFieldType::Enum:
            {
                p["type"] = "integer";
                OJson values = OJson::array(), names = OJson::array();
                const auto first = std::int64_t(field.Min.value_or(0.0));
                for (std::size_t i = 0; i < field.EnumNames.size(); ++i)
                {
                    values.push_back(first + std::int64_t(i));
                    names.push_back(std::string(field.EnumNames[i]));
                }
                p["enum"] = std::move(values);
                p["x-enum-names"] = std::move(names);
                break;
            }
            case ConfigFieldType::String:
                p["type"] = field.Nullable ? OJson::array({"string", "null"}) : OJson("string");
                if (field.NonEmpty) p["minLength"] = 1;
                break;
            case ConfigFieldType::StringEnum:
                p["type"] = "string";
                p["enum"] = field.EnumValues;
                if (!field.EnumNames.empty()) p["x-enum-names"] = field.EnumNames;
                break;
            case ConfigFieldType::Array:
                p["type"] = "array";
                if (field.Item) p["items"] = BuildFieldSchema(*field.Item, nullptr);
                if (field.MinItems) p["minItems"] = *field.MinItems;
                if (field.MaxItems) p["maxItems"] = *field.MaxItems;
                break;
            case ConfigFieldType::Object:
            {
                p["type"] = "object";
                p["properties"] = OJson::object();
                OJson required = OJson::array();
                for (const auto& child : std::span{field.Fields, field.FieldCount})
                {
                    const nlohmann::json* childDefault = nullptr;
                    if (defaultValue && defaultValue->is_object())
                        if (const auto it = defaultValue->find(std::string(child.Name)); it != defaultValue->end()) childDefault = &*it;
                    p["properties"][std::string(child.Name)] = BuildFieldSchema(child, childDefault);
                    if (child.Required)
                        required.push_back(std::string(child.Name));
                }
                if (!required.empty())
                    p["required"] = std::move(required);
                p["additionalProperties"] = false;
                break;
            }
            case ConfigFieldType::UIntArray:
                p["type"] = "array";
                p["items"] = {{"type", "integer"}, {"minimum", 0}, {"maximum", std::numeric_limits<std::uint32_t>::max()}};
                break;
            case ConfigFieldType::PropertyRef:
            {
                OJson kinds = RefKindTokens(field);
                if (field.IntegerRefKinds)
                {
                    kinds = OJson::array();
                    for (unsigned code = unsigned(Kind::Unknown); code <= unsigned(Kind::Vec4); ++code)
                    {
                        const auto kind = Kind(code);
                        bool accepted = std::ranges::find(field.RefKinds, kind) != field.RefKinds.end();
                        if (field.AnyScalar && kind >= kFirstScalar && kind <= kLastScalar)
                            for (const auto listed : field.RefKinds)
                                accepted |= GeometryPropertyComponentCount(listed) == 1u;
                        if (accepted) kinds.push_back(code);
                    }
                }
                const auto domains = RefDomainTokens(field);
                p["type"] = field.Nullable ? OJson::array({"object", "null"}) : OJson("object");
                p["properties"] = {{"domain", {{"type", "string"}, {"enum", domains}}},
                                   {"name", {{"type", "string"}, {"minLength", 1}}},
                                   {"kind", {{"type", field.IntegerRefKinds ? "integer" : "string"}, {"enum", kinds}}}};
                p["required"] = {"domain", "name", "kind"};
                p["additionalProperties"] = false;
                p["x-property-kinds"] = kinds;
                p["x-domains"] = domains;
                break;
            }
            }
            if (field.Nullable && p["type"].is_string()) p["type"] = OJson::array({p["type"], "null"});
            if (!field.Description.empty()) p["description"] = std::string(field.Description);
            if (defaultValue) p["default"] = OJson::parse(defaultValue->dump());
            return p;
        }
    }

    std::string BuildSectionSchemaJson(
        const std::string_view schemaId, const std::string_view title, const std::string_view description,
        const std::span<const ConfigFieldSpec> fields, const nlohmann::json& defaults)
    {
        OJson properties = OJson::object();
        for (const auto& field : fields)
        {
            const auto it = defaults.find(std::string(field.Name));
            properties[std::string(field.Name)] = BuildFieldSchema(field, it == defaults.end() ? nullptr : &*it);
        }
        OJson schema{{"$schema", "https://json-schema.org/draft/2020-12/schema"}, {"$id", std::string(schemaId)},
                     {"title", std::string(title)}, {"description", std::string(description)}, {"type", "object"},
                     {"properties", std::move(properties)}, {"additionalProperties", false}};
        return schema.dump(-1, ' ', false, OJson::error_handler_t::replace);
    }

    void DecodePointPropertyRef(const nlohmann::json& value, GeometryPropertyRef& ref)
    {
        ref.Name = value.at("name");
        for (unsigned i = unsigned(Geometry::PropertyValueKind::Bool); i <= unsigned(Geometry::PropertyValueKind::Vec4); ++i)
            if (value.at("kind") == PointPropertyKindToken(Geometry::PropertyValueKind(i)))
                ref.ValueKind = Geometry::PropertyValueKind(i);
        for (unsigned i = 0; i <= unsigned(GeometryElementDomain::PointCloudPoint); ++i)
            if (value.at("domain") == ToString(GeometryElementDomain(i)))
                ref.Domain = GeometryElementDomain(i);
    }
}

namespace Extrinsic::Runtime
{
    namespace
    {
        using json = nlohmann::json;

        using ConfigDetail::SectionValidatorFn;
        using ConfigDetail::FindValidatedCanonicalPayload;

        using ConfigDetail::MakeConfigSection;
        using ConfigDetail::MakeSectionRegistration;


        using FT = ConfigFieldType;
        using Kind = Geometry::PropertyValueKind;
        using Domain = GeometryElementDomain;
        constexpr std::array kScalarKinds{Kind::Bool, Kind::Int32, Kind::UInt32, Kind::UInt64, Kind::Float, Kind::Double};
        constexpr std::array kFeatureKinds{Kind::Bool, Kind::Int32, Kind::UInt32, Kind::UInt64, Kind::Float, Kind::Double, Kind::Vec2, Kind::Vec3};
        constexpr std::array kVec2Kinds{Kind::Vec2}, kVec3Kinds{Kind::Vec3}, kVec4Kinds{Kind::Vec4};
        constexpr std::array kKnownDomains{Domain::MeshVertex, Domain::MeshEdge, Domain::MeshHalfedge, Domain::MeshFace,
                                           Domain::GraphNode, Domain::GraphHalfedge, Domain::GraphEdge, Domain::PointCloudPoint};
        constexpr std::array kAllDomains{Domain::Unknown, Domain::MeshVertex, Domain::MeshEdge, Domain::MeshHalfedge, Domain::MeshFace,
                                         Domain::GraphNode, Domain::GraphHalfedge, Domain::GraphEdge, Domain::PointCloudPoint};
        constexpr std::array kVertexDomain{Domain::MeshVertex}, kEdgeDomain{Domain::MeshEdge},
                             kFaceDomain{Domain::MeshFace}, kHalfedgeDomain{Domain::MeshHalfedge};
        constexpr std::array kMeshInputDomains{Domain::MeshVertex, Domain::MeshFace};
        constexpr std::array<std::string_view, 2> kInitialBackendTokens{"cpu", "vulkan"};
        constexpr std::array<std::string_view, 4> kConsolidationBackendLabels{
            "CPU reference neighborhoods", "Vulkan", "CPU LBVH (cached)", "Vulkan LBVH"};
        constexpr ConfigFieldSpec kUvCoordinate{.Type = FT::Float,
            .Min = -static_cast<double>(std::numeric_limits<float>::max()),
            .Max = static_cast<double>(std::numeric_limits<float>::max())};
        constexpr ConfigFieldSpec kUvPair{.Type = FT::Array, .Item = &kUvCoordinate, .MinItems = 2, .MaxItems = 2};
        constexpr ConfigFieldSpec kFiniteNumber{.Type = FT::Float};
        constexpr ConfigFieldSpec kSegmentationFeature{.Type = FT::PropertyRef,
            .RefKinds = kFeatureKinds, .RefDomains = kMeshInputDomains, .IntegerRefKinds = true};
        // These are registered-default binding domains. File-load validation can use a
        // non-default reference; ReadPropertyRef preserves that existing narrower contract.
        const std::array kClusteringPropertyFields{
            ConfigFieldSpec{.Name = "positions", .Type = FT::PropertyRef, .RefKinds = kVec3Kinds, .RefDomains = kKnownDomains, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "labels", .Type = FT::PropertyRef, .RefKinds = kScalarKinds, .RefDomains = kKnownDomains, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "colors", .Type = FT::PropertyRef, .RefKinds = kVec4Kinds, .RefDomains = kKnownDomains, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "scalar_labels", .Type = FT::PropertyRef, .Nullable = true, .RefKinds = kScalarKinds, .RefDomains = kKnownDomains, .IntegerRefKinds = true},
        };

        constexpr std::array<std::string_view, 4> kProgressivePoissonChannelTokens{"Level", "Rank", "SplatRadius", "PrefixVisible"};
        constexpr std::array<std::string_view, 2> kProgressivePoissonBackendTokens{"CpuReference", "VulkanCompute"};
        constexpr std::array<std::string_view, 2> kClusteringBackendTokens{"CpuReference", "VulkanCompute"};
        constexpr std::array<std::string_view, 2> kCurvatureSegmentationSelectionModeTokens{"fixed_count", "automatic"};
        constexpr std::array<std::string_view, 3> kCurvatureSegmentationMethodTokens{"curvature_gmm", "feature_aligned_patches", "feature_boundary_curves_v1"};
        const std::array kPointCloudConsolidationStrategyTokens{StableToken(PointCloudConsolidationStrategy::Lop), StableToken(PointCloudConsolidationStrategy::Wlop), StableToken(PointCloudConsolidationStrategy::Clop), StableToken(PointCloudConsolidationStrategy::Ear)};
        const std::array kPointCloudConsolidationBackendTokens{StableToken(PointCloudConsolidationBackend::CpuReference), StableToken(PointCloudConsolidationBackend::VulkanCompute), StableToken(PointCloudConsolidationBackend::CpuLBVH), StableToken(PointCloudConsolidationBackend::VulkanLBVH)};
        const std::array kPointCloudConsolidationNormalSourceTokens{StableToken(PointCloudConsolidationNormalSource::AuthoredOrEstimate), StableToken(PointCloudConsolidationNormalSource::RequireAuthored)};
        const std::array kPointCloudConsolidationSupportRadiusModeTokens{StableToken(PointCloudConsolidationSupportRadiusMode::Auto), StableToken(PointCloudConsolidationSupportRadiusMode::Manual)};
        constexpr std::array<std::string_view, 2> kKMeansInitializationTokens{"Random", "Hierarchical"};
        constexpr std::array<std::string_view, 2> kAtlasMethodTokens{"fast_staged", "xatlas"};
        constexpr std::array<std::string_view, 4> kAtlasDistortionTokens{"none", "angle", "area", "both"};
        constexpr std::array<std::string_view, 4> kParameterizationStrategyTokens{"lscm", "harmonic_cotangent", "tutte_uniform", "bff"};
        constexpr std::array<std::string_view, 3> kParameterizationBoundaryPolicyTokens{"circle", "square", "custom"};
        constexpr std::array<std::string_view, 3> kParameterizationBffBoundaryModeTokens{"automatic_conformal", "target_lengths", "target_angles"};
        constexpr std::array<std::string_view, 2> kParameterizationUvRenderModeTokens{"cpu_layout", "gpu_shaded"};
        constexpr std::array<std::string_view, 4> kParameterizationUvBackgroundModeTokens{"grid", "checker", "texel_density", "texture"};

        const std::array kParameterizationViewFields{
            ConfigFieldSpec{.Name = "render_mode", .Type = FT::StringEnum, .EnumValues = kParameterizationUvRenderModeTokens},
            ConfigFieldSpec{.Name = "background_mode", .Type = FT::StringEnum, .EnumValues = kParameterizationUvBackgroundModeTokens},
            ConfigFieldSpec{.Name = "show_distortion_heatmap", .Type = FT::Bool},
            ConfigFieldSpec{.Name = "split_enabled", .Type = FT::Bool},
            ConfigFieldSpec{.Name = "atlas_on_left", .Type = FT::Bool},
            ConfigFieldSpec{.Name = "split_ratio", .Type = FT::Float, .Min = 0.2, .Max = static_cast<double>(0.8f)},
        };
        const std::array kParameterizationAtlasFields{
            ConfigFieldSpec{.Name = "method", .Type = FT::StringEnum, .Description = "XAtlas requires the angle distortion objective; fast_staged supports every listed objective.", .EnumValues = kAtlasMethodTokens},
            ConfigFieldSpec{.Name = "distortion", .Type = FT::StringEnum, .EnumValues = kAtlasDistortionTokens},
            ConfigFieldSpec{.Name = "guide", .Type = FT::PropertyRef, .Description = "Optional scalar mesh vertex or face property; its nonempty name must not contain NUL.", .Nullable = true, .RefKinds = kScalarKinds, .RefDomains = kMeshInputDomains, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "region_count", .Type = FT::UInt, .Description = "Zero selects the number of guide components automatically.", .Min = 0, .Max = 64},
            ConfigFieldSpec{.Name = "resolution", .Type = FT::UInt, .Min = 16, .Max = 16384},
            ConfigFieldSpec{.Name = "padding", .Type = FT::UInt, .Description = "Packing requires 2 * padding + 1 < resolution so usable interior space remains.", .Min = 0, .Max = 32},
            ConfigFieldSpec{.Name = "texels_per_unit", .Type = FT::Float, .Min = 0.0, .Max = 1.0e12},
            ConfigFieldSpec{.Name = "max_conformal_distortion", .Type = FT::Float, .Min = 1.0, .Max = 1.0e6},
            ConfigFieldSpec{.Name = "max_area_distortion", .Type = FT::Float, .Min = 1.0, .Max = 1.0e6},
            ConfigFieldSpec{.Name = "max_charts", .Type = FT::UInt, .Min = 1, .Max = 1000000},
            ConfigFieldSpec{.Name = "max_iterations", .Type = FT::UInt, .Min = 1, .Max = 1000},
            ConfigFieldSpec{.Name = "allow_xatlas_fallback", .Type = FT::Bool},
        };
        const std::array kParameterizationLscmFields{
            ConfigFieldSpec{.Name = "auto_pins", .Type = FT::Bool, .Description = "When false, the two pin vertices must differ; invalid manual pins retain the entire reference LSCM group."},
            ConfigFieldSpec{.Name = "pin_vertex_0", .Type = FT::UInt, .Min = 0, .Max = std::numeric_limits<std::uint32_t>::max()},
            ConfigFieldSpec{.Name = "pin_vertex_1", .Type = FT::UInt, .Min = 0, .Max = std::numeric_limits<std::uint32_t>::max()},
            ConfigFieldSpec{.Name = "pin_uv_0", .Type = FT::Array, .Item = &kUvCoordinate, .MinItems = kUvPair.MinItems, .MaxItems = kUvPair.MaxItems},
            ConfigFieldSpec{.Name = "pin_uv_1", .Type = FT::Array, .Item = &kUvCoordinate, .MinItems = kUvPair.MinItems, .MaxItems = kUvPair.MaxItems},
            ConfigFieldSpec{.Name = "solver_tolerance", .Type = FT::Float, .Min = std::numeric_limits<double>::min(), .Max = 1.0e30},
            ConfigFieldSpec{.Name = "max_solver_iterations", .Type = FT::UInt, .Min = 1, .Max = std::numeric_limits<std::uint32_t>::max()},
        };
        const std::array kParameterizationHarmonicFields{
            ConfigFieldSpec{.Name = "boundary", .Type = FT::StringEnum, .EnumValues = kParameterizationBoundaryPolicyTokens},
            ConfigFieldSpec{.Name = "arc_length_spacing", .Type = FT::Bool},
            ConfigFieldSpec{.Name = "clamp_non_convex_weights", .Type = FT::Bool},
            ConfigFieldSpec{.Name = "pinned_vertices", .Type = FT::UIntArray, .Description = "Provide together with pinned_uvs and with equal length; otherwise both reference arrays are retained."},
            ConfigFieldSpec{.Name = "pinned_uvs", .Type = FT::Array, .Description = "Provide together with pinned_vertices and with equal length; otherwise both reference arrays are retained.", .Item = &kUvPair},
        };
        const std::array kParameterizationBffFields{
            ConfigFieldSpec{.Name = "mode", .Type = FT::StringEnum, .EnumValues = kParameterizationBffBoundaryModeTokens},
            ConfigFieldSpec{.Name = "boundary_data", .Type = FT::Array, .Description = "Automatic mode requires no data; target_lengths requires nonempty positive lengths; target_angles requires nonempty angles summing to 2*pi within angle_sum_tolerance. Invalid combinations retain the reference BFF group.", .Item = &kFiniteNumber},
            ConfigFieldSpec{.Name = "angle_sum_tolerance", .Type = FT::Float, .Min = std::numeric_limits<double>::min(), .Max = 1.0e30},
            ConfigFieldSpec{.Name = "degeneracy_tolerance", .Type = FT::Float, .Min = std::numeric_limits<double>::min(), .Max = 1.0e30},
        };
        const std::array kClusteringFields{
            ConfigFieldSpec{.Name = "cluster_count", .Type = FT::UInt, .Min = 1, .Max = 1024},
            ConfigFieldSpec{.Name = "gpu_preview_interval", .Type = FT::UInt, .Min = 1, .Max = 1000000},
            ConfigFieldSpec{.Name = "max_iterations", .Type = FT::UInt, .Min = 1, .Max = 4096},
            ConfigFieldSpec{.Name = "seed", .Type = FT::UInt, .Min = 0, .Max = std::numeric_limits<std::uint32_t>::max()},
            ConfigFieldSpec{.Name = "initialization", .Type = FT::StringEnum, .EnumValues = kKMeansInitializationTokens},
            ConfigFieldSpec{.Name = "backend", .Type = FT::StringEnum, .EnumValues = kClusteringBackendTokens},
            ConfigFieldSpec{.Name = "properties", .Type = FT::Object, .Description = "Bindings share one element domain and have distinct nonempty NUL-free names. Outputs must not replace topology or position storage; unknown nested keys are silently ignored.", .Nullable = true, .Fields = kClusteringPropertyFields.data(), .FieldCount = kClusteringPropertyFields.size()},
        };
        const std::array kCurvatureSegmentationFields{
            ConfigFieldSpec{.Name = "features", .Type = FT::Array, .Description = "At most three total scalar components; feature-curve methods require computed curvature.", .Item = &kSegmentationFeature, .MaxItems = 3},
            ConfigFieldSpec{.Name = "positions", .Type = FT::PropertyRef, .RefKinds = kVec3Kinds, .RefDomains = kVertexDomain, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "components", .Type = FT::PropertyRef, .RefKinds = kScalarKinds, .RefDomains = kFaceDomain, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "regions", .Type = FT::PropertyRef, .RefKinds = kScalarKinds, .RefDomains = kFaceDomain, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "region_colors", .Type = FT::PropertyRef, .RefKinds = kVec4Kinds, .RefDomains = kFaceDomain, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "boundaries", .Type = FT::PropertyRef, .RefKinds = kScalarKinds, .RefDomains = kEdgeDomain, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "boundary_colors", .Type = FT::PropertyRef, .RefKinds = kVec4Kinds, .RefDomains = kEdgeDomain, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "hard_features", .Type = FT::PropertyRef, .RefKinds = kScalarKinds, .RefDomains = kEdgeDomain, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "feature_confidence", .Type = FT::PropertyRef, .RefKinds = kScalarKinds, .RefDomains = kEdgeDomain, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "boundary_roles", .Type = FT::PropertyRef, .RefKinds = kScalarKinds, .RefDomains = kEdgeDomain, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "feature_colors", .Type = FT::PropertyRef, .RefKinds = kVec4Kinds, .RefDomains = kEdgeDomain, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "method", .Type = FT::StringEnum, .EnumValues = kCurvatureSegmentationMethodTokens},
            ConfigFieldSpec{.Name = "selection_mode", .Type = FT::StringEnum, .EnumValues = kCurvatureSegmentationSelectionModeTokens},
            ConfigFieldSpec{.Name = "fixed_component_count", .Type = FT::UInt, .Min = 1, .Max = 1024},
            ConfigFieldSpec{.Name = "automatic_min_components", .Type = FT::UInt, .Description = "Must not exceed automatic_max_components; invalid ordering retains both reference bounds.", .Min = 1, .Max = 1024},
            ConfigFieldSpec{.Name = "automatic_max_components", .Type = FT::UInt, .Description = "Must be at least automatic_min_components; invalid ordering retains both reference bounds.", .Min = 1, .Max = 1024},
            ConfigFieldSpec{.Name = "automatic_fit_tolerance", .Type = FT::Float, .Min = 1.0e-12, .Max = 1.0e12},
            ConfigFieldSpec{.Name = "automatic_complexity_weight", .Type = FT::Float, .Min = 0.0, .Max = 1.0e12},
            ConfigFieldSpec{.Name = "max_em_iterations", .Type = FT::UInt, .Min = 1, .Max = 100000},
            ConfigFieldSpec{.Name = "em_relative_tolerance", .Type = FT::Float, .Min = 0.0, .Max = 1.0},
            ConfigFieldSpec{.Name = "covariance_floor", .Type = FT::Float, .Min = 1.0e-15, .Max = 1.0e6},
            ConfigFieldSpec{.Name = "seed", .Type = FT::UInt, .Min = 0, .Max = std::numeric_limits<std::uint32_t>::max()},
            ConfigFieldSpec{.Name = "spatial_weight", .Type = FT::Float, .Min = 0.0, .Max = 1.0e12},
            ConfigFieldSpec{.Name = "feature_sensitivity", .Type = FT::Float, .Min = 0.0, .Max = 1.0e12},
            ConfigFieldSpec{.Name = "max_spatial_iterations", .Type = FT::UInt, .Min = 1, .Max = 100000},
            ConfigFieldSpec{.Name = "minimum_region_faces", .Type = FT::UInt, .Min = 1, .Max = std::numeric_limits<std::uint32_t>::max()},
            ConfigFieldSpec{.Name = "feature_base_radius_ratio", .Type = FT::Float, .Min = 1.0e-12, .Max = 1.0},
            ConfigFieldSpec{.Name = "hard_dihedral_threshold_degrees", .Type = FT::Float, .Min = 0.0, .Max = 180.0},
            ConfigFieldSpec{.Name = "patch_complexity_cost", .Type = FT::Float, .Min = 0.0, .Max = 1.0e12},
        };
        const std::array kPointCloudConsolidationFields{
            ConfigFieldSpec{.Name = "backend", .Type = FT::StringEnum, .EnumNames = kConsolidationBackendLabels, .EnumValues = kPointCloudConsolidationBackendTokens},
            ConfigFieldSpec{.Name = "initial_backend", .Type = FT::StringEnum, .EnumValues = kInitialBackendTokens},
            ConfigFieldSpec{.Name = "strategy", .Type = FT::StringEnum, .EnumValues = kPointCloudConsolidationStrategyTokens},
            ConfigFieldSpec{.Name = "support_radius_mode", .Type = FT::StringEnum, .EnumValues = kPointCloudConsolidationSupportRadiusModeTokens},
            ConfigFieldSpec{.Name = "support_radius", .Type = FT::Float, .Min = 1.0e-12, .Max = 1.0e12},
            ConfigFieldSpec{.Name = "max_support_neighbors", .Type = FT::UInt, .Min = 1, .Max = 1'000'000},
            ConfigFieldSpec{.Name = "max_predicted_contributions", .Type = FT::UInt, .Min = 1, .Max = 1'000'000'000'000LL},
            ConfigFieldSpec{.Name = "repulsion_weight", .Type = FT::Float, .Min = 0.0, .Max = 0.499999999999},
            ConfigFieldSpec{.Name = "max_iterations", .Type = FT::UInt, .Min = 1, .Max = 4096},
            ConfigFieldSpec{.Name = "convergence_tolerance", .Type = FT::Float, .Min = 0.0, .Max = 1.0e12},
            ConfigFieldSpec{.Name = "target_point_count", .Type = FT::UInt, .Description = "Zero or an explicit count of at least two is accepted. One retains the reference value with a warning.", .Min = 0, .Max = 1'000'000},
            ConfigFieldSpec{.Name = "seed", .Type = FT::UInt, .Min = 0, .Max = std::numeric_limits<std::uint32_t>::max()},
            ConfigFieldSpec{.Name = "gpu_preview_interval", .Type = FT::UInt, .Min = 1, .Max = 1000000},
            ConfigFieldSpec{.Name = "gpu_query_batch_size", .Type = FT::UInt, .Min = 1, .Max = 16384},
            ConfigFieldSpec{.Name = "gpu_radius_capacity", .Type = FT::UInt, .Min = 1, .Max = 1024},
            ConfigFieldSpec{.Name = "wlop_anisotropic", .Type = FT::Bool},
            ConfigFieldSpec{.Name = "normal_source", .Type = FT::StringEnum, .EnumValues = kPointCloudConsolidationNormalSourceTokens},
            ConfigFieldSpec{.Name = "normal_angle_radians", .Type = FT::Float, .Min = 1.0e-6, .Max = std::numbers::pi_v<double> - 1.0e-6},
            ConfigFieldSpec{.Name = "normal_refinement_rounds", .Type = FT::UInt, .Description = "For EAR or anisotropic WLOP, values above max_iterations fall back to the reference rounds clamped to [1, max_iterations].", .Min = 1, .Max = 4096},
            ConfigFieldSpec{.Name = "clop_mixture_component_count", .Type = FT::UInt, .Min = 1, .Max = 1'000'000},
            ConfigFieldSpec{.Name = "clop_mixture_max_iterations", .Type = FT::UInt, .Min = 1, .Max = 4096},
            ConfigFieldSpec{.Name = "clop_mixture_relative_tolerance", .Type = FT::Float, .Min = 0.0, .Max = 1.0},
            ConfigFieldSpec{.Name = "clop_covariance_floor", .Type = FT::Float, .Min = 1.0e-18, .Max = 1.0e12},
            ConfigFieldSpec{.Name = "ear_edge_sensitivity", .Type = FT::Float, .Min = 1.0e-12, .Max = 1.0e6},
        };
        const std::array kProgressivePoissonFields{
            ConfigFieldSpec{.Name = "positions", .Type = FT::PropertyRef, .Description = "Bindings use distinct nonempty NUL-free names and cannot name topology storage or Unknown-domain v:deleted. Output domains must be Unknown or the input domain. Listed domains describe registered defaults; file load may restrict them to a non-default reference domain.", .RefKinds = kVec3Kinds, .RefDomains = kAllDomains, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "level_property", .Type = FT::PropertyRef, .RefKinds = kScalarKinds, .RefDomains = kAllDomains, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "rank_property", .Type = FT::PropertyRef, .RefKinds = kScalarKinds, .RefDomains = kAllDomains, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "splat_radius_property", .Type = FT::PropertyRef, .RefKinds = kScalarKinds, .RefDomains = kAllDomains, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "prefix_visible_property", .Type = FT::PropertyRef, .RefKinds = kScalarKinds, .RefDomains = kAllDomains, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "dimension", .Type = FT::UInt, .Min = 2, .Max = 3},
            ConfigFieldSpec{.Name = "grid_width", .Type = FT::UInt, .Min = 1, .Max = 4096},
            ConfigFieldSpec{.Name = "max_levels", .Type = FT::UInt, .Min = 1, .Max = 32},
            ConfigFieldSpec{.Name = "hash_load_factor", .Type = FT::Float, .Min = 0.01, .Max = 16.0},
            ConfigFieldSpec{.Name = "radius_alpha", .Type = FT::Float, .Min = -1.0, .Max = 0.999},
            ConfigFieldSpec{.Name = "randomize_grid_origin", .Type = FT::Bool},
            ConfigFieldSpec{.Name = "grid_origin_seed", .Type = FT::UInt, .Min = 0, .Max = std::numeric_limits<std::int32_t>::max()},
            ConfigFieldSpec{.Name = "shuffle_within_levels", .Type = FT::Bool},
            ConfigFieldSpec{.Name = "shuffle_seed", .Type = FT::UInt, .Min = 0, .Max = std::numeric_limits<std::int32_t>::max()},
            ConfigFieldSpec{.Name = "prefix_count", .Type = FT::UInt, .Min = 0, .Max = 10'000'000},
            ConfigFieldSpec{.Name = "channel", .Type = FT::StringEnum, .EnumValues = kProgressivePoissonChannelTokens},
            ConfigFieldSpec{.Name = "backend", .Type = FT::StringEnum, .EnumValues = kProgressivePoissonBackendTokens},
            ConfigFieldSpec{.Name = "auto_run_on_edit", .Type = FT::Bool},
            ConfigFieldSpec{.Name = "debounce_seconds", .Type = FT::Float, .Min = 0.0, .Max = 10.0},
        };
        const std::array kParameterizationFields{
            ConfigFieldSpec{.Name = "positions", .Type = FT::PropertyRef, .Description = "Vertex positions must have a nonempty NUL-free name distinct from texcoords.", .RefKinds = kVec3Kinds, .RefDomains = kVertexDomain, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "texcoords", .Type = FT::PropertyRef, .Description = "Output must have a nonempty NUL-free name distinct from positions and must not replace structural vertex storage.", .RefKinds = kVec2Kinds, .RefDomains = kVertexDomain, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "corner_texcoords_to_retire", .Type = FT::PropertyRef, .Description = "Optional non-structural halfedge vec2 property with a nonempty NUL-free name.", .Nullable = true, .RefKinds = kVec2Kinds, .RefDomains = kHalfedgeDomain, .IntegerRefKinds = true},
            ConfigFieldSpec{.Name = "strategy", .Type = FT::StringEnum, .EnumValues = kParameterizationStrategyTokens},
            ConfigFieldSpec{.Name = "lscm", .Type = FT::Object, .Fields = kParameterizationLscmFields.data(), .FieldCount = kParameterizationLscmFields.size()},
            ConfigFieldSpec{.Name = "harmonic", .Type = FT::Object, .Fields = kParameterizationHarmonicFields.data(), .FieldCount = kParameterizationHarmonicFields.size()},
            ConfigFieldSpec{.Name = "bff", .Type = FT::Object, .Fields = kParameterizationBffFields.data(), .FieldCount = kParameterizationBffFields.size()},
            ConfigFieldSpec{.Name = "view", .Type = FT::Object, .Fields = kParameterizationViewFields.data(), .FieldCount = kParameterizationViewFields.size()},
            ConfigFieldSpec{.Name = "atlas", .Type = FT::Object, .Fields = kParameterizationAtlasFields.data(), .FieldCount = kParameterizationAtlasFields.size()},
        };

        // Reuse the sampling owner's prefixed specs; both schema and parser share them.
        const std::vector<ConfigFieldSpec>& ConsolidationFields()
        {
            static const auto fields = [] {
                std::vector<ConfigFieldSpec> result(kPointCloudConsolidationFields.begin(), kPointCloudConsolidationFields.end());
                const auto sampling = PointSamplingFieldSpecs("initial_");
                result.insert(result.end(), sampling.begin(), sampling.end());
                return result;
            }();
            return fields;
        }

        struct ValidationContext
        {
            Core::Config::EngineConfigSectionValidationResult* Result{};
            std::string Path{};
        };

        [[nodiscard]] std::string FieldSubject(
            const std::string_view path,
            const std::string_view key)
        {
            if (path.empty())
            {
                return std::string{key};
            }
            return std::string{path} + "." + std::string{key};
        }

        [[nodiscard]] ValidationContext ChildContext(
            const ValidationContext& context,
            const std::string_view key)
        {
            return ValidationContext{
                .Result = context.Result,
                .Path = FieldSubject(context.Path, key),
            };
        }

        void AddWarning(
            ValidationContext& context,
            const Core::Config::EngineConfigDiagnosticCode code,
            std::string subject,
            std::string message)
        {
            if (context.Result == nullptr)
            {
                return;
            }
            if (context.Result->State != Core::Config::EngineConfigState::Invalid)
                context.Result->State = Core::Config::EngineConfigState::FallbackApplied;
            context.Result->Diagnostics.push_back(Core::Config::EngineConfigDiagnostic{
                .State = Core::Config::EngineConfigState::FallbackApplied,
                .Severity =
                    Core::Config::EngineConfigDiagnosticSeverity::Warning,
                .Code = code,
                .Subject = std::move(subject),
                .Message = std::move(message),
            });
        }

        void CountParsed(ValidationContext& context, const std::uint32_t count = 1u)
        {
            if (context.Result != nullptr)
            {
                context.Result->ParsedFieldCount += count;
            }
        }

        [[nodiscard]] const json* FindMember(
            const json& object,
            const std::string_view key)
        {
            const auto it = object.find(std::string{key});
            return it == object.end() ? nullptr : &*it;
        }

        void AddUnknownFieldDiagnostics(
            ValidationContext& context,
            const json& object,
            const std::span<const ConfigFieldSpec> fields)
        {
            if (!object.is_object())
            {
                return;
            }
            for (const auto& [key, value] : object.items())
            {
                (void)value;
                if (!FindConfigFieldSpec(fields, key))
                {
                    AddWarning(
                        context,
                        Core::Config::EngineConfigDiagnosticCode::UnknownField,
                        FieldSubject(context.Path, key),
                        "Unknown config field; reference default remains authoritative for this field.");
                }
            }
        }

        [[nodiscard]] std::optional<json> ParseObject(
            ValidationContext& context,
            const std::string_view payload)
        {
            json object = ConfigDetail::ParseConfigJson(payload, false);
            if (object.is_discarded())
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::ParseError,
                    context.Path,
                    "Invalid JSON payload; reference config retained.");
                return std::nullopt;
            }
            if (!object.is_object())
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    context.Path,
                    "Expected a JSON object; reference config retained.");
                return std::nullopt;
            }
            return object;
        }

        [[nodiscard]] std::optional<bool> ReadBool(
            ValidationContext& context,
            const json& object,
            const std::string_view key)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_boolean())
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, key),
                    "Expected a boolean value; reference default retained.");
                return std::nullopt;
            }
            return value->get<bool>();
        }

        [[nodiscard]] std::optional<std::string> ReadString(
            ValidationContext& context,
            const json& object,
            const std::string_view key)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_string())
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, key),
                    "Expected a string value; reference default retained.");
                return std::nullopt;
            }
            std::string text = value->get<std::string>();
            if (text.empty())
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, key),
                    "Expected a non-empty string value; reference default retained.");
                return std::nullopt;
            }
            return text;
        }

        [[nodiscard]] std::optional<std::int64_t> ReadInteger(
            ValidationContext& context,
            const json& object,
            const std::string_view key,
            const std::span<const ConfigFieldSpec> fields)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_number_integer())
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, key),
                    "Expected an integer value; reference default retained.");
                return std::nullopt;
            }

            std::optional<std::int64_t> number{};
            if (value->is_number_unsigned())
            {
                const std::uint64_t unsignedNumber = value->get<std::uint64_t>();
                if (unsignedNumber <=
                    static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max()))
                {
                    number = static_cast<std::int64_t>(unsignedNumber);
                }
            }
            else
            {
                number = value->get<std::int64_t>();
            }
            if (!number.has_value() || !ConfigDetail::InConfigFieldRange(fields, key, static_cast<double>(*number)))
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, key),
                    "Integer value is outside the supported range; reference default retained.");
                return std::nullopt;
            }
            return number;
        }

        [[nodiscard]] std::optional<double> ReadNumber(
            ValidationContext& context,
            const json& object,
            const std::string_view key,
            const std::span<const ConfigFieldSpec> fields)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_number())
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, key),
                    "Expected a numeric value; reference default retained.");
                return std::nullopt;
            }
            const double number = value->get<double>();
            if (!ConfigDetail::InConfigFieldRange(fields, key, number))
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, key),
                    "Numeric value is outside the supported range; reference default retained.");
                return std::nullopt;
            }
            return number;
        }

        [[nodiscard]] std::optional<ParameterizationUvConfig> ReadUv(
            ValidationContext& context,
            const json& object,
            const std::string_view key)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_array() || value->size() != kUvPair.MinItems.value() ||
                !(*value)[0].is_number() || !(*value)[1].is_number())
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, key),
                    "Expected an array of two numeric UV coordinates; reference default retained.");
                return std::nullopt;
            }

            const double u = (*value)[0].get<double>();
            const double v = (*value)[1].get<double>();
            if (!AcceptsConfigFieldNumber(kUvCoordinate, u) || !AcceptsConfigFieldNumber(kUvCoordinate, v))
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, key),
                    "UV coordinates must be finite and representable as floats; reference default retained.");
                return std::nullopt;
            }
            return ParameterizationUvConfig{.U = u, .V = v};
        }

        [[nodiscard]] std::optional<std::vector<std::uint32_t>> ReadIndexArray(
            ValidationContext& context,
            const json& object,
            const std::string_view key)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_array())
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, key),
                    "Expected an array of vertex indices; reference default retained.");
                return std::nullopt;
            }

            std::vector<std::uint32_t> indices{};
            indices.reserve(value->size());
            for (std::size_t index = 0; index < value->size(); ++index)
            {
                const json& element = (*value)[index];
                std::optional<std::uint64_t> parsed{};
                if (element.is_number_unsigned())
                {
                    parsed = element.get<std::uint64_t>();
                }
                else if (element.is_number_integer())
                {
                    const std::int64_t signedValue = element.get<std::int64_t>();
                    if (signedValue >= 0)
                    {
                        parsed = static_cast<std::uint64_t>(signedValue);
                    }
                }
                if (!parsed.has_value() ||
                    *parsed > std::numeric_limits<std::uint32_t>::max())
                {
                    AddWarning(
                        context,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        FieldSubject(context.Path, key) + "[" +
                            std::to_string(index) + "]",
                        "Expected a non-negative 32-bit vertex index; reference array retained.");
                    return std::nullopt;
                }
                indices.push_back(static_cast<std::uint32_t>(*parsed));
            }
            return indices;
        }

        [[nodiscard]] std::optional<std::vector<ParameterizationUvConfig>>
        ReadUvArray(
            ValidationContext& context,
            const json& object,
            const std::string_view key)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_array())
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, key),
                    "Expected an array of UV coordinate pairs; reference array retained.");
                return std::nullopt;
            }

            std::vector<ParameterizationUvConfig> uvs{};
            uvs.reserve(value->size());
            for (std::size_t index = 0; index < value->size(); ++index)
            {
                const json& element = (*value)[index];
                if (!element.is_array() || element.size() != kUvPair.MinItems.value() ||
                    !element[0].is_number() || !element[1].is_number())
                {
                    AddWarning(
                        context,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        FieldSubject(context.Path, key) + "[" +
                            std::to_string(index) + "]",
                        "Expected an array of two numeric UV coordinates; reference array retained.");
                    return std::nullopt;
                }
                const double u = element[0].get<double>();
                const double v = element[1].get<double>();
                if (!AcceptsConfigFieldNumber(kUvCoordinate, u) || !AcceptsConfigFieldNumber(kUvCoordinate, v))
                {
                    AddWarning(
                        context,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        FieldSubject(context.Path, key) + "[" +
                            std::to_string(index) + "]",
                        "UV coordinates must be finite and representable as floats; reference array retained.");
                    return std::nullopt;
                }
                uvs.push_back(ParameterizationUvConfig{.U = u, .V = v});
            }
            return uvs;
        }

        [[nodiscard]] std::optional<std::vector<double>> ReadNumberArray(
            ValidationContext& context,
            const json& object,
            const std::string_view key)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_array())
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, key),
                    "Expected a numeric array; reference default retained.");
                return std::nullopt;
            }

            std::vector<double> numbers{};
            numbers.reserve(value->size());
            for (std::size_t index = 0; index < value->size(); ++index)
            {
                const json& element = (*value)[index];
                if (!element.is_number())
                {
                    AddWarning(
                        context,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        FieldSubject(context.Path, key) + "[" +
                            std::to_string(index) + "]",
                        "Expected a finite numeric value; reference array retained.");
                    return std::nullopt;
                }
                const double number = element.get<double>();
                if (!std::isfinite(number))
                {
                    AddWarning(
                        context,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        FieldSubject(context.Path, key) + "[" +
                            std::to_string(index) + "]",
                        "Expected a finite numeric value; reference array retained.");
                    return std::nullopt;
                }
                numbers.push_back(number);
            }
            return numbers;
        }

        [[nodiscard]] std::optional<ProgressivePoissonPlaygroundChannel>
        ParseProgressivePoissonChannel(const std::string_view value) noexcept
        {
            if (value == kProgressivePoissonChannelTokens[0]) return ProgressivePoissonPlaygroundChannel::Level;
            if (value == kProgressivePoissonChannelTokens[1]) return ProgressivePoissonPlaygroundChannel::Rank;
            if (value == kProgressivePoissonChannelTokens[2])
                return ProgressivePoissonPlaygroundChannel::SplatRadius;
            if (value == kProgressivePoissonChannelTokens[3])
                return ProgressivePoissonPlaygroundChannel::PrefixVisible;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ProgressivePoissonPlaygroundBackend>
        ParseProgressivePoissonBackend(const std::string_view value) noexcept
        {
            if (value == kProgressivePoissonBackendTokens[0])
                return ProgressivePoissonPlaygroundBackend::CpuReference;
            if (value == kProgressivePoissonBackendTokens[1])
                return ProgressivePoissonPlaygroundBackend::VulkanCompute;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ClusteringBackend>
        ParseClusteringBackend(const std::string_view value) noexcept
        {
            if (value == kClusteringBackendTokens[0])
                return ClusteringBackend::CpuReference;
            if (value == kClusteringBackendTokens[1])
                return ClusteringBackend::VulkanCompute;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<CurvatureSegmentationSelectionMode>
        ParseCurvatureSegmentationSelectionMode(
            const std::string_view value) noexcept
        {
            if (value == kCurvatureSegmentationSelectionModeTokens[0])
            {
                return CurvatureSegmentationSelectionMode::FixedCount;
            }
            if (value == kCurvatureSegmentationSelectionModeTokens[1])
            {
                return CurvatureSegmentationSelectionMode::Automatic;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<CurvatureSegmentationMethod>
        ParseCurvatureSegmentationMethod(
            const std::string_view value) noexcept
        {
            if (value == kCurvatureSegmentationMethodTokens[0])
                return CurvatureSegmentationMethod::CurvatureGmm;
            if (value == kCurvatureSegmentationMethodTokens[1])
                return CurvatureSegmentationMethod::FeatureAlignedPatches;
            if (value == kCurvatureSegmentationMethodTokens[2])
                return CurvatureSegmentationMethod::FeatureBoundaryCurves;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<PointCloudConsolidationStrategy>
        ParsePointCloudConsolidationStrategy(
            const std::string_view value) noexcept
        {
            if (value == kPointCloudConsolidationStrategyTokens[0])
                return PointCloudConsolidationStrategy::Lop;
            if (value == kPointCloudConsolidationStrategyTokens[1])
                return PointCloudConsolidationStrategy::Wlop;
            if (value == kPointCloudConsolidationStrategyTokens[2])
                return PointCloudConsolidationStrategy::Clop;
            if (value == kPointCloudConsolidationStrategyTokens[3])
                return PointCloudConsolidationStrategy::Ear;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<PointCloudConsolidationBackend>
        ParsePointCloudConsolidationBackend(
            const std::string_view value) noexcept
        {
            if (value == kPointCloudConsolidationBackendTokens[0])
                return PointCloudConsolidationBackend::CpuReference;
            if (value == kPointCloudConsolidationBackendTokens[1])
                return PointCloudConsolidationBackend::VulkanCompute;
            if (value == kPointCloudConsolidationBackendTokens[2]) return PointCloudConsolidationBackend::CpuLBVH;
            if (value == kPointCloudConsolidationBackendTokens[3]) return PointCloudConsolidationBackend::VulkanLBVH;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<PointCloudConsolidationNormalSource>
        ParsePointCloudConsolidationNormalSource(
            const std::string_view value) noexcept
        {
            if (value == kPointCloudConsolidationNormalSourceTokens[0])
            {
                return PointCloudConsolidationNormalSource::
                    AuthoredOrEstimate;
            }
            if (value == kPointCloudConsolidationNormalSourceTokens[1])
            {
                return PointCloudConsolidationNormalSource::RequireAuthored;
            }
            return std::nullopt;
        }

        [[nodiscard]]
        std::optional<PointCloudConsolidationSupportRadiusMode>
        ParsePointCloudConsolidationSupportRadiusMode(
            const std::string_view value) noexcept
        {
            if (value == kPointCloudConsolidationSupportRadiusModeTokens[0])
                return PointCloudConsolidationSupportRadiusMode::Auto;
            if (value == kPointCloudConsolidationSupportRadiusModeTokens[1])
                return PointCloudConsolidationSupportRadiusMode::Manual;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<KMeansInitialization>
        ParseKMeansInitialization(const std::string_view value) noexcept
        {
            if (value == kKMeansInitializationTokens[0]) return KMeansInitialization::Random;
            if (value == kKMeansInitializationTokens[1])
                return KMeansInitialization::Hierarchical;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<Geometry::UvAtlas::UvAtlasMethod>
        ParseAtlasMethod(const std::string_view value) noexcept
        {
            using M = Geometry::UvAtlas::UvAtlasMethod;
            if (value == kAtlasMethodTokens[0]) return M::FastStaged;
            if (value == kAtlasMethodTokens[1]) return M::XAtlas;
            return std::nullopt;
        }
        [[nodiscard]] std::optional<Geometry::UvAtlas::UvAtlasDistortion>
        ParseAtlasDistortion(const std::string_view value) noexcept
        {
            using D = Geometry::UvAtlas::UvAtlasDistortion;
            if (value == kAtlasDistortionTokens[0]) return D::None;
            if (value == kAtlasDistortionTokens[1]) return D::Angle;
            if (value == kAtlasDistortionTokens[2]) return D::Area;
            if (value == kAtlasDistortionTokens[3]) return D::Both;
            return std::nullopt;
        }
        [[nodiscard]] std::string_view ToConfigString(const Geometry::UvAtlas::UvAtlasMethod value) noexcept
        {
            using M = Geometry::UvAtlas::UvAtlasMethod;
            switch (value) { case M::FastStaged: return kAtlasMethodTokens[0]; case M::XAtlas: return kAtlasMethodTokens[1]; default: return "invalid"; }
        }
        [[nodiscard]] std::string_view ToConfigString(const Geometry::UvAtlas::UvAtlasDistortion value) noexcept
        {
            using D = Geometry::UvAtlas::UvAtlasDistortion;
            switch (value) { case D::None: return kAtlasDistortionTokens[0]; case D::Angle: return kAtlasDistortionTokens[1]; case D::Area: return kAtlasDistortionTokens[2]; case D::Both: return kAtlasDistortionTokens[3]; }
            return "invalid";
        }

        [[nodiscard]] std::optional<ParameterizationStrategyKind>
        ParseParameterizationStrategy(const std::string_view value) noexcept
        {
            if (value == kParameterizationStrategyTokens[0]) return ParameterizationStrategyKind::Lscm;
            if (value == kParameterizationStrategyTokens[1])
                return ParameterizationStrategyKind::HarmonicCotangent;
            if (value == kParameterizationStrategyTokens[2])
                return ParameterizationStrategyKind::TutteUniform;
            if (value == kParameterizationStrategyTokens[3]) return ParameterizationStrategyKind::Bff;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ParameterizationBoundaryPolicy>
        ParseParameterizationBoundaryPolicy(const std::string_view value) noexcept
        {
            if (value == kParameterizationBoundaryPolicyTokens[0]) return ParameterizationBoundaryPolicy::Circle;
            if (value == kParameterizationBoundaryPolicyTokens[1]) return ParameterizationBoundaryPolicy::Square;
            if (value == kParameterizationBoundaryPolicyTokens[2]) return ParameterizationBoundaryPolicy::Custom;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ParameterizationBffBoundaryMode>
        ParseParameterizationBffBoundaryMode(const std::string_view value) noexcept
        {
            if (value == kParameterizationBffBoundaryModeTokens[0])
                return ParameterizationBffBoundaryMode::AutomaticConformal;
            if (value == kParameterizationBffBoundaryModeTokens[1])
                return ParameterizationBffBoundaryMode::TargetLengths;
            if (value == kParameterizationBffBoundaryModeTokens[2])
                return ParameterizationBffBoundaryMode::TargetAngles;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ParameterizationUvRenderMode>
        ParseParameterizationUvRenderMode(const std::string_view value) noexcept
        {
            if (value == kParameterizationUvRenderModeTokens[0])
                return ParameterizationUvRenderMode::CpuLayout;
            if (value == kParameterizationUvRenderModeTokens[1])
                return ParameterizationUvRenderMode::GpuShaded;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ParameterizationUvBackgroundMode>
        ParseParameterizationUvBackgroundMode(
            const std::string_view value) noexcept
        {
            if (value == kParameterizationUvBackgroundModeTokens[0]) return ParameterizationUvBackgroundMode::Grid;
            if (value == kParameterizationUvBackgroundModeTokens[1])
                return ParameterizationUvBackgroundMode::Checker;
            if (value == kParameterizationUvBackgroundModeTokens[2])
                return ParameterizationUvBackgroundMode::TexelDensity;
            if (value == kParameterizationUvBackgroundModeTokens[3])
                return ParameterizationUvBackgroundMode::Texture;
            return std::nullopt;
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ProgressivePoissonPlaygroundChannel value) noexcept
        {
            switch (value)
            {
            case ProgressivePoissonPlaygroundChannel::Level: return kProgressivePoissonChannelTokens[0];
            case ProgressivePoissonPlaygroundChannel::Rank: return kProgressivePoissonChannelTokens[1];
            case ProgressivePoissonPlaygroundChannel::SplatRadius:
                return kProgressivePoissonChannelTokens[2];
            case ProgressivePoissonPlaygroundChannel::PrefixVisible:
                return kProgressivePoissonChannelTokens[3];
            }
            return kProgressivePoissonChannelTokens[0];
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ProgressivePoissonPlaygroundBackend value) noexcept
        {
            switch (value)
            {
            case ProgressivePoissonPlaygroundBackend::CpuReference:
                return kProgressivePoissonBackendTokens[0];
            case ProgressivePoissonPlaygroundBackend::VulkanCompute:
                return kProgressivePoissonBackendTokens[1];
            }
            return kProgressivePoissonBackendTokens[0];
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ClusteringBackend value) noexcept
        {
            switch (value)
            {
            case ClusteringBackend::VulkanCompute: return kClusteringBackendTokens[1];
            case ClusteringBackend::None:
            case ClusteringBackend::CpuReference: return kClusteringBackendTokens[0];
            }
            return kClusteringBackendTokens[0];
        }

        [[nodiscard]] std::string_view ToConfigString(
            const CurvatureSegmentationSelectionMode value) noexcept
        {
            switch (value)
            {
            case CurvatureSegmentationSelectionMode::FixedCount:
                return kCurvatureSegmentationSelectionModeTokens[0];
            case CurvatureSegmentationSelectionMode::Automatic:
                return kCurvatureSegmentationSelectionModeTokens[1];
            }
            return kCurvatureSegmentationSelectionModeTokens[1];
        }

        [[nodiscard]] std::string_view ToConfigString(
            const CurvatureSegmentationMethod value) noexcept
        {
            switch (value)
            {
            case CurvatureSegmentationMethod::CurvatureGmm:
                return kCurvatureSegmentationMethodTokens[0];
            case CurvatureSegmentationMethod::FeatureAlignedPatches:
                return kCurvatureSegmentationMethodTokens[1];
            case CurvatureSegmentationMethod::FeatureBoundaryCurves:
                return kCurvatureSegmentationMethodTokens[2];
            }
            return kCurvatureSegmentationMethodTokens[0];
        }

        [[nodiscard]] std::string_view ToConfigString(
            const KMeansInitialization value) noexcept
        {
            switch (value)
            {
            case KMeansInitialization::Random: return kKMeansInitializationTokens[0];
            case KMeansInitialization::Hierarchical: return kKMeansInitializationTokens[1];
            }
            return kKMeansInitializationTokens[1];
        }

        [[nodiscard]] std::string_view ToConfigString(
            const PointCloudConsolidationStrategy value) noexcept
        {
            return StableToken(value);
        }

        [[nodiscard]] std::string_view ToConfigString(
            const PointCloudConsolidationBackend value) noexcept
        {
            return StableToken(value);
        }

        [[nodiscard]] std::string_view ToConfigString(
            const PointCloudConsolidationNormalSource value) noexcept
        {
            return StableToken(value);
        }

        [[nodiscard]] std::string_view ToConfigString(
            const PointCloudConsolidationSupportRadiusMode value) noexcept
        {
            return StableToken(value);
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ParameterizationStrategyKind value) noexcept
        {
            switch (value)
            {
            case ParameterizationStrategyKind::Lscm: return kParameterizationStrategyTokens[0];
            case ParameterizationStrategyKind::HarmonicCotangent:
                return kParameterizationStrategyTokens[1];
            case ParameterizationStrategyKind::TutteUniform:
                return kParameterizationStrategyTokens[2];
            case ParameterizationStrategyKind::Bff: return kParameterizationStrategyTokens[3];
            }
            return kParameterizationStrategyTokens[0];
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ParameterizationBoundaryPolicy value) noexcept
        {
            switch (value)
            {
            case ParameterizationBoundaryPolicy::Circle: return kParameterizationBoundaryPolicyTokens[0];
            case ParameterizationBoundaryPolicy::Square: return kParameterizationBoundaryPolicyTokens[1];
            case ParameterizationBoundaryPolicy::Custom: return kParameterizationBoundaryPolicyTokens[2];
            }
            return kParameterizationBoundaryPolicyTokens[0];
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ParameterizationBffBoundaryMode value) noexcept
        {
            switch (value)
            {
            case ParameterizationBffBoundaryMode::AutomaticConformal:
                return kParameterizationBffBoundaryModeTokens[0];
            case ParameterizationBffBoundaryMode::TargetLengths:
                return kParameterizationBffBoundaryModeTokens[1];
            case ParameterizationBffBoundaryMode::TargetAngles:
                return kParameterizationBffBoundaryModeTokens[2];
            }
            return kParameterizationBffBoundaryModeTokens[0];
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ParameterizationUvRenderMode value) noexcept
        {
            switch (value)
            {
            case ParameterizationUvRenderMode::CpuLayout: return kParameterizationUvRenderModeTokens[0];
            case ParameterizationUvRenderMode::GpuShaded: return kParameterizationUvRenderModeTokens[1];
            }
            return kParameterizationUvRenderModeTokens[0];
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ParameterizationUvBackgroundMode value) noexcept
        {
            switch (value)
            {
            case ParameterizationUvBackgroundMode::Grid: return kParameterizationUvBackgroundModeTokens[0];
            case ParameterizationUvBackgroundMode::Checker: return kParameterizationUvBackgroundModeTokens[1];
            case ParameterizationUvBackgroundMode::TexelDensity:
                return kParameterizationUvBackgroundModeTokens[2];
            case ParameterizationUvBackgroundMode::Texture: return kParameterizationUvBackgroundModeTokens[3];
            }
            return kParameterizationUvBackgroundModeTokens[0];
        }

        template <typename Enum, typename Parser>
        bool ReadEnum(
            ValidationContext& context,
            const json& object,
            const std::string_view key,
            Parser parse,
            Enum& outValue)
        {
            const std::optional<std::string> text =
                ReadString(context, object, key);
            if (!text.has_value())
            {
                return false;
            }
            const std::optional<Enum> parsed = parse(*text);
            if (!parsed.has_value())
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, key),
                    "Unsupported enum value; reference default retained.");
                return false;
            }
            outValue = *parsed;
            return true;
        }

        // These values are persisted by the shared codec; changing them requires a schema change.
        static_assert(static_cast<unsigned>(Geometry::PropertyValueKind::Bool) == 1u &&
                      static_cast<unsigned>(Geometry::PropertyValueKind::Int32) == 2u &&
                      static_cast<unsigned>(Geometry::PropertyValueKind::UInt32) == 3u &&
                      static_cast<unsigned>(Geometry::PropertyValueKind::UInt64) == 4u &&
                      static_cast<unsigned>(Geometry::PropertyValueKind::Float) == 5u &&
                      static_cast<unsigned>(Geometry::PropertyValueKind::Double) == 6u &&
                      static_cast<unsigned>(Geometry::PropertyValueKind::Vec2) == 7u &&
                      static_cast<unsigned>(Geometry::PropertyValueKind::Vec3) == 8u &&
                      static_cast<unsigned>(Geometry::PropertyValueKind::Vec4) == 9u);

        [[nodiscard]] json EncodePropertyRef(const Runtime::GeometryPropertyRef& ref)
        {
            return {{"domain", Runtime::ToString(ref.Domain)}, {"name", ref.Name},
                    {"kind", static_cast<unsigned>(ref.ValueKind)}};
        }

        void ReadPropertyRef(ValidationContext& context, const json& object,
            const char* key, Runtime::GeometryPropertyRef& ref, const bool chooseDomain = false, const bool chooseKind = false)
        {
            const auto* value = FindMember(object, key);
            if (!value) return;
            if (chooseDomain && value->is_object() && value->contains("domain"))
            {
                for (unsigned i = 1; i <= static_cast<unsigned>(Runtime::GeometryElementDomain::PointCloudPoint); ++i)
                    if (value->at("domain") == Runtime::ToString(static_cast<Runtime::GeometryElementDomain>(i)))
                        ref.Domain = static_cast<Runtime::GeometryElementDomain>(i);
            }
            if (chooseKind && value->is_object() && value->contains("kind") &&
                value->at("kind").is_number_unsigned() &&
                value->at("kind").get<std::uint64_t>() <= static_cast<unsigned>(Geometry::PropertyValueKind::Vec4))
                ref.ValueKind = static_cast<Geometry::PropertyValueKind>(value->at("kind").get<unsigned>());
            const auto expected = EncodePropertyRef(ref);
            if (!value->is_object() || value->size() != 3 || !value->contains("domain") ||
                value->at("domain") != expected["domain"] || !value->contains("kind") ||
                value->at("kind") != expected["kind"] || !value->contains("name") || !value->at("name").is_string())
            {
                if (context.Result)
                {
                    context.Result->State = Core::Config::EngineConfigState::Invalid;
                    context.Result->Diagnostics.push_back({.Code=Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        .Subject=FieldSubject(context.Path, key), .Message="Invalid typed property binding."});
                }
                return;
            }
            ref.Name = value->at("name").get<std::string>();
            CountParsed(context);
        }

        [[nodiscard]] ClusteringConfig ParseClustering(
            const std::string_view payload,
            ClusteringConfig config,
            ValidationContext context)
        {
            const std::optional<json> object = ParseObject(context, payload);
            if (!object.has_value())
                return config;

            AddUnknownFieldDiagnostics(context, *object, kClusteringFields);
            if (const auto value = ReadInteger(context, *object, "cluster_count", kClusteringFields))
            {
                config.Parameters.ClusterCount =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "gpu_preview_interval", kClusteringFields))
            {
                config.Parameters.GpuPreviewInterval = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "max_iterations", kClusteringFields))
            {
                config.Parameters.MaxIterations =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "seed", kClusteringFields))
            {
                config.Parameters.Seed =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (ReadEnum(
                    context,
                    *object,
                    "initialization",
                    ParseKMeansInitialization,
                    config.Parameters.Initialization))
            {
                CountParsed(context);
            }
            if (ReadEnum(
                    context,
                    *object,
                    "backend",
                    ParseClusteringBackend,
                    config.Backend))
            {
                CountParsed(context);
            }
            if (const auto* bindings = FindMember(*object, "properties"))
            {
                if (bindings->is_null()) config.Properties.reset();
                else
                {
                    auto properties = config.Properties.value_or(KMeansPropertyRefs{});
                    ReadPropertyRef(context, *bindings, "positions", properties.InputPositions, true);
                    ReadPropertyRef(context, *bindings, "labels", properties.OutputLabels, true, true);
                    ReadPropertyRef(context, *bindings, "colors", properties.OutputColors, true);
                    if (bindings->contains("scalar_labels") && !bindings->at("scalar_labels").is_null())
                    {
                        properties.OutputScalarLabels = Runtime::GeometryPropertyRef{
                            properties.InputPositions.Domain, "v:kmeans_scalar_label", Geometry::PropertyValueKind::Float};
                        ReadPropertyRef(context, *bindings, "scalar_labels", *properties.OutputScalarLabels, true, true);
                    }
                    else properties.OutputScalarLabels.reset();
                    const bool valid = bindings->is_object() && IsValidKMeansPropertyBindings(properties);
                    if (!valid && context.Result)
                    {
                        context.Result->State = Core::Config::EngineConfigState::Invalid;
                        context.Result->Diagnostics.push_back({.Code=Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                            .Subject=context.Path, .Message="K-Means requires distinct properties on one element domain."});
                    }
                    config.Properties = std::move(properties);
                }
            }
            return config;
        }

        [[nodiscard]] CurvatureSegmentationConfig
        ParseCurvatureSegmentation(
            const std::string_view payload,
            CurvatureSegmentationConfig config,
            ValidationContext context)
        {
            const CurvatureSegmentationConfig reference = config;
            const std::optional<json> object = ParseObject(context, payload);
            if (!object.has_value())
                return config;

            AddUnknownFieldDiagnostics(context, *object, kCurvatureSegmentationFields);

            if (ReadEnum(
                    context,
                    *object,
                    "method",
                    ParseCurvatureSegmentationMethod,
                    config.Method))
            {
                CountParsed(context);
            }

            if (ReadEnum(
                    context,
                    *object,
                    "selection_mode",
                    ParseCurvatureSegmentationSelectionMode,
                    config.SelectionMode))
            {
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "fixed_component_count", kCurvatureSegmentationFields))
            {
                config.FixedComponentCount =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "feature_base_radius_ratio", kCurvatureSegmentationFields))
            {
                config.FeatureBaseRadiusRatio = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "hard_dihedral_threshold_degrees", kCurvatureSegmentationFields))
            {
                config.HardDihedralThresholdDegrees = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "patch_complexity_cost", kCurvatureSegmentationFields))
            {
                config.PatchComplexityCost = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "automatic_min_components", kCurvatureSegmentationFields))
            {
                config.AutomaticMinComponents =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "automatic_max_components", kCurvatureSegmentationFields))
            {
                config.AutomaticMaxComponents =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "automatic_fit_tolerance", kCurvatureSegmentationFields))
            {
                config.AutomaticFitTolerance = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "automatic_complexity_weight", kCurvatureSegmentationFields))
            {
                config.AutomaticComplexityWeight = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "max_em_iterations", kCurvatureSegmentationFields))
            {
                config.MaxEmIterations =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "em_relative_tolerance", kCurvatureSegmentationFields))
            {
                config.EmRelativeTolerance = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "covariance_floor", kCurvatureSegmentationFields))
            {
                config.CovarianceFloor = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "seed", kCurvatureSegmentationFields))
            {
                config.Seed = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "spatial_weight", kCurvatureSegmentationFields))
            {
                config.SpatialWeight = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "feature_sensitivity", kCurvatureSegmentationFields))
            {
                config.FeatureSensitivity = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "max_spatial_iterations", kCurvatureSegmentationFields))
            {
                config.MaxSpatialIterations =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "minimum_region_faces", kCurvatureSegmentationFields))
            {
                config.MinimumRegionFaces =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }

            if (config.AutomaticMaxComponents <
                config.AutomaticMinComponents)
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, "automatic_max_components"),
                    "Automatic maximum components must be greater than or equal to the minimum; reference bounds retained.");
                config.AutomaticMinComponents =
                    reference.AutomaticMinComponents;
                config.AutomaticMaxComponents =
                    reference.AutomaticMaxComponents;
            }
            if (const auto* features = FindMember(*object, "features"))
            {
                config.Features.clear();
                if (!features->is_array() || features->size() > FindConfigFieldSpec(kCurvatureSegmentationFields, "features")->MaxItems.value())
                {
                    config.Features.emplace_back();
                }
                else
                {
                    for (const auto& item : *features)
                    {
                        GeometryPropertyRef ref{};
                        ReadPropertyRef(context, json{{"features", item}}, "features", ref, true, true);
                        config.Features.push_back(std::move(ref));
                    }
                }
            }
            ReadPropertyRef(context, *object, "positions", config.Positions);
            ReadPropertyRef(context, *object, "components", config.Components, false, true);
            ReadPropertyRef(context, *object, "regions", config.Regions, false, true);
            ReadPropertyRef(context, *object, "region_colors", config.RegionColors);
            ReadPropertyRef(context, *object, "boundaries", config.Boundaries, false, true);
            ReadPropertyRef(context, *object, "boundary_colors", config.BoundaryColors);
            ReadPropertyRef(context, *object, "hard_features", config.HardFeatures, false, true);
            ReadPropertyRef(context, *object, "feature_confidence", config.FeatureConfidence, false, true);
            ReadPropertyRef(context, *object, "boundary_roles", config.BoundaryRoles, false, true);
            ReadPropertyRef(context, *object, "feature_colors", config.FeatureColors);
            if (!Runtime::IsValidCurvatureSegmentationConfig(config) && context.Result)
            {
                context.Result->State = Core::Config::EngineConfigState::Invalid;
                context.Result->Diagnostics.push_back({.Code=Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    .Subject=context.Path, .Message="Segmentation requires distinct typed outputs and at most three numeric feature channels; feature-curve methods require computed curvature."});
            }
            return config;
        }

        [[nodiscard]] PointCloudConsolidationConfig
        ParsePointCloudConsolidation(
            const std::string_view payload,
            PointCloudConsolidationConfig config,
            ValidationContext context)
        {
            const PointCloudConsolidationConfig reference = config;
            const std::optional<json> object = ParseObject(context, payload);
            if (!object.has_value())
                return config;

            // The shared "initial_*" sampling block is checked by its own field specs.
            json plain = json::object();
            json sampling = json::object();
            for (const auto& [key, value] : object->items())
                (key.starts_with("initial_") && key != "initial_backend" ? sampling : plain)[key] = value;
            if (!sampling.empty())
            {
                json merged = json::object();
                ConfigDetail::EncodePointSampling(merged, "initial_", config.InitialSampling);
                PointSamplingConfig parsed = config.InitialSampling;
                std::optional<std::string> error = ConfigDetail::ValidateDeclaredFields(
                    sampling, merged, PointSamplingFieldSpecs("initial_"), "Initial sampling must be an object.",
                    "Unknown initial sampling field: ");
                if (!error)
                {
                    ConfigDetail::DecodePointSampling(merged, "initial_", parsed);
                    error = ValidatePointSamplingConfig(parsed);
                }
                if (error)
                    AddWarning(context, Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                               FieldSubject(context.Path, "initial_method"),
                               *error + " Reference default retained.");
                else
                {
                    config.InitialSampling = parsed;
                    for (std::size_t k = 0; k < sampling.size(); ++k) CountParsed(context);
                }
            }

            AddUnknownFieldDiagnostics(context, plain, kPointCloudConsolidationFields);

            if (ReadEnum(
                    context,
                    *object,
                    "backend",
                    ParsePointCloudConsolidationBackend,
                    config.Backend))
            {
                CountParsed(context);
            }

            if (ReadEnum(
                    context,
                    *object,
                    "initial_backend",
                    [](std::string_view text) -> std::optional<PointSamplingBackend> {
                        if (text == kInitialBackendTokens[0]) return PointSamplingBackend::Cpu;
                        if (text == kInitialBackendTokens[1]) return PointSamplingBackend::Vulkan;
                        return std::nullopt;
                    },
                    config.InitialBackend))
            {
                CountParsed(context);
            }
            if (ReadEnum(
                    context,
                    *object,
                    "strategy",
                    ParsePointCloudConsolidationStrategy,
                    config.Strategy))
            {
                CountParsed(context);
            }
            if (ReadEnum(
                    context,
                    *object,
                    "support_radius_mode",
                    ParsePointCloudConsolidationSupportRadiusMode,
                    config.SupportRadiusMode))
            {
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "support_radius", kPointCloudConsolidationFields))
            {
                config.SupportRadius = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "max_support_neighbors", kPointCloudConsolidationFields))
            {
                config.MaxSupportNeighbors =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "max_predicted_contributions", kPointCloudConsolidationFields))
            {
                config.MaxPredictedContributions =
                    static_cast<std::uint64_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "repulsion_weight", kPointCloudConsolidationFields))
            {
                config.RepulsionWeight = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "max_iterations", kPointCloudConsolidationFields))
            {
                config.MaxIterations = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "convergence_tolerance", kPointCloudConsolidationFields))
            {
                config.ConvergenceTolerance = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "target_point_count", kPointCloudConsolidationFields))
            {
                if (*value == 1)
                {
                    AddWarning(
                        context,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        FieldSubject(context.Path, "target_point_count"),
                        "Target count must be zero or at least two; reference default retained.");
                }
                else
                {
                    config.TargetPointCount =
                        static_cast<std::uint32_t>(*value);
                    CountParsed(context);
                }
            }
            if (const auto value = ReadInteger(context, *object, "seed", kPointCloudConsolidationFields))
            {
                config.Seed = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "gpu_preview_interval", kPointCloudConsolidationFields))
            { config.GpuPreviewInterval = static_cast<std::uint32_t>(*value); CountParsed(context); }
            if (const auto value = ReadInteger(context, *object, "gpu_query_batch_size", kPointCloudConsolidationFields))
            { config.GpuQueryBatchSize = static_cast<std::uint32_t>(*value); CountParsed(context); }
            if (const auto value = ReadInteger(context, *object, "gpu_radius_capacity", kPointCloudConsolidationFields))
            { config.GpuRadiusCapacity = static_cast<std::uint32_t>(*value); CountParsed(context); }
            if (const auto value = ReadBool(
                    context, *object, "wlop_anisotropic"))
            {
                config.WlopAnisotropic = *value;
                CountParsed(context);
            }
            if (ReadEnum(
                    context,
                    *object,
                    "normal_source",
                    ParsePointCloudConsolidationNormalSource,
                    config.NormalSource))
            {
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "normal_angle_radians", kPointCloudConsolidationFields))
            {
                config.NormalAngleRadians = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "normal_refinement_rounds", kPointCloudConsolidationFields))
            {
                config.NormalRefinementRounds =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "clop_mixture_component_count", kPointCloudConsolidationFields))
            {
                config.ClopMixtureComponentCount =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "clop_mixture_max_iterations", kPointCloudConsolidationFields))
            {
                config.ClopMixtureMaxIterations =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "clop_mixture_relative_tolerance", kPointCloudConsolidationFields))
            {
                config.ClopMixtureRelativeTolerance = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "clop_covariance_floor", kPointCloudConsolidationFields))
            {
                config.ClopCovarianceFloor = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "ear_edge_sensitivity", kPointCloudConsolidationFields))
            {
                config.EarEdgeSensitivity = *value;
                CountParsed(context);
            }

            if ((config.Strategy == PointCloudConsolidationStrategy::Ear ||
                 (config.Strategy == PointCloudConsolidationStrategy::Wlop && config.WlopAnisotropic)) &&
                config.NormalRefinementRounds > config.MaxIterations)
            {
                AddWarning(
                    context,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    FieldSubject(context.Path, "normal_refinement_rounds"),
                    "Normal refinement rounds exceed max_iterations; reference-compatible value retained.");
                config.NormalRefinementRounds = std::min(
                    config.MaxIterations,
                    std::max(1u, reference.NormalRefinementRounds));
            }
            return config;
        }

        [[nodiscard]] ProgressivePoissonPlaygroundConfig ParseProgressivePoisson(
            const std::string_view payload,
            ProgressivePoissonPlaygroundConfig config,
            ValidationContext context)
        {
            const std::optional<json> object = ParseObject(context, payload);
            if (!object.has_value())
            {
                return config;
            }

            AddUnknownFieldDiagnostics(context, *object, kProgressivePoissonFields);

            if (const auto value = ReadInteger(context, *object, "dimension", kProgressivePoissonFields))
            {
                config.Dimension = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value =
                    ReadInteger(context, *object, "grid_width", kProgressivePoissonFields))
            {
                config.GridWidth = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value =
                    ReadInteger(context, *object, "max_levels", kProgressivePoissonFields))
            {
                config.MaxLevels = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value =
                    ReadNumber(context, *object, "hash_load_factor", kProgressivePoissonFields))
            {
                config.HashLoadFactor = *value;
                CountParsed(context);
            }
            if (const auto value =
                    ReadNumber(context, *object, "radius_alpha", kProgressivePoissonFields))
            {
                config.RadiusAlpha = *value;
                CountParsed(context);
            }
            if (const auto value =
                    ReadBool(context, *object, "randomize_grid_origin"))
            {
                config.RandomizeGridOrigin = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "grid_origin_seed", kProgressivePoissonFields))
            {
                config.GridOriginSeed = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value =
                    ReadBool(context, *object, "shuffle_within_levels"))
            {
                config.ShuffleWithinLevels = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "shuffle_seed", kProgressivePoissonFields))
            {
                config.ShuffleSeed = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "prefix_count", kProgressivePoissonFields))
            {
                config.PrefixCount = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (ReadEnum(
                    context,
                    *object,
                    "channel",
                    ParseProgressivePoissonChannel,
                    config.Channel))
            {
                CountParsed(context);
            }
            if (ReadEnum(
                    context,
                    *object,
                    "backend",
                    ParseProgressivePoissonBackend,
                    config.Backend))
            {
                CountParsed(context);
            }
            if (const auto value =
                    ReadBool(context, *object, "auto_run_on_edit"))
            {
                config.AutoRunOnEdit = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(context, *object, "debounce_seconds", kProgressivePoissonFields))
            {
                config.DebounceSeconds = *value;
                CountParsed(context);
            }
            ReadPropertyRef(context, *object, "positions", config.Positions, true);
            ReadPropertyRef(context, *object, "level_property", config.Level, true, true);
            ReadPropertyRef(context, *object, "rank_property", config.Rank, true, true);
            ReadPropertyRef(context, *object, "splat_radius_property", config.SplatRadius, true, true);
            ReadPropertyRef(context, *object, "prefix_visible_property", config.PrefixVisible, true, true);
            if (!IsValidProgressivePoissonPropertyBindings(config) && context.Result)
            {
                context.Result->State = Core::Config::EngineConfigState::Invalid;
                context.Result->Diagnostics.push_back({.Code=Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    .Subject=context.Path, .Message="Progressive Poisson requires distinct properties on one element domain."});
            }
            return config;
        }

        [[nodiscard]] ParameterizationConfig ParseParameterization(
            const std::string_view payload,
            ParameterizationConfig config,
            ValidationContext context)
        {
            const std::optional<json> object = ParseObject(context, payload);
            if (!object.has_value())
            {
                return config;
            }

            AddUnknownFieldDiagnostics(context, *object, kParameterizationFields);
            const ParameterizationLscmConfig referenceLscm = config.Lscm;
            const ParameterizationBffConfig referenceBff = config.Bff;

            if (ReadEnum(
                    context,
                    *object,
                    "strategy",
                    ParseParameterizationStrategy,
                    config.Strategy))
            {
                CountParsed(context);
            }

            if (const json* view = FindMember(*object, "view"); view != nullptr)
            {
                ValidationContext viewContext = ChildContext(context, "view");
                if (!view->is_object())
                {
                    AddWarning(
                        viewContext,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        viewContext.Path,
                        "Expected an object; reference parameterization view config retained.");
                }
                else
                {
                    AddUnknownFieldDiagnostics(viewContext, *view, kParameterizationViewFields);
                    if (ReadEnum(
                            viewContext,
                            *view,
                            "render_mode",
                            ParseParameterizationUvRenderMode,
                            config.View.RenderMode))
                    {
                        CountParsed(viewContext);
                    }
                    if (ReadEnum(
                            viewContext,
                            *view,
                            "background_mode",
                            ParseParameterizationUvBackgroundMode,
                            config.View.BackgroundMode))
                    {
                        CountParsed(viewContext);
                    }
                    if (const auto value = ReadBool(
                            viewContext, *view, "show_distortion_heatmap"))
                    {
                        config.View.ShowDistortionHeatmap = *value;
                        CountParsed(viewContext);
                    }
                    if (const auto value = ReadBool(viewContext, *view, "split_enabled")) config.View.SplitEnabled = *value;
                    if (const auto value = ReadBool(viewContext, *view, "atlas_on_left")) config.View.AtlasOnLeft = *value;
                    if (const auto value = ReadNumber(viewContext, *view, "split_ratio", kParameterizationViewFields))
                        config.View.SplitRatio = static_cast<float>(*value);
                    else if (FindMember(*view, "split_ratio") && context.Result)
                    {
                        context.Result->State = Core::Config::EngineConfigState::Invalid;
                        context.Result->Diagnostics.push_back({.Code = Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                            .Subject = viewContext.Path, .Message = "Scene split ratio must be finite and in [0.2, 0.8]."});
                    }
                }
            }

            if (const json* atlas = FindMember(*object, "atlas"); atlas != nullptr)
            {
                auto atlasContext = ChildContext(context, "atlas");
                const auto invalid = [&](const std::string& message)
                {
                    if (context.Result)
                    {
                        context.Result->State = Core::Config::EngineConfigState::Invalid;
                        context.Result->Diagnostics.push_back({
                            .Code = Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                            .Subject = atlasContext.Path, .Message = message});
                    }
                };
                if (!atlas->is_object()) invalid("Atlas configuration must be an object.");
                else
                {
                    AddUnknownFieldDiagnostics(atlasContext, *atlas, kParameterizationAtlasFields);
                    if (FindMember(*atlas, "method") && !ReadEnum(atlasContext, *atlas, "method", ParseAtlasMethod, config.Atlas.Method))
                        invalid("Atlas method must be fast_staged or xatlas.");
                    if (FindMember(*atlas, "distortion") && !ReadEnum(atlasContext, *atlas, "distortion", ParseAtlasDistortion, config.Atlas.Distortion))
                        invalid("Atlas distortion must be none, angle, area or both.");
                    const auto integer = [&](const char* key, std::uint32_t& target)
                    {
                        if (!FindMember(*atlas, key)) return;
                        if (const auto value = ReadInteger(atlasContext, *atlas, key, kParameterizationAtlasFields))
                        { target = static_cast<std::uint32_t>(*value); CountParsed(atlasContext); }
                        else invalid(std::string{"Invalid atlas "} + key + ".");
                    };
                    integer("region_count", config.Atlas.RegionCount);
                    integer("resolution", config.Atlas.Resolution);
                    integer("padding", config.Atlas.Padding);
                    integer("max_charts", config.Atlas.MaxCharts);
                    integer("max_iterations", config.Atlas.MaxIterations);
                    const auto number = [&](const char* key, auto& target)
                    {
                        if (!FindMember(*atlas, key)) return;
                        if (const auto value = ReadNumber(atlasContext, *atlas, key, kParameterizationAtlasFields))
                        { target = static_cast<std::remove_reference_t<decltype(target)>>(*value); CountParsed(atlasContext); }
                        else invalid(std::string{"Invalid atlas "} + key + ".");
                    };
                    number("texels_per_unit", config.Atlas.TexelsPerUnit);
                    number("max_conformal_distortion", config.Atlas.MaxConformalDistortion);
                    number("max_area_distortion", config.Atlas.MaxAreaDistortion);
                    if (FindMember(*atlas, "allow_xatlas_fallback"))
                    {
                        if (const auto value = ReadBool(atlasContext, *atlas, "allow_xatlas_fallback"))
                            config.Atlas.AllowXAtlasFallback = *value;
                        else invalid("Atlas fallback setting must be boolean.");
                    }
                    if (const auto* guide = FindMember(*atlas, "guide"))
                    {
                        config.Atlas.Guide.reset();
                        if (!guide->is_null())
                        {
                            GeometryPropertyRef ref{};
                            ReadPropertyRef(atlasContext, *atlas, "guide", ref, true, true);
                            config.Atlas.Guide = std::move(ref);
                        }
                    }
                    if (const auto error = ValidateParameterizationAtlasConfig(config.Atlas)) invalid(*error);
                }
            }

            if (const json* lscm = FindMember(*object, "lscm"); lscm != nullptr)
            {
                ValidationContext lscmContext = ChildContext(context, "lscm");
                if (!lscm->is_object())
                {
                    AddWarning(
                        lscmContext,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        lscmContext.Path,
                        "Expected an object; reference LSCM config retained.");
                }
                else
                {
                    AddUnknownFieldDiagnostics(lscmContext, *lscm, kParameterizationLscmFields);
                    if (const auto value =
                            ReadBool(lscmContext, *lscm, "auto_pins"))
                    {
                        config.Lscm.AutoPins = *value;
                        CountParsed(lscmContext);
                    }
                    if (const auto value = ReadInteger(lscmContext, *lscm, "pin_vertex_0", kParameterizationLscmFields))
                    {
                        config.Lscm.PinVertex0 =
                            static_cast<std::uint32_t>(*value);
                        CountParsed(lscmContext);
                    }
                    if (const auto value = ReadInteger(lscmContext, *lscm, "pin_vertex_1", kParameterizationLscmFields))
                    {
                        config.Lscm.PinVertex1 =
                            static_cast<std::uint32_t>(*value);
                        CountParsed(lscmContext);
                    }
                    if (const auto value =
                            ReadUv(lscmContext, *lscm, "pin_uv_0"))
                    {
                        config.Lscm.PinUv0 = *value;
                        CountParsed(lscmContext);
                    }
                    if (const auto value =
                            ReadUv(lscmContext, *lscm, "pin_uv_1"))
                    {
                        config.Lscm.PinUv1 = *value;
                        CountParsed(lscmContext);
                    }
                    if (const auto value = ReadNumber(lscmContext, *lscm, "solver_tolerance", kParameterizationLscmFields))
                    {
                        config.Lscm.SolverTolerance = *value;
                        CountParsed(lscmContext);
                    }
                    if (const auto value = ReadInteger(lscmContext, *lscm, "max_solver_iterations", kParameterizationLscmFields))
                    {
                        config.Lscm.MaxSolverIterations =
                            static_cast<std::uint32_t>(*value);
                        CountParsed(lscmContext);
                    }
                }
            }

            if (const json* harmonic = FindMember(*object, "harmonic");
                harmonic != nullptr)
            {
                ValidationContext harmonicContext =
                    ChildContext(context, "harmonic");
                if (!harmonic->is_object())
                {
                    AddWarning(
                        harmonicContext,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        harmonicContext.Path,
                        "Expected an object; reference harmonic config retained.");
                }
                else
                {
                    AddUnknownFieldDiagnostics(harmonicContext, *harmonic, kParameterizationHarmonicFields);
                    if (ReadEnum(
                            harmonicContext,
                            *harmonic,
                            "boundary",
                            ParseParameterizationBoundaryPolicy,
                            config.Harmonic.Boundary))
                    {
                        CountParsed(harmonicContext);
                    }
                    if (const auto value = ReadBool(
                            harmonicContext, *harmonic, "arc_length_spacing"))
                    {
                        config.Harmonic.ArcLengthSpacing = *value;
                        CountParsed(harmonicContext);
                    }
                    if (const auto value = ReadBool(
                            harmonicContext,
                            *harmonic,
                            "clamp_non_convex_weights"))
                    {
                        config.Harmonic.ClampNonConvexWeights = *value;
                        CountParsed(harmonicContext);
                    }

                    const bool hasVertices =
                        FindMember(*harmonic, "pinned_vertices") != nullptr;
                    const bool hasUvs =
                        FindMember(*harmonic, "pinned_uvs") != nullptr;
                    const auto vertices = ReadIndexArray(
                        harmonicContext, *harmonic, "pinned_vertices");
                    const auto uvs =
                        ReadUvArray(harmonicContext, *harmonic, "pinned_uvs");
                    if (hasVertices != hasUvs)
                    {
                        AddWarning(
                            harmonicContext,
                            Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                            harmonicContext.Path,
                            "Pinned vertex and UV arrays must be provided together; reference arrays retained.");
                    }
                    else if (hasVertices && vertices.has_value() &&
                             uvs.has_value())
                    {
                        if (vertices->size() != uvs->size())
                        {
                            AddWarning(
                                harmonicContext,
                                Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                                harmonicContext.Path,
                                "Pinned vertex and UV arrays must have equal length; reference arrays retained.");
                        }
                        else
                        {
                            config.Harmonic.PinnedVertices = *vertices;
                            config.Harmonic.PinnedUvs = *uvs;
                            CountParsed(harmonicContext, 2u);
                        }
                    }
                }
            }

            if (const json* bff = FindMember(*object, "bff"); bff != nullptr)
            {
                ValidationContext bffContext = ChildContext(context, "bff");
                if (!bff->is_object())
                {
                    AddWarning(
                        bffContext,
                        Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        bffContext.Path,
                        "Expected an object; reference BFF config retained.");
                }
                else
                {
                    AddUnknownFieldDiagnostics(bffContext, *bff, kParameterizationBffFields);
                    if (ReadEnum(
                            bffContext,
                            *bff,
                            "mode",
                            ParseParameterizationBffBoundaryMode,
                            config.Bff.Mode))
                    {
                        CountParsed(bffContext);
                    }
                    if (const auto value = ReadNumberArray(
                            bffContext, *bff, "boundary_data"))
                    {
                        config.Bff.BoundaryData = *value;
                        CountParsed(bffContext);
                    }
                    if (const auto value = ReadNumber(bffContext, *bff, "angle_sum_tolerance", kParameterizationBffFields))
                    {
                        config.Bff.AngleSumTolerance = *value;
                        CountParsed(bffContext);
                    }
                    if (const auto value = ReadNumber(bffContext, *bff, "degeneracy_tolerance", kParameterizationBffFields))
                    {
                        config.Bff.DegeneracyTolerance = *value;
                        CountParsed(bffContext);
                    }
                }
            }

            if (!config.Lscm.AutoPins &&
                config.Lscm.PinVertex0 == config.Lscm.PinVertex1)
            {
                ValidationContext lscmContext = ChildContext(context, "lscm");
                AddWarning(
                    lscmContext,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    lscmContext.Path,
                    "Manual LSCM pins must select distinct vertices; reference LSCM config retained.");
                config.Lscm = referenceLscm;
            }

            bool bffValid = true;
            std::string_view bffMessage{};
            switch (config.Bff.Mode)
            {
            case ParameterizationBffBoundaryMode::AutomaticConformal:
                if (!config.Bff.BoundaryData.empty())
                {
                    bffValid = false;
                    bffMessage =
                        "Automatic BFF mode requires empty boundary data; reference BFF config retained.";
                }
                break;
            case ParameterizationBffBoundaryMode::TargetLengths:
                if (config.Bff.BoundaryData.empty())
                {
                    bffValid = false;
                    bffMessage =
                        "Target-length BFF mode requires boundary data; reference BFF config retained.";
                }
                else if (!std::all_of(
                             config.Bff.BoundaryData.begin(),
                             config.Bff.BoundaryData.end(),
                             [](const double value) { return value > 0.0; }))
                {
                    bffValid = false;
                    bffMessage =
                        "Target BFF boundary lengths must be positive; reference BFF config retained.";
                }
                break;
            case ParameterizationBffBoundaryMode::TargetAngles:
                if (config.Bff.BoundaryData.empty())
                {
                    bffValid = false;
                    bffMessage =
                        "Target-angle BFF mode requires boundary data; reference BFF config retained.";
                }
                else
                {
                    double angleSum = 0.0;
                    for (const double angle : config.Bff.BoundaryData)
                    {
                        angleSum += angle;
                    }
                    if (!std::isfinite(angleSum) ||
                        std::abs(
                            angleSum - 2.0 * std::numbers::pi_v<double>) >
                            config.Bff.AngleSumTolerance)
                    {
                        bffValid = false;
                        bffMessage =
                            "Target BFF boundary angles must sum to 2*pi within the configured tolerance; reference BFF config retained.";
                    }
                }
                break;
            }
            if (!bffValid)
            {
                ValidationContext bffContext = ChildContext(context, "bff");
                AddWarning(
                    bffContext,
                    Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    bffContext.Path,
                    std::string{bffMessage});
                config.Bff = referenceBff;
            }
            ReadPropertyRef(context, *object, "positions", config.Positions);
            ReadPropertyRef(context, *object, "texcoords", config.Texcoords);
            if (const auto* corner = FindMember(*object, "corner_texcoords_to_retire"))
            {
                config.CornerTexcoordsToRetire.reset();
                if (!corner->is_null())
                {
                    GeometryPropertyRef ref{GeometryElementDomain::MeshHalfedge, "h:texcoord", Geometry::PropertyValueKind::Vec2};
                    ReadPropertyRef(context, *object, "corner_texcoords_to_retire", ref);
                    config.CornerTexcoordsToRetire = std::move(ref);
                }
            }
            if (config.CornerTexcoordsToRetire && context.Result)
            {
                const auto& ref = *config.CornerTexcoordsToRetire;
                if (ref.Name.empty() || ref.Name.find('\0') != std::string::npos ||
                    IsTopologyProperty(ref.Domain, ref.Name))
                {
                    context.Result->State = Core::Config::EngineConfigState::Invalid;
                    context.Result->Diagnostics.push_back({.Code=Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                        .Subject=context.Path, .Message="Corner UV retirement requires a non-structural halfedge vec2 property."});
                }
            }
            if ((config.Positions.Name.empty() || config.Positions.Name.find('\0') != std::string::npos ||
                 config.Texcoords.Name.empty() ||
                 IsStructuralVertexProperty(config.Texcoords.Name) ||
                 config.Positions.Name == config.Texcoords.Name || config.Texcoords.Name.find('\0') != std::string::npos) && context.Result)
            {
                context.Result->State = Core::Config::EngineConfigState::Invalid;
                context.Result->Diagnostics.push_back({.Code=Core::Config::EngineConfigDiagnosticCode::InvalidValue,
                    .Subject=context.Path, .Message="Parameterization requires distinct typed vertex properties."});
            }
            return config;
        }

        [[nodiscard]] json SerializeUv(const ParameterizationUvConfig& uv)
        {
            return json::array({uv.U, uv.V});
        }

        [[nodiscard]] json SerializeUvs(
            const std::vector<ParameterizationUvConfig>& uvs)
        {
            json values = json::array();
            for (const ParameterizationUvConfig& uv : uvs)
            {
                values.push_back(SerializeUv(uv));
            }
            return values;
        }

        [[nodiscard]] ProgressivePoissonPlaygroundConfig
        DecodeProgressivePoissonCanonical(const std::string_view payload)
        {
            return ParseProgressivePoisson(
                payload,
                ProgressivePoissonPlaygroundConfig{},
                ValidationContext{});
        }

        [[nodiscard]] ClusteringConfig DecodeClusteringCanonical(
            const std::string_view payload)
        {
            return ParseClustering(
                payload,
                ClusteringConfig{},
                ValidationContext{});
        }

        [[nodiscard]] CurvatureSegmentationConfig
        DecodeCurvatureSegmentationCanonical(
            const std::string_view payload)
        {
            return ParseCurvatureSegmentation(
                payload,
                CurvatureSegmentationConfig{},
                ValidationContext{});
        }

        [[nodiscard]] ParameterizationConfig DecodeParameterizationCanonical(
            const std::string_view payload)
        {
            return ParseParameterization(
                payload,
                ParameterizationConfig{},
                ValidationContext{});
        }

        [[nodiscard]] PointCloudConsolidationConfig
        DecodePointCloudConsolidationCanonical(
            const std::string_view payload)
        {
            return ParsePointCloudConsolidation(
                payload,
                PointCloudConsolidationConfig{},
                ValidationContext{});
        }
    }

    RunKMeans MakeConfiguredKMeansRequest(
        const std::uint32_t stableEntityId,
        KMeansPropertyRefs properties,
        const ClusteringConfig& config)
    {
        return RunKMeans{
            .StableEntityId = stableEntityId,
            .Properties = std::move(properties),
            .Parameters = config.Parameters,
            .Backend = config.Backend,
        };
    }

    RunKMeans MakeConfiguredKMeansRequest(
        const std::uint32_t stableEntityId, const ClusteringConfig& config)
    {
        return MakeConfiguredKMeansRequest(stableEntityId, config.Properties.value_or(KMeansPropertyRefs{}), config);
    }

    std::string SerializeClusteringConfig(
        const ClusteringConfig& config)
    {
        json properties = nullptr;
        if (config.Properties)
            properties = {{"positions", EncodePropertyRef(config.Properties->InputPositions)},
                          {"labels", EncodePropertyRef(config.Properties->OutputLabels)},
                          {"colors", EncodePropertyRef(config.Properties->OutputColors)},
                          {"scalar_labels", config.Properties->OutputScalarLabels
                              ? EncodePropertyRef(*config.Properties->OutputScalarLabels) : json(nullptr)}};
        return ConfigDetail::SerializeConfigJson(json::object({
            {"properties", properties},
            {"cluster_count", config.Parameters.ClusterCount},
            {"gpu_preview_interval", config.Parameters.GpuPreviewInterval},
            {"max_iterations", config.Parameters.MaxIterations},
            {"seed", config.Parameters.Seed},
            {"initialization",
             std::string{ToConfigString(config.Parameters.Initialization)}},
            {"backend", std::string{ToConfigString(config.Backend)}},
        }));
    }

    std::string SerializeCurvatureSegmentationConfig(
        const CurvatureSegmentationConfig& config)
    {
        json features = json::array();
        for (const auto& ref : config.Features) features.push_back(EncodePropertyRef(ref));
        return ConfigDetail::SerializeConfigJson(json::object({
            {"features", std::move(features)},
            {"positions", EncodePropertyRef(config.Positions)},
            {"components", EncodePropertyRef(config.Components)},
            {"regions", EncodePropertyRef(config.Regions)},
            {"region_colors", EncodePropertyRef(config.RegionColors)},
            {"boundaries", EncodePropertyRef(config.Boundaries)},
            {"boundary_colors", EncodePropertyRef(config.BoundaryColors)},
            {"hard_features", EncodePropertyRef(config.HardFeatures)},
            {"feature_confidence", EncodePropertyRef(config.FeatureConfidence)},
            {"boundary_roles", EncodePropertyRef(config.BoundaryRoles)},
            {"feature_colors", EncodePropertyRef(config.FeatureColors)},

            {"method", std::string{ToConfigString(config.Method)}},
            {"selection_mode",
             std::string{ToConfigString(config.SelectionMode)}},
            {"fixed_component_count", config.FixedComponentCount},
            {"automatic_min_components", config.AutomaticMinComponents},
            {"automatic_max_components", config.AutomaticMaxComponents},
            {"automatic_fit_tolerance", config.AutomaticFitTolerance},
            {"automatic_complexity_weight",
             config.AutomaticComplexityWeight},
            {"max_em_iterations", config.MaxEmIterations},
            {"em_relative_tolerance", config.EmRelativeTolerance},
            {"covariance_floor", config.CovarianceFloor},
            {"seed", config.Seed},
            {"spatial_weight", config.SpatialWeight},
            {"feature_sensitivity", config.FeatureSensitivity},
            {"max_spatial_iterations", config.MaxSpatialIterations},
            {"minimum_region_faces", config.MinimumRegionFaces},
            {"feature_base_radius_ratio", config.FeatureBaseRadiusRatio},
            {"hard_dihedral_threshold_degrees",
             config.HardDihedralThresholdDegrees},
            {"patch_complexity_cost", config.PatchComplexityCost},
        }));
    }

    bool IsValidProgressivePoissonPropertyBindings(
        const ProgressivePoissonPlaygroundConfig& config) noexcept
    {
        const std::array refs{&config.Positions, &config.Level, &config.Rank,
                              &config.SplatRadius, &config.PrefixVisible};
        for (std::size_t i = 0; i < refs.size(); ++i)
        {
            const auto& ref = *refs[i];
            if (!ref.HasName() || ref.Name.find('\0') != std::string::npos ||
                IsTopologyProperty(ref.Domain, ref.Name) ||
                (ref.Domain == GeometryElementDomain::Unknown && ref.Name == "v:deleted") ||
                ref.Domain > GeometryElementDomain::PointCloudPoint ||
                (i == 0 ? ref.ValueKind != Geometry::PropertyValueKind::Vec3
                        : GeometryPropertyComponentCount(ref.ValueKind) != 1))
                return false;
            if (i != 0 && ref.Domain != GeometryElementDomain::Unknown &&
                ref.Domain != config.Positions.Domain)
                return false;
            for (std::size_t j = 0; j < i; ++j)
                if (ref.Name == refs[j]->Name) return false;
        }
        return true;
    }

    std::string SerializeProgressivePoissonPlaygroundConfig(
        const ProgressivePoissonPlaygroundConfig& config)
    {
        return ConfigDetail::SerializeConfigJson(json::object({
            {"positions", EncodePropertyRef(config.Positions)},
            {"level_property", EncodePropertyRef(config.Level)},
            {"rank_property", EncodePropertyRef(config.Rank)},
            {"splat_radius_property", EncodePropertyRef(config.SplatRadius)},
            {"prefix_visible_property", EncodePropertyRef(config.PrefixVisible)},

            {"dimension", config.Dimension},
            {"grid_width", config.GridWidth},
            {"max_levels", config.MaxLevels},
            {"hash_load_factor", config.HashLoadFactor},
            {"radius_alpha", config.RadiusAlpha},
            {"randomize_grid_origin", config.RandomizeGridOrigin},
            {"grid_origin_seed", config.GridOriginSeed},
            {"shuffle_within_levels", config.ShuffleWithinLevels},
            {"shuffle_seed", config.ShuffleSeed},
            {"prefix_count", config.PrefixCount},
            {"channel", std::string{ToConfigString(config.Channel)}},
            {"backend", std::string{ToConfigString(config.Backend)}},
            {"auto_run_on_edit", config.AutoRunOnEdit},
            {"debounce_seconds", config.DebounceSeconds},
        }));
    }

    std::optional<std::string> ValidateParameterizationAtlasConfig(const ParameterizationAtlasConfig& config)
    {
        using M = Geometry::UvAtlas::UvAtlasMethod;
        using D = Geometry::UvAtlas::UvAtlasDistortion;
        if (config.Method != M::FastStaged && config.Method != M::XAtlas)
            return "Select fast_staged or xatlas atlas generation.";
        if (config.Distortion != D::None && config.Distortion != D::Angle &&
            config.Distortion != D::Area && config.Distortion != D::Both)
            return "Select none, angle, area or both distortion objectives.";
        if (config.Method == M::XAtlas && config.Distortion != D::Angle)
            return "XAtlas supports the angle objective; select fast_staged for none, area or both.";
        if (!ConfigDetail::InConfigFieldRange(kParameterizationAtlasFields, "resolution", config.Resolution) ||
            !ConfigDetail::InConfigFieldRange(kParameterizationAtlasFields, "padding", config.Padding) ||
            config.Padding * 2u + 1u >= config.Resolution)
            return "Atlas resolution must be 16..16384 with at most 32 padding texels and usable interior space.";
        if (!ConfigDetail::InConfigFieldRange(kParameterizationAtlasFields, "texels_per_unit", config.TexelsPerUnit))
            return "Atlas texel density must be finite and non-negative.";
        if (!ConfigDetail::InConfigFieldRange(kParameterizationAtlasFields, "max_conformal_distortion", config.MaxConformalDistortion) ||
            !ConfigDetail::InConfigFieldRange(kParameterizationAtlasFields, "max_area_distortion", config.MaxAreaDistortion))
            return "Atlas distortion limits must be finite and at least one (at most 1e6).";
        if (!ConfigDetail::InConfigFieldRange(kParameterizationAtlasFields, "max_charts", config.MaxCharts) ||
            !ConfigDetail::InConfigFieldRange(kParameterizationAtlasFields, "max_iterations", config.MaxIterations) ||
            !ConfigDetail::InConfigFieldRange(kParameterizationAtlasFields, "region_count", config.RegionCount))
            return "Atlas budgets require 1..1000000 charts, 1..1000 iterations and 0..64 guide components (zero selects automatically).";
        if (config.Guide)
        {
            const auto& ref = *config.Guide;
            if ((ref.Domain != GeometryElementDomain::MeshVertex && ref.Domain != GeometryElementDomain::MeshFace) ||
                GeometryPropertyComponentCount(ref.ValueKind) != 1u || ref.ValueKind == Geometry::PropertyValueKind::Unknown ||
                ref.Name.empty() || ref.Name.find('\0') != std::string::npos)
                return "Atlas guide must name a scalar mesh vertex or mesh face property.";
        }
        return std::nullopt;
    }

    std::string SerializeParameterizationConfig(
        const ParameterizationConfig& config)
    {
        return ConfigDetail::SerializeConfigJson(json::object({
            {"positions", EncodePropertyRef(config.Positions)},
            {"texcoords", EncodePropertyRef(config.Texcoords)},
            {"corner_texcoords_to_retire", config.CornerTexcoordsToRetire
                ? EncodePropertyRef(*config.CornerTexcoordsToRetire) : json(nullptr)},
            {"strategy", std::string{ToConfigString(config.Strategy)}},
            {"atlas", json::object({
                {"method", std::string{ToConfigString(config.Atlas.Method)}},
                {"distortion", std::string{ToConfigString(config.Atlas.Distortion)}},
                {"guide", config.Atlas.Guide ? EncodePropertyRef(*config.Atlas.Guide) : json(nullptr)},
                {"region_count", config.Atlas.RegionCount}, {"resolution", config.Atlas.Resolution},
                {"padding", config.Atlas.Padding}, {"texels_per_unit", config.Atlas.TexelsPerUnit},
                {"max_conformal_distortion", config.Atlas.MaxConformalDistortion},
                {"max_area_distortion", config.Atlas.MaxAreaDistortion},
                {"max_charts", config.Atlas.MaxCharts}, {"max_iterations", config.Atlas.MaxIterations},
                {"allow_xatlas_fallback", config.Atlas.AllowXAtlasFallback},
            })},
            {"view",
             json::object({
                 {"render_mode",
                  std::string{ToConfigString(config.View.RenderMode)}},
                 {"background_mode",
                  std::string{ToConfigString(config.View.BackgroundMode)}},
                 {"show_distortion_heatmap",
                  config.View.ShowDistortionHeatmap},
                 {"split_enabled", config.View.SplitEnabled},
                 {"atlas_on_left", config.View.AtlasOnLeft},
                 {"split_ratio", config.View.SplitRatio},
             })},
            {"lscm",
             json::object({
                 {"auto_pins", config.Lscm.AutoPins},
                 {"pin_vertex_0", config.Lscm.PinVertex0},
                 {"pin_vertex_1", config.Lscm.PinVertex1},
                 {"pin_uv_0", SerializeUv(config.Lscm.PinUv0)},
                 {"pin_uv_1", SerializeUv(config.Lscm.PinUv1)},
                 {"solver_tolerance", config.Lscm.SolverTolerance},
                 {"max_solver_iterations", config.Lscm.MaxSolverIterations},
             })},
            {"harmonic",
             json::object({
                 {"boundary",
                  std::string{ToConfigString(config.Harmonic.Boundary)}},
                 {"arc_length_spacing", config.Harmonic.ArcLengthSpacing},
                 {"clamp_non_convex_weights",
                  config.Harmonic.ClampNonConvexWeights},
                 {"pinned_vertices", config.Harmonic.PinnedVertices},
                 {"pinned_uvs", SerializeUvs(config.Harmonic.PinnedUvs)},
             })},
            {"bff",
             json::object({
                 {"mode", std::string{ToConfigString(config.Bff.Mode)}},
                 {"boundary_data", config.Bff.BoundaryData},
                 {"angle_sum_tolerance", config.Bff.AngleSumTolerance},
                 {"degeneracy_tolerance", config.Bff.DegeneracyTolerance},
             })},
        }));
    }

    std::string SerializePointCloudConsolidationConfig(
        const PointCloudConsolidationConfig& config)
    {
        json doc = json::object({
            {"gpu_preview_interval", config.GpuPreviewInterval},
            {"gpu_query_batch_size", config.GpuQueryBatchSize},
            {"gpu_radius_capacity", config.GpuRadiusCapacity},
            {"backend", std::string{ToConfigString(config.Backend)}},
            {"initial_backend", config.InitialBackend == PointSamplingBackend::Vulkan ? "vulkan" : "cpu"},
            {"strategy", std::string{ToConfigString(config.Strategy)}},
            {"support_radius_mode",
             std::string{ToConfigString(config.SupportRadiusMode)}},
            {"support_radius", config.SupportRadius},
            {"max_support_neighbors", config.MaxSupportNeighbors},
            {"max_predicted_contributions",
             config.MaxPredictedContributions},
            {"repulsion_weight", config.RepulsionWeight},
            {"max_iterations", config.MaxIterations},
            {"convergence_tolerance", config.ConvergenceTolerance},
            {"target_point_count", config.TargetPointCount},
            {"seed", config.Seed},
            {"wlop_anisotropic", config.WlopAnisotropic},
            {"normal_source", std::string{ToConfigString(config.NormalSource)}},
            {"normal_angle_radians", config.NormalAngleRadians},
            {"normal_refinement_rounds", config.NormalRefinementRounds},
            {"clop_mixture_component_count", config.ClopMixtureComponentCount},
            {"clop_mixture_max_iterations", config.ClopMixtureMaxIterations},
            {"clop_mixture_relative_tolerance", config.ClopMixtureRelativeTolerance},
            {"clop_covariance_floor", config.ClopCovarianceFloor},
            {"ear_edge_sensitivity", config.EarEdgeSensitivity},
        });
        ConfigDetail::EncodePointSampling(doc, "initial_", config.InitialSampling);
        return ConfigDetail::SerializeConfigJson(doc);
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidateClusteringConfigSection(
        const std::string_view documentPayloadJson,
        const std::string_view referencePayloadJson,
        const std::string_view diagnosticSubject)
    {
        const ClusteringConfig reference = ParseClustering(
            referencePayloadJson,
            ClusteringConfig{},
            ValidationContext{});
        Core::Config::EngineConfigSectionValidationResult result{
            .State = Core::Config::EngineConfigState::Valid,
        };
        const ClusteringConfig config = ParseClustering(
            documentPayloadJson,
            reference,
            ValidationContext{
                .Result = &result,
                .Path = std::string{diagnosticSubject},
            });
        result.CanonicalPayloadJson = SerializeClusteringConfig(config);
        return result;
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidateCurvatureSegmentationConfigSection(
        const std::string_view documentPayloadJson,
        const std::string_view referencePayloadJson,
        const std::string_view diagnosticSubject)
    {
        const CurvatureSegmentationConfig reference =
            ParseCurvatureSegmentation(
                referencePayloadJson,
                CurvatureSegmentationConfig{},
                ValidationContext{});
        Core::Config::EngineConfigSectionValidationResult result{
            .State = Core::Config::EngineConfigState::Valid,
        };
        const CurvatureSegmentationConfig config =
            ParseCurvatureSegmentation(
                documentPayloadJson,
                reference,
                ValidationContext{
                    .Result = &result,
                    .Path = std::string{diagnosticSubject},
                });
        result.CanonicalPayloadJson =
            SerializeCurvatureSegmentationConfig(config);
        return result;
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidateProgressivePoissonConfigSection(
        const std::string_view documentPayloadJson,
        const std::string_view referencePayloadJson,
        const std::string_view diagnosticSubject)
    {
        const ProgressivePoissonPlaygroundConfig reference =
            ParseProgressivePoisson(
                referencePayloadJson,
                ProgressivePoissonPlaygroundConfig{},
                ValidationContext{});
        Core::Config::EngineConfigSectionValidationResult result{
            .State = Core::Config::EngineConfigState::Valid,
        };
        const ProgressivePoissonPlaygroundConfig config =
            ParseProgressivePoisson(
                documentPayloadJson,
                reference,
                ValidationContext{
                    .Result = &result,
                    .Path = std::string{diagnosticSubject},
                });
        result.CanonicalPayloadJson =
            SerializeProgressivePoissonPlaygroundConfig(config);
        return result;
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidateParameterizationConfigSection(
        const std::string_view documentPayloadJson,
        const std::string_view referencePayloadJson,
        const std::string_view diagnosticSubject)
    {
        const ParameterizationConfig reference = ParseParameterization(
            referencePayloadJson,
            ParameterizationConfig{},
            ValidationContext{});
        Core::Config::EngineConfigSectionValidationResult result{
            .State = Core::Config::EngineConfigState::Valid,
        };
        const ParameterizationConfig config = ParseParameterization(
            documentPayloadJson,
            reference,
            ValidationContext{
                .Result = &result,
                .Path = std::string{diagnosticSubject},
            });
        result.CanonicalPayloadJson = SerializeParameterizationConfig(config);
        return result;
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidatePointCloudConsolidationConfigSection(
        const std::string_view documentPayloadJson,
        const std::string_view referencePayloadJson,
        const std::string_view diagnosticSubject)
    {
        const PointCloudConsolidationConfig reference =
            ParsePointCloudConsolidation(
                referencePayloadJson,
                PointCloudConsolidationConfig{},
                ValidationContext{});
        Core::Config::EngineConfigSectionValidationResult result{
            .State = Core::Config::EngineConfigState::Valid,
        };
        const PointCloudConsolidationConfig config =
            ParsePointCloudConsolidation(
                documentPayloadJson,
                reference,
                ValidationContext{
                    .Result = &result,
                    .Path = std::string{diagnosticSubject},
                });
        result.CanonicalPayloadJson =
            SerializePointCloudConsolidationConfig(config);
        return result;
    }

    std::optional<ClusteringConfig> GetClusteringConfig(
        const Core::Config::EngineConfig& config)
    {
        const std::optional<std::string> payload =
            FindValidatedCanonicalPayload(
                config,
                kClusteringConfigSectionName,
                kClusteringConfigSectionSchemaId,
                kClusteringConfigSectionSchemaVersion,
                [] { return SerializeClusteringConfig(ClusteringConfig{}); },
                ValidateClusteringConfigSection);
        if (!payload.has_value())
            return std::nullopt;
        return DecodeClusteringCanonical(*payload);
    }

    void SetClusteringConfig(
        Core::Config::EngineConfig& config,
        const ClusteringConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(
            config.AppSections,
            MakeConfigSection(
                kClusteringConfigSectionName,
                kClusteringConfigSectionSchemaId,
                kClusteringConfigSectionSchemaVersion,
                SerializeClusteringConfig(value)));
    }

    std::optional<CurvatureSegmentationConfig>
    GetCurvatureSegmentationConfig(
        const Core::Config::EngineConfig& config)
    {
        const std::optional<std::string> payload =
            FindValidatedCanonicalPayload(
                config,
                kCurvatureSegmentationConfigSectionName,
                kCurvatureSegmentationConfigSectionSchemaId,
                kCurvatureSegmentationConfigSectionSchemaVersion,
                []
                {
                    return SerializeCurvatureSegmentationConfig(
                        CurvatureSegmentationConfig{});
                },
                ValidateCurvatureSegmentationConfigSection);
        if (!payload.has_value())
            return std::nullopt;
        return DecodeCurvatureSegmentationCanonical(*payload);
    }

    void SetCurvatureSegmentationConfig(
        Core::Config::EngineConfig& config,
        const CurvatureSegmentationConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(
            config.AppSections,
            MakeConfigSection(
                kCurvatureSegmentationConfigSectionName,
                kCurvatureSegmentationConfigSectionSchemaId,
                kCurvatureSegmentationConfigSectionSchemaVersion,
                SerializeCurvatureSegmentationConfig(value)));
    }

    std::optional<ProgressivePoissonPlaygroundConfig>
    GetProgressivePoissonPlaygroundConfig(
        const Core::Config::EngineConfig& config)
    {
        const std::optional<std::string> payload =
            FindValidatedCanonicalPayload(
                config,
                kProgressivePoissonConfigSectionName,
                kProgressivePoissonConfigSectionSchemaId,
                kProgressivePoissonConfigSectionSchemaVersion,
                []
                {
                    return SerializeProgressivePoissonPlaygroundConfig(
                        ProgressivePoissonPlaygroundConfig{});
                },
                ValidateProgressivePoissonConfigSection);
        if (!payload.has_value())
        {
            return std::nullopt;
        }
        return DecodeProgressivePoissonCanonical(*payload);
    }

    void SetProgressivePoissonPlaygroundConfig(
        Core::Config::EngineConfig& config,
        const ProgressivePoissonPlaygroundConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(
            config.AppSections,
            MakeConfigSection(
                kProgressivePoissonConfigSectionName,
                kProgressivePoissonConfigSectionSchemaId,
                kProgressivePoissonConfigSectionSchemaVersion,
                SerializeProgressivePoissonPlaygroundConfig(value)));
    }

    std::optional<ParameterizationConfig> GetParameterizationConfig(
        const Core::Config::EngineConfig& config)
    {
        const std::optional<std::string> payload =
            FindValidatedCanonicalPayload(
                config,
                kParameterizationConfigSectionName,
                kParameterizationConfigSectionSchemaId,
                kParameterizationConfigSectionSchemaVersion,
                []
                {
                    return SerializeParameterizationConfig(
                        ParameterizationConfig{});
                },
                ValidateParameterizationConfigSection);
        if (!payload.has_value())
        {
            return std::nullopt;
        }
        return DecodeParameterizationCanonical(*payload);
    }

    void SetParameterizationConfig(
        Core::Config::EngineConfig& config,
        const ParameterizationConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(
            config.AppSections,
            MakeConfigSection(
                kParameterizationConfigSectionName,
                kParameterizationConfigSectionSchemaId,
                kParameterizationConfigSectionSchemaVersion,
                SerializeParameterizationConfig(value)));
    }

    std::optional<PointCloudConsolidationConfig>
    GetPointCloudConsolidationConfig(
        const Core::Config::EngineConfig& config)
    {
        const std::optional<std::string> payload =
            FindValidatedCanonicalPayload(
                config,
                kPointCloudConsolidationConfigSectionName,
                kPointCloudConsolidationConfigSectionSchemaId,
                kPointCloudConsolidationConfigSectionSchemaVersion,
                []
                {
                    return SerializePointCloudConsolidationConfig(
                        PointCloudConsolidationConfig{});
                },
                ValidatePointCloudConsolidationConfigSection);
        if (!payload.has_value())
            return std::nullopt;
        return DecodePointCloudConsolidationCanonical(*payload);
    }

    void SetPointCloudConsolidationConfig(
        Core::Config::EngineConfig& config,
        const PointCloudConsolidationConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(
            config.AppSections,
            MakeConfigSection(
                kPointCloudConsolidationConfigSectionName,
                kPointCloudConsolidationConfigSectionSchemaId,
                kPointCloudConsolidationConfigSectionSchemaVersion,
                SerializePointCloudConsolidationConfig(value)));
    }

    Core::Config::EngineConfigSectionRegistration
    MakeClusteringConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return MakeSectionRegistration(
            kClusteringConfigSectionName,
            kClusteringConfigSectionSchemaId,
            kClusteringConfigSectionSchemaVersion,
            SerializeClusteringConfig(ClusteringConfig{}),
            ValidateClusteringConfigSection,
            ConfigDetail::BuildSectionSchemaJson(kClusteringConfigSectionSchemaId, "Clustering",
                "Fields omitted or invalid retain the reference where supported; binding and cross-field rules remain authoritative.",
                kClusteringFields, ConfigDetail::ParseConfigJson(SerializeClusteringConfig(ClusteringConfig{}), true)),
            std::move(onChanged));
    }

    Core::Config::EngineConfigSectionRegistration
    MakeCurvatureSegmentationConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return MakeSectionRegistration(
            kCurvatureSegmentationConfigSectionName,
            kCurvatureSegmentationConfigSectionSchemaId,
            kCurvatureSegmentationConfigSectionSchemaVersion,
            SerializeCurvatureSegmentationConfig(
                CurvatureSegmentationConfig{}),
            ValidateCurvatureSegmentationConfigSection,
            ConfigDetail::BuildSectionSchemaJson(kCurvatureSegmentationConfigSectionSchemaId, "CurvatureSegmentation",
                "Fields omitted or invalid retain the reference where supported; binding and cross-field rules remain authoritative.",
                kCurvatureSegmentationFields, ConfigDetail::ParseConfigJson(SerializeCurvatureSegmentationConfig(CurvatureSegmentationConfig{}), true)),
            std::move(onChanged));
    }

    Core::Config::EngineConfigSectionRegistration
    MakeProgressivePoissonConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return MakeSectionRegistration(
            kProgressivePoissonConfigSectionName,
            kProgressivePoissonConfigSectionSchemaId,
            kProgressivePoissonConfigSectionSchemaVersion,
            SerializeProgressivePoissonPlaygroundConfig(
                ProgressivePoissonPlaygroundConfig{}),
            ValidateProgressivePoissonConfigSection,
            ConfigDetail::BuildSectionSchemaJson(kProgressivePoissonConfigSectionSchemaId, "ProgressivePoisson",
                "Fields omitted or invalid retain the reference where supported; binding and cross-field rules remain authoritative.",
                kProgressivePoissonFields, ConfigDetail::ParseConfigJson(SerializeProgressivePoissonPlaygroundConfig(ProgressivePoissonPlaygroundConfig{}), true)),
            std::move(onChanged));
    }

    Core::Config::EngineConfigSectionRegistration
    MakeParameterizationConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        ParameterizationConfig defaults;
        defaults.CornerTexcoordsToRetire = GeometryPropertyRef{
            GeometryElementDomain::MeshHalfedge, "h:texcoord", Geometry::PropertyValueKind::Vec2};
        return MakeSectionRegistration(
            kParameterizationConfigSectionName,
            kParameterizationConfigSectionSchemaId,
            kParameterizationConfigSectionSchemaVersion,
            SerializeParameterizationConfig(defaults),
            ValidateParameterizationConfigSection,
            ConfigDetail::BuildSectionSchemaJson(kParameterizationConfigSectionSchemaId, "Parameterization",
                "Fields omitted or invalid retain the reference where supported; binding and cross-field rules remain authoritative.",
                kParameterizationFields, ConfigDetail::ParseConfigJson(SerializeParameterizationConfig(defaults), true)),
            std::move(onChanged));
    }

    std::span<const ConfigFieldSpec> PointCloudConsolidationConfigFieldSpecs() noexcept
    { return ConsolidationFields(); }

    Core::Config::EngineConfigSectionRegistration
    MakePointCloudConsolidationConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return MakeSectionRegistration(
            kPointCloudConsolidationConfigSectionName,
            kPointCloudConsolidationConfigSectionSchemaId,
            kPointCloudConsolidationConfigSectionSchemaVersion,
            SerializePointCloudConsolidationConfig(
                PointCloudConsolidationConfig{}),
            ValidatePointCloudConsolidationConfigSection,
            ConfigDetail::BuildSectionSchemaJson(kPointCloudConsolidationConfigSectionSchemaId, "PointCloudConsolidation",
                "Fields omitted or invalid retain the reference where supported; binding and cross-field rules remain authoritative.",
                ConsolidationFields(), ConfigDetail::ParseConfigJson(SerializePointCloudConsolidationConfig(PointCloudConsolidationConfig{}), true)),
            std::move(onChanged));
    }
}

namespace Extrinsic::Runtime::ConfigDetail
{
    void EncodePointSampling(nlohmann::json& o, const std::string_view prefix, const PointSamplingConfig& c)
    {
        const auto key = [&](const char* suffix) { return std::string(prefix) + suffix; };
        o[key("method")] = unsigned(c.Method);
        o[key("seed")] = c.Seed;
        o[key("eta")] = c.Eta;
        o[key("candidate_cap")] = c.CandidateCap;
        o[key("beta")] = c.Beta;
        o[key("batch_priority")] = unsigned(c.BatchPriority);
        o[key("batch_ordering")] = unsigned(c.BatchOrdering);
        o[key("void_density")] = c.VoidDensity;
        o[key("poisson_selection")] = unsigned(c.PoissonSelection);
        o[key("poisson_retries")] = c.PoissonRetries;
        o[key("poisson_repair_levels")] = c.PoissonRepairLevels;
        o[key("poisson_budget")] = c.PoissonBudget;
        o[key("poisson_random_phases")] = c.PoissonRandomPhases;
        o[key("poisson_balanced")] = c.PoissonBalanced;
        o[key("poisson_grid_width")] = c.PoissonGridWidth;
        o[key("poisson_max_levels")] = c.PoissonMaxLevels;
        o[key("elimination_alpha")] = c.EliminationAlpha;
        o[key("elimination_radius")] = c.EliminationRadius;
        o[key("manifold_dimension")] = c.ManifoldDimension;
    }

    void DecodePointSampling(const nlohmann::json& o, const std::string_view prefix, PointSamplingConfig& c)
    {
        const auto read = [&](const char* suffix, auto& value) {
            const auto it = o.find(std::string(prefix) + suffix);
            if (it == o.end()) return;
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_enum_v<T>) value = T(it->template get<unsigned>());
            else value = it->template get<T>();
        };
        read("method", c.Method);
        read("seed", c.Seed);
        read("eta", c.Eta);
        read("candidate_cap", c.CandidateCap);
        read("beta", c.Beta);
        read("batch_priority", c.BatchPriority);
        read("batch_ordering", c.BatchOrdering);
        read("void_density", c.VoidDensity);
        read("poisson_selection", c.PoissonSelection);
        read("poisson_retries", c.PoissonRetries);
        read("poisson_repair_levels", c.PoissonRepairLevels);
        read("poisson_budget", c.PoissonBudget);
        read("poisson_random_phases", c.PoissonRandomPhases);
        read("poisson_balanced", c.PoissonBalanced);
        read("poisson_grid_width", c.PoissonGridWidth);
        read("poisson_max_levels", c.PoissonMaxLevels);
        read("elimination_alpha", c.EliminationAlpha);
        read("elimination_radius", c.EliminationRadius);
        read("manifold_dimension", c.ManifoldDimension);
    }
}
