module;

#include <cstdint>
#include <memory>

export module Graphics:CompositionStrategy;

import :RenderPipeline;

export namespace Graphics
{
    // -------------------------------------------------------------------------
    // ICompositionStrategy — future-compatible lighting/composition interface
    // -------------------------------------------------------------------------
    // Abstracts how scene geometry is composed into the final SceneColorHDR
    // target. Each strategy defines:
    //   1. How geometry passes should configure their outputs (forward: direct
    //      to SceneColorHDR; deferred: to G-buffer channels).
    //   2. An optional composition pass that produces SceneColorHDR from
    //      intermediate buffers.
    //
    // The active strategy is selected by FrameLightingPath and injected into
    // DefaultPipeline. Geometry passes (Surface, Line, Point) query the
    // strategy for their output target rather than hard-coding SceneColorHDR.
    //
    // Current implementation: ForwardComposition (geometry writes directly to
    // SceneColorHDR; no separate composition pass needed).
    //
    // Future implementations:
    //   - DeferredComposition: geometry writes to G-buffer; a fullscreen
    //     lighting pass produces SceneColorHDR.
    //   - HybridComposition: opaque geometry deferred, transparent forward,
    //     then merge into SceneColorHDR.
    //   - ForwardPlusComposition: clustered/tiled light culling with forward
    //     shading.
    // -------------------------------------------------------------------------
    class ICompositionStrategy
    {
    public:
        virtual ~ICompositionStrategy() = default;

        // Returns the lighting path this strategy implements.
        [[nodiscard]] virtual FrameLightingPath GetLightingPath() const = 0;

        // Returns the render resource that geometry passes should write their
        // lit/unlit color output to. For forward rendering this is
        // SceneColorHDR. For deferred rendering this would be Albedo (with
        // normals/material written to other channels).
        [[nodiscard]] virtual RenderResource GetGeometryColorTarget() const = 0;

        // Returns true if this strategy requires G-buffer channels (normals,
        // albedo, material) to be allocated by the FrameRecipe.
        [[nodiscard]] virtual bool RequiresGBuffer() const = 0;

        // Add the composition/lighting pass(es) to the render graph. Called
        // after all geometry passes have executed. For forward rendering this
        // is a no-op (geometry already wrote lit color to SceneColorHDR). For
        // deferred rendering this would add a fullscreen lighting pass.
        virtual void AddCompositionPasses(RenderPassContext& ctx) = 0;

        // Populate FrameRecipe fields based on strategy requirements. Called
        // during BuildFrameRecipe to ensure the recipe requests all resources
        // the strategy needs (e.g. G-buffer channels for deferred).
        virtual void ConfigureRecipe(FrameRecipe& recipe) const = 0;
    };

    // -------------------------------------------------------------------------
    // ForwardComposition — current forward rendering path
    // -------------------------------------------------------------------------
    // Geometry passes write directly to SceneColorHDR with lighting evaluated
    // in the fragment shader. No separate composition pass is needed.
    class ForwardComposition final : public ICompositionStrategy
    {
    public:
        [[nodiscard]] FrameLightingPath GetLightingPath() const override
        {
            return FrameLightingPath::Forward;
        }

        [[nodiscard]] RenderResource GetGeometryColorTarget() const override
        {
            return RenderResource::SceneColorHDR;
        }

        [[nodiscard]] bool RequiresGBuffer() const override
        {
            return false;
        }

        void AddCompositionPasses(RenderPassContext& /*ctx*/) override
        {
            // Forward path: geometry passes already wrote lit color to
            // SceneColorHDR. Nothing to compose.
        }

        void ConfigureRecipe(FrameRecipe& recipe) const override
        {
            // Forward path only needs SceneColorHDR, which is already
            // requested via LightingPath != None in FrameRecipe::Requires().
            recipe.LightingPath = FrameLightingPath::Forward;
        }
    };

    // -------------------------------------------------------------------------
    // Factory
    // -------------------------------------------------------------------------
    // Creates a composition strategy for the given lighting path. Returns
    // nullptr for FrameLightingPath::None (no lighting).
    [[nodiscard]] inline std::unique_ptr<ICompositionStrategy>
    CreateCompositionStrategy(FrameLightingPath path)
    {
        switch (path)
        {
        case FrameLightingPath::Forward:
            return std::make_unique<ForwardComposition>();

        case FrameLightingPath::Deferred:
        case FrameLightingPath::Hybrid:
            // Future: return std::make_unique<DeferredComposition>();
            // Future: return std::make_unique<HybridComposition>();
            // Fall back to forward for now.
            return std::make_unique<ForwardComposition>();

        case FrameLightingPath::None:
            return nullptr;
        }
        return nullptr;
    }
}
