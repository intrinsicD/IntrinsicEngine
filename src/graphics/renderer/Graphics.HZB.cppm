module;

#include <cstdint>
#include <memory>
#include <vector>

export module Extrinsic.Graphics.HZB;

import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureManager;

export namespace Extrinsic::Graphics
{
    // Hi-Z buffer (HZB) resource shape (`GRAPHICS-038A`, decision 1).
    //
    // A conservative max-depth depth pyramid: a single-channel 32-bit float
    // texture sized to the next power of two of the render extent, with a full
    // mip chain halved to 1x1. Each tile stores the **max** depth of the source
    // region (built with a max reduction by `GRAPHICS-038B`) so the two-phase
    // occlusion cull (`GRAPHICS-038C`) only ever rejects an instance whose
    // screen-bounded nearest depth exceeds the sampled HZB max-depth — the
    // no-false-rejection invariant. `RHI::Format::R32_FLOAT` is the engine's
    // single-channel float format (the Vulkan `R32_SFLOAT`); a normalized format
    // is forbidden because it could quantize the conservative bound the wrong way.
    struct HZBDesc
    {
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::uint32_t MipLevels{0u};
        RHI::Format   Fmt{RHI::Format::R32_FLOAT};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Width > 0u && Height > 0u && MipLevels > 0u;
        }
        [[nodiscard]] bool operator==(const HZBDesc&) const noexcept = default;
    };

    // Smallest power of two >= `v` (clamped to 1 for `v == 0`). Saturates to the
    // top bit on overflow so a pathological extent can never wrap to 0.
    [[nodiscard]] std::uint32_t NextPow2(std::uint32_t v) noexcept;

    // Pure HZB sizing for a render extent (decision 1). Width/Height are the next
    // power of two of each render dimension; `MipLevels = floor(log2(max(W,H))) + 1`
    // so the chain halves to 1x1. A zero render extent yields an invalid (all-zero)
    // desc.
    [[nodiscard]] HZBDesc ComputeHZBDesc(std::uint32_t renderWidth,
                                         std::uint32_t renderHeight) noexcept;

    enum class HZBBuildMode : std::uint8_t
    {
        PerMipDispatch = 0,
        SinglePassMipChain,
    };

    struct HZBBuildCapabilities
    {
        bool SupportsSinglePassMipChain{false};
    };

    struct HZBBuildDispatchDesc
    {
        std::uint32_t TargetMip{0u};
        std::uint32_t SourceMip{0u};
        bool ReadsDepthSource{true};
        std::uint32_t TargetWidth{0u};
        std::uint32_t TargetHeight{0u};
        std::uint32_t GroupCountX{0u};
        std::uint32_t GroupCountY{0u};
        std::uint32_t GroupCountZ{1u};
    };

    struct HZBBuildDispatchPlan
    {
        HZBBuildMode Mode{HZBBuildMode::PerMipDispatch};
        HZBDesc Desc{};
        std::vector<HZBBuildDispatchDesc> Dispatches{};

        [[nodiscard]] bool IsValid() const noexcept
        {
            return Desc.IsValid() && !Dispatches.empty();
        }
    };

    // GRAPHICS-038B — CPU-testable dispatch planning for the HZB build pass.
    // `SupportsSinglePassMipChain` selects the SPD-style one-dispatch path;
    // otherwise each mip is dispatched in ascending target-mip order with
    // inter-dispatch barriers recorded by `RecordHZBBuild(...)`.
    [[nodiscard]] HZBBuildDispatchPlan ComputeHZBBuildDispatchPlan(
        const HZBDesc& desc,
        HZBBuildCapabilities capabilities,
        std::uint32_t tileSize = 16u);

    // Matches `assets/shaders/hzb_build.comp` std430 push layout.
    struct HZBBuildPushConstants
    {
        std::uint32_t RenderWidth{0u};
        std::uint32_t RenderHeight{0u};
        std::uint32_t TargetMip{0u};
        std::uint32_t SourceMip{0u};
        std::uint32_t MipCount{0u};
        std::uint32_t BuildMode{0u};
        std::uint32_t TargetWidth{0u};
        std::uint32_t TargetHeight{0u};
    };

    // Records the backend-neutral HZB build command shape. Returns false when
    // required inputs are invalid; otherwise records every planned dispatch.
    bool RecordHZBBuild(RHI::ICommandContext& cmd,
                        RHI::PipelineHandle pipeline,
                        RHI::TextureHandle hzbTexture,
                        const HZBBuildDispatchPlan& plan);

    struct HZBDiagnostics
    {
        // Ping-pong pair allocations (each allocation creates two textures).
        std::uint32_t AllocationCount{0u};
        // Reallocations triggered by an extent/format change (each retires the
        // previous pair through the retire-deadline window).
        std::uint32_t ReallocationCount{0u};
        // Superseded textures actually freed by `Tick(...)` after their deadline.
        std::uint32_t RetiredTextureCount{0u};
        // Superseded textures still waiting in the retire queue.
        std::uint32_t PendingRetireCount{0u};
    };

    // Retained graphics-owned ping-pong HZB pair (`GRAPHICS-038A`, decision 3).
    //
    // Owns two retained textures carried across frames. Each frame one is the
    // *previous* HZB (read by the phase-1 cull) and the other is the *current* HZB
    // (written by the phase-2 build); `AdvanceFrame()` swaps the two roles so the
    // current frame's pyramid becomes next frame's history. A render-extent or
    // format change reallocates both textures and retires the superseded leases
    // through a `framesInFlight` retire-deadline window (the `GRAPHICS-015Q`
    // pattern) so an in-flight frame can never sample a freed texture.
    //
    // This slice owns the resource shape + retained ping-pong lifetime only; the
    // build compute shader is `GRAPHICS-038B`, the two-phase cull-shader extension
    // is `GRAPHICS-038C`, and the opt-in `gpu;vulkan` conservatism smoke is
    // `GRAPHICS-038E`. Layering: imports RHI only (renderer -> rhi), mirroring the
    // retained-resource ownership of `Extrinsic.Graphics.ShadowSystem`.
    class HZBSystem
    {
    public:
        HZBSystem();
        ~HZBSystem();

        HZBSystem(const HZBSystem&)            = delete;
        HZBSystem& operator=(const HZBSystem&) = delete;

        // The texture manager must outlive the system; it owns texture creation
        // and bindless registration.
        void Initialize(RHI::IDevice& device, RHI::TextureManager& textureMgr);
        void Shutdown();
        [[nodiscard]] bool IsInitialized() const noexcept;

        // Allocate or reallocate the ping-pong pair for the render extent. A no-op
        // (returns true) when the resolved `HZBDesc` is unchanged. On a changed
        // desc the previous pair is retired with deadline `currentFrame` and a new
        // pair is created. Returns false (leaving the system unallocated) for a
        // degenerate extent or a texture-create failure.
        bool EnsureAllocated(std::uint32_t renderWidth,
                             std::uint32_t renderHeight,
                             std::uint64_t currentFrame);

        // Drain the retire queue: free every superseded lease whose deadline is at
        // least `framesInFlight` frames in the past relative to `currentFrame`.
        void Tick(std::uint64_t currentFrame, std::uint32_t framesInFlight);

        // Swap the previous/current ping-pong roles for the next frame.
        void AdvanceFrame() noexcept;

        // Phase-2 write target (this frame's pyramid).
        [[nodiscard]] RHI::TextureHandle CurrentHZB() const noexcept;
        // Phase-1 read source (the previous frame's pyramid).
        [[nodiscard]] RHI::TextureHandle PreviousHZB() const noexcept;

        [[nodiscard]] HZBDesc        GetAllocatedDesc() const noexcept;
        [[nodiscard]] bool           IsAllocated() const noexcept;
        [[nodiscard]] HZBDiagnostics GetDiagnostics() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
