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

module Extrinsic.Runtime.Private.FeatureConfigCodecs;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;

namespace Extrinsic::Runtime::FeatureConfigDetail
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
                 "backend"});
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
                {"method",
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

    RunKMeans MakeConfiguredKMeansRequestImpl(
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

    std::string SerializeClusteringConfigImpl(
        const ClusteringConfig& config)
    {
        return json::object({
            {"cluster_count", config.Parameters.ClusterCount},
            {"max_iterations", config.Parameters.MaxIterations},
            {"seed", config.Parameters.Seed},
            {"initialization",
             std::string{ToConfigString(config.Parameters.Initialization)}},
            {"backend", std::string{ToConfigString(config.Backend)}},
        }).dump();
    }

    std::string SerializeCurvatureSegmentationConfigImpl(
        const CurvatureSegmentationConfig& config)
    {
        return json::object({
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
        }).dump();
    }

    std::string SerializeProgressivePoissonPlaygroundConfigImpl(
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
            {"auto_run_on_edit", config.AutoRunOnEdit},
            {"debounce_seconds", config.DebounceSeconds},
        }).dump();
    }

    std::string SerializeParameterizationConfigImpl(
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

    std::string SerializePointCloudConsolidationConfigImpl(
        const PointCloudConsolidationConfig& config)
    {
        return json::object({
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
        }).dump();
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidateClusteringConfigSectionImpl(
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
        result.CanonicalPayloadJson = SerializeClusteringConfigImpl(config);
        return result;
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidateCurvatureSegmentationConfigSectionImpl(
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
            SerializeCurvatureSegmentationConfigImpl(config);
        return result;
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidateProgressivePoissonConfigSectionImpl(
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
            SerializeProgressivePoissonPlaygroundConfigImpl(config);
        return result;
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidateParameterizationConfigSectionImpl(
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
        result.CanonicalPayloadJson = SerializeParameterizationConfigImpl(config);
        return result;
    }

    Core::Config::EngineConfigSectionValidationResult
    ValidatePointCloudConsolidationConfigSectionImpl(
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
            SerializePointCloudConsolidationConfigImpl(config);
        return result;
    }

    std::optional<ClusteringConfig> GetClusteringConfigImpl(
        const Core::Config::EngineConfig& config)
    {
        const Core::Config::EngineConfigSection* section =
            Core::Config::FindEngineConfigSection(
                config.AppSections,
                kClusteringConfigSectionName);
        if (section == nullptr ||
            section->SchemaId != kClusteringConfigSectionSchemaId ||
            section->SchemaVersion !=
                kClusteringConfigSectionSchemaVersion)
        {
            return std::nullopt;
        }
        const auto validated = ValidateClusteringConfigSectionImpl(
            section->PayloadJson,
            SerializeClusteringConfigImpl(ClusteringConfig{}),
            kClusteringConfigSectionName);
        if (validated.State != Core::Config::EngineConfigState::Valid)
            return std::nullopt;
        return DecodeClusteringCanonical(validated.CanonicalPayloadJson);
    }

    void SetClusteringConfigImpl(
        Core::Config::EngineConfig& config,
        const ClusteringConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(
            config.AppSections,
            Core::Config::EngineConfigSection{
                .Name = std::string{kClusteringConfigSectionName},
                .SchemaId = std::string{kClusteringConfigSectionSchemaId},
                .SchemaVersion = kClusteringConfigSectionSchemaVersion,
                .PayloadJson = SerializeClusteringConfigImpl(value),
            });
    }

    std::optional<CurvatureSegmentationConfig>
    GetCurvatureSegmentationConfigImpl(
        const Core::Config::EngineConfig& config)
    {
        const Core::Config::EngineConfigSection* section =
            Core::Config::FindEngineConfigSection(
                config.AppSections,
                kCurvatureSegmentationConfigSectionName);
        if (section == nullptr ||
            section->SchemaId !=
                kCurvatureSegmentationConfigSectionSchemaId ||
            section->SchemaVersion !=
                kCurvatureSegmentationConfigSectionSchemaVersion)
        {
            return std::nullopt;
        }
        const auto validated =
            ValidateCurvatureSegmentationConfigSectionImpl(
                section->PayloadJson,
                SerializeCurvatureSegmentationConfigImpl(
                    CurvatureSegmentationConfig{}),
                kCurvatureSegmentationConfigSectionName);
        if (validated.State != Core::Config::EngineConfigState::Valid)
            return std::nullopt;
        return DecodeCurvatureSegmentationCanonical(
            validated.CanonicalPayloadJson);
    }

    void SetCurvatureSegmentationConfigImpl(
        Core::Config::EngineConfig& config,
        const CurvatureSegmentationConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(
            config.AppSections,
            Core::Config::EngineConfigSection{
                .Name = std::string{
                    kCurvatureSegmentationConfigSectionName},
                .SchemaId = std::string{
                    kCurvatureSegmentationConfigSectionSchemaId},
                .SchemaVersion =
                    kCurvatureSegmentationConfigSectionSchemaVersion,
                .PayloadJson =
                    SerializeCurvatureSegmentationConfigImpl(value),
            });
    }

    std::optional<ProgressivePoissonPlaygroundConfig>
    GetProgressivePoissonPlaygroundConfigImpl(
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
        const auto validated = ValidateProgressivePoissonConfigSectionImpl(
            section->PayloadJson,
            SerializeProgressivePoissonPlaygroundConfigImpl(
                ProgressivePoissonPlaygroundConfig{}),
            kProgressivePoissonConfigSectionName);
        if (validated.State != Core::Config::EngineConfigState::Valid)
        {
            return std::nullopt;
        }
        return DecodeProgressivePoissonCanonical(
            validated.CanonicalPayloadJson);
    }

    void SetProgressivePoissonPlaygroundConfigImpl(
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
                    SerializeProgressivePoissonPlaygroundConfigImpl(value),
            });
    }

    std::optional<ParameterizationConfig> GetParameterizationConfigImpl(
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
        const auto validated = ValidateParameterizationConfigSectionImpl(
            section->PayloadJson,
            SerializeParameterizationConfigImpl(ParameterizationConfig{}),
            kParameterizationConfigSectionName);
        if (validated.State != Core::Config::EngineConfigState::Valid)
        {
            return std::nullopt;
        }
        return DecodeParameterizationCanonical(validated.CanonicalPayloadJson);
    }

    void SetParameterizationConfigImpl(
        Core::Config::EngineConfig& config,
        const ParameterizationConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(
            config.AppSections,
            Core::Config::EngineConfigSection{
                .Name = std::string{kParameterizationConfigSectionName},
                .SchemaId = std::string{kParameterizationConfigSectionSchemaId},
                .SchemaVersion = kParameterizationConfigSectionSchemaVersion,
                .PayloadJson = SerializeParameterizationConfigImpl(value),
            });
    }

    std::optional<PointCloudConsolidationConfig>
    GetPointCloudConsolidationConfigImpl(
        const Core::Config::EngineConfig& config)
    {
        const Core::Config::EngineConfigSection* section =
            Core::Config::FindEngineConfigSection(
                config.AppSections,
                kPointCloudConsolidationConfigSectionName);
        if (section == nullptr ||
            section->SchemaId !=
                kPointCloudConsolidationConfigSectionSchemaId ||
            section->SchemaVersion !=
                kPointCloudConsolidationConfigSectionSchemaVersion)
        {
            return std::nullopt;
        }
        const auto validated =
            ValidatePointCloudConsolidationConfigSectionImpl(
                section->PayloadJson,
                SerializePointCloudConsolidationConfigImpl(
                    PointCloudConsolidationConfig{}),
                kPointCloudConsolidationConfigSectionName);
        if (validated.State != Core::Config::EngineConfigState::Valid)
            return std::nullopt;
        return DecodePointCloudConsolidationCanonical(
            validated.CanonicalPayloadJson);
    }

    void SetPointCloudConsolidationConfigImpl(
        Core::Config::EngineConfig& config,
        const PointCloudConsolidationConfig& value)
    {
        Core::Config::UpsertEngineConfigSection(
            config.AppSections,
            Core::Config::EngineConfigSection{
                .Name = std::string{
                    kPointCloudConsolidationConfigSectionName},
                .SchemaId = std::string{
                    kPointCloudConsolidationConfigSectionSchemaId},
                .SchemaVersion =
                    kPointCloudConsolidationConfigSectionSchemaVersion,
                .PayloadJson =
                    SerializePointCloudConsolidationConfigImpl(value),
            });
    }

    Core::Config::EngineConfigSectionRegistration
    MakeClusteringConfigSectionRegistrationImpl(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return Core::Config::EngineConfigSectionRegistration{
            .DefaultSection =
                Core::Config::EngineConfigSection{
                    .Name = std::string{kClusteringConfigSectionName},
                    .SchemaId =
                        std::string{kClusteringConfigSectionSchemaId},
                    .SchemaVersion =
                        kClusteringConfigSectionSchemaVersion,
                    .PayloadJson =
                        SerializeClusteringConfigImpl(ClusteringConfig{}),
                },
            .Validate = ValidateClusteringConfigSectionImpl,
            .OnChanged = std::move(onChanged),
        };
    }

    Core::Config::EngineConfigSectionRegistration
    MakeCurvatureSegmentationConfigSectionRegistrationImpl(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return Core::Config::EngineConfigSectionRegistration{
            .DefaultSection =
                Core::Config::EngineConfigSection{
                    .Name = std::string{
                        kCurvatureSegmentationConfigSectionName},
                    .SchemaId = std::string{
                        kCurvatureSegmentationConfigSectionSchemaId},
                    .SchemaVersion =
                        kCurvatureSegmentationConfigSectionSchemaVersion,
                    .PayloadJson =
                        SerializeCurvatureSegmentationConfigImpl(
                            CurvatureSegmentationConfig{}),
                },
            .Validate =
                ValidateCurvatureSegmentationConfigSectionImpl,
            .OnChanged = std::move(onChanged),
        };
    }

    Core::Config::EngineConfigSectionRegistration
    MakeProgressivePoissonConfigSectionRegistrationImpl(
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
                        SerializeProgressivePoissonPlaygroundConfigImpl(
                            ProgressivePoissonPlaygroundConfig{}),
                },
            .Validate = ValidateProgressivePoissonConfigSectionImpl,
            .OnChanged = std::move(onChanged),
        };
    }

    Core::Config::EngineConfigSectionRegistration
    MakeParameterizationConfigSectionRegistrationImpl(
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
                        SerializeParameterizationConfigImpl(
                            ParameterizationConfig{}),
                },
            .Validate = ValidateParameterizationConfigSectionImpl,
            .OnChanged = std::move(onChanged),
        };
    }

    Core::Config::EngineConfigSectionRegistration
    MakePointCloudConsolidationConfigSectionRegistrationImpl(
        Core::Config::EngineConfigSectionChangedCallback onChanged)
    {
        return Core::Config::EngineConfigSectionRegistration{
            .DefaultSection =
                Core::Config::EngineConfigSection{
                    .Name = std::string{
                        kPointCloudConsolidationConfigSectionName},
                    .SchemaId = std::string{
                        kPointCloudConsolidationConfigSectionSchemaId},
                    .SchemaVersion =
                        kPointCloudConsolidationConfigSectionSchemaVersion,
                    .PayloadJson =
                        SerializePointCloudConsolidationConfigImpl(
                            PointCloudConsolidationConfig{}),
                },
            .Validate =
                ValidatePointCloudConsolidationConfigSectionImpl,
            .OnChanged = std::move(onChanged),
        };
    }
}
