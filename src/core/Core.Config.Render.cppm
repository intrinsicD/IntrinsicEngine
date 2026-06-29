module;

#include <cstdint>
#include <string>

export module Extrinsic.Core.Config.Render;

namespace Extrinsic::Core::Config
{
    export enum class GraphicsBackend : std::uint8_t
    {
        Vulkan
    };

    export struct RenderConfig
    {
        GraphicsBackend Backend{GraphicsBackend::Vulkan};
        bool EnablePromotedVulkanDevice{false};
        bool EnableValidation{true};
        bool EnableVSync{true};
        std::uint32_t FramesInFlight{2};
        std::string DefaultRecipeConfigPath{"config/render-recipe.json"};
        // GRAPHICS-036 decision 6 — when true (the default) the runtime drives a
        // single-buffer `RenderWorldPool` so extraction and rendering stay serial
        // and behavior matches the pre-pool path. When false the pool is sized for
        // triple-buffered pipelined frames (sim-N / render-N-1); flipping the
        // default to pipelined is owned by `GRAPHICS-036D`, never this flag's
        // declaration. Lives on the core render config (never in graphics) so the
        // composition root can size the pool without a graphics dependency.
        bool SynchronousExtraction{true};
    };
}
