// JSON side of the lane visualization style table (RUNTIME-329): the scene file's keys, and the
// flat object by spec name the scene reader checks (RUNTIME-330 generates its tests from it).
// Include after <nlohmann/json.hpp> and `import Extrinsic.Graphics.Component.VisualizationConfig;`.
#pragma once

extern "C++"
{
    namespace Extrinsic::Runtime
    {
        // The scene object of `config` (nested "scalar" and "isolines" objects). An enum value
        // outside its tokens is written as the first token, as scenes always did.
        [[nodiscard]] nlohmann::json LaneVisualizationStyleToJson(
            const Graphics::Components::VisualizationConfig& config);
        // Reads a scene object into `config`: "source" is required, a missing key keeps the
        // current value, a wrong type, token or non-finite number refuses the whole object and
        // leaves `config` unchanged.
        [[nodiscard]] bool ReadLaneVisualizationStyleJson(
            const nlohmann::json& value, Graphics::Components::VisualizationConfig& config);
        // Every field by spec name; an enum outside its tokens keeps its number (what the check reads).
        [[nodiscard]] nlohmann::json LaneVisualizationStyleFieldsJson(
            const Graphics::Components::VisualizationConfig& config);
        // Applies the fields present in `values` (by spec name, other keys ignored) after checking
        // each as sent and as stored (a double a float cannot hold is refused); a refused value
        // leaves `config` unchanged. The scene reader's core.
        [[nodiscard]] bool ReadLaneVisualizationStyleFields(
            const nlohmann::json& values, Graphics::Components::VisualizationConfig& config);
    }
}
