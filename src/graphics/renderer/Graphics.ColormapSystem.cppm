module;

#include <cstdint>
#include <memory>
#include <glm/glm.hpp>

export module Extrinsic.Graphics.ColormapSystem;

import Extrinsic.Graphics.Colormap;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Transfer;

// ============================================================
// ColormapSystem — GPU colourmap LUT owner.
//
// Allocates one 256×1 RGBA8_UNORM 1-D texture per Colormap::Type,
// submits its initial bytes through ITransferQueue at Initialize(),
// and registers each in the bindless descriptor heap.  Shaders sample the LUT at the
// normalised scalar value t ∈ [0,1] to obtain the mapped colour.
//
// Also provides a CPU-side SampleCpu() path for tests/tools that need a
// reference colourmap lookup. Retained surface, line, and point renderables
// resolve scalar fields on the GPU through common/gpu_scene.glsl.
//
// Lifetime contract:
//   Initialize() before Sync() or any query.
//   Shutdown() before the device is destroyed.
//   TextureManager and SamplerManager leases held internally.
//
// Thread-safety:
//   Initialize() / Shutdown() — render thread only.
//   GetBindlessIndex()        — lock-free read after Initialize().
//   SampleCpu()               — const, any thread.
// ============================================================

export namespace Extrinsic::Graphics
{
    class ColormapSystem
    {
    public:
        ColormapSystem();
        ~ColormapSystem();

        ColormapSystem(const ColormapSystem&)            = delete;
        ColormapSystem& operator=(const ColormapSystem&) = delete;

        // -----------------------------------------------------------------
        // Lifecycle
        // -----------------------------------------------------------------

        /// Upload all colourmap LUT textures to the GPU and register them
        /// in the bindless heap.  Idempotent — harmless to call twice.
        void Initialize(RHI::IDevice&        device,
                        RHI::TextureManager& textureMgr,
                        RHI::SamplerManager& samplerMgr);

        /// Release all GPU textures and bindless slots.
        void Shutdown();

        [[nodiscard]] bool IsInitialized() const noexcept;

        /// True once all asynchronous LUT uploads submitted by Initialize()
        /// have completed. Bindless indices remain invalid until this returns
        /// true so first-frame users can branch deterministically instead of
        /// sampling a texture whose staging transfer is still in flight.
        [[nodiscard]] bool IsReady() const noexcept;

        // -----------------------------------------------------------------
        // GPU query
        // -----------------------------------------------------------------

        /// Bindless slot index for the given colourmap's 1-D LUT texture.
        /// Returns kInvalidBindlessIndex before Initialize(), before IsReady(),
        /// or for Colormap::Type::Count (the sentinel value).
        [[nodiscard]] RHI::BindlessIndex GetBindlessIndex(Colormap::Type t) const noexcept;

        // -----------------------------------------------------------------
        // CPU sampling — reference lookup for tests/tools.
        // -----------------------------------------------------------------

        /// Sample the colourmap at normalised scalar value t ∈ [0,1].
        /// t is clamped to [0,1] before lookup.
        /// Returns RGBA in [0,255] per channel.
        struct RGBA8 { std::uint8_t R, G, B, A; };
        [[nodiscard]] RGBA8 SampleCpu(Colormap::Type t, float scalar) const noexcept;

        /// Pack a RGBA8 sample as a uint32_t (R in MSB, A in LSB):
        ///   bits [31:24] = R  [23:16] = G  [15:8] = B  [7:0] = A
        [[nodiscard]] static std::uint32_t PackRGBA8(RGBA8 c) noexcept;

        /// Pack a normalised glm::vec4 colour to a uint32_t RGBA8.
        [[nodiscard]] static std::uint32_t PackVec4(glm::vec4 c) noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
