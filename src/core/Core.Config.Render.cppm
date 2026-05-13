module;

#include <cstdint>

export module Extrinsic.Core.Config.Render;

namespace Extrinsic::Core::Config
{
    export enum class GraphicsBackend : std::uint8_t
    {
        Vulkan
    };

    // GRAPHICS-032A — opt-in frame recipe selection. Default keeps the existing
    // `BuildDefaultFrameRecipe` path. `MinimalDebug` opts into the
    // `recipe.minimal-debug-surface` recipe; bootstrap-only and retired by
    // GRAPHICS-081 once the default recipe records every pass operationally.
    export enum class FrameRecipeKind : std::uint8_t
    {
        Default = 0,
        MinimalDebug,
    };

    export struct RenderConfig
    {
        GraphicsBackend Backend{GraphicsBackend::Vulkan};
        bool EnablePromotedVulkanDevice{false};
        bool EnableValidation{true};
        bool EnableVSync{true};
        std::uint32_t FramesInFlight{2};
        FrameRecipeKind FrameRecipe{FrameRecipeKind::Default};
    };
}
