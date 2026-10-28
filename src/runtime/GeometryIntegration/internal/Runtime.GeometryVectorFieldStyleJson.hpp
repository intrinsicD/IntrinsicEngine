// JSON side of the vector-field layer style table (RUNTIME-327): the one declaration of the
// style fields, in the agent's names or the scene file's keys.
// Include after <nlohmann/json.hpp> and `import Extrinsic.Runtime.GeometryPresentation;`.
#pragma once

extern "C++"
{
    namespace Extrinsic::Runtime
    {
        enum class GeometryVectorFieldStyleKeys : std::uint8_t
        {
            Agent, // snake_case names and lower-case tokens (`line_width`, "raw")
            Scene, // the scene file's camelCase keys and tokens (`lineWidthPx`, "Raw")
        };

        // Every style field of `layer` as one object (the vector property is not part of the style).
        [[nodiscard]] nlohmann::json GeometryVectorFieldStyleToJson(
            const GeometryVectorFieldLayerRecipe& layer, GeometryVectorFieldStyleKeys keys);
        // Applies the style keys present in `object` (Scene: the keys scenes have always written
        // are required) after checking each value's type and declared range, as sent and as
        // stored (a double a float cannot hold is refused); other keys are ignored. On a refused
        // value `layer` is unchanged and the reason is returned.
        [[nodiscard]] std::optional<std::string> ReadGeometryVectorFieldStyleJson(
            const nlohmann::json& object, GeometryVectorFieldLayerRecipe& layer, GeometryVectorFieldStyleKeys keys);
        // JSON Schema `properties` of the style in agent names (types, bounds, enums, descriptions).
        [[nodiscard]] nlohmann::json GeometryVectorFieldStyleSchemaProperties();
    }
}
