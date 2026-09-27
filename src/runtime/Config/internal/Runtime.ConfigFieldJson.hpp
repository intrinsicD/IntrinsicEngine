// JSON side of the declarative config field tables (RUNTIME-276): the table-driven
// strict merge/type/range check and the generated JSON Schema of a section.
// Include after Runtime.PointConfigJson.hpp and `import Extrinsic.Runtime.ConfigFieldSpec;`.
#pragma once

extern "C++"
{
    namespace Extrinsic::Runtime::ConfigDetail
    {
        // Merges `input` into `merged` (the serialized defaults) rejecting unknown keys,
        // then checks every declared field's type and range in table order. Returns the
        // first diagnostic; cross-field rules stay with the section's validator.
        [[nodiscard]] std::optional<std::string> ValidateDeclaredFields(
            const nlohmann::json& input, nlohmann::json& merged, std::span<const ConfigFieldSpec> fields,
            std::string_view objectError, std::string_view unknownFieldPrefix);

        // JSON Schema (draft 2020-12) of a section payload: one property per declared
        // field with its description, bounds, `x-enum-names`, property-ref kinds and
        // domains, and the default from `defaults`; `additionalProperties:false`.
        [[nodiscard]] std::string BuildSectionSchemaJson(
            std::string_view schemaId, std::string_view title, std::string_view description,
            std::span<const ConfigFieldSpec> fields, const nlohmann::json& defaults);
    }
}
