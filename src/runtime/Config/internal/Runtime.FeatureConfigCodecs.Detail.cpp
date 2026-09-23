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
import Extrinsic.Runtime.GeometryProperty.Types;

#include "Config/internal/Runtime.PointConfigJson.hpp"

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
        const std::initializer_list<std::string_view> unsignedFields)
    {
        if (!input.is_object()) return std::string(objectError);
        for (auto it = input.begin(); it != input.end(); ++it)
        {
            if (!defaults.contains(it.key()))
                return std::string(unknownFieldPrefix) + it.key();
            defaults[it.key()] = it.value();
        }
        for (const auto key : unsignedFields)
        {
            const auto& value = defaults.at(std::string(key));
            if (!value.is_number_unsigned() ||
                value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
                return std::string(key) + " must be an unsigned 32-bit integer.";
        }
        return std::nullopt;
    }

    std::optional<std::string> ValidatePointConfigNonnegativeFloats(
        const nlohmann::json& values, const std::initializer_list<std::string_view> fields)
    {
        for (const auto key : fields)
        {
            const auto& value = values.at(std::string(key));
            if (!value.is_number() || !std::isfinite(value.get<double>()) || value.get<double>() < 0 ||
                value.get<double>() > std::numeric_limits<float>::max())
                return std::string(key) + " must be a finite nonnegative float.";
        }
        return std::nullopt;
    }

    const char* PointPropertyKindToken(const Geometry::PropertyValueKind kind) noexcept
    {
        switch (kind)
        {
        case Geometry::PropertyValueKind::Vec3: return "vec3";
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
        const std::initializer_list<std::pair<std::string_view, Geometry::PropertyValueKind>> fields)
    {
        for (const auto& [key, kind] : fields)
        {
            const auto validation = ValidatePointPropertyRef(values.at(std::string(key)), kind, true);
            if (validation == PointPropertyValidation::InvalidReference)
                return std::string(key) + " needs a canonical typed property reference.";
            if (validation == PointPropertyValidation::UnknownDomain)
                return "Unknown element domain.";
        }
        return std::nullopt;
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

        [[nodiscard]] Core::Config::EngineConfigSection MakeConfigSection(
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

        [[nodiscard]] Core::Config::EngineConfigSectionRegistration
        MakeSectionRegistration(
            const std::string_view name,
            const std::string_view schemaId,
            const std::uint32_t schemaVersion,
            std::string defaultPayloadJson,
            const SectionValidatorFn validate,
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
            };
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

        [[nodiscard]] bool Contains(
            const std::initializer_list<std::string_view> allowed,
            const std::string_view key) noexcept
        {
            return std::find(allowed.begin(), allowed.end(), key) != allowed.end();
        }

        void AddUnknownFieldDiagnostics(
            ValidationContext& context,
            const json& object,
            const std::initializer_list<std::string_view> allowed)
        {
            if (!object.is_object())
            {
                return;
            }
            for (const auto& [key, value] : object.items())
            {
                (void)value;
                if (!Contains(allowed, key))
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
            const std::int64_t minValue,
            const std::int64_t maxValue)
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
            if (!number.has_value() || *number < minValue || *number > maxValue)
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
            const double minValue,
            const double maxValue)
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
            if (!std::isfinite(number) || number < minValue || number > maxValue)
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
            if (!value->is_array() || value->size() != 2u ||
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
            constexpr double kMaxFloat =
                static_cast<double>(std::numeric_limits<float>::max());
            if (!std::isfinite(u) || !std::isfinite(v) ||
                std::abs(u) > kMaxFloat || std::abs(v) > kMaxFloat)
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
                if (!element.is_array() || element.size() != 2u ||
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
                constexpr double kMaxFloat =
                    static_cast<double>(std::numeric_limits<float>::max());
                if (!std::isfinite(u) || !std::isfinite(v) ||
                    std::abs(u) > kMaxFloat || std::abs(v) > kMaxFloat)
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
            if (value == "Level") return ProgressivePoissonPlaygroundChannel::Level;
            if (value == "Rank") return ProgressivePoissonPlaygroundChannel::Rank;
            if (value == "SplatRadius")
                return ProgressivePoissonPlaygroundChannel::SplatRadius;
            if (value == "PrefixVisible")
                return ProgressivePoissonPlaygroundChannel::PrefixVisible;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ProgressivePoissonPlaygroundBackend>
        ParseProgressivePoissonBackend(const std::string_view value) noexcept
        {
            if (value == "CpuReference")
                return ProgressivePoissonPlaygroundBackend::CpuReference;
            if (value == "VulkanCompute")
                return ProgressivePoissonPlaygroundBackend::VulkanCompute;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ClusteringBackend>
        ParseClusteringBackend(const std::string_view value) noexcept
        {
            if (value == "CpuReference")
                return ClusteringBackend::CpuReference;
            if (value == "VulkanCompute")
                return ClusteringBackend::VulkanCompute;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<CurvatureSegmentationSelectionMode>
        ParseCurvatureSegmentationSelectionMode(
            const std::string_view value) noexcept
        {
            if (value == "fixed_count")
            {
                return CurvatureSegmentationSelectionMode::FixedCount;
            }
            if (value == "automatic")
            {
                return CurvatureSegmentationSelectionMode::Automatic;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<CurvatureSegmentationMethod>
        ParseCurvatureSegmentationMethod(
            const std::string_view value) noexcept
        {
            if (value == "curvature_gmm")
                return CurvatureSegmentationMethod::CurvatureGmm;
            if (value == "feature_aligned_patches")
                return CurvatureSegmentationMethod::FeatureAlignedPatches;
            if (value == "feature_boundary_curves_v1")
                return CurvatureSegmentationMethod::FeatureBoundaryCurves;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<PointCloudConsolidationStrategy>
        ParsePointCloudConsolidationStrategy(
            const std::string_view value) noexcept
        {
            if (value == "lop")
                return PointCloudConsolidationStrategy::Lop;
            if (value == "wlop")
                return PointCloudConsolidationStrategy::Wlop;
            if (value == "clop")
                return PointCloudConsolidationStrategy::Clop;
            if (value == "ear")
                return PointCloudConsolidationStrategy::Ear;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<PointCloudConsolidationBackend>
        ParsePointCloudConsolidationBackend(
            const std::string_view value) noexcept
        {
            if (value == "cpu_reference")
                return PointCloudConsolidationBackend::CpuReference;
            if (value == "gpu_vulkan_compute")
                return PointCloudConsolidationBackend::VulkanCompute;
            if (value == "cpu_lbvh") return PointCloudConsolidationBackend::CpuLBVH;
            if (value == "vulkan_lbvh") return PointCloudConsolidationBackend::VulkanLBVH;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<PointCloudConsolidationNormalSource>
        ParsePointCloudConsolidationNormalSource(
            const std::string_view value) noexcept
        {
            if (value == "authored_or_estimate")
            {
                return PointCloudConsolidationNormalSource::
                    AuthoredOrEstimate;
            }
            if (value == "require_authored")
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
            if (value == "auto")
                return PointCloudConsolidationSupportRadiusMode::Auto;
            if (value == "manual")
                return PointCloudConsolidationSupportRadiusMode::Manual;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<KMeansInitialization>
        ParseKMeansInitialization(const std::string_view value) noexcept
        {
            if (value == "Random") return KMeansInitialization::Random;
            if (value == "Hierarchical")
                return KMeansInitialization::Hierarchical;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<Geometry::UvAtlas::UvAtlasMethod>
        ParseAtlasMethod(const std::string_view value) noexcept
        {
            using M = Geometry::UvAtlas::UvAtlasMethod;
            if (value == "fast_staged") return M::FastStaged;
            if (value == "xatlas") return M::XAtlas;
            return std::nullopt;
        }
        [[nodiscard]] std::optional<Geometry::UvAtlas::UvAtlasDistortion>
        ParseAtlasDistortion(const std::string_view value) noexcept
        {
            using D = Geometry::UvAtlas::UvAtlasDistortion;
            if (value == "none") return D::None;
            if (value == "angle") return D::Angle;
            if (value == "area") return D::Area;
            if (value == "both") return D::Both;
            return std::nullopt;
        }
        [[nodiscard]] std::string_view ToConfigString(const Geometry::UvAtlas::UvAtlasMethod value) noexcept
        {
            using M = Geometry::UvAtlas::UvAtlasMethod;
            switch (value) { case M::FastStaged: return "fast_staged"; case M::XAtlas: return "xatlas"; default: return "invalid"; }
        }
        [[nodiscard]] std::string_view ToConfigString(const Geometry::UvAtlas::UvAtlasDistortion value) noexcept
        {
            using D = Geometry::UvAtlas::UvAtlasDistortion;
            switch (value) { case D::None: return "none"; case D::Angle: return "angle"; case D::Area: return "area"; case D::Both: return "both"; }
            return "invalid";
        }

        [[nodiscard]] std::optional<ParameterizationStrategyKind>
        ParseParameterizationStrategy(const std::string_view value) noexcept
        {
            if (value == "lscm") return ParameterizationStrategyKind::Lscm;
            if (value == "harmonic_cotangent")
                return ParameterizationStrategyKind::HarmonicCotangent;
            if (value == "tutte_uniform")
                return ParameterizationStrategyKind::TutteUniform;
            if (value == "bff") return ParameterizationStrategyKind::Bff;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ParameterizationBoundaryPolicy>
        ParseParameterizationBoundaryPolicy(const std::string_view value) noexcept
        {
            if (value == "circle") return ParameterizationBoundaryPolicy::Circle;
            if (value == "square") return ParameterizationBoundaryPolicy::Square;
            if (value == "custom") return ParameterizationBoundaryPolicy::Custom;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ParameterizationBffBoundaryMode>
        ParseParameterizationBffBoundaryMode(const std::string_view value) noexcept
        {
            if (value == "automatic_conformal")
                return ParameterizationBffBoundaryMode::AutomaticConformal;
            if (value == "target_lengths")
                return ParameterizationBffBoundaryMode::TargetLengths;
            if (value == "target_angles")
                return ParameterizationBffBoundaryMode::TargetAngles;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ParameterizationUvRenderMode>
        ParseParameterizationUvRenderMode(const std::string_view value) noexcept
        {
            if (value == "cpu_layout")
                return ParameterizationUvRenderMode::CpuLayout;
            if (value == "gpu_shaded")
                return ParameterizationUvRenderMode::GpuShaded;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ParameterizationUvBackgroundMode>
        ParseParameterizationUvBackgroundMode(
            const std::string_view value) noexcept
        {
            if (value == "grid") return ParameterizationUvBackgroundMode::Grid;
            if (value == "checker")
                return ParameterizationUvBackgroundMode::Checker;
            if (value == "texel_density")
                return ParameterizationUvBackgroundMode::TexelDensity;
            if (value == "texture")
                return ParameterizationUvBackgroundMode::Texture;
            return std::nullopt;
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ProgressivePoissonPlaygroundChannel value) noexcept
        {
            switch (value)
            {
            case ProgressivePoissonPlaygroundChannel::Level: return "Level";
            case ProgressivePoissonPlaygroundChannel::Rank: return "Rank";
            case ProgressivePoissonPlaygroundChannel::SplatRadius:
                return "SplatRadius";
            case ProgressivePoissonPlaygroundChannel::PrefixVisible:
                return "PrefixVisible";
            }
            return "Level";
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ProgressivePoissonPlaygroundBackend value) noexcept
        {
            switch (value)
            {
            case ProgressivePoissonPlaygroundBackend::CpuReference:
                return "CpuReference";
            case ProgressivePoissonPlaygroundBackend::VulkanCompute:
                return "VulkanCompute";
            }
            return "CpuReference";
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ClusteringBackend value) noexcept
        {
            switch (value)
            {
            case ClusteringBackend::VulkanCompute: return "VulkanCompute";
            case ClusteringBackend::None:
            case ClusteringBackend::CpuReference: return "CpuReference";
            }
            return "CpuReference";
        }

        [[nodiscard]] std::string_view ToConfigString(
            const CurvatureSegmentationSelectionMode value) noexcept
        {
            switch (value)
            {
            case CurvatureSegmentationSelectionMode::FixedCount:
                return "fixed_count";
            case CurvatureSegmentationSelectionMode::Automatic:
                return "automatic";
            }
            return "automatic";
        }

        [[nodiscard]] std::string_view ToConfigString(
            const CurvatureSegmentationMethod value) noexcept
        {
            switch (value)
            {
            case CurvatureSegmentationMethod::CurvatureGmm:
                return "curvature_gmm";
            case CurvatureSegmentationMethod::FeatureAlignedPatches:
                return "feature_aligned_patches";
            case CurvatureSegmentationMethod::FeatureBoundaryCurves:
                return "feature_boundary_curves_v1";
            }
            return "curvature_gmm";
        }

        [[nodiscard]] std::string_view ToConfigString(
            const KMeansInitialization value) noexcept
        {
            switch (value)
            {
            case KMeansInitialization::Random: return "Random";
            case KMeansInitialization::Hierarchical: return "Hierarchical";
            }
            return "Hierarchical";
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
            case ParameterizationStrategyKind::Lscm: return "lscm";
            case ParameterizationStrategyKind::HarmonicCotangent:
                return "harmonic_cotangent";
            case ParameterizationStrategyKind::TutteUniform:
                return "tutte_uniform";
            case ParameterizationStrategyKind::Bff: return "bff";
            }
            return "lscm";
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ParameterizationBoundaryPolicy value) noexcept
        {
            switch (value)
            {
            case ParameterizationBoundaryPolicy::Circle: return "circle";
            case ParameterizationBoundaryPolicy::Square: return "square";
            case ParameterizationBoundaryPolicy::Custom: return "custom";
            }
            return "circle";
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ParameterizationBffBoundaryMode value) noexcept
        {
            switch (value)
            {
            case ParameterizationBffBoundaryMode::AutomaticConformal:
                return "automatic_conformal";
            case ParameterizationBffBoundaryMode::TargetLengths:
                return "target_lengths";
            case ParameterizationBffBoundaryMode::TargetAngles:
                return "target_angles";
            }
            return "automatic_conformal";
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ParameterizationUvRenderMode value) noexcept
        {
            switch (value)
            {
            case ParameterizationUvRenderMode::CpuLayout: return "cpu_layout";
            case ParameterizationUvRenderMode::GpuShaded: return "gpu_shaded";
            }
            return "cpu_layout";
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ParameterizationUvBackgroundMode value) noexcept
        {
            switch (value)
            {
            case ParameterizationUvBackgroundMode::Grid: return "grid";
            case ParameterizationUvBackgroundMode::Checker: return "checker";
            case ParameterizationUvBackgroundMode::TexelDensity:
                return "texel_density";
            case ParameterizationUvBackgroundMode::Texture: return "texture";
            }
            return "grid";
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

            AddUnknownFieldDiagnostics(
                context,
                *object,
                {"cluster_count",
                 "max_iterations",
                 "seed",
                 "initialization",
                 "backend", "properties"});
            if (const auto value = ReadInteger(
                    context, *object, "cluster_count", 1, 1024))
            {
                config.Parameters.ClusterCount =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context, *object, "max_iterations", 1, 4096))
            {
                config.Parameters.MaxIterations =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "seed",
                    0,
                    std::numeric_limits<std::uint32_t>::max()))
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

            AddUnknownFieldDiagnostics(
                context,
                *object,
                {"features", "positions", "components", "regions", "region_colors", "boundaries", "boundary_colors", "hard_features", "feature_confidence", "boundary_roles", "feature_colors", "method",
                 "selection_mode",
                 "fixed_component_count",
                 "automatic_min_components",
                 "automatic_max_components",
                 "automatic_fit_tolerance",
                 "automatic_complexity_weight",
                 "max_em_iterations",
                 "em_relative_tolerance",
                 "covariance_floor",
                 "seed",
                 "spatial_weight",
                 "feature_sensitivity",
                 "max_spatial_iterations",
                 "minimum_region_faces",
                 "feature_base_radius_ratio",
                 "hard_dihedral_threshold_degrees",
                 "patch_complexity_cost"});

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
            if (const auto value = ReadInteger(
                    context, *object, "fixed_component_count", 1, 1024))
            {
                config.FixedComponentCount =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context,
                    *object,
                    "feature_base_radius_ratio",
                    1.0e-12,
                    1.0))
            {
                config.FeatureBaseRadiusRatio = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context,
                    *object,
                    "hard_dihedral_threshold_degrees",
                    0.0,
                    180.0))
            {
                config.HardDihedralThresholdDegrees = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context,
                    *object,
                    "patch_complexity_cost",
                    0.0,
                    1.0e12))
            {
                config.PatchComplexityCost = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context, *object, "automatic_min_components", 1, 1024))
            {
                config.AutomaticMinComponents =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context, *object, "automatic_max_components", 1, 1024))
            {
                config.AutomaticMaxComponents =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context,
                    *object,
                    "automatic_fit_tolerance",
                    1.0e-12,
                    1.0e12))
            {
                config.AutomaticFitTolerance = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context,
                    *object,
                    "automatic_complexity_weight",
                    0.0,
                    1.0e12))
            {
                config.AutomaticComplexityWeight = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context, *object, "max_em_iterations", 1, 100000))
            {
                config.MaxEmIterations =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context,
                    *object,
                    "em_relative_tolerance",
                    0.0,
                    1.0))
            {
                config.EmRelativeTolerance = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context,
                    *object,
                    "covariance_floor",
                    1.0e-15,
                    1.0e6))
            {
                config.CovarianceFloor = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "seed",
                    0,
                    std::numeric_limits<std::uint32_t>::max()))
            {
                config.Seed = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context, *object, "spatial_weight", 0.0, 1.0e12))
            {
                config.SpatialWeight = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context, *object, "feature_sensitivity", 0.0, 1.0e12))
            {
                config.FeatureSensitivity = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "max_spatial_iterations",
                    1,
                    100000))
            {
                config.MaxSpatialIterations =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "minimum_region_faces",
                    1,
                    std::numeric_limits<std::uint32_t>::max()))
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
                if (!features->is_array() || features->size() > 3u)
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

            AddUnknownFieldDiagnostics(
                context,
                *object,
                {"backend",
                 "strategy",
                 "support_radius_mode",
                 "support_radius",
                 "max_support_neighbors",
                 "max_predicted_contributions",
                 "repulsion_weight",
                 "max_iterations",
                 "convergence_tolerance",
                 "target_point_count",
                 "seed",
                 "gpu_query_batch_size",
                 "gpu_radius_capacity",
                 "wlop_anisotropic",
                 "normal_source",
                 "normal_angle_radians",
                 "normal_refinement_rounds",
                 "clop_mixture_component_count",
                 "clop_mixture_max_iterations",
                 "clop_mixture_relative_tolerance",
                 "clop_covariance_floor",
                 "ear_edge_sensitivity"});

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
            if (const auto value = ReadNumber(
                    context, *object, "support_radius", 1.0e-12, 1.0e12))
            {
                config.SupportRadius = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "max_support_neighbors",
                    1,
                    1'000'000))
            {
                config.MaxSupportNeighbors =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "max_predicted_contributions",
                    1,
                    1'000'000'000'000LL))
            {
                config.MaxPredictedContributions =
                    static_cast<std::uint64_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context, *object, "repulsion_weight", 0.0, 0.499999999999))
            {
                config.RepulsionWeight = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context, *object, "max_iterations", 1, 4096))
            {
                config.MaxIterations = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context,
                    *object,
                    "convergence_tolerance",
                    0.0,
                    1.0e12))
            {
                config.ConvergenceTolerance = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context, *object, "target_point_count", 0, 1'000'000))
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
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "seed",
                    0,
                    std::numeric_limits<std::uint32_t>::max()))
            {
                config.Seed = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(context, *object, "gpu_query_batch_size", 1, 16384))
            { config.GpuQueryBatchSize = static_cast<std::uint32_t>(*value); CountParsed(context); }
            if (const auto value = ReadInteger(context, *object, "gpu_radius_capacity", 1, 1024))
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
            if (const auto value = ReadNumber(
                    context,
                    *object,
                    "normal_angle_radians",
                    1.0e-6,
                    std::numbers::pi_v<double> - 1.0e-6))
            {
                config.NormalAngleRadians = *value;
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "normal_refinement_rounds",
                    1,
                    4096))
            {
                config.NormalRefinementRounds =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "clop_mixture_component_count",
                    1,
                    1'000'000))
            {
                config.ClopMixtureComponentCount =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "clop_mixture_max_iterations",
                    1,
                    4096))
            {
                config.ClopMixtureMaxIterations =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context,
                    *object,
                    "clop_mixture_relative_tolerance",
                    0.0,
                    1.0))
            {
                config.ClopMixtureRelativeTolerance = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context,
                    *object,
                    "clop_covariance_floor",
                    1.0e-18,
                    1.0e12))
            {
                config.ClopCovarianceFloor = *value;
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context,
                    *object,
                    "ear_edge_sensitivity",
                    1.0e-12,
                    1.0e6))
            {
                config.EarEdgeSensitivity = *value;
                CountParsed(context);
            }

            if (config.NormalRefinementRounds > config.MaxIterations)
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

            AddUnknownFieldDiagnostics(
                context,
                *object,
                {"positions", "level_property", "rank_property", "splat_radius_property", "prefix_visible_property", "dimension",
                 "grid_width",
                 "max_levels",
                 "hash_load_factor",
                 "radius_alpha",
                 "randomize_grid_origin",
                 "grid_origin_seed",
                 "shuffle_within_levels",
                 "shuffle_seed",
                 "prefix_count",
                 "channel",
                 "backend",
                 "auto_run_on_edit",
                 "debounce_seconds"});

            if (const auto value = ReadInteger(context, *object, "dimension", 2, 3))
            {
                config.Dimension = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value =
                    ReadInteger(context, *object, "grid_width", 1, 4096))
            {
                config.GridWidth = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value =
                    ReadInteger(context, *object, "max_levels", 1, 32))
            {
                config.MaxLevels = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value =
                    ReadNumber(context, *object, "hash_load_factor", 0.01, 16.0))
            {
                config.HashLoadFactor = *value;
                CountParsed(context);
            }
            if (const auto value =
                    ReadNumber(context, *object, "radius_alpha", -1.0, 0.999))
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
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "grid_origin_seed",
                    0,
                    std::numeric_limits<std::int32_t>::max()))
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
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "shuffle_seed",
                    0,
                    std::numeric_limits<std::int32_t>::max()))
            {
                config.ShuffleSeed = static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context, *object, "prefix_count", 0, 10'000'000))
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
            if (const auto value = ReadNumber(
                    context, *object, "debounce_seconds", 0.0, 10.0))
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

            AddUnknownFieldDiagnostics(
                context,
                *object,
                {"positions", "texcoords", "corner_texcoords_to_retire", "strategy", "lscm", "harmonic", "bff", "view", "atlas"});
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
                    AddUnknownFieldDiagnostics(
                        viewContext,
                        *view,
                        {"render_mode",
                         "background_mode",
                         "show_distortion_heatmap", "split_enabled", "atlas_on_left", "split_ratio"});
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
                    if (const auto value = ReadNumber(viewContext, *view, "split_ratio", 0.2, static_cast<double>(0.8f)))
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
                    AddUnknownFieldDiagnostics(atlasContext, *atlas,
                        {"method", "distortion", "guide", "region_count", "resolution", "padding",
                         "texels_per_unit", "max_conformal_distortion", "max_area_distortion",
                         "max_charts", "max_iterations", "allow_xatlas_fallback"});
                    if (FindMember(*atlas, "method") && !ReadEnum(atlasContext, *atlas, "method", ParseAtlasMethod, config.Atlas.Method))
                        invalid("Atlas method must be fast_staged or xatlas.");
                    if (FindMember(*atlas, "distortion") && !ReadEnum(atlasContext, *atlas, "distortion", ParseAtlasDistortion, config.Atlas.Distortion))
                        invalid("Atlas distortion must be none, angle, area or both.");
                    const auto integer = [&](const char* key, std::uint32_t& target, const std::int64_t minimum, const std::int64_t maximum)
                    {
                        if (!FindMember(*atlas, key)) return;
                        if (const auto value = ReadInteger(atlasContext, *atlas, key, minimum, maximum))
                        { target = static_cast<std::uint32_t>(*value); CountParsed(atlasContext); }
                        else invalid(std::string{"Invalid atlas "} + key + ".");
                    };
                    integer("region_count", config.Atlas.RegionCount, 0, 64);
                    integer("resolution", config.Atlas.Resolution, 16, 16384);
                    integer("padding", config.Atlas.Padding, 0, 32);
                    integer("max_charts", config.Atlas.MaxCharts, 1, 1000000);
                    integer("max_iterations", config.Atlas.MaxIterations, 1, 1000);
                    const auto number = [&](const char* key, auto& target, const double minimum, const double maximum)
                    {
                        if (!FindMember(*atlas, key)) return;
                        if (const auto value = ReadNumber(atlasContext, *atlas, key, minimum, maximum))
                        { target = static_cast<std::remove_reference_t<decltype(target)>>(*value); CountParsed(atlasContext); }
                        else invalid(std::string{"Invalid atlas "} + key + ".");
                    };
                    number("texels_per_unit", config.Atlas.TexelsPerUnit, 0.0, 1.0e12);
                    number("max_conformal_distortion", config.Atlas.MaxConformalDistortion, 1.0, 1.0e6);
                    number("max_area_distortion", config.Atlas.MaxAreaDistortion, 1.0, 1.0e6);
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
                    AddUnknownFieldDiagnostics(
                        lscmContext,
                        *lscm,
                        {"auto_pins",
                         "pin_vertex_0",
                         "pin_vertex_1",
                         "pin_uv_0",
                         "pin_uv_1",
                         "solver_tolerance",
                         "max_solver_iterations"});
                    if (const auto value =
                            ReadBool(lscmContext, *lscm, "auto_pins"))
                    {
                        config.Lscm.AutoPins = *value;
                        CountParsed(lscmContext);
                    }
                    if (const auto value = ReadInteger(
                            lscmContext,
                            *lscm,
                            "pin_vertex_0",
                            0,
                            std::numeric_limits<std::uint32_t>::max()))
                    {
                        config.Lscm.PinVertex0 =
                            static_cast<std::uint32_t>(*value);
                        CountParsed(lscmContext);
                    }
                    if (const auto value = ReadInteger(
                            lscmContext,
                            *lscm,
                            "pin_vertex_1",
                            0,
                            std::numeric_limits<std::uint32_t>::max()))
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
                    if (const auto value = ReadNumber(
                            lscmContext,
                            *lscm,
                            "solver_tolerance",
                            std::numeric_limits<double>::min(),
                            1.0e30))
                    {
                        config.Lscm.SolverTolerance = *value;
                        CountParsed(lscmContext);
                    }
                    if (const auto value = ReadInteger(
                            lscmContext,
                            *lscm,
                            "max_solver_iterations",
                            1,
                            std::numeric_limits<std::uint32_t>::max()))
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
                    AddUnknownFieldDiagnostics(
                        harmonicContext,
                        *harmonic,
                        {"boundary",
                         "arc_length_spacing",
                         "clamp_non_convex_weights",
                         "pinned_vertices",
                         "pinned_uvs"});
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
                    AddUnknownFieldDiagnostics(
                        bffContext,
                        *bff,
                        {"mode",
                         "boundary_data",
                         "angle_sum_tolerance",
                         "degeneracy_tolerance"});
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
                    if (const auto value = ReadNumber(
                            bffContext,
                            *bff,
                            "angle_sum_tolerance",
                            std::numeric_limits<double>::min(),
                            1.0e30))
                    {
                        config.Bff.AngleSumTolerance = *value;
                        CountParsed(bffContext);
                    }
                    if (const auto value = ReadNumber(
                            bffContext,
                            *bff,
                            "degeneracy_tolerance",
                            std::numeric_limits<double>::min(),
                            1.0e30))
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
        if (config.Resolution < 16u || config.Resolution > 16384u || config.Padding > 32u ||
            config.Padding * 2u + 1u >= config.Resolution)
            return "Atlas resolution must be 16..16384 with at most 32 padding texels and usable interior space.";
        if (!std::isfinite(config.TexelsPerUnit) || config.TexelsPerUnit < 0.0f || config.TexelsPerUnit > 1.0e12f)
            return "Atlas texel density must be finite and non-negative.";
        if (!std::isfinite(config.MaxConformalDistortion) || config.MaxConformalDistortion < 1.0 || config.MaxConformalDistortion > 1.0e6 ||
            !std::isfinite(config.MaxAreaDistortion) || config.MaxAreaDistortion < 1.0 || config.MaxAreaDistortion > 1.0e6)
            return "Atlas distortion limits must be finite and at least one (at most 1e6).";
        if (config.MaxCharts == 0u || config.MaxCharts > 1000000u || config.MaxIterations == 0u || config.MaxIterations > 1000u || config.RegionCount > 64u)
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
        return ConfigDetail::SerializeConfigJson(json::object({
            {"gpu_query_batch_size", config.GpuQueryBatchSize},
            {"gpu_radius_capacity", config.GpuRadiusCapacity},
            {"backend", std::string{ToConfigString(config.Backend)}},
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
        }));
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
            std::move(onChanged));
    }

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
            std::move(onChanged));
    }
}
