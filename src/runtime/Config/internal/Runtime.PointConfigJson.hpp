// Strict field validation and string-token property JSON shared by point configs.
// Include after JSON, standard-library and property-reference declarations.
#pragma once

extern "C++"
{
    namespace Extrinsic::Runtime::ConfigDetail
    {
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
            const nlohmann::json& ref, Geometry::PropertyValueKind kind);
        // Fields exist in the merged defaults; diagnostics follow the supplied order.
        [[nodiscard]] std::optional<std::string> ValidatePointConfigPropertyRefs(
            const nlohmann::json& values,
            std::initializer_list<std::pair<std::string_view, Geometry::PropertyValueKind>> fields);
        // Decodes validated bindings without changing the caller's expected value kind.
        void DecodePointPropertyRef(const nlohmann::json& value, GeometryPropertyRef& ref);
    }
}
