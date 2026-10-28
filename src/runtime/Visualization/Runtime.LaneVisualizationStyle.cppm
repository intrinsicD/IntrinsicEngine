// RUNTIME-329: the lane visualization style (`VisualizationConfig`) declared once. The rows in
// the implementation unit drive the scene keys, the config command's check and equality, the
// panel model's cache signature and the panel's enum names.
module;

#include <cstdint>
#include <optional>
#include <span>
#include <string>

export module Extrinsic.Runtime.LaneVisualizationStyle;

import Extrinsic.Graphics.Component.VisualizationConfig;
import Extrinsic.Runtime.ConfigFieldSpec;

export namespace Extrinsic::Runtime
{
    // One spec per style field (types, enum tokens, finite floats, at most 8 highlight values).
    [[nodiscard]] std::span<const ConfigFieldSpec> LaneVisualizationStyleFields() noexcept;
    // The first field the declared rules refuse ("lane style colormap must be ..."), if any.
    [[nodiscard]] std::optional<std::string> CheckLaneVisualizationStyle(
        const Graphics::Components::VisualizationConfig& config);
    // Field by field: numbers compare as numbers (0 == -0, NaN differs), the active highlight
    // prefix only. The command's no-change test.
    [[nodiscard]] bool SameLaneVisualizationStyle(const Graphics::Components::VisualizationConfig& lhs,
        const Graphics::Components::VisualizationConfig& rhs);
    // Hash of every field with exact float bits (tells 0 from -0 and +inf from -inf), the active
    // highlight prefix only; an out-of-range enum keeps its number. The panel model's cache signature.
    [[nodiscard]] std::uint64_t LaneVisualizationStyleHash(const Graphics::Components::VisualizationConfig& config);
}
