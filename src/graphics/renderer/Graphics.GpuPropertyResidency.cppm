// GPU copies of CPU properties for GPU methods (ADR 0030, GRAPHICS-154/155). Per ECS-blind key
// the residency holds a canonical slot (the property's own bytes, type and layout for one CPU
// revision: uploaded once when a GPU user first needs that revision, shared while it holds) and,
// while a method writes the property, a ring of output slots (a back slot being written and a
// front that the renderer observes). Buffers are device-local; a slot is rewritten only after
// every recorded completion (frames in flight, transfer and readback tokens) and never while
// it is leased or front. Canonical slots are a cache: idle slots and, over a byte budget, the
// least recently used ones are evicted and re-uploaded on their next use. Render buffers are
// never a source: data flows CPU -> residency -> render.
module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
export module Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.RHI.Handles;
import Extrinsic.RHI.Transfer;
import Extrinsic.RHI.TransferQueue;

extern "C++" { namespace Extrinsic::RHI { class IDevice; } }

export namespace Extrinsic::Graphics
{
    struct GpuPropertyKey
    {
        std::uint64_t Scope{};   // e.g. the world
        std::uint64_t Owner{};   // e.g. the stable entity id
        std::uint32_t Domain{};  // element domain
        std::uint32_t ValueKind{};
        std::string Name{};
        [[nodiscard]] bool operator==(const GpuPropertyKey&) const = default;
    };

    enum class GpuScalarType : std::uint8_t { Float32, Float64, Int32, UInt32, Int64, UInt64 };

    [[nodiscard]] constexpr std::uint32_t GpuScalarBytes(const GpuScalarType type) noexcept
    {
        switch (type)
        {
        case GpuScalarType::Float32: case GpuScalarType::Int32: case GpuScalarType::UInt32: return 4u;
        case GpuScalarType::Float64: case GpuScalarType::Int64: case GpuScalarType::UInt64: return 8u;
        }
        return 0u;
    }

    // The typed identity of a resident slot. A slot only serves requests with an equal layout;
    // a double property is stored as double (a float presentation view is derived elsewhere).
    struct GpuPropertyLayout
    {
        GpuScalarType Scalar{GpuScalarType::Float32};
        std::uint32_t Channels{1};  // 1..4
        std::uint32_t Stride{};     // bytes per element; 0 = tightly packed
        std::uint32_t Count{};      // elements
        std::uint64_t RowMap{};     // identity of the row map the slot follows; 0 = the CPU rows
        [[nodiscard]] constexpr std::uint32_t ElementBytes() const noexcept
        {
            return Stride != 0u ? Stride : GpuScalarBytes(Scalar) * Channels;
        }
        [[nodiscard]] constexpr std::uint64_t Bytes() const noexcept { return std::uint64_t(ElementBytes()) * Count; }
        [[nodiscard]] constexpr bool Valid() const noexcept
        {
            return Channels >= 1u && Channels <= 4u && Count > 0u && GpuScalarBytes(Scalar) != 0u &&
                   (Stride == 0u || Stride >= GpuScalarBytes(Scalar) * Channels);
        }
        [[nodiscard]] bool operator==(const GpuPropertyLayout&) const = default;
    };

    // A resident slot. The lease keeps the slot from being rewritten or released while held.
    // `Upload` is the transfer that filled a canonical slot; consumers record a
    // TransferWrite -> ShaderRead barrier before their first read.
    struct GpuPropertyView
    {
        RHI::BufferHandle Buffer{};
        std::uint64_t Address{};
        std::uint64_t Bytes{};
        std::uint64_t Revision{};
        // The residency-wide publication that made a ring slot the front (0 for a slot
        // uploaded from the CPU). Ring slots are reused without changing their buffer, so
        // this, not the buffer, identifies a front's bytes.
        std::uint64_t Publication{};
        GpuPropertyLayout Layout{};
        RHI::TransferToken Upload{};
        std::shared_ptr<const void> Lease{};
        [[nodiscard]] bool Valid() const noexcept { return Buffer.IsValid() && Layout.Count > 0u; }
    };

    struct GpuPropertyResidencyConfig
    {
        double IdleEvictSeconds{60.0};  // canonical slots unused this long are evicted; <= 0 disables
        std::uint64_t BudgetBytes{};    // resident bytes above this evict LRU canonical slots; 0 = no budget
        std::function<double()> Clock{}; // steady seconds; empty = std::chrono::steady_clock
    };

    struct GpuPropertyResidencyStats
    {
        std::uint64_t Uploads{}, UploadBytes{}, UploadRefusals{}, Readbacks{}, ReadbackBytes{};
        std::uint64_t Hits{}, Misses{}, Evictions{}, Releases{}, ResidentBytes{};
        std::uint64_t Publishes{}, RingWaits{}, DroppedPreviews{};
        std::uint32_t Slots{}, Rings{};
    };

    class GpuPropertyResidency
    {
    public:
        explicit GpuPropertyResidency(RHI::IDevice& device, GpuPropertyResidencyConfig config = {});
        ~GpuPropertyResidency();
        GpuPropertyResidency(const GpuPropertyResidency&) = delete;
        GpuPropertyResidency& operator=(const GpuPropertyResidency&) = delete;

        // ---- canonical input -----------------------------------------------------------
        // The canonical slot for `key` at `revision` in `layout` (a tightly packed stride and
        // stride 0 are one identity): a hit when resident (and a use for eviction), otherwise
        // `fill` writes layout.Bytes() into a staging span that the transfer queue uploads
        // once into a fresh device-local buffer (a slot of another revision or layout is
        // released once its completions and leases are gone). Empty, caching nothing, on a
        // non-operational device, an invalid layout, an allocation failure or a refused
        // upload (counted; the caller defers). Device-owner thread only.
        [[nodiscard]] std::optional<GpuPropertyView> AcquireInput(
            const GpuPropertyKey& key, std::uint64_t revision, const GpuPropertyLayout& layout,
            const std::function<void(std::span<std::byte>)>& fill);

        // ---- output ring ---------------------------------------------------------------
        // A write slot of the key's ring (created on first use with `depth` slots, 1..3, and
        // `layout`; a ring keeps its depth and layout until Discard/BindRevision). Empty when
        // the ring is exhausted: every slot is front, leased or has pending completions.
        // That is counted as a dropped preview; the ring never blocks and never overwrites.
        // A refused allocation leaves no ring behind when this call would have created it.
        [[nodiscard]] std::optional<GpuPropertyView> AcquireBack(
            const GpuPropertyKey& key, const GpuPropertyLayout& layout, std::uint32_t depth);
        // The last acquired back becomes the front (the previous front is reusable once its
        // completions are done). False without a back.
        bool Publish(const GpuPropertyKey& key);
        // Releases the ring; its buffers are freed once their completions and leases are gone.
        void Discard(const GpuPropertyKey& key);
        // The key's current ring identity (residency-wide, assigned when the ring is created;
        // 0 without a ring). A run that acquired a ring discards only that ring: a later ring
        // on the same key belongs to a later run.
        [[nodiscard]] std::uint64_t RingGeneration(const GpuPropertyKey& key) const;
        // Discard only when the key's ring is `generation`; false (nothing released) otherwise.
        bool Discard(const GpuPropertyKey& key, std::uint64_t generation);
        // The ring's front while one exists, otherwise the canonical slot. No side effects.
        [[nodiscard]] std::optional<GpuPropertyView> Front(const GpuPropertyKey& key) const;
        // After Accept: the ring's front becomes the canonical slot for `revision` (the ring
        // and the old canonical slot are released). False without a front, or when
        // `publication` is nonzero and the front is a later publication than the one Accept
        // read back (the caller then discards the ring: the CPU holds the accepted bytes).
        bool BindRevision(const GpuPropertyKey& key, std::uint64_t revision, std::uint64_t publication = 0u);
        [[nodiscard]] bool HasRing(const GpuPropertyKey& key) const;

        // ---- completions and use -------------------------------------------------------
        // Commands recorded in `frame` read or write the slot: it is reusable once
        // GetGlobalFrameNumber() - frame > GetFramesInFlight() (the frame's fence is waited
        // one BeginFrame after the counter reaches the distance).
        void NoteUse(RHI::BufferHandle slot, std::uint64_t frame);
        // Transfers into / readbacks from the slot: reusable once the token IsComplete. A
        // readback also counts `bytes` towards the readback counters.
        void AddCompletion(RHI::BufferHandle slot, RHI::TransferToken token);
        void AddCompletion(RHI::BufferHandle slot, RHI::ReadbackToken token, std::uint64_t bytes);
        // The renderer shows the key's observed slot (Front) this frame: a use for eviction
        // and a frame-in-flight completion on that slot.
        void MarkObserved(const GpuPropertyKey& key);

        // ---- maintenance ---------------------------------------------------------------
        // Once per frame: frees released slots whose completions are done, then evicts idle
        // canonical slots and, over budget, the least recently used first (size-weighted).
        // Slots with a ring, pending completions, a lease or observed this frame stay.
        void Tick();
        // Releases the slots and rings of keys `keep` rejects (e.g. entities that no longer
        // exist); freed once their completions and leases are gone.
        void Prune(const std::function<bool(const GpuPropertyKey&)>& keep);
        // Releases everything at once (device shutdown); outstanding views keep their
        // handles but the buffers are gone once the device retires them.
        void Clear();
        [[nodiscard]] GpuPropertyResidencyStats Stats() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
