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
        std::uint64_t      Generation  = 0;
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
        RHI::SamplerHandle Sampler{};
    };

    class GpuAssetCache
    {
    public:
        GpuAssetCache(RHI::BufferManager&  buffers,
                      RHI::TextureManager& textures,
                      RHI::ITransferQueue& transfer);
        ~GpuAssetCache();

        GpuAssetCache(const GpuAssetCache&)            = delete;
        GpuAssetCache& operator=(const GpuAssetCache&) = delete;

        // Submit a CPU byte blob for upload.
        //   ResourceBusy      — entry is already GpuUploading; previous upload
        //                       is still in flight, retry after the next Tick.
        //   OutOfDeviceMemory — Buffer/Texture allocation failed; entry
        //                       transitions to Failed.
        //   InvalidArgument   — Id is not valid.
        Core::Result RequestUpload(const GpuBufferRequest&  req);
        Core::Result RequestUpload(const GpuTextureRequest& req);

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

        [[nodiscard]] std::size_t TrackedCount()       const;
        [[nodiscard]] std::size_t PendingRetireCount() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
