module;

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

export module Extrinsic.Graphics.GpuAssetCache;

import Extrinsic.Core.Error;
import Extrinsic.Asset.Registry;
import Extrinsic.RHI.Bindless;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.SamplerManager;
import Extrinsic.RHI.TextureManager;
import Extrinsic.RHI.TransferQueue;

// ============================================================
// Graphics.GpuAssetCache
// ============================================================
// Graphics-owned bridge between asset identity and GPU resources.
// AssetRegistry stores AssetMeta only; this side table maps AssetId
// onto refcounted Buffer/Texture leases plus a small state machine.
//
// State machine:
//
//   NotRequested -- Reserve --------------> CpuPending
//   NotRequested -- RequestUpload ---------> GpuUploading
//   CpuPending   -- RequestUpload ---------> GpuUploading
//   Failed       -- RequestUpload ---------> GpuUploading
//   Ready        -- RequestUpload ---------> GpuUploading  (old lease retired)
//   Ready        -- NotifyReloaded --------> CpuPending    (old lease retained)
//   GpuUploading -- transfer complete -----> Ready
//   GpuUploading -- create / queue fail ---> Failed
//   *            -- non-operational GPU --> unchanged (retryable deferral)
//   *            -- NotifyDestroyed -------> NotRequested  (lease retired)
//
// Old-view lifetime preservation:
//   When a hot reload swaps in a new GPU resource, the previous
//   Lease is moved into a retire queue with
//   `retireDeadline = currentFrame + framesInFlight`.  Tick()
//   drops queue entries whose deadline has been reached.  Renderer
//   snapshots that captured a GpuAssetView in an earlier frame
//   continue to dereference a live GPU resource for at least
//   `framesInFlight` frames after the swap.
//
// Threading:
//   RequestUpload / NotifyFailed / NotifyReloaded / NotifyDestroyed
//   are safe from any thread (they do not call IDevice).  Tick()
//   must run on the render thread because Lease destruction calls
//   the manager's Release() path, which may reach into IDevice.
//   GetState / GetView are lock-protected reads.
//
// Texture residency slice:
//   Texture uploads may carry an externally owned sampler handle or a
//   sampler descriptor.  When a SamplerManager is provided to the cache,
//   descriptor-backed requests retain a deduplicated sampler lease alongside
//   the texture lease.  A single deterministic fallback texture can be
//   initialized and queried through GetViewOrFallback() for missing, pending,
//   or failed texture assets.  The current cache is explicitly non-evicting;
//   diagnostics expose this policy until a later capacity task introduces
//   bounded eviction semantics.
// ============================================================

export namespace Extrinsic::Graphics
{
    enum class GpuAssetState : std::uint8_t
    {
        NotRequested,
        CpuPending,
        GpuUploading,
        Ready,
        Failed,
    };

    enum class GpuAssetKind : std::uint8_t
    {
        Buffer,
        Texture,
    };

    struct GpuAssetView
    {
        GpuAssetKind       Kind        = GpuAssetKind::Buffer;
        RHI::BufferHandle  Buffer{};
        RHI::TextureHandle Texture{};
        RHI::BindlessIndex BindlessIdx = RHI::kInvalidBindlessIndex;
        RHI::SamplerHandle Sampler{};
        std::uint64_t      Generation  = 0;
    };

    enum class GpuAssetFallbackReason : std::uint8_t
    {
        None,
        InvalidId,
        Missing,
        Pending,
        Failed,
        Unavailable,
    };

    struct GpuAssetResolvedView
    {
        GpuAssetView View{};
        GpuAssetState RequestedState = GpuAssetState::NotRequested;
        GpuAssetFallbackReason FallbackReason = GpuAssetFallbackReason::None;
        bool UsedFallback = false;
    };

    struct GpuAssetCacheDiagnostics
    {
        std::size_t TrackedAssets = 0;
        std::size_t PendingRetireRecords = 0;
        std::uint64_t UploadRequests = 0;
        std::uint64_t TextureUploadRequests = 0;
        std::uint64_t UploadFailures = 0;
        std::uint64_t UploadDeferrals = 0;
        std::uint64_t TextureCreateFailures = 0;
        std::uint64_t SamplerCreateFailures = 0;
        std::uint64_t FallbackHits = 0;
        std::uint64_t FallbackMisses = 0;
        bool FallbackTextureReady = false;
        bool NonEvictingCache = true;
    };

    struct GpuBufferRequest
    {
        Assets::AssetId Id{};
        std::span<const std::byte> Bytes{};
        RHI::BufferDesc Desc{};
    };

    struct GpuTextureRequest
    {
        Assets::AssetId Id{};
        std::span<const std::byte> Bytes{};
        RHI::TextureDesc Desc{};
        RHI::SamplerDesc SamplerDesc{};
        RHI::SamplerHandle Sampler{};
    };

    struct GpuProducedTextureRequest
    {
        Assets::AssetId Id{};
        RHI::TextureDesc Desc{};
        RHI::SamplerDesc SamplerDesc{};
        RHI::SamplerHandle Sampler{};
        std::uint64_t ReadyFrame = 0;
        bool HasReadyFrame = false;
    };

    struct GpuProducedTexturePendingView
    {
        RHI::TextureHandle Texture{};
        RHI::BindlessIndex BindlessIdx = RHI::kInvalidBindlessIndex;
        RHI::SamplerHandle Sampler{};
        std::uint64_t Generation = 0;
    };

    struct GpuTextureFallbackDesc
    {
        std::span<const std::byte> Bytes{};
        RHI::TextureDesc Desc{
            .Width = 1,
            .Height = 1,
            .MipLevels = 1,
            .Fmt = RHI::Format::RGBA8_UNORM,
            .Usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst,
            .DebugName = "gpu-asset-fallback-texture",
        };
        RHI::SamplerDesc SamplerDesc{
            .MagFilter = RHI::FilterMode::Nearest,
            .MinFilter = RHI::FilterMode::Nearest,
            .MipFilter = RHI::MipmapMode::Nearest,
            .AddressU = RHI::AddressMode::ClampToEdge,
            .AddressV = RHI::AddressMode::ClampToEdge,
            .AddressW = RHI::AddressMode::ClampToEdge,
            .DebugName = "gpu-asset-fallback-sampler",
        };
        RHI::SamplerHandle Sampler{};
    };

    class GpuAssetCache
    {
    public:
        GpuAssetCache(RHI::BufferManager&  buffers,
                      RHI::TextureManager& textures,
                      RHI::ITransferQueue& transfer);
        GpuAssetCache(RHI::BufferManager&  buffers,
                      RHI::TextureManager& textures,
                      RHI::SamplerManager& samplers,
                      RHI::ITransferQueue& transfer);
        ~GpuAssetCache();

        GpuAssetCache(const GpuAssetCache&)            = delete;
        GpuAssetCache& operator=(const GpuAssetCache&) = delete;

        // Submit a CPU byte blob for upload.
        //   ResourceBusy      — entry is already GpuUploading; previous upload
        //                       is still in flight, retry after the next Tick.
        //   DeviceNotOperational — GPU backend cannot accept work yet; entry
        //                          stays in its previous state for a retry.
        //   OutOfDeviceMemory    — Buffer/Texture allocation failed; entry
        //                          transitions to Failed.
        //   InvalidArgument   — Id is not valid.
        Core::Result RequestUpload(const GpuBufferRequest&  req);
        Core::Result RequestUpload(const GpuTextureRequest& req);
        [[nodiscard]] Core::Expected<GpuProducedTexturePendingView>
            BeginGpuProducedTexture(const GpuProducedTextureRequest& req);
        Core::Result SetGpuProducedTextureReadyFrame(Assets::AssetId id,
                                                     std::uint64_t readyFrame);
        // Fail only the exact GPU-produced pending generation. This never
        // creates a missing slot and never retires a newer replacement.
        Core::Result FailGpuProducedTexture(Assets::AssetId id,
                                            std::uint64_t generation);
        Core::Result InitializeFallbackTexture(const GpuTextureFallbackDesc& desc = {});

        // Force CpuPending (no GPU work yet).  Idempotent for an entry that
        // is already in a non-Ready state; on a Ready entry it is a no-op
        // (Reserve does not retire — use NotifyReloaded for that).
        void Reserve(Assets::AssetId id);

        // Mark an entry Failed.  Creates the entry in Failed state if it
        // did not exist.  Any Pending lease is moved to the retire queue.
        void NotifyFailed(Assets::AssetId id);

        // Hot-reload signal: a Ready entry transitions back to CpuPending,
        // keeping the current lease live so renderer snapshots remain valid
        // until a subsequent RequestUpload swaps in the new GPU resource.
        // No-op for entries that are not Ready.
        void NotifyReloaded(Assets::AssetId id);

        // Asset destroyed at the CPU layer — drop the entry and queue any
        // live leases for retirement.
        void NotifyDestroyed(Assets::AssetId id);

        // Per-frame maintenance.  Polls in-flight transfer tokens and
        // drops retire-queue entries whose deadline has been reached.
        void Tick(std::uint64_t currentFrame, std::uint32_t framesInFlight);

        [[nodiscard]] GpuAssetState                GetState(Assets::AssetId id) const;
        [[nodiscard]] Core::Expected<GpuAssetView> GetView (Assets::AssetId id) const;
        [[nodiscard]] Core::Expected<GpuAssetResolvedView> GetViewOrFallback(Assets::AssetId id);

        [[nodiscard]] std::size_t TrackedCount()       const;
        [[nodiscard]] std::size_t PendingRetireCount() const;
        [[nodiscard]] GpuAssetCacheDiagnostics GetDiagnostics() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
