module;

#include <cstdint>
#include <span>
#include <cstddef>

export module Extrinsic.RHI.TransferQueue;

import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;

// ============================================================
// ITransferQueue — API-agnostic async GPU upload interface.
//
// Obtained via IDevice::GetTransferQueue().
// Lifetime is tied to the IDevice.
//
// Why separate from IDevice:
//   GpuAssetCache holds a reference to the transfer queue
//   independently of the device — it must submit uploads from
//   the asset-streaming path without dragging in the full device
//   interface.  Keeping upload/poll/collect here respects the
//   Assets ↔ Graphics boundary: GpuAssetCache injects
//   ITransferQueue&, never IDevice&.
//
// Thread-safety contract:
//   UploadBuffer / UploadTexture — safe to call from any thread
//     (e.g. asset loader thread).  Data is copied into a staging
//     buffer on the caller thread; the GPU submission happens
//     asynchronously on the transfer queue.
//   IsComplete — safe from any thread (atomic read).
//   CollectCompleted — render thread only, once per frame, after
//     EndFrame().  This is the only call that waits on GPU state.
//
// Invariant (matches src/ async-upload guarantee):
//   No caller thread ever blocks on a GPU fence inside
//   UploadBuffer / UploadTexture.  Fence waiting happens
//   exclusively inside CollectCompleted() on the render thread.
// ============================================================

export namespace Extrinsic::RHI
{
    class ITransferQueue
    {
    public:
        virtual ~ITransferQueue() = default;

        // ---- Buffer uploads ------------------------------------------

        /// Non-blocking: copies `size` bytes from `data` into a staging
        /// buffer and queues a GPU transfer into `dst` at `offset`.
        /// Returns a token the caller can poll with IsComplete().
        /// `dst` must have been created with BufferUsage::TransferDst.
        [[nodiscard]] virtual TransferToken UploadBuffer(BufferHandle   dst,
                                                         const void*    data,
                                                         std::uint64_t  size,
                                                         std::uint64_t  offset = 0) = 0;

        /// Span overload — preferred when the source is a typed range.
        [[nodiscard]] virtual TransferToken UploadBuffer(BufferHandle              dst,
                                                         std::span<const std::byte> src,
                                                         std::uint64_t              offset = 0) = 0;

        // ---- Texture uploads -----------------------------------------

        /// Non-blocking: copies `dataSizeBytes` bytes from `data` into a
        /// staging buffer and queues a GPU transfer into `dst` mip/layer.
        /// `dst` must have been created with TextureUsage::TransferDst.
        [[nodiscard]] virtual TransferToken UploadTexture(TextureHandle  dst,
                                                          const void*    data,
                                                          std::uint64_t  dataSizeBytes,
                                                          std::uint32_t  mipLevel   = 0,
                                                          std::uint32_t  arrayLayer = 0) = 0;

        // ---- Completion polling --------------------------------------

        /// Returns true when the GPU has finished the transfer associated
        /// with `token`.  Safe to call from any thread.
        [[nodiscard]] virtual bool IsComplete(TransferToken token) const = 0;

        // ---- Frame maintenance ---------------------------------------

        /// Retire completed staging allocations and reclaim staging memory.
        /// Must be called exactly once per frame on the render thread,
        /// after EndFrame().  Never call from a loader thread.
        virtual void CollectCompleted() = 0;
    };
}

