module;

#include <atomic>
#include <cstdint>
#include <vector>

export module Extrinsic.Runtime.RenderWorldPool;

export namespace Extrinsic::Runtime
{
    // Counters surfaced by the pool for editor overlays / tests. All three are
    // cumulative event counters that only ever grow, so assertions over a known
    // schedule are stable. `GRAPHICS-036` decision 7 places these on the runtime
    // extraction diagnostics surface; wiring them through `RenderDiagnostics` is
    // owned by `GRAPHICS-036-Impl-B`, which reads them from here.
    struct RenderWorldPoolDiagnostics
    {
        // Consumer reused the previous front because no new snapshot was
        // published since the last `AcquireFront` (consumer faster than
        // producer).
        std::uint64_t PipelineStallCount = 0u;
        // Producer overwrote a still-unpublished back slot because no free slot
        // was available at `AcquireBack` (producer faster than consumer).
        std::uint64_t ExtractionSkipCount = 0u;
        // Age, in frames, of the snapshot returned by the most recent
        // `AcquireFront` (published-frame delta). `0` in synchronous mode or when
        // the consumer reads the freshly published front.
        std::uint64_t LastConsumedFrameAge = 0u;
    };

    // Runtime-owned multi-buffer slot-lifecycle pool (`GRAPHICS-036-Impl-A`).
    //
    // Implements the pipelined-frames slot state machine: the producer
    // (extraction) acquires a free *back* slot, writes the next snapshot, and
    // publishes it as the *front*; the consumer (renderer) acquires the current
    // front under a refcount across its in-flight GPU window and releases it at
    // frame retire. The published front index is a single
    // `std::atomic<std::uint32_t>` (release on publish / acquire on consume) and
    // per-slot refcounts are atomic, so the only cross-thread shared mutation is
    // the index plus the refcounts and no torn snapshot is ever visible — slot
    // contents are fully written before the index publish.
    //
    // This slice owns the *lifecycle of slot indices* only; binding the real
    // `RuntimeRenderSnapshotBatch` backing storage to each slot is owned by
    // `GRAPHICS-036-Impl-C`. The deterministic index-based seam matches
    // `GRAPHICS-036` decision 9.
    //
    // Buffer-count policy (decision 1): default 3 (triple-buffer with
    // reclamation), configurable down to 1. A single buffer collapses to
    // synchronous behavior — publish and consume share the one logical slot.
    //
    // Reclamation (decision 4): a released slot returns to the free list only
    // once its refcount reaches zero *and* it is no longer the published front.
    // Pending reclamations are drained at the start of each `AcquireBack`.
    //
    // Back-pressure (decision 5): producer-faster-than-consumer replaces the
    // still-unpublished back slot (`ExtractionSkipCount`); consumer-faster-than-
    // producer reuses the previously consumed front (`PipelineStallCount`).
    //
    // Layering: imports nothing from graphics/ECS/platform; manages only
    // indices and atomics. Single-threaded-safe and (for the index/refcount
    // mutations) thread-safe for the documented one-producer/one-consumer model.
    class RenderWorldPool
    {
    public:
        static constexpr std::uint32_t kMinBuffers   = 1u;
        static constexpr std::uint32_t kMaxBuffers   = 4u;
        static constexpr std::uint32_t kDefaultBuffers = 3u;
        static constexpr std::uint32_t kInvalidSlot  = 0xFFFF'FFFFu;

        explicit RenderWorldPool(std::uint32_t bufferCount = kDefaultBuffers);

        [[nodiscard]] std::uint32_t BufferCount() const noexcept
        {
            return static_cast<std::uint32_t>(m_Slots.size());
        }
        [[nodiscard]] bool IsSynchronous() const noexcept { return m_Slots.size() == 1u; }

        // --- producer (extraction) ----------------------------------------
        // Acquire a free slot to write the next snapshot into. Drains pending
        // reclamations first. If no slot is free and an unpublished back slot
        // exists, that back slot is returned for overwrite and
        // `ExtractionSkipCount` is incremented. `frameIndex` stamps the slot for
        // the consumer's frame-age computation. Always returns a valid slot for
        // the supported one-producer/one-consumer configurations.
        [[nodiscard]] std::uint32_t AcquireBack(std::uint64_t frameIndex) noexcept;

        // Publish a previously acquired back slot as the new front (release).
        void PublishFront(std::uint32_t slot) noexcept;

        // --- consumer (renderer) ------------------------------------------
        // Acquire the current front (acquire), incrementing its refcount. If no
        // new front has been published since the last `AcquireFront`, the
        // previously consumed front is reused and `PipelineStallCount` is
        // incremented. Returns `kInvalidSlot` only before any `PublishFront`.
        [[nodiscard]] std::uint32_t AcquireFront(std::uint64_t frameIndex) noexcept;

        // Release a previously acquired front slot (decrement refcount). When the
        // refcount reaches zero and the slot is no longer the published front it
        // becomes reclaimable at the next `AcquireBack`.
        void ReleaseFront(std::uint32_t slot) noexcept;

        // --- introspection / test seam ------------------------------------
        [[nodiscard]] std::uint32_t FrontSlot() const noexcept
        {
            return m_Front.load(std::memory_order_acquire);
        }
        [[nodiscard]] std::uint32_t RefCount(std::uint32_t slot) const noexcept;
        [[nodiscard]] std::uint32_t FreeSlotCount() const noexcept;
        [[nodiscard]] const RenderWorldPoolDiagnostics& GetDiagnostics() const noexcept
        {
            return m_Diagnostics;
        }

    private:
        struct Slot
        {
            std::atomic<std::uint32_t> RefCount{0u};
            std::uint64_t              PublishedFrame{0u};
            bool                       PendingReclaim{false};
        };

        // Move a slot back to availability once it is safe to reuse.
        void DrainReclamations() noexcept;
        [[nodiscard]] std::uint32_t FindFreeSlot() const noexcept;

        std::vector<Slot> m_Slots;

        std::atomic<std::uint32_t> m_Front{kInvalidSlot};   // published front index
        std::atomic<std::uint64_t> m_PublishSeq{0u};        // monotonic publish count
        std::uint32_t              m_Back{kInvalidSlot};    // acquired-not-published back
        std::uint64_t              m_LastConsumedSeq{0u};   // publish seq seen at last consume

        RenderWorldPoolDiagnostics m_Diagnostics{};
    };
}
