module;

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

export module Extrinsic.Core.Config.EngineLoad;

import Extrinsic.Core.Config.Engine;

namespace Extrinsic::Core::Config
{
    export inline constexpr std::string_view kEngineConfigSchemaId =
        "intrinsic.core.engine-config";
    export inline constexpr std::uint32_t kEngineConfigSchemaVersion = 1u;

    export enum class EngineConfigState : std::uint8_t
    {
        Valid = 0,
        Invalid,
        Unsupported,
        FallbackApplied,
    };

    export enum class EngineConfigDiagnosticSeverity : std::uint8_t
    {
        Info = 0,
        Warning,
        Error,
    };

    export enum class EngineConfigDiagnosticCode : std::uint8_t
    {
        None = 0,
        EmptyDocument,
        LoadError,
        ParseError,
        InvalidSchema,
        UnsupportedVersion,
        UnknownField,
        InvalidValue,
        FallbackApplied,
    };

    export struct EngineConfigDiagnostic
    {
        EngineConfigState State{EngineConfigState::Invalid};
        EngineConfigDiagnosticSeverity Severity{EngineConfigDiagnosticSeverity::Error};
        EngineConfigDiagnosticCode Code{EngineConfigDiagnosticCode::None};
        std::string Subject{};
        std::string Message{};
    };

    export struct EngineConfigPreview
    {
        EngineConfig Config{};
        bool SideEffectFree{true};
        std::uint32_t ParsedFieldCount{0u};
    };

    export struct EngineConfigParseOptions
    {
        std::string SourceId{"<memory>"};
    };

    export struct EngineConfigLoadResult
    {
        EngineConfigState State{EngineConfigState::Invalid};
        std::string SourceId{};
        std::uint32_t SchemaVersion{0u};
        EngineConfigPreview Preview{};
        std::vector<EngineConfigDiagnostic> Diagnostics{};
    };

    export [[nodiscard]] std::string_view ToString(EngineConfigState value) noexcept;
    export [[nodiscard]] std::string_view ToString(EngineConfigDiagnosticSeverity value) noexcept;
    export [[nodiscard]] std::string_view ToString(EngineConfigDiagnosticCode value) noexcept;
    export [[nodiscard]] bool HasErrors(const EngineConfigLoadResult& result) noexcept;
    export [[nodiscard]] bool IsConfigUsable(const EngineConfigLoadResult& result) noexcept;
    export [[nodiscard]] bool HasDiagnostic(const EngineConfigLoadResult& result,
                                            EngineConfigDiagnosticCode code) noexcept;
    export [[nodiscard]] std::uint32_t CountByState(const EngineConfigLoadResult& result,
                                                    EngineConfigState state) noexcept;

    export [[nodiscard]] EngineConfigLoadResult PreviewEngineConfig(
        std::string_view document,
        const EngineConfig& referenceDefaults,
        const EngineConfigParseOptions& options = {});

    export [[nodiscard]] EngineConfigLoadResult PreviewEngineConfig(
        std::string_view document,
        const EngineConfigParseOptions& options = {});

    export [[nodiscard]] EngineConfigLoadResult LoadEngineConfigFile(
        std::string_view path,
        const EngineConfig& referenceDefaults,
        const EngineConfigParseOptions& options = {});

    export [[nodiscard]] EngineConfigLoadResult LoadEngineConfigFile(
        std::string_view path,
        const EngineConfigParseOptions& options = {});

    export [[nodiscard]] std::string SerializeEngineConfig(
        const EngineConfig& config);
}
