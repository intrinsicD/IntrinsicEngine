module;

#include <compare>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <span>
#include <utility>

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
//   UploadBuffer / UploadTexture / UploadTextureFullChain — safe to call from any thread
//     (e.g. asset loader thread).  Data is copied into a staging
//     buffer on the caller thread; the GPU submission happens
//     asynchronously on the transfer queue.
//   DownloadBuffer — safe to call from any thread. Backends copy the requested
//     device range into backend-owned staging and deliver bytes later through a
//     ReadbackSink from CollectCompleted().
//   IsComplete — safe from any thread (atomic read).
//   CollectCompleted — render thread only, once per frame, after
//     EndFrame().  This is the only call that waits on GPU state.
//
// Invariant (matches src/ async-upload guarantee):
//   No caller thread ever blocks on a GPU fence inside
//   UploadBuffer / UploadTexture / UploadTextureFullChain / DownloadBuffer.
//   Fence waiting happens exclusively inside CollectCompleted() on the render
//   thread.
// ============================================================

export namespace Extrinsic::RHI
{
    struct ReadbackToken
    {
        std::uint64_t Value = 0;

        [[nodiscard]] bool IsValid() const noexcept { return Value != 0; }
        [[nodiscard]] explicit operator bool() const noexcept { return IsValid(); }
        [[nodiscard]] auto operator<=>(const ReadbackToken&) const noexcept = default;
    };

    using ReadbackCallback = std::function<void(std::span<const std::byte>)>;

    struct ReadbackSink
    {
        std::span<std::byte> Destination{};
        ReadbackCallback Callback{};

        [[nodiscard]] static ReadbackSink CopyTo(std::span<std::byte> destination)
        {
            return ReadbackSink{.Destination = destination};
        }

        [[nodiscard]] static ReadbackSink Invoke(ReadbackCallback callback)
        {
            return ReadbackSink{.Callback = std::move(callback)};
        }

        [[nodiscard]] bool IsValidForSize(std::uint64_t sizeBytes) const noexcept
        {
            return Callback || Destination.size_bytes() == static_cast<std::size_t>(sizeBytes);
        }

        void Deliver(std::span<const std::byte> bytes) const
        {
            if (!Destination.empty() && Destination.size_bytes() == bytes.size_bytes())
                std::memcpy(Destination.data(), bytes.data(), bytes.size_bytes());
            if (Callback)
                Callback(bytes);
        }
    };

    struct TransferQueueDiagnostics
    {
        std::uint64_t DownloadsQueued = 0;
        std::uint64_t DownloadsCompleted = 0;
        std::uint64_t DownloadsDropped = 0;
        std::uint64_t ReadbackBytesStaged = 0;
        std::uint64_t ReadbackRingHighWaterBytes = 0;
    };

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

        /// Non-blocking full texture-chain upload. `src` must be packed according
        /// to `ComputeFullChainUploadLayout()` for the destination texture's
        /// descriptor: layer-major, mip-minor order with each subresource offset
        /// aligned to `RequiredBufferOffsetAlignment(format)`. Backends must
        /// validate the destination texture metadata and byte count before
        /// accepting the upload; unsupported formats, depth-stencil textures, or
        /// size mismatches fail closed by returning an invalid token.
        ///
        /// Keep this appended after the original upload/poll/collect virtuals so
        /// existing consumers continue to call the same vtable slots for
        /// IsComplete() and CollectCompleted().
        [[nodiscard]] virtual TransferToken UploadTextureFullChain(TextureHandle              dst,
                                                                   std::span<const std::byte> src) = 0;

        // ---- Buffer readbacks ----------------------------------------

        /// Non-blocking: queues a GPU transfer from `src` at `offset` into
        /// backend-owned host-visible staging. The requested bytes are delivered
        /// to `sink` from CollectCompleted() after the transfer timeline reports
        /// completion. `src` must have BufferUsage::TransferSrc support.
        [[nodiscard]] virtual ReadbackToken DownloadBuffer(BufferHandle src,
                                                           std::uint64_t size,
                                                           std::uint64_t offset,
                                                           ReadbackSink sink)
        {
            (void)src;
            (void)size;
            (void)offset;
            (void)sink;
            return {};
        }

        /// Returns true when the GPU has finished the readback associated with
        /// `token`. Safe to call from any thread.
        [[nodiscard]] virtual bool IsComplete(ReadbackToken token) const
        {
            return !token.IsValid();
        }

        [[nodiscard]] virtual TransferQueueDiagnostics GetDiagnostics() const noexcept
        {
            return {};
        }
    };
}
