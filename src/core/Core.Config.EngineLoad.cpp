module;

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

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
                                    "camera"});

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
        return root.dump(2);
    }
}
