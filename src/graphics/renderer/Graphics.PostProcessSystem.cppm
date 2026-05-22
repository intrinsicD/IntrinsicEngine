module;

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

export module Extrinsic.Graphics.PostProcessSystem;

import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.TextureManager;

export namespace Extrinsic::Graphics
{
    // GRAPHICS-075 Slice D.2b — retained SMAA lookup-texture dimensions.
    // The SMAA reference defines a 160x560 RG8_UNORM area texture and a
    // 66x33 R8_UNORM search texture (see
    // `src/legacy/Graphics/Passes/Graphics.SMAALookupTextures.hpp` for the
    // generator). Both are uploaded once at device-aware Initialize() time
    // and survive `RebuildOperationalResources()` byte-identical so the
    // SMAA blend pass can sample them across recipe rebuilds without
    // re-uploading the analytical LUT bytes.
    inline constexpr std::uint32_t kPostProcessSMAAAreaTextureWidth  = 160u;
    inline constexpr std::uint32_t kPostProcessSMAAAreaTextureHeight = 560u;
    inline constexpr std::uint32_t kPostProcessSMAASearchTextureWidth  = 66u;
    inline constexpr std::uint32_t kPostProcessSMAASearchTextureHeight = 33u;

    enum class PostProcessAntiAliasing : std::uint8_t
    {
        None = 0,
        FXAA,
        SMAA,
    };

    enum class PostProcessStageKind : std::uint8_t
    {
        Histogram = 0,
        Bloom,
        ToneMap,
        FXAA,
        SMAA,
    };

    struct PostProcessSettings
    {
        bool Enabled{true};
        bool EnableHistogram{false};
        bool EnableBloom{false};
        PostProcessAntiAliasing AntiAliasing{PostProcessAntiAliasing::None};
        float Exposure{1.0f};
        float Gamma{2.2f};
        float BloomIntensity{0.05f};
        std::uint32_t HistogramBinCount{256u};
    };

    struct PostProcessStageDesc
    {
        PostProcessStageKind Kind{PostProcessStageKind::ToneMap};
        const char* Name{"ToneMap"};
        bool ReadsHDR{false};
        bool WritesLDR{false};
        bool UsesIntermediate{false};
    };

    struct PostProcessDiagnostics
    {
        std::uint32_t InvalidSettingCount{0u};
        std::uint32_t UnsupportedCombinationCount{0u};
        bool ChainEnabled{true};
        bool WritesLDR{true};
    };

    struct PostProcessChainDesc
    {
        bool Enabled{true};
        bool WritesLDR{true};
        std::vector<PostProcessStageDesc> Stages{};
        PostProcessDiagnostics Diagnostics{};
    };

    struct PostProcessPushConstants
    {
        float Exposure{1.0f};
        float Gamma{2.2f};
        float BloomIntensity{0.05f};
        std::uint32_t HistogramBinCount{256u};
        std::uint32_t StageKind{0u};
    };

    // GRAPHICS-075 Slice D.2b — CPU mirror of the std430 exposure-adaptation
    // history buffer that the histogram readback drain (Slice E) updates and
    // the tonemap pass reads back next frame. Slice D.2b allocates the
    // buffer; Slice E populates it.
    struct PostProcessExposureHistory
    {
        float          PreviousAverageLogLum{0.0f};
        float          AdaptationVelocity{0.0f};
        std::uint32_t  FrameIndex{0u};
        std::uint32_t  _Pad0{0u};
    };

    class PostProcessSystem
    {
    public:
        PostProcessSystem();
        ~PostProcessSystem();

        PostProcessSystem(const PostProcessSystem&)            = delete;
        PostProcessSystem& operator=(const PostProcessSystem&) = delete;

        // CPU-only initializer used by unit tests and by callers that do not
        // own a live device. Preserves the historical no-args path so the
        // pre-Slice D.2b unit tests (which exercise the chain descriptor and
        // push-constant builder without any GPU resources) continue to
        // compile and pass unchanged.
        void Initialize();

        // GRAPHICS-075 Slice D.2b — device-aware initializer. Allocates the
        // retained SMAA `AreaTex` / `SearchTex` LUT textures (uploaded via
        // `device.GetTransferQueue().UploadTexture(...)`) and the
        // exposure-adaptation history buffer. Idempotent: a second call
        // re-binds the manager pointers but does not re-allocate or
        // re-upload, so the leases survive `RebuildOperationalResources()`
        // byte-identical. Allocation is skipped when the device reports
        // `IsOperational() == false`; a later call against an operational
        // device performs the allocation.
        //
        // The references must outlive the system.
        void Initialize(RHI::IDevice& device,
                        RHI::TextureManager& textureMgr,
                        RHI::BufferManager& bufferMgr);

        void Shutdown();

        void SetSettings(const PostProcessSettings& settings);

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const PostProcessSettings& GetSettings() const noexcept;
        [[nodiscard]] PostProcessDiagnostics GetDiagnostics() const noexcept;
        [[nodiscard]] PostProcessChainDesc DescribeChain() const;
        [[nodiscard]] bool IsStageEnabled(PostProcessStageKind stage) const;
        [[nodiscard]] PostProcessPushConstants BuildPushConstants(PostProcessStageKind stage) const noexcept;

        // GRAPHICS-075 Slice D.2b — retained-resource accessors. Handles are
        // invalid until the device-aware Initialize() has run against an
        // operational device.
        [[nodiscard]] RHI::TextureHandle GetSMAAAreaTexture() const noexcept;
        [[nodiscard]] RHI::TextureHandle GetSMAASearchTexture() const noexcept;
        [[nodiscard]] RHI::BufferHandle  GetExposureHistoryBuffer() const noexcept;

        // GRAPHICS-075 Slice E.2 — consume one frame's 256-bin histogram
        // payload after the `BeginFrame()`-side readback drain decodes it
        // from the renderer-owned host-visible `Histogram.Readback` slot.
        // Updates the retained `PostProcessExposureHistory` CPU mirror
        // (`PreviousAverageLogLum`, `AdaptationVelocity`, `FrameIndex`) from
        // the bin distribution and, when an operational device is available,
        // uploads the new history payload into the device-side
        // `PostProcess.ExposureHistory` storage buffer via
        // `IDevice::GetTransferQueue().UploadBuffer(...)` (mirroring the SMAA
        // LUT upload path Slice D.2b uses). `bins` must carry exactly 256
        // entries; shorter spans are rejected as a diagnostics counter bump
        // and treated as a no-op. The publish counter on the diagnostics
        // surface lets contract tests assert the drain → publish handshake
        // ran exactly once per completed frame.
        void PublishHistogramReadback(std::span<const std::uint32_t> bins,
                                      std::uint64_t frameIndex,
                                      RHI::IDevice* device) noexcept;

        // GRAPHICS-075 Slice E.2 — CPU-visible snapshot of the most recent
        // exposure-history update. Test seam: the readback drain's effect on
        // the retained `PostProcessExposureHistory` payload is observable
        // here without round-tripping through the GPU buffer. Returns the
        // default-constructed payload before the first publish call.
        [[nodiscard]] PostProcessExposureHistory GetExposureHistorySnapshot() const noexcept;

        // GRAPHICS-075 Slice E.2 — count of successful publish calls (rejected
        // payloads with non-256 bin spans are tracked separately via
        // `PostProcessDiagnostics::InvalidSettingCount`).
        [[nodiscard]] std::uint32_t GetHistogramPublishCount() const noexcept;

    private:
        struct Impl;
        // GRAPHICS-075 Slice D.2b — declared as a private static helper so
        // the implementation TU can touch `Impl` (private nested type)
        // without being declared a friend. Mirrors the ShadowSystem
        // `TryAllocateAtlas` pattern. Idempotent: a no-op when the leases
        // are already valid or when the device is non-operational.
        static void TryAllocateRetainedResources(Impl& impl,
                                                 RHI::IDevice& device);
        std::unique_ptr<Impl> m_Impl;
    };
}
