// Parsing, serialization, validated lookup and property JSON shared by runtime configs.
// Include after core config, JSON, standard-library and property declarations.
#pragma once

extern "C++"
{
    namespace Extrinsic::Runtime::ConfigDetail
    {
        [[nodiscard]] nlohmann::json ParseConfigJson(
            std::string_view payload, bool allowExceptions);
        [[nodiscard]] std::string SerializeConfigJson(const nlohmann::json& value);

        [[nodiscard]] Core::Config::EngineConfigSectionValidationResult RejectConfigSection(
            std::string_view subject, std::string message);

        using SectionValidatorFn = Core::Config::EngineConfigSectionValidationResult (*)(
            std::string_view, std::string_view, std::string_view);

        // Only Valid payloads are decoded; fallback remains a preview/apply concern.
        // A null default serializer supplies an empty reference payload to validation.
        [[nodiscard]] std::optional<std::string> FindValidatedCanonicalPayload(
            const Core::Config::EngineConfig& config, std::string_view name,
            std::string_view schemaId, std::uint32_t schemaVersion,
            std::string (*serializeDefault)(), SectionValidatorFn validate);

        // Defaults contain each unsigned field. Nested values replace defaults;
        // unknown keys precede integer diagnostics, which follow the supplied order.
        [[nodiscard]] std::optional<std::string> ValidatePointConfigFields(
            const nlohmann::json& input, nlohmann::json& defaults,
            std::string_view objectError, std::string_view unknownFieldPrefix,
            std::initializer_list<std::string_view> unsignedFields);

        // Fields exist in the merged defaults; the first invalid field wins.
        [[nodiscard]] std::optional<std::string> ValidatePointConfigNonnegativeFloats(
            const nlohmann::json& values,
            std::initializer_list<std::string_view> fields);

        enum class PointPropertyValidation { Valid, InvalidReference, UnknownDomain };

        [[nodiscard]] const char* PointPropertyKindToken(Geometry::PropertyValueKind kind) noexcept;
        [[nodiscard]] nlohmann::json EncodePointPropertyRef(const GeometryPropertyRef& ref);
        [[nodiscard]] nlohmann::json EncodeVec3PointPropertyRef(const GeometryPropertyRef& ref);
        [[nodiscard]] PointPropertyValidation ValidatePointPropertyRef(
            const nlohmann::json& ref, Geometry::PropertyValueKind kind, bool allowScalarConversion = false);
        // Fields exist in the merged defaults; diagnostics follow the supplied order.
        [[nodiscard]] std::optional<std::string> ValidatePointConfigPropertyRefs(
            const nlohmann::json& values,
            std::initializer_list<std::pair<std::string_view, Geometry::PropertyValueKind>> fields);
        // Decodes validated bindings without changing the caller's expected value kind.
        void DecodePointPropertyRef(const nlohmann::json& value, GeometryPropertyRef& ref);
    }
}
