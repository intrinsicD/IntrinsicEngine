module;

#include <memory>
#include <cstdint>

#include <glm/glm.hpp>

export module Extrinsic.Graphics.ShadowSystem;

import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.Types;

export namespace Extrinsic::Graphics
{
    struct ShadowParams
    {
        bool Enabled{false};
        std::uint32_t CascadeCount{0u};
        std::uint32_t AtlasResolution{2048u};
        float DepthBias{0.001f};
        float NormalBias{0.01f};
        float PcfRadius{1.0f};
        float SplitLambda{0.5f};
    };

    struct ShadowCascadeData
    {
        glm::mat4 ViewProj[RHI::kMaxShadowCascades]{};
        float Splits[RHI::kMaxShadowCascades]{};
        std::uint32_t CascadeCount{0u};
    };

    struct ShadowAtlasDesc
    {
        bool Enabled{false};
        std::uint32_t Width{0u};
        std::uint32_t Height{0u};
        std::uint32_t CascadeCount{0u};
        std::uint32_t Resolution{0u};
    };

    struct ShadowDiagnostics
    {
        std::uint32_t UnsupportedCascadeCount{0u};
        std::uint32_t DisabledShadowStateCount{0u};
        // GRAPHICS-073 Slice B — `Pass.Shadows::Execute` increments this when
        // shadows are enabled but the `ShadowOpaque` cull bucket carries no
        // casters. Surfacing this lets the operational gate distinguish "no
        // casters extracted yet" from "atlas wiring broken" without inspecting
        // a recorded-status taxonomy that, by design, reports
        // `SkippedUnavailable` for the same condition.
        std::uint32_t MissingCasterCount{0u};
    };

    class ShadowSystem
    {
    public:
        ShadowSystem();
        ~ShadowSystem();

        ShadowSystem(const ShadowSystem&)            = delete;
        ShadowSystem& operator=(const ShadowSystem&) = delete;

        // GRAPHICS-073 Slice B — the device/manager-aware Initialize takes the
        // texture and sampler managers so the system can own the depth-only
        // shadow atlas and its `sampler2DShadow`-bindable sampler. The atlas is
        // allocated lazily on the first `SetParams(...)` call that enables
        // shadows so the operational `Initialize` path (which still runs with
        // shadows disabled) does not allocate a 2048x2048 D32 atlas that no
        // caller would sample. The references must outlive the system.
        void Initialize(RHI::IDevice& device,
                        RHI::TextureManager& textureMgr,
                        RHI::SamplerManager& samplerMgr);
        void Shutdown();

        void SetParams(const ShadowParams& params) noexcept;
        [[nodiscard]] ShadowParams GetParams() const noexcept;

        void SetCascadeData(const ShadowCascadeData& cascades) noexcept;
        [[nodiscard]] ShadowCascadeData GetCascadeData() const noexcept;

        [[nodiscard]] ShadowAtlasDesc BuildAtlasDesc() const noexcept;
        void ApplyTo(RHI::CameraUBO& camera) const noexcept;
        [[nodiscard]] ShadowDiagnostics GetDiagnostics() const noexcept;
        [[nodiscard]] bool IsEnabled() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        // GRAPHICS-073 Slice B — `ShadowSystem`-owned atlas/sampler accessors.
        // Returns the bound atlas handle once the lazy allocation has run, or
        // an invalid handle when shadows have never been enabled or the atlas
        // was released. The handle is byte-identical across renderer
        // operational rebuilds because `ShadowSystem` is not re-initialized
        // there (the leases hang off `Impl`, not the per-rebuild pipeline
        // manager).
        [[nodiscard]] RHI::TextureHandle GetAtlasTexture() const noexcept;
        [[nodiscard]] RHI::SamplerHandle GetAtlasSampler() const noexcept;
        [[nodiscard]] RHI::BindlessIndex GetAtlasBindlessIndex() const noexcept;
        [[nodiscard]] ShadowAtlasDesc    GetAllocatedAtlasDesc() const noexcept;
        // Used by `Pass.Shadows::Execute` to record the missing-caster
        // diagnostic in `ShadowDiagnostics` without exposing the impl struct.
        void RecordMissingCaster() noexcept;

    private:
        struct Impl;
        // GRAPHICS-073 Slice B — kept as a private static helper so it can
        // touch `Impl` (private nested type) without being declared a friend.
        // Idempotent: a no-op when the atlas is already allocated or the
        // params do not yet ask for shadows.
        static bool TryAllocateAtlas(Impl& impl) noexcept;
        std::unique_ptr<Impl> m_Impl;
    };
}
