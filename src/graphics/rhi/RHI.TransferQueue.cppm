module;

#include <compare>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <span>
#include <utility>

export module Extrinsic.RHI.TransferQueue;

import Extrinsic.RHI.Descriptors;
import Extrinsic.RHI.Handles;
export import Extrinsic.RHI.QueueAffinity;
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
//   DownloadBuffer / DownloadTexture — safe to call from any thread. Backends
//     copy the requested device range/subresource into backend-owned staging
//     and deliver bytes later through a ReadbackSink from CollectCompleted().
//   IsComplete — safe from any thread (atomic read).
//   CollectCompleted — render thread only, once per frame, after
//     EndFrame().  Retires staging and delivers sinks; it never waits.
//   WaitCompute / TakeCompletedReadback / ReleaseReadback — any thread (ARCH-023):
//     a method worker waits, bounded, for its own submission and consumes (or
//     gives up) its pulled readback (ReadbackSink::Pull) without the render
//     thread or a frame. Ordinary sinks run only from CollectCompleted.
//
// Invariant (matches src/ async-upload guarantee):
//   No caller thread ever blocks on a GPU fence inside
//   UploadBuffer / UploadTexture / UploadTextureFullChain / DownloadBuffer /
//   DownloadTexture. The only wait is WaitCompute's, bounded by its timeout and
//   on the caller's own token.
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
        // ARCH-023: the submitter pulls the bytes itself (ITransferQueue::TakeCompletedReadback, any thread) or gives
        // them up (ReleaseReadback). CollectCompleted never delivers, copies or drops a pulled readback.
        bool Pulled{false};

        [[nodiscard]] static ReadbackSink CopyTo(std::span<std::byte> destination)
        {
            return ReadbackSink{.Destination = destination};
        }

        [[nodiscard]] static ReadbackSink Invoke(ReadbackCallback callback)
        {
            return ReadbackSink{.Callback = std::move(callback)};
        }

        [[nodiscard]] static ReadbackSink Pull()
        {
            return ReadbackSink{.Pulled = true};
        }

        [[nodiscard]] bool IsValidForSize(std::uint64_t sizeBytes) const noexcept
        {
            return Pulled || Callback || Destination.size_bytes() == static_cast<std::size_t>(sizeBytes);
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
        // Readback staging slots placed in host memory without HOST_CACHED although the device offers a cached
        // coherent host-visible type (VMA may still fall back when that allocation fails): CPU reads of such
        // slots run at uncached speed.
        std::uint64_t ReadbackSlotsUncached = 0;
        // Downloads whose copy into their staging slot was followed by a transfer-write to host-read barrier.
        std::uint64_t ReadbackHostBarriers = 0;
        // ARCH-023: where IDevice::SubmitComputeReadback runs. Requested is async compute unless configuration
        // forces graphics; Demoted when no async compute queue exists. Resolved is the actual lane.
        QueueAffinityResolution ComputeLane{};
        // True when hardware/configuration forces method work to share the framegraph's compute queue.
        bool ComputeSharesFrameComputeQueue = false;
        // True when method compute submits under the host lock of a frame queue (graphics, present, frame compute or
        // transfer): a blocked present or frame submission then also delays a method worker's submission.
        bool ComputeSharesFrameQueueLock = false;
        // Accepted submissions on a separate compute lane, and the cross-queue handoffs they carried: waits on
        // the last graphics-lane transfer ticket issued before them (uploads, downloads, device one-shot uploads, and
        // through queue order earlier graphics batches; ComputeInputOrder::AfterIssuedTransfers submissions only),
        // compute-lane readbacks with their
        // host-read barrier, and graphics-queue submissions that acquired completed (published) compute results.
        std::uint64_t ComputeSubmissions = 0;
        std::uint64_t ComputeGraphicsWaits = 0;
        std::uint64_t ComputeHostBarriers = 0;
        std::uint64_t ComputeVisibilityWaits = 0;
    };

    // WaitCompute's answer: the token's work finished; still running when the timeout passed; or it never will
    // (lost device, a backend that cannot wait, an invalid service).
    enum class ComputeWaitStatus : std::uint8_t
    {
        Complete,
        Pending,
        Failed
    };

    // ARCH-023: what an IDevice::SubmitComputeReadback waits for before it runs. Either way it follows the earlier
    // submissions of its own lane (queue order; the recorder's barriers make their writes visible). Neither order
    // names a frame, but neither promises to skip one: see AfterIssuedTransfers and the shared-queue note below.
    enum class ComputeInputOrder : std::uint8_t
    {
        // Also every transfer-queue upload/download and device one-shot upload issued before the call: a wait on
        // the graphics lane's last issued ticket (counted in ComputeGraphicsWaits). The default; owner submissions
        // and every first submission of a run use it. That ticket is signaled by a graphics-queue submission, so it
        // also covers every earlier graphics-queue batch, frames and their swapchain-image waits included: after a
        // renderer or preview upload the submission can wait for the frame submitted before that upload. Use it
        // only where an upload must be ordered (a run's first submission), not for every page.
        AfterIssuedTransfers,
        // Its own lane only, no graphics-lane wait: valid only when every input the submission reads was written by
        // the host before the call, by earlier work on the compute lane, or on the graphics lane before an accepted
        // AfterIssuedTransfers submission of this lane that preceded it (a worker's pages after its run's prepared
        // first submission). A graphics-queue upload issued since (a preview, another run) is then not waited for.
        // Where method compute shares a queue with frames (the graphics lane, or the framegraph's compute queue,
        // TransferQueueDiagnostics::ComputeSharesFrameComputeQueue), queue order always applies: both orders then
        // follow the frame work submitted earlier to that queue.
        LaneOrderOnly,
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
        /// The backend orders prior device reads/writes of the destination
        /// range before this transfer write. The caller still owns the
        /// transfer-write-to-consumer barrier after the returned token.
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
        /// backend-owned host-visible staging. The producer submission must
        /// already be enqueued before this call. The backend owns the source
        /// write -> transfer-read memory barrier and must serialize it after the
        /// producer (or fail closed). The requested bytes are delivered to
        /// `sink` from CollectCompleted() after the transfer timeline reports
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

        /// Non-blocking: queues a GPU transfer from one color texture
        /// subresource into backend-owned host-visible staging. The source
        /// texture must already be in `srcLayout` and this method does not
        /// transition it; callers own before/after layout barriers. Accepted
        /// subresources are delivered to `sink` from CollectCompleted() after
        /// the transfer timeline reports completion.
        [[nodiscard]] virtual ReadbackToken DownloadTexture(TextureHandle src,
                                                            TextureLayout srcLayout,
                                                            std::uint32_t mipLevel,
                                                            std::uint32_t arrayLayer,
                                                            ReadbackSink sink)
        {
            (void)src;
            (void)srcLayout;
            (void)mipLevel;
            (void)arrayLayer;
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

        /// ARCH-023: a token that IsComplete once every compute submission accepted so far finished (on a
        /// separate compute lane, whose work a frame's retirement does not cover). Invalid when there is none,
        /// which IsComplete treats as complete. Owners that reuse host-visible or pooled buffers by frame count
        /// stamp it as well.
        [[nodiscard]] virtual ReadbackToken ComputeFence() const noexcept
        {
            return {};
        }

        /// ARCH-023, any thread: waits at most `timeout` for the device work of `token` (a token of
        /// IDevice::SubmitComputeReadback) on its own lane's timeline, without CollectCompleted or a frame. An
        /// invalid token is Complete. The default (no backend wait) answers from IsComplete and never blocks.
        [[nodiscard]] virtual ComputeWaitStatus WaitCompute(ReadbackToken token, std::uint64_t timeoutNanoseconds)
        {
            (void)timeoutNanoseconds;
            return IsComplete(token) ? ComputeWaitStatus::Complete : ComputeWaitStatus::Failed;
        }

        /// ARCH-023, any thread: once the device finished a pulled readback (submitted with ReadbackSink::Pull),
        /// copies its staged bytes into `destination` (exactly its size, empty for zero bytes), releases its staging
        /// slot and its record, and retires the lane's completed command buffers; it counts as completed once.
        /// CollectCompleted never takes it first. False (nothing taken) for an unknown, already taken or released,
        /// not pulled or not yet complete token, or another size. IsComplete holds from the next CollectCompleted
        /// on, which also never reports a later token of the lane complete while an earlier one is untaken.
        [[nodiscard]] virtual bool TakeCompletedReadback(ReadbackToken token, std::span<std::byte> destination)
        {
            (void)token;
            (void)destination;
            return false;
        }

        /// ARCH-023, any thread: gives up a pulled readback that will not be taken (a cancelled worker step). No
        /// callback runs; its staging slot and record are released once the device finished it (here when it
        /// already has, otherwise by a later Take, Release or CollectCompleted). Ignored for other tokens.
        virtual void ReleaseReadback(ReadbackToken token)
        {
            (void)token;
        }
    };
}
