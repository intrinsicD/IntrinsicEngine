// Parsing, serialization, section construction, validated lookup and property JSON
// shared by runtime configs.
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
        [[nodiscard]] Core::Config::EngineConfigSectionValidationResult AcceptConfigSection(
            std::string canonicalPayloadJson, std::size_t parsedFieldCount);

        using SectionValidatorFn = Core::Config::EngineConfigSectionValidationResult (*)(
            std::string_view, std::string_view, std::string_view);

        // Only Valid payloads are decoded; fallback remains a preview/apply concern.
        // A null default serializer supplies an empty reference payload to validation.
        [[nodiscard]] std::optional<std::string> FindValidatedCanonicalPayload(
            const Core::Config::EngineConfig& config, std::string_view name,
            std::string_view schemaId, std::uint32_t schemaVersion,
            std::string (*serializeDefault)(), SectionValidatorFn validate);

        [[nodiscard]] Core::Config::EngineConfigSection MakeConfigSection(
            std::string_view name, std::string_view schemaId,
            std::uint32_t schemaVersion, std::string payloadJson);
        [[nodiscard]] Core::Config::EngineConfigSectionRegistration MakeSectionRegistration(
            std::string_view name, std::string_view schemaId, std::uint32_t schemaVersion,
            std::string defaultPayloadJson, SectionValidatorFn validate,
            std::string schemaJson,
            Core::Config::EngineConfigSectionChangedCallback&& onChanged = {});

        // Decodes a point config only from a Valid canonical payload.
        template <class T>
        [[nodiscard]] std::optional<T> GetPointConfig(
            const Core::Config::EngineConfig& config, std::string_view name,
            std::string_view schemaId, std::uint32_t schemaVersion,
            SectionValidatorFn validate, T (*parse)(const nlohmann::json&))
        {
            const auto payload = FindValidatedCanonicalPayload(
                config, name, schemaId, schemaVersion, nullptr, validate);
            if (!payload) return std::nullopt;
            return parse(ParseConfigJson(*payload, true));
        }

        enum class PointPropertyValidation { Valid, InvalidReference, UnknownDomain };

        [[nodiscard]] const char* PointPropertyKindToken(Geometry::PropertyValueKind kind) noexcept;
        [[nodiscard]] nlohmann::json EncodePointPropertyRef(const GeometryPropertyRef& ref);
        [[nodiscard]] nlohmann::json EncodeVec3PointPropertyRef(const GeometryPropertyRef& ref);
        [[nodiscard]] PointPropertyValidation ValidatePointPropertyRef(
            const nlohmann::json& ref, Geometry::PropertyValueKind kind, bool allowScalarConversion = false);
        // Decodes validated bindings without changing the caller's expected value kind.
        void DecodePointPropertyRef(const nlohmann::json& value, GeometryPropertyRef& ref);
    }
}
