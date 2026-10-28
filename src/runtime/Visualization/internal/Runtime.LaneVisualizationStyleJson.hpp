// JSON side of the lane visualization style table (RUNTIME-329): the scene file's keys.
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
    }
}
