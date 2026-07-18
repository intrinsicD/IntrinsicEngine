module;

#include <algorithm>
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
#include <vector>

#include <nlohmann/json.hpp>

module Extrinsic.Runtime.SandboxConfigSections;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

namespace Extrinsic::Runtime
{
    namespace
    {
        using json = nlohmann::json;

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
            json object = json::parse(payload, nullptr, false);
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
            if (value == "Phase") return ProgressivePoissonPlaygroundChannel::Phase;
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
            case ProgressivePoissonPlaygroundChannel::Phase: return "Phase";
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
                {"dimension",
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
                 "mesh_surface_sample_count",
                 "mesh_surface_seed",
                 "mesh_surface_min_triangle_area",
                 "mesh_surface_interpolate_normals",
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
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "mesh_surface_sample_count",
                    1,
                    10'000'000))
            {
                config.MeshSurfaceSampleCount =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadInteger(
                    context,
                    *object,
                    "mesh_surface_seed",
                    0,
                    std::numeric_limits<std::int32_t>::max()))
            {
                config.MeshSurfaceSampleSeed =
                    static_cast<std::uint32_t>(*value);
                CountParsed(context);
            }
            if (const auto value = ReadNumber(
                    context,
                    *object,
                    "mesh_surface_min_triangle_area",
                    1.0e-30,
                    1.0e30))
            {
                config.MeshSurfaceMinTriangleArea = *value;
                CountParsed(context);
            }
            if (const auto value =
                    ReadBool(context, *object, "mesh_surface_interpolate_normals"))
            {
                config.MeshSurfaceInterpolateNormals = *value;
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
                {"strategy", "lscm", "harmonic", "bff", "view"});
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
                         "show_distortion_heatmap"});
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

        [[nodiscard]] ParameterizationConfig DecodeParameterizationCanonical(
            const std::string_view payload)
        {
            return ParseParameterization(
                payload,
                ParameterizationConfig{},
                ValidationContext{});
        }
    }

    std::string SerializeProgressivePoissonPlaygroundConfig(
        const ProgressivePoissonPlaygroundConfig& config)
    {
        return json::object({
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
            {"mesh_surface_sample_count", config.MeshSurfaceSampleCount},
            {"mesh_surface_seed", config.MeshSurfaceSampleSeed},
            {"mesh_surface_min_triangle_area",
             config.MeshSurfaceMinTriangleArea},
            {"mesh_surface_interpolate_normals",
             config.MeshSurfaceInterpolateNormals},
            {"auto_run_on_edit", config.AutoRunOnEdit},
            {"debounce_seconds", config.DebounceSeconds},
        }).dump();
    }

    std::string SerializeParameterizationConfig(
        const ParameterizationConfig& config)
    {
        return json::object({
            {"strategy", std::string{ToConfigString(config.Strategy)}},
            {"view",
             json::object({
                 {"render_mode",
                  std::string{ToConfigString(config.View.RenderMode)}},
                 {"background_mode",
                  std::string{ToConfigString(config.View.BackgroundMode)}},
                 {"show_distortion_heatmap",
                  config.View.ShowDistortionHeatmap},
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
        }).dump();
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

    std::optional<ProgressivePoissonPlaygroundConfig>
    GetProgressivePoissonPlaygroundConfig(
        const Core::Config::EngineConfig& config)
    {
        const Core::Config::EngineConfigSection* section =
            Core::Config::FindEngineConfigSection(
                config.AppSections,
                kProgressivePoissonConfigSectionName);
        if (section == nullptr ||
            section->SchemaId != kProgressivePoissonConfigSectionSchemaId ||
            section->SchemaVersion !=
                kProgressivePoissonConfigSectionSchemaVersion)
        {
            return std::nullopt;
        }
        const auto validated = ValidateProgressivePoissonConfigSection(
            section->PayloadJson,
            SerializeProgressivePoissonPlaygroundConfig(
                ProgressivePoissonPlaygroundConfig{}),
            kProgressivePoissonConfigSectionName);
        if (validated.State != Core::Config::EngineConfigState::Valid)
        {
            return std::nullopt;
        }
        return DecodeProgressivePoissonCanonical(
            validated.CanonicalPayloadJson);
    }

    void SetProgressivePoissonPlaygroundConfig(
        Core::Config::EngineConfig& config,
        const ProgressivePoissonPlaygroundConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(
            config.AppSections,
            Core::Config::EngineConfigSection{
                .Name = std::string{kProgressivePoissonConfigSectionName},
                .SchemaId =
                    std::string{kProgressivePoissonConfigSectionSchemaId},
                .SchemaVersion =
                    kProgressivePoissonConfigSectionSchemaVersion,
                .PayloadJson =
                    SerializeProgressivePoissonPlaygroundConfig(value),
            });
    }

    std::optional<ParameterizationConfig> GetParameterizationConfig(
        const Core::Config::EngineConfig& config)
    {
        const Core::Config::EngineConfigSection* section =
            Core::Config::FindEngineConfigSection(
                config.AppSections,
                kParameterizationConfigSectionName);
        if (section == nullptr ||
            section->SchemaId != kParameterizationConfigSectionSchemaId ||
            section->SchemaVersion !=
                kParameterizationConfigSectionSchemaVersion)
        {
            return std::nullopt;
        }
        const auto validated = ValidateParameterizationConfigSection(
            section->PayloadJson,
            SerializeParameterizationConfig(ParameterizationConfig{}),
            kParameterizationConfigSectionName);
        if (validated.State != Core::Config::EngineConfigState::Valid)
        {
            return std::nullopt;
        }
        return DecodeParameterizationCanonical(validated.CanonicalPayloadJson);
    }

    void SetParameterizationConfig(
        Core::Config::EngineConfig& config,
        const ParameterizationConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(
            config.AppSections,
            Core::Config::EngineConfigSection{
                .Name = std::string{kParameterizationConfigSectionName},
                .SchemaId = std::string{kParameterizationConfigSectionSchemaId},
                .SchemaVersion = kParameterizationConfigSectionSchemaVersion,
                .PayloadJson = SerializeParameterizationConfig(value),
            });
    }

    Core::Config::EngineConfigSectionRegistration
    MakeProgressivePoissonConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return Core::Config::EngineConfigSectionRegistration{
            .DefaultSection =
                Core::Config::EngineConfigSection{
                    .Name =
                        std::string{kProgressivePoissonConfigSectionName},
                    .SchemaId =
                        std::string{
                            kProgressivePoissonConfigSectionSchemaId},
                    .SchemaVersion =
                        kProgressivePoissonConfigSectionSchemaVersion,
                    .PayloadJson =
                        SerializeProgressivePoissonPlaygroundConfig(
                            ProgressivePoissonPlaygroundConfig{}),
                },
            .Validate = ValidateProgressivePoissonConfigSection,
            .OnChanged = std::move(onChanged),
        };
    }

    Core::Config::EngineConfigSectionRegistration
    MakeParameterizationConfigSectionRegistration(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return Core::Config::EngineConfigSectionRegistration{
            .DefaultSection =
                Core::Config::EngineConfigSection{
                    .Name = std::string{kParameterizationConfigSectionName},
                    .SchemaId =
                        std::string{kParameterizationConfigSectionSchemaId},
                    .SchemaVersion =
                        kParameterizationConfigSectionSchemaVersion,
                    .PayloadJson =
                        SerializeParameterizationConfig(
                            ParameterizationConfig{}),
                },
            .Validate = ValidateParameterizationConfigSection,
            .OnChanged = std::move(onChanged),
        };
    }
}
