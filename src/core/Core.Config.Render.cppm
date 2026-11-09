// Render configuration values consumed by the runtime composition root.
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
        // Runtime samples this hot-applied control once per frame after editor
        // UI capture and copies it into the immutable RenderFrameInput.
        bool EnableGpuProfiling{false};
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
        // ADR 0030: canonical GPU property slots are a cache. A slot unused for this many
        // seconds is evicted (0 disables); above the budget the least recently used slots
        // go first, size-weighted (0 = no budget). Read at boot by the residency owner.
        std::uint32_t GpuPropertyIdleEvictSeconds{60u};
        std::uint32_t GpuPropertyBudgetMegabytes{256u};
        // ARCH-023: method scratch buffers (SpatialIndexCache::MethodScratch) in use, awaiting GPU completion or
        // idle stay within this many MiB (1..1048576); a run that would exceed it is refused. An engineering
        // budget, not a tuned value. Boot-only.
        std::uint32_t GpuMethodBufferBudgetMegabytes{1024u};
        // ARCH-023: method compute (IDevice::SubmitComputeReadback) runs on the async compute queue when the
        // device has one; true forces the graphics queue (fallback testing, driver workarounds). Boot-only.
        bool MethodComputeOnGraphicsQueue{false};
    };
}
