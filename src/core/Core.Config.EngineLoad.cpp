module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

module Extrinsic.Core.Config.EngineLoad;

import Extrinsic.Core.Config.Render;
import Extrinsic.Core.Config.Simulation;
import Extrinsic.Core.Config.Window;
import Extrinsic.Core.Error;
import Extrinsic.Core.IOBackend;

namespace Extrinsic::Core::Config
{
    namespace
    {
        using json = nlohmann::json;

        [[nodiscard]] const json* FindMember(const json& object, const std::string_view key)
        {
            const auto it = object.find(std::string{key});
            return it == object.end() ? nullptr : &*it;
        }

        [[nodiscard]] bool Contains(const std::initializer_list<std::string_view> allowed,
                                    const std::string_view key) noexcept
        {
            return std::find(allowed.begin(), allowed.end(), key) != allowed.end();
        }

        void AddDiagnostic(EngineConfigLoadResult& result,
                           const EngineConfigState state,
                           const EngineConfigDiagnosticSeverity severity,
                           const EngineConfigDiagnosticCode code,
                           std::string subject,
                           std::string message)
        {
            result.Diagnostics.push_back(EngineConfigDiagnostic{
                .State = state,
                .Severity = severity,
                .Code = code,
                .Subject = std::move(subject),
                .Message = std::move(message),
            });
        }

        void AddError(EngineConfigLoadResult& result,
                      const EngineConfigState state,
                      const EngineConfigDiagnosticCode code,
                      std::string subject,
                      std::string message)
        {
            AddDiagnostic(result,
                          state,
                          EngineConfigDiagnosticSeverity::Error,
                          code,
                          std::move(subject),
                          std::move(message));
        }

        void AddWarning(EngineConfigLoadResult& result,
                        const EngineConfigDiagnosticCode code,
                        std::string subject,
                        std::string message)
        {
            AddDiagnostic(result,
                          EngineConfigState::FallbackApplied,
                          EngineConfigDiagnosticSeverity::Warning,
                          code,
                          std::move(subject),
                          std::move(message));
        }

        void AddInfo(EngineConfigLoadResult& result,
                     const EngineConfigDiagnosticCode code,
                     std::string subject,
                     std::string message)
        {
            AddDiagnostic(result,
                          EngineConfigState::FallbackApplied,
                          EngineConfigDiagnosticSeverity::Info,
                          code,
                          std::move(subject),
                          std::move(message));
        }

        void AddUnknownFieldDiagnostics(EngineConfigLoadResult& result,
                                        const json& object,
                                        const std::string_view path,
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
                    AddWarning(result,
                               EngineConfigDiagnosticCode::UnknownField,
                               std::string{path} + "." + key,
                               "Unknown engine config field; reference defaults remain authoritative for this field.");
                }
            }
        }

        [[nodiscard]] std::string FieldSubject(const std::string_view path,
                                               const std::string_view key)
        {
            return std::string{path} + "." + std::string{key};
        }

        [[nodiscard]] std::optional<bool> ReadBool(EngineConfigLoadResult& result,
                                                   const json& object,
                                                   const std::string_view key,
                                                   const std::string_view path)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_boolean())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           FieldSubject(path, key),
                           "Expected a boolean value; reference default retained.");
                return std::nullopt;
            }
            return value->get<bool>();
        }

        [[nodiscard]] std::optional<std::string> ReadString(EngineConfigLoadResult& result,
                                                            const json& object,
                                                            const std::string_view key,
                                                            const std::string_view path,
                                                            const bool allowEmpty)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_string())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           FieldSubject(path, key),
                           "Expected a string value; reference default retained.");
                return std::nullopt;
            }
            std::string text = value->get<std::string>();
            if (!allowEmpty && text.empty())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           FieldSubject(path, key),
                           "Expected a non-empty string value; reference default retained.");
                return std::nullopt;
            }
            return text;
        }

        [[nodiscard]] std::optional<std::int64_t> ReadInteger(EngineConfigLoadResult& result,
                                                              const json& object,
                                                              const std::string_view key,
                                                              const std::string_view path,
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
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           FieldSubject(path, key),
                           "Expected an integer value; reference default retained.");
                return std::nullopt;
            }
            const auto number = value->get<std::int64_t>();
            if (number < minValue || number > maxValue)
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           FieldSubject(path, key),
                           "Integer value is outside the supported range; reference default retained.");
                return std::nullopt;
            }
            return number;
        }

        [[nodiscard]] std::optional<double> ReadNumber(EngineConfigLoadResult& result,
                                                       const json& object,
                                                       const std::string_view key,
                                                       const std::string_view path,
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
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           FieldSubject(path, key),
                           "Expected a numeric value; reference default retained.");
                return std::nullopt;
            }
            const double number = value->get<double>();
            if (!std::isfinite(number) || number < minValue || number > maxValue)
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           FieldSubject(path, key),
                           "Numeric value is outside the supported range; reference default retained.");
                return std::nullopt;
            }
            return number;
        }

        [[nodiscard]] std::optional<ParameterizationUvConfig> ReadUv(
            EngineConfigLoadResult& result,
            const json& object,
            const std::string_view key,
            const std::string_view path)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_array() || value->size() != 2u ||
                !(*value)[0].is_number() || !(*value)[1].is_number())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           FieldSubject(path, key),
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
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           FieldSubject(path, key),
                           "UV coordinates must be finite and representable as floats; reference default retained.");
                return std::nullopt;
            }
            return ParameterizationUvConfig{.U = u, .V = v};
        }

        [[nodiscard]] std::optional<std::vector<std::uint32_t>> ReadIndexArray(
            EngineConfigLoadResult& result,
            const json& object,
            const std::string_view key,
            const std::string_view path)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_array())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           FieldSubject(path, key),
                           "Expected an array of vertex indices; reference default retained.");
                return std::nullopt;
            }

            std::vector<std::uint32_t> indices{};
            indices.reserve(value->size());
            for (std::size_t index = 0; index < value->size(); ++index)
            {
                const json& element = (*value)[index];
                if (!element.is_number_integer())
                {
                    AddWarning(result,
                               EngineConfigDiagnosticCode::InvalidValue,
                               FieldSubject(path, key) + "[" + std::to_string(index) + "]",
                               "Expected a non-negative 32-bit vertex index; reference array retained.");
                    return std::nullopt;
                }
                const std::int64_t parsed = element.get<std::int64_t>();
                if (parsed < 0 ||
                    parsed > static_cast<std::int64_t>(
                                 std::numeric_limits<std::uint32_t>::max()))
                {
                    AddWarning(result,
                               EngineConfigDiagnosticCode::InvalidValue,
                               FieldSubject(path, key) + "[" + std::to_string(index) + "]",
                               "Vertex index is outside the supported range; reference array retained.");
                    return std::nullopt;
                }
                indices.push_back(static_cast<std::uint32_t>(parsed));
            }
            return indices;
        }

        [[nodiscard]] std::optional<std::vector<ParameterizationUvConfig>> ReadUvArray(
            EngineConfigLoadResult& result,
            const json& object,
            const std::string_view key,
            const std::string_view path)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_array())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           FieldSubject(path, key),
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
                    AddWarning(result,
                               EngineConfigDiagnosticCode::InvalidValue,
                               FieldSubject(path, key) + "[" + std::to_string(index) + "]",
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
                    AddWarning(result,
                               EngineConfigDiagnosticCode::InvalidValue,
                               FieldSubject(path, key) + "[" + std::to_string(index) + "]",
                               "UV coordinates must be finite and representable as floats; reference array retained.");
                    return std::nullopt;
                }
                uvs.push_back(ParameterizationUvConfig{.U = u, .V = v});
            }
            return uvs;
        }

        [[nodiscard]] std::optional<std::vector<double>> ReadNumberArray(
            EngineConfigLoadResult& result,
            const json& object,
            const std::string_view key,
            const std::string_view path)
        {
            const json* value = FindMember(object, key);
            if (value == nullptr)
            {
                return std::nullopt;
            }
            if (!value->is_array())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           FieldSubject(path, key),
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
                    AddWarning(result,
                               EngineConfigDiagnosticCode::InvalidValue,
                               FieldSubject(path, key) + "[" + std::to_string(index) + "]",
                               "Expected a finite numeric value; reference array retained.");
                    return std::nullopt;
                }
                const double number = element.get<double>();
                if (!std::isfinite(number))
                {
                    AddWarning(result,
                               EngineConfigDiagnosticCode::InvalidValue,
                               FieldSubject(path, key) + "[" + std::to_string(index) + "]",
                               "Expected a finite numeric value; reference array retained.");
                    return std::nullopt;
                }
                numbers.push_back(number);
            }
            return numbers;
        }

        [[nodiscard]] std::optional<GraphicsBackend> ParseGraphicsBackend(
            const std::string_view value) noexcept
        {
            if (value == "Vulkan") return GraphicsBackend::Vulkan;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<WindowBackend> ParseWindowBackend(
            const std::string_view value) noexcept
        {
            if (value == "Configured") return WindowBackend::Configured;
            if (value == "Null") return WindowBackend::Null;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ReferenceSceneSelector> ParseReferenceSceneSelector(
            const std::string_view value) noexcept
        {
            if (value == "Triangle") return ReferenceSceneSelector::Triangle;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<CameraControllerKind> ParseCameraController(
            const std::string_view value) noexcept
        {
            if (value == "Orbit") return CameraControllerKind::Orbit;
            if (value == "Fly") return CameraControllerKind::Fly;
            if (value == "FreeLook") return CameraControllerKind::FreeLook;
            if (value == "TopDown") return CameraControllerKind::TopDown;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ProgressivePoissonPlaygroundChannel>
        ParseProgressivePoissonChannel(const std::string_view value) noexcept
        {
            if (value == "Level") return ProgressivePoissonPlaygroundChannel::Level;
            if (value == "Phase") return ProgressivePoissonPlaygroundChannel::Phase;
            if (value == "SplatRadius") return ProgressivePoissonPlaygroundChannel::SplatRadius;
            if (value == "PrefixVisible") return ProgressivePoissonPlaygroundChannel::PrefixVisible;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ProgressivePoissonPlaygroundBackend>
        ParseProgressivePoissonBackend(const std::string_view value) noexcept
        {
            if (value == "CpuReference") return ProgressivePoissonPlaygroundBackend::CpuReference;
            if (value == "VulkanCompute") return ProgressivePoissonPlaygroundBackend::VulkanCompute;
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
            if (value == "cpu_layout") return ParameterizationUvRenderMode::CpuLayout;
            if (value == "gpu_shaded") return ParameterizationUvRenderMode::GpuShaded;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<ParameterizationUvBackgroundMode>
        ParseParameterizationUvBackgroundMode(const std::string_view value) noexcept
        {
            if (value == "grid") return ParameterizationUvBackgroundMode::Grid;
            if (value == "checker") return ParameterizationUvBackgroundMode::Checker;
            if (value == "texel_density")
                return ParameterizationUvBackgroundMode::TexelDensity;
            if (value == "texture") return ParameterizationUvBackgroundMode::Texture;
            return std::nullopt;
        }

        [[nodiscard]] std::string_view ToConfigString(const GraphicsBackend value) noexcept
        {
            switch (value)
            {
            case GraphicsBackend::Vulkan: return "Vulkan";
            }
            return "Vulkan";
        }

        [[nodiscard]] std::string_view ToConfigString(const WindowBackend value) noexcept
        {
            switch (value)
            {
            case WindowBackend::Configured: return "Configured";
            case WindowBackend::Null: return "Null";
            }
            return "Configured";
        }

        [[nodiscard]] std::string_view ToConfigString(const ReferenceSceneSelector value) noexcept
        {
            switch (value)
            {
            case ReferenceSceneSelector::Triangle: return "Triangle";
            }
            return "Triangle";
        }

        [[nodiscard]] std::string_view ToConfigString(const CameraControllerKind value) noexcept
        {
            switch (value)
            {
            case CameraControllerKind::Orbit: return "Orbit";
            case CameraControllerKind::Fly: return "Fly";
            case CameraControllerKind::FreeLook: return "FreeLook";
            case CameraControllerKind::TopDown: return "TopDown";
            }
            return "Orbit";
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ProgressivePoissonPlaygroundChannel value) noexcept
        {
            switch (value)
            {
            case ProgressivePoissonPlaygroundChannel::Level: return "Level";
            case ProgressivePoissonPlaygroundChannel::Phase: return "Phase";
            case ProgressivePoissonPlaygroundChannel::SplatRadius: return "SplatRadius";
            case ProgressivePoissonPlaygroundChannel::PrefixVisible: return "PrefixVisible";
            }
            return "Level";
        }

        [[nodiscard]] std::string_view ToConfigString(
            const ProgressivePoissonPlaygroundBackend value) noexcept
        {
            switch (value)
            {
            case ProgressivePoissonPlaygroundBackend::CpuReference: return "CpuReference";
            case ProgressivePoissonPlaygroundBackend::VulkanCompute: return "VulkanCompute";
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
            case ParameterizationStrategyKind::TutteUniform: return "tutte_uniform";
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

        template <typename Enum, typename Parser>
        bool ReadEnum(EngineConfigLoadResult& result,
                      const json& object,
                      const std::string_view key,
                      const std::string_view path,
                      Parser parse,
                      Enum& outValue)
        {
            const std::optional<std::string> text = ReadString(result, object, key, path, false);
            if (!text.has_value())
            {
                return false;
            }
            const std::optional<Enum> parsed = parse(*text);
            if (!parsed.has_value())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           FieldSubject(path, key),
                           "Unsupported enum value; reference default retained.");
                return false;
            }
            outValue = *parsed;
            return true;
        }

        void ParseWindowConfig(EngineConfigLoadResult& result, const json& root)
        {
            const json* object = FindMember(root, "window");
            if (object == nullptr)
            {
                return;
            }
            if (!object->is_object())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           "window",
                           "Expected an object; reference window config retained.");
                return;
            }

            AddUnknownFieldDiagnostics(result,
                                       *object,
                                       "window",
                                       {"title", "width", "height", "resizable", "backend"});

            WindowConfig& config = result.Preview.Config.Window;
            if (const std::optional<std::string> title =
                    ReadString(result, *object, "title", "window", false);
                title.has_value())
            {
                config.Title = *title;
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<std::int64_t> width =
                    ReadInteger(result, *object, "width", "window", 1, 32768);
                width.has_value())
            {
                config.Width = static_cast<int>(*width);
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<std::int64_t> height =
                    ReadInteger(result, *object, "height", "window", 1, 32768);
                height.has_value())
            {
                config.Height = static_cast<int>(*height);
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<bool> resizable =
                    ReadBool(result, *object, "resizable", "window");
                resizable.has_value())
            {
                config.Resizable = *resizable;
                ++result.Preview.ParsedFieldCount;
            }
            if (ReadEnum(result, *object, "backend", "window", ParseWindowBackend, config.Backend))
            {
                ++result.Preview.ParsedFieldCount;
            }
        }

        void ParseRenderConfig(EngineConfigLoadResult& result, const json& root)
        {
            const json* object = FindMember(root, "render");
            if (object == nullptr)
            {
                return;
            }
            if (!object->is_object())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           "render",
                           "Expected an object; reference render config retained.");
                return;
            }

            AddUnknownFieldDiagnostics(result,
                                       *object,
                                       "render",
                                       {"backend",
                                        "enable_promoted_vulkan_device",
                                        "enable_validation",
                                        "enable_vsync",
                                        "frames_in_flight",
                                        "default_recipe_config_path",
                                        "synchronous_extraction"});

            RenderConfig& config = result.Preview.Config.Render;
            if (ReadEnum(result, *object, "backend", "render", ParseGraphicsBackend, config.Backend))
            {
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<bool> promoted =
                    ReadBool(result, *object, "enable_promoted_vulkan_device", "render");
                promoted.has_value())
            {
                config.EnablePromotedVulkanDevice = *promoted;
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<bool> validation =
                    ReadBool(result, *object, "enable_validation", "render");
                validation.has_value())
            {
                config.EnableValidation = *validation;
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<bool> vsync =
                    ReadBool(result, *object, "enable_vsync", "render");
                vsync.has_value())
            {
                config.EnableVSync = *vsync;
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<std::int64_t> frames =
                    ReadInteger(result, *object, "frames_in_flight", "render", 1, 8);
                frames.has_value())
            {
                config.FramesInFlight = static_cast<std::uint32_t>(*frames);
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<std::string> defaultRecipe =
                    ReadString(result, *object, "default_recipe_config_path", "render", true);
                defaultRecipe.has_value())
            {
                config.DefaultRecipeConfigPath = *defaultRecipe;
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<bool> synchronous =
                    ReadBool(result, *object, "synchronous_extraction", "render");
                synchronous.has_value())
            {
                config.SynchronousExtraction = *synchronous;
                ++result.Preview.ParsedFieldCount;
            }
        }

        void ParseSimulationConfig(EngineConfigLoadResult& result, const json& root)
        {
            const json* object = FindMember(root, "simulation");
            if (object == nullptr)
            {
                return;
            }
            if (!object->is_object())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           "simulation",
                           "Expected an object; reference simulation config retained.");
                return;
            }

            AddUnknownFieldDiagnostics(result, *object, "simulation", {"worker_thread_count"});

            SimulationConfig& config = result.Preview.Config.Simulation;
            if (const std::optional<std::int64_t> workers =
                    ReadInteger(result, *object, "worker_thread_count", "simulation", 0, 1024);
                workers.has_value())
            {
                config.WorkerThreadCount = static_cast<unsigned>(*workers);
                ++result.Preview.ParsedFieldCount;
            }
        }

        void ParseReferenceSceneConfig(EngineConfigLoadResult& result, const json& root)
        {
            const json* object = FindMember(root, "reference_scene");
            if (object == nullptr)
            {
                return;
            }
            if (!object->is_object())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           "reference_scene",
                           "Expected an object; reference scene config retained.");
                return;
            }

            AddUnknownFieldDiagnostics(result, *object, "reference_scene", {"enabled", "selector"});

            ReferenceSceneConfig& config = result.Preview.Config.ReferenceScene;
            if (const std::optional<bool> enabled =
                    ReadBool(result, *object, "enabled", "reference_scene");
                enabled.has_value())
            {
                config.Enabled = *enabled;
                ++result.Preview.ParsedFieldCount;
            }
            if (ReadEnum(result,
                         *object,
                         "selector",
                         "reference_scene",
                         ParseReferenceSceneSelector,
                         config.Selector))
            {
                ++result.Preview.ParsedFieldCount;
            }
        }

        void ParseCameraConfig(EngineConfigLoadResult& result, const json& root)
        {
            const json* object = FindMember(root, "camera");
            if (object == nullptr)
            {
                return;
            }
            if (!object->is_object())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           "camera",
                           "Expected an object; reference camera config retained.");
                return;
            }

            AddUnknownFieldDiagnostics(result, *object, "camera", {"enabled", "controller"});

            CameraConfig& config = result.Preview.Config.Camera;
            if (const std::optional<bool> enabled =
                    ReadBool(result, *object, "enabled", "camera");
                enabled.has_value())
            {
                config.Enabled = *enabled;
                ++result.Preview.ParsedFieldCount;
            }
            if (ReadEnum(result,
                         *object,
                         "controller",
                         "camera",
                         ParseCameraController,
                         config.Controller))
            {
                ++result.Preview.ParsedFieldCount;
            }
        }

        void ParseParameterizationConfig(EngineConfigLoadResult& result,
                                         const json& sandbox)
        {
            const json* object = FindMember(sandbox, "parameterization");
            if (object == nullptr)
            {
                return;
            }
            if (!object->is_object())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           "sandbox.parameterization",
                           "Expected an object; reference parameterization config retained.");
                return;
            }

            AddUnknownFieldDiagnostics(
                result,
                *object,
                "sandbox.parameterization",
                {"strategy", "lscm", "harmonic", "bff", "view"});

            ParameterizationConfig& config =
                result.Preview.Config.Sandbox.Parameterization;
            const ParameterizationLscmConfig referenceLscm = config.Lscm;
            const ParameterizationBffConfig referenceBff = config.Bff;
            if (ReadEnum(result,
                         *object,
                         "strategy",
                         "sandbox.parameterization",
                         ParseParameterizationStrategy,
                         config.Strategy))
            {
                ++result.Preview.ParsedFieldCount;
            }

            if (const json* view = FindMember(*object, "view"); view != nullptr)
            {
                constexpr std::string_view kPath =
                    "sandbox.parameterization.view";
                if (!view->is_object())
                {
                    AddWarning(result,
                               EngineConfigDiagnosticCode::InvalidValue,
                               std::string{kPath},
                               "Expected an object; reference parameterization view config retained.");
                }
                else
                {
                    AddUnknownFieldDiagnostics(
                        result,
                        *view,
                        kPath,
                        {"render_mode",
                         "background_mode",
                         "show_distortion_heatmap"});
                    if (ReadEnum(result,
                                 *view,
                                 "render_mode",
                                 kPath,
                                 ParseParameterizationUvRenderMode,
                                 config.View.RenderMode))
                    {
                        ++result.Preview.ParsedFieldCount;
                    }
                    if (ReadEnum(result,
                                 *view,
                                 "background_mode",
                                 kPath,
                                 ParseParameterizationUvBackgroundMode,
                                 config.View.BackgroundMode))
                    {
                        ++result.Preview.ParsedFieldCount;
                    }
                    if (const std::optional<bool> showDistortionHeatmap =
                            ReadBool(result,
                                     *view,
                                     "show_distortion_heatmap",
                                     kPath);
                        showDistortionHeatmap.has_value())
                    {
                        config.View.ShowDistortionHeatmap =
                            *showDistortionHeatmap;
                        ++result.Preview.ParsedFieldCount;
                    }
                }
            }

            if (const json* lscm = FindMember(*object, "lscm"); lscm != nullptr)
            {
                constexpr std::string_view kPath = "sandbox.parameterization.lscm";
                if (!lscm->is_object())
                {
                    AddWarning(result,
                               EngineConfigDiagnosticCode::InvalidValue,
                               std::string{kPath},
                               "Expected an object; reference LSCM config retained.");
                }
                else
                {
                    AddUnknownFieldDiagnostics(
                        result,
                        *lscm,
                        kPath,
                        {"auto_pins",
                         "pin_vertex_0",
                         "pin_vertex_1",
                         "pin_uv_0",
                         "pin_uv_1",
                         "solver_tolerance",
                         "max_solver_iterations"});

                    if (const std::optional<bool> autoPins =
                            ReadBool(result, *lscm, "auto_pins", kPath);
                        autoPins.has_value())
                    {
                        config.Lscm.AutoPins = *autoPins;
                        ++result.Preview.ParsedFieldCount;
                    }
                    if (const std::optional<std::int64_t> pin = ReadInteger(
                            result,
                            *lscm,
                            "pin_vertex_0",
                            kPath,
                            0,
                            std::numeric_limits<std::uint32_t>::max());
                        pin.has_value())
                    {
                        config.Lscm.PinVertex0 = static_cast<std::uint32_t>(*pin);
                        ++result.Preview.ParsedFieldCount;
                    }
                    if (const std::optional<std::int64_t> pin = ReadInteger(
                            result,
                            *lscm,
                            "pin_vertex_1",
                            kPath,
                            0,
                            std::numeric_limits<std::uint32_t>::max());
                        pin.has_value())
                    {
                        config.Lscm.PinVertex1 = static_cast<std::uint32_t>(*pin);
                        ++result.Preview.ParsedFieldCount;
                    }
                    if (const std::optional<ParameterizationUvConfig> uv =
                            ReadUv(result, *lscm, "pin_uv_0", kPath);
                        uv.has_value())
                    {
                        config.Lscm.PinUv0 = *uv;
                        ++result.Preview.ParsedFieldCount;
                    }
                    if (const std::optional<ParameterizationUvConfig> uv =
                            ReadUv(result, *lscm, "pin_uv_1", kPath);
                        uv.has_value())
                    {
                        config.Lscm.PinUv1 = *uv;
                        ++result.Preview.ParsedFieldCount;
                    }
                    if (const std::optional<double> tolerance = ReadNumber(
                            result,
                            *lscm,
                            "solver_tolerance",
                            kPath,
                            std::numeric_limits<double>::min(),
                            1.0e30);
                        tolerance.has_value())
                    {
                        config.Lscm.SolverTolerance = *tolerance;
                        ++result.Preview.ParsedFieldCount;
                    }
                    if (const std::optional<std::int64_t> iterations = ReadInteger(
                            result,
                            *lscm,
                            "max_solver_iterations",
                            kPath,
                            1,
                            std::numeric_limits<std::uint32_t>::max());
                        iterations.has_value())
                    {
                        config.Lscm.MaxSolverIterations =
                            static_cast<std::uint32_t>(*iterations);
                        ++result.Preview.ParsedFieldCount;
                    }
                }
            }

            if (const json* harmonic = FindMember(*object, "harmonic");
                harmonic != nullptr)
            {
                constexpr std::string_view kPath =
                    "sandbox.parameterization.harmonic";
                if (!harmonic->is_object())
                {
                    AddWarning(result,
                               EngineConfigDiagnosticCode::InvalidValue,
                               std::string{kPath},
                               "Expected an object; reference harmonic config retained.");
                }
                else
                {
                    AddUnknownFieldDiagnostics(
                        result,
                        *harmonic,
                        kPath,
                        {"boundary",
                         "arc_length_spacing",
                         "clamp_non_convex_weights",
                         "pinned_vertices",
                         "pinned_uvs"});

                    if (ReadEnum(result,
                                 *harmonic,
                                 "boundary",
                                 kPath,
                                 ParseParameterizationBoundaryPolicy,
                                 config.Harmonic.Boundary))
                    {
                        ++result.Preview.ParsedFieldCount;
                    }
                    if (const std::optional<bool> spacing = ReadBool(
                            result, *harmonic, "arc_length_spacing", kPath);
                        spacing.has_value())
                    {
                        config.Harmonic.ArcLengthSpacing = *spacing;
                        ++result.Preview.ParsedFieldCount;
                    }
                    if (const std::optional<bool> clamp = ReadBool(
                            result,
                            *harmonic,
                            "clamp_non_convex_weights",
                            kPath);
                        clamp.has_value())
                    {
                        config.Harmonic.ClampNonConvexWeights = *clamp;
                        ++result.Preview.ParsedFieldCount;
                    }

                    const bool hasVertices =
                        FindMember(*harmonic, "pinned_vertices") != nullptr;
                    const bool hasUvs = FindMember(*harmonic, "pinned_uvs") != nullptr;
                    const std::optional<std::vector<std::uint32_t>> vertices =
                        ReadIndexArray(result, *harmonic, "pinned_vertices", kPath);
                    const std::optional<std::vector<ParameterizationUvConfig>> uvs =
                        ReadUvArray(result, *harmonic, "pinned_uvs", kPath);
                    if (hasVertices != hasUvs)
                    {
                        AddWarning(
                            result,
                            EngineConfigDiagnosticCode::InvalidValue,
                            std::string{kPath},
                            "Pinned vertex and UV arrays must be provided together; reference arrays retained.");
                    }
                    else if (hasVertices && vertices.has_value() && uvs.has_value())
                    {
                        if (vertices->size() != uvs->size())
                        {
                            AddWarning(
                                result,
                                EngineConfigDiagnosticCode::InvalidValue,
                                std::string{kPath},
                                "Pinned vertex and UV arrays must have equal length; reference arrays retained.");
                        }
                        else
                        {
                            config.Harmonic.PinnedVertices = *vertices;
                            config.Harmonic.PinnedUvs = *uvs;
                            result.Preview.ParsedFieldCount += 2u;
                        }
                    }
                }
            }

            if (const json* bff = FindMember(*object, "bff"); bff != nullptr)
            {
                constexpr std::string_view kPath = "sandbox.parameterization.bff";
                if (!bff->is_object())
                {
                    AddWarning(result,
                               EngineConfigDiagnosticCode::InvalidValue,
                               std::string{kPath},
                               "Expected an object; reference BFF config retained.");
                }
                else
                {
                    AddUnknownFieldDiagnostics(
                        result,
                        *bff,
                        kPath,
                        {"mode",
                         "boundary_data",
                         "angle_sum_tolerance",
                         "degeneracy_tolerance"});

                    if (ReadEnum(result,
                                 *bff,
                                 "mode",
                                 kPath,
                                 ParseParameterizationBffBoundaryMode,
                                 config.Bff.Mode))
                    {
                        ++result.Preview.ParsedFieldCount;
                    }
                    if (const std::optional<std::vector<double>> boundaryData =
                            ReadNumberArray(result, *bff, "boundary_data", kPath);
                        boundaryData.has_value())
                    {
                        config.Bff.BoundaryData = *boundaryData;
                        ++result.Preview.ParsedFieldCount;
                    }
                    if (const std::optional<double> tolerance = ReadNumber(
                            result,
                            *bff,
                            "angle_sum_tolerance",
                            kPath,
                            std::numeric_limits<double>::min(),
                            1.0e30);
                        tolerance.has_value())
                    {
                        config.Bff.AngleSumTolerance = *tolerance;
                        ++result.Preview.ParsedFieldCount;
                    }
                    if (const std::optional<double> tolerance = ReadNumber(
                            result,
                            *bff,
                            "degeneracy_tolerance",
                            kPath,
                            std::numeric_limits<double>::min(),
                            1.0e30);
                        tolerance.has_value())
                    {
                        config.Bff.DegeneracyTolerance = *tolerance;
                        ++result.Preview.ParsedFieldCount;
                    }
                }
            }

            if (!config.Lscm.AutoPins &&
                config.Lscm.PinVertex0 == config.Lscm.PinVertex1)
            {
                AddWarning(
                    result,
                    EngineConfigDiagnosticCode::InvalidValue,
                    "sandbox.parameterization.lscm",
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
                else if (!std::all_of(config.Bff.BoundaryData.begin(),
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
                        std::abs(angleSum - 2.0 * std::numbers::pi_v<double>) >
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
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           "sandbox.parameterization.bff",
                           std::string{bffMessage});
                config.Bff = referenceBff;
            }
        }

        void ParseSandboxConfig(EngineConfigLoadResult& result, const json& root)
        {
            const json* object = FindMember(root, "sandbox");
            if (object == nullptr)
            {
                return;
            }
            if (!object->is_object())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           "sandbox",
                           "Expected an object; reference sandbox config retained.");
                return;
            }

            AddUnknownFieldDiagnostics(
                result,
                *object,
                "sandbox",
                {"progressive_poisson", "parameterization"});

            const json emptyPoisson = json::object();
            const json* poisson = FindMember(*object, "progressive_poisson");
            if (poisson == nullptr)
            {
                poisson = &emptyPoisson;
            }
            else if (!poisson->is_object())
            {
                AddWarning(result,
                           EngineConfigDiagnosticCode::InvalidValue,
                           "sandbox.progressive_poisson",
                           "Expected an object; reference progressive Poisson config retained.");
                poisson = &emptyPoisson;
            }

            AddUnknownFieldDiagnostics(
                result,
                *poisson,
                "sandbox.progressive_poisson",
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

            ProgressivePoissonPlaygroundConfig& config =
                result.Preview.Config.Sandbox.ProgressivePoisson;
            if (const std::optional<std::int64_t> dimension =
                    ReadInteger(result, *poisson, "dimension", "sandbox.progressive_poisson", 2, 3);
                dimension.has_value())
            {
                config.Dimension = static_cast<std::uint32_t>(*dimension);
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<std::int64_t> gridWidth =
                    ReadInteger(result, *poisson, "grid_width", "sandbox.progressive_poisson", 1, 4096);
                gridWidth.has_value())
            {
                config.GridWidth = static_cast<std::uint32_t>(*gridWidth);
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<std::int64_t> maxLevels =
                    ReadInteger(result, *poisson, "max_levels", "sandbox.progressive_poisson", 1, 32);
                maxLevels.has_value())
            {
                config.MaxLevels = static_cast<std::uint32_t>(*maxLevels);
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<double> hashLoad =
                    ReadNumber(result, *poisson, "hash_load_factor", "sandbox.progressive_poisson", 0.01, 16.0);
                hashLoad.has_value())
            {
                config.HashLoadFactor = *hashLoad;
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<double> radiusAlpha =
                    ReadNumber(result, *poisson, "radius_alpha", "sandbox.progressive_poisson", -1.0, 0.999);
                radiusAlpha.has_value())
            {
                config.RadiusAlpha = *radiusAlpha;
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<bool> randomize =
                    ReadBool(result, *poisson, "randomize_grid_origin", "sandbox.progressive_poisson");
                randomize.has_value())
            {
                config.RandomizeGridOrigin = *randomize;
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<std::int64_t> gridSeed =
                    ReadInteger(result, *poisson, "grid_origin_seed", "sandbox.progressive_poisson", 0, std::numeric_limits<std::int32_t>::max());
                gridSeed.has_value())
            {
                config.GridOriginSeed = static_cast<std::uint32_t>(*gridSeed);
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<bool> shuffle =
                    ReadBool(result, *poisson, "shuffle_within_levels", "sandbox.progressive_poisson");
                shuffle.has_value())
            {
                config.ShuffleWithinLevels = *shuffle;
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<std::int64_t> shuffleSeed =
                    ReadInteger(result, *poisson, "shuffle_seed", "sandbox.progressive_poisson", 0, std::numeric_limits<std::int32_t>::max());
                shuffleSeed.has_value())
            {
                config.ShuffleSeed = static_cast<std::uint32_t>(*shuffleSeed);
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<std::int64_t> prefix =
                    ReadInteger(result, *poisson, "prefix_count", "sandbox.progressive_poisson", 0, 10'000'000);
                prefix.has_value())
            {
                config.PrefixCount = static_cast<std::uint32_t>(*prefix);
                ++result.Preview.ParsedFieldCount;
            }
            if (ReadEnum(result,
                         *poisson,
                         "channel",
                         "sandbox.progressive_poisson",
                         ParseProgressivePoissonChannel,
                         config.Channel))
            {
                ++result.Preview.ParsedFieldCount;
            }
            if (ReadEnum(result,
                         *poisson,
                         "backend",
                         "sandbox.progressive_poisson",
                         ParseProgressivePoissonBackend,
                         config.Backend))
            {
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<std::int64_t> samples =
                    ReadInteger(result, *poisson, "mesh_surface_sample_count", "sandbox.progressive_poisson", 1, 10'000'000);
                samples.has_value())
            {
                config.MeshSurfaceSampleCount = static_cast<std::uint32_t>(*samples);
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<std::int64_t> surfaceSeed =
                    ReadInteger(result, *poisson, "mesh_surface_seed", "sandbox.progressive_poisson", 0, std::numeric_limits<std::int32_t>::max());
                surfaceSeed.has_value())
            {
                config.MeshSurfaceSampleSeed = static_cast<std::uint32_t>(*surfaceSeed);
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<double> minArea =
                    ReadNumber(result, *poisson, "mesh_surface_min_triangle_area", "sandbox.progressive_poisson", 1.0e-30, 1.0e30);
                minArea.has_value())
            {
                config.MeshSurfaceMinTriangleArea = *minArea;
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<bool> interpolate =
                    ReadBool(result, *poisson, "mesh_surface_interpolate_normals", "sandbox.progressive_poisson");
                interpolate.has_value())
            {
                config.MeshSurfaceInterpolateNormals = *interpolate;
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<bool> autoRun =
                    ReadBool(result, *poisson, "auto_run_on_edit", "sandbox.progressive_poisson");
                autoRun.has_value())
            {
                config.AutoRunOnEdit = *autoRun;
                ++result.Preview.ParsedFieldCount;
            }
            if (const std::optional<double> debounce =
                    ReadNumber(result, *poisson, "debounce_seconds", "sandbox.progressive_poisson", 0.0, 10.0);
                debounce.has_value())
            {
                config.DebounceSeconds = *debounce;
                ++result.Preview.ParsedFieldCount;
            }

            ParseParameterizationConfig(result, *object);
        }

        [[nodiscard]] bool HasFallbackDiagnostics(const EngineConfigLoadResult& result) noexcept
        {
            return std::any_of(result.Diagnostics.begin(),
                               result.Diagnostics.end(),
                               [](const EngineConfigDiagnostic& diagnostic)
                               {
                                   return diagnostic.Severity != EngineConfigDiagnosticSeverity::Error;
                               });
        }
    }

    std::string_view ToString(const EngineConfigState value) noexcept
    {
        switch (value)
        {
        case EngineConfigState::Valid: return "Valid";
        case EngineConfigState::Invalid: return "Invalid";
        case EngineConfigState::Unsupported: return "Unsupported";
        case EngineConfigState::FallbackApplied: return "FallbackApplied";
        }
        return "Invalid";
    }

    std::string_view ToString(const EngineConfigDiagnosticSeverity value) noexcept
    {
        switch (value)
        {
        case EngineConfigDiagnosticSeverity::Info: return "Info";
        case EngineConfigDiagnosticSeverity::Warning: return "Warning";
        case EngineConfigDiagnosticSeverity::Error: return "Error";
        }
        return "Error";
    }

    std::string_view ToString(const EngineConfigDiagnosticCode value) noexcept
    {
        switch (value)
        {
        case EngineConfigDiagnosticCode::None: return "None";
        case EngineConfigDiagnosticCode::EmptyDocument: return "EmptyDocument";
        case EngineConfigDiagnosticCode::LoadError: return "LoadError";
        case EngineConfigDiagnosticCode::ParseError: return "ParseError";
        case EngineConfigDiagnosticCode::InvalidSchema: return "InvalidSchema";
        case EngineConfigDiagnosticCode::UnsupportedVersion: return "UnsupportedVersion";
        case EngineConfigDiagnosticCode::UnknownField: return "UnknownField";
        case EngineConfigDiagnosticCode::InvalidValue: return "InvalidValue";
        case EngineConfigDiagnosticCode::FallbackApplied: return "FallbackApplied";
        }
        return "None";
    }

    bool HasErrors(const EngineConfigLoadResult& result) noexcept
    {
        return std::any_of(result.Diagnostics.begin(),
                           result.Diagnostics.end(),
                           [](const EngineConfigDiagnostic& diagnostic)
                           {
                               return diagnostic.Severity == EngineConfigDiagnosticSeverity::Error;
                           });
    }

    bool IsConfigUsable(const EngineConfigLoadResult& result) noexcept
    {
        return result.State == EngineConfigState::Valid
            || result.State == EngineConfigState::FallbackApplied;
    }

    bool HasDiagnostic(const EngineConfigLoadResult& result,
                       const EngineConfigDiagnosticCode code) noexcept
    {
        return std::any_of(result.Diagnostics.begin(),
                           result.Diagnostics.end(),
                           [code](const EngineConfigDiagnostic& diagnostic)
                           {
                               return diagnostic.Code == code;
                           });
    }

    std::uint32_t CountByState(const EngineConfigLoadResult& result,
                               const EngineConfigState state) noexcept
    {
        return static_cast<std::uint32_t>(std::count_if(
            result.Diagnostics.begin(),
            result.Diagnostics.end(),
            [state](const EngineConfigDiagnostic& diagnostic)
            {
                return diagnostic.State == state;
            }));
    }

    EngineConfigLoadResult PreviewEngineConfig(
        const std::string_view document,
        const EngineConfig& referenceDefaults,
        const EngineConfigParseOptions& options)
    {
        EngineConfigLoadResult result{};
        result.SourceId = options.SourceId;
        result.Preview.Config = referenceDefaults;

        if (document.empty())
        {
            AddError(result,
                     EngineConfigState::Invalid,
                     EngineConfigDiagnosticCode::EmptyDocument,
                     result.SourceId,
                     "Engine config document is empty.");
            result.State = EngineConfigState::Invalid;
            return result;
        }

        json root = json::parse(document, nullptr, false);
        if (root.is_discarded())
        {
            AddError(result,
                     EngineConfigState::Invalid,
                     EngineConfigDiagnosticCode::ParseError,
                     result.SourceId,
                     "Failed to parse engine config JSON.");
            result.State = EngineConfigState::Invalid;
            return result;
        }

        if (!root.is_object())
        {
            AddError(result,
                     EngineConfigState::Invalid,
                     EngineConfigDiagnosticCode::InvalidValue,
                     result.SourceId,
                     "Engine config document root must be a JSON object.");
            result.State = EngineConfigState::Invalid;
            return result;
        }

        AddUnknownFieldDiagnostics(result,
                                   root,
                                   "engine_config",
                                   {"schema",
                                    "version",
                                    "window",
                                    "render",
                                    "simulation",
                                    "reference_scene",
                                    "camera",
                                    "sandbox"});

        const json* schema = FindMember(root, "schema");
        if (schema == nullptr || !schema->is_string()
            || schema->get<std::string>() != kEngineConfigSchemaId)
        {
            AddError(result,
                     EngineConfigState::Invalid,
                     EngineConfigDiagnosticCode::InvalidSchema,
                     "schema",
                     "Engine config schema id is missing or unsupported.");
            result.State = EngineConfigState::Invalid;
            return result;
        }

        const std::optional<std::int64_t> version =
            ReadInteger(result,
                        root,
                        "version",
                        "engine_config",
                        1,
                        std::numeric_limits<std::uint32_t>::max());
        if (!version.has_value())
        {
            AddError(result,
                     EngineConfigState::Unsupported,
                     EngineConfigDiagnosticCode::UnsupportedVersion,
                     "version",
                     "Engine config schema version is missing or invalid.");
            result.State = EngineConfigState::Unsupported;
            return result;
        }
        result.SchemaVersion = static_cast<std::uint32_t>(*version);
        if (result.SchemaVersion != kEngineConfigSchemaVersion)
        {
            AddError(result,
                     EngineConfigState::Unsupported,
                     EngineConfigDiagnosticCode::UnsupportedVersion,
                     "version",
                     "Engine config schema version is not supported by this build.");
            result.State = EngineConfigState::Unsupported;
            return result;
        }

        ParseWindowConfig(result, root);
        ParseRenderConfig(result, root);
        ParseSimulationConfig(result, root);
        ParseReferenceSceneConfig(result, root);
        ParseCameraConfig(result, root);
        ParseSandboxConfig(result, root);

        if (HasErrors(result))
        {
            result.State = EngineConfigState::Invalid;
        }
        else if (HasFallbackDiagnostics(result))
        {
            AddInfo(result,
                    EngineConfigDiagnosticCode::FallbackApplied,
                    result.SourceId,
                    "One or more engine config fields used reference defaults.");
            result.State = EngineConfigState::FallbackApplied;
        }
        else
        {
            result.State = EngineConfigState::Valid;
        }
        return result;
    }

    EngineConfigLoadResult PreviewEngineConfig(
        const std::string_view document,
        const EngineConfigParseOptions& options)
    {
        return PreviewEngineConfig(document, EngineConfig{}, options);
    }

    EngineConfigLoadResult LoadEngineConfigFile(
        const std::string_view path,
        const EngineConfig& referenceDefaults,
        const EngineConfigParseOptions& options)
    {
        EngineConfigParseOptions effectiveOptions = options;
        if (!path.empty())
        {
            effectiveOptions.SourceId = std::string{path};
        }

        EngineConfigLoadResult fallbackResult{};
        fallbackResult.SourceId = effectiveOptions.SourceId;
        fallbackResult.Preview.Config = referenceDefaults;

        Core::IO::FileIOBackend backend{};
        const Core::IO::IORequest request{.Path = std::string{path}};
        Core::Expected<Core::IO::IOReadResult> read = backend.Read(request);
        if (!read.has_value())
        {
            AddError(fallbackResult,
                     EngineConfigState::Invalid,
                     EngineConfigDiagnosticCode::LoadError,
                     fallbackResult.SourceId,
                     std::string{"Failed to read engine config file: "}
                         + std::string{Core::Error::ToString(read.error())});
            fallbackResult.State = EngineConfigState::Invalid;
            return fallbackResult;
        }

        std::string document{};
        document.assign(reinterpret_cast<const char*>(read->Data.data()), read->Data.size());
        return PreviewEngineConfig(document, referenceDefaults, effectiveOptions);
    }

    EngineConfigLoadResult LoadEngineConfigFile(
        const std::string_view path,
        const EngineConfigParseOptions& options)
    {
        return LoadEngineConfigFile(path, EngineConfig{}, options);
    }

    std::string SerializeEngineConfig(const EngineConfig& config)
    {
        json root = json::object();
        root["schema"] = std::string{kEngineConfigSchemaId};
        root["version"] = kEngineConfigSchemaVersion;
        root["window"] = json::object({
            {"title", config.Window.Title},
            {"width", config.Window.Width},
            {"height", config.Window.Height},
            {"resizable", config.Window.Resizable},
            {"backend", std::string{ToConfigString(config.Window.Backend)}},
        });
        root["render"] = json::object({
            {"backend", std::string{ToConfigString(config.Render.Backend)}},
            {"enable_promoted_vulkan_device", config.Render.EnablePromotedVulkanDevice},
            {"enable_validation", config.Render.EnableValidation},
            {"enable_vsync", config.Render.EnableVSync},
            {"frames_in_flight", config.Render.FramesInFlight},
            {"default_recipe_config_path", config.Render.DefaultRecipeConfigPath},
            {"synchronous_extraction", config.Render.SynchronousExtraction},
        });
        root["simulation"] = json::object({
            {"worker_thread_count", config.Simulation.WorkerThreadCount},
        });
        root["reference_scene"] = json::object({
            {"enabled", config.ReferenceScene.Enabled},
            {"selector", std::string{ToConfigString(config.ReferenceScene.Selector)}},
        });
        root["camera"] = json::object({
            {"enabled", config.Camera.Enabled},
            {"controller", std::string{ToConfigString(config.Camera.Controller)}},
        });
        root["sandbox"] = json::object({
            {"progressive_poisson",
             json::object({
                 {"dimension", config.Sandbox.ProgressivePoisson.Dimension},
                 {"grid_width", config.Sandbox.ProgressivePoisson.GridWidth},
                 {"max_levels", config.Sandbox.ProgressivePoisson.MaxLevels},
                 {"hash_load_factor", config.Sandbox.ProgressivePoisson.HashLoadFactor},
                 {"radius_alpha", config.Sandbox.ProgressivePoisson.RadiusAlpha},
                 {"randomize_grid_origin",
                  config.Sandbox.ProgressivePoisson.RandomizeGridOrigin},
                 {"grid_origin_seed", config.Sandbox.ProgressivePoisson.GridOriginSeed},
                 {"shuffle_within_levels",
                  config.Sandbox.ProgressivePoisson.ShuffleWithinLevels},
                 {"shuffle_seed", config.Sandbox.ProgressivePoisson.ShuffleSeed},
                 {"prefix_count", config.Sandbox.ProgressivePoisson.PrefixCount},
                 {"channel",
                  std::string{ToConfigString(config.Sandbox.ProgressivePoisson.Channel)}},
                 {"backend",
                  std::string{ToConfigString(config.Sandbox.ProgressivePoisson.Backend)}},
                 {"mesh_surface_sample_count",
                  config.Sandbox.ProgressivePoisson.MeshSurfaceSampleCount},
                 {"mesh_surface_seed",
                  config.Sandbox.ProgressivePoisson.MeshSurfaceSampleSeed},
                 {"mesh_surface_min_triangle_area",
                  config.Sandbox.ProgressivePoisson.MeshSurfaceMinTriangleArea},
                 {"mesh_surface_interpolate_normals",
                  config.Sandbox.ProgressivePoisson.MeshSurfaceInterpolateNormals},
                 {"auto_run_on_edit", config.Sandbox.ProgressivePoisson.AutoRunOnEdit},
                 {"debounce_seconds", config.Sandbox.ProgressivePoisson.DebounceSeconds},
             })},
            {"parameterization",
             json::object({
                 {"strategy",
                  std::string{ToConfigString(config.Sandbox.Parameterization.Strategy)}},
                 {"view",
                  json::object({
                      {"render_mode",
                       std::string{ToConfigString(
                           config.Sandbox.Parameterization.View.RenderMode)}},
                      {"background_mode",
                       std::string{ToConfigString(
                           config.Sandbox.Parameterization.View.BackgroundMode)}},
                      {"show_distortion_heatmap",
                       config.Sandbox.Parameterization.View
                           .ShowDistortionHeatmap},
                  })},
                 {"lscm",
                  json::object({
                      {"auto_pins", config.Sandbox.Parameterization.Lscm.AutoPins},
                      {"pin_vertex_0",
                       config.Sandbox.Parameterization.Lscm.PinVertex0},
                      {"pin_vertex_1",
                       config.Sandbox.Parameterization.Lscm.PinVertex1},
                      {"pin_uv_0",
                       SerializeUv(config.Sandbox.Parameterization.Lscm.PinUv0)},
                      {"pin_uv_1",
                       SerializeUv(config.Sandbox.Parameterization.Lscm.PinUv1)},
                      {"solver_tolerance",
                       config.Sandbox.Parameterization.Lscm.SolverTolerance},
                      {"max_solver_iterations",
                       config.Sandbox.Parameterization.Lscm.MaxSolverIterations},
                  })},
                 {"harmonic",
                  json::object({
                      {"boundary",
                       std::string{ToConfigString(
                           config.Sandbox.Parameterization.Harmonic.Boundary)}},
                      {"arc_length_spacing",
                       config.Sandbox.Parameterization.Harmonic.ArcLengthSpacing},
                      {"clamp_non_convex_weights",
                       config.Sandbox.Parameterization.Harmonic
                           .ClampNonConvexWeights},
                      {"pinned_vertices",
                       config.Sandbox.Parameterization.Harmonic.PinnedVertices},
                      {"pinned_uvs",
                       SerializeUvs(
                           config.Sandbox.Parameterization.Harmonic.PinnedUvs)},
                  })},
                 {"bff",
                  json::object({
                      {"mode",
                       std::string{ToConfigString(
                           config.Sandbox.Parameterization.Bff.Mode)}},
                      {"boundary_data",
                       config.Sandbox.Parameterization.Bff.BoundaryData},
                      {"angle_sum_tolerance",
                       config.Sandbox.Parameterization.Bff.AngleSumTolerance},
                      {"degeneracy_tolerance",
                       config.Sandbox.Parameterization.Bff.DegeneracyTolerance},
                  })},
             })},
        });
        return root.dump(2);
    }
}
