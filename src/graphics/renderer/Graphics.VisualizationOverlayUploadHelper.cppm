module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

export module Extrinsic.Graphics.VisualizationOverlayUploadHelper;

import Extrinsic.Graphics.VisualizationPackets;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;

// GRAPHICS-078 Slices B + C — per-frame host-visible upload helper for
// the visualization overlay pass. The helper packs sanitized
// `VectorFieldOverlayPacket` (Slice B) and `IsolineOverlayPacket`
// (Slice C) spans into per-lane host-visible vertex buffers that
// survive across frames (geometric growth on demand) and reports per-
// lane upload results so the executor's
// `RecordVisualizationOverlayPass(...)` helper can record deterministic
// `BindPipeline + PushConstants + Draw(N, 1, 0, 0)` shapes.
//
// Mirrors `Extrinsic.Graphics.TransientDebugUploadHelper` exactly,
// substituting visualization lanes for transient-debug lanes:
//   - Slice B wires the vector-field lane (one glyph = one line
//     segment = two packed vertices).
//   - Slice C wires the isoline lane (each iso value contributes a
//     `LineList` deterministic placeholder segment of two packed vertices
//     until a source-buffer expansion path lands).
// All lanes share the `position(vec3) + packed RGBA8 color(uint32)`
// 16-byte packed-vertex layout consumed by the matching
// `assets/shaders/visualization_*.{vert,frag}` shader pairs.
//
// Backend policy: per `GRAPHICS-014Q` and the task non-goals, the
// helper must not retain GPU resources on `GpuWorld`, must not expose
// itself through RHI or renderer module surfaces (only the
// `IRenderer`-internal `Pass.VisualizationOverlay` consumes the upload
// result), and must not route through the retained line/point cull
// buckets. The interface is declared in the renderer module so the
// abstract type is reachable from CPU contract tests; the default
// concrete implementation here is CPU-functional through
// `BufferManager` + `IDevice::WriteBuffer(...)`. GRAPHICS-078E adds
// deterministic fixture positions for pixel-readback parity; source-BDA
// expansion into actual per-glyph / per-isoline world-space endpoints remains
// future method-specific work.

export namespace Extrinsic::Graphics
{
    // GRAPHICS-078 — deterministic CPU diagnostics for the
    // `VisualizationOverlayPass` upload + recording path. All counters
    // stay at zero in Slice A. Slice B populates the vector-field
    // counters; Slice C populates the isoline counters.
    // `MissingPipelineSkipCount` increments when the executor reaches
    // the pass branch with the device operational but at least one
    // required pipeline lease is missing (so the pass returns
    // `SkippedUnavailable` for that lane); useful for distinguishing
    // "feature off" (counter stays zero, pass not in stats) from
    // "feature on but pipeline missing" (counter increments).
    // `UploadOverflowCount` reports transient-buffer-allocator capacity
    // exhaustion from the upload helper.
    //
    // Reset per-frame through the renderer's existing
    // `m_LastRenderGraphStats = {}` cadence in `ExecuteFrame()`.
    struct VisualizationOverlayUploadDiagnostics
    {
        std::uint64_t UploadOverflowCount = 0;
        std::uint64_t VectorFieldRecordsSubmitted = 0;
        std::uint64_t IsolineRecordsSubmitted = 0;
        std::uint64_t VectorFieldRecordsRecorded = 0;
        std::uint64_t IsolineRecordsRecorded = 0;
        std::uint64_t MissingPipelineSkipCount = 0;
    };

    // GRAPHICS-078 Slice B — vector-field lane upload result. Mirrors
    // the transient-debug lane upload result: vertex buffer handle +
    // BDA + per-frame `Uploaded` flag. `VertexCount = 2 * sum of
    // ElementCount` across all packets (one glyph = one line segment
    // = two vertices).
    struct VisualizationVectorFieldUploadResult
    {
        RHI::BufferHandle VertexBuffer{};
        std::uint64_t     VertexBufferBDA{0u};
        std::uint32_t     VertexCount{0u};
        std::uint32_t     PacketCount{0u};
        bool              Uploaded{false};
        bool              Overflow{false};
    };

    // GRAPHICS-078 Slice C — isoline lane upload result. Mirrors the
    // vector-field upload result shape. On the CPU/null contract path,
    // each iso value contributes a single placeholder line segment
    // (two packed vertices) so the pass can issue
    // `Draw(2 * IsoValueCount, 1, 0, 0)` per packet via `LineList`
    // topology. GRAPHICS-078E makes those placeholder segments deterministic
    // so opt-in Vulkan pixel-readback can sample the isoline lane; actual
    // scalar-field-derived polyline expansion remains future work.
    struct VisualizationIsolineUploadResult
    {
        RHI::BufferHandle VertexBuffer{};
        std::uint64_t     VertexBufferBDA{0u};
        std::uint32_t     VertexCount{0u};
        std::uint32_t     PacketCount{0u};
        bool              Uploaded{false};
        bool              Overflow{false};
    };

    class IVisualizationOverlayUploadHelper
    {
    public:
        virtual ~IVisualizationOverlayUploadHelper() = default;

        IVisualizationOverlayUploadHelper(const IVisualizationOverlayUploadHelper&)            = delete;
        IVisualizationOverlayUploadHelper& operator=(const IVisualizationOverlayUploadHelper&) = delete;

        virtual void BeginFrame() = 0;

        [[nodiscard]] virtual VisualizationVectorFieldUploadResult UploadVectorFields(
            std::span<const VectorFieldOverlayPacket> vectorFields) = 0;

        // GRAPHICS-078 Slice C — isoline-lane upload. Same per-lane
        // buffer-lease + geometric-growth shape as the vector-field
        // lane. Returns `Uploaded = false` when the lane has no
        // packets, the device is non-operational, or no manager is
        // attached; `Overflow = true` when the requested vertex count
        // exceeds the per-lane cap or buffer creation fails.
        [[nodiscard]] virtual VisualizationIsolineUploadResult UploadIsolines(
            std::span<const IsolineOverlayPacket> isolines) = 0;

        [[nodiscard]] virtual std::uint64_t GetBufferAllocationCount() const noexcept = 0;

    protected:
        IVisualizationOverlayUploadHelper() = default;
    };

    // Default in-renderer implementation. Pairs `RHI::BufferManager` with
    // the device's `WriteBuffer(...)` path: per frame the helper resets
    // its bookkeeping, the renderer calls `UploadVectorFields(...)`
    // once per draw stream, the helper ensures the per-lane host-
    // visible vertex buffer has capacity for the requested vertex count
    // (geometric growth ×2 up to the per-lane cap), copies the packed
    // `position(vec3) + packed RGBA8 color(uint32)` vertices through
    // `device.WriteBuffer(...)`, and returns the per-lane vertex
    // buffer handle + BDA the pass uses for `BindPipeline +
    // PushConstants(BDA + FirstVertex) + Draw(N, 1, 0, 0)` per packet
    // (N = 2 * ElementCount for vector-field glyphs).
    //
    // CPU/null contract note: the helper does not have CPU access to
    // the source `PositionBufferBDA` / `VectorBufferBDA` payloads
    // (those are GPU pointers), so it writes deterministic placeholder
    // positions and the packet's packed color into each packed vertex.
    // GRAPHICS-078E validates those placeholders with opt-in Vulkan
    // pixel-readback; actual source-BDA expansion remains future work.
    //
    // Buffer recycling: a single growing buffer is reused across
    // frames per lane. `GetBufferAllocationCount()` returns the
    // cumulative number of underlying `BufferManager::Create(...)`
    // calls across all lanes. The
    // `PerFrameBufferRecyclingDoesNotLeakVectorField` contract test
    // pins this to the post-frame-1 baseline across N frames with
    // constant payload (no per-frame leak).
    // Lifetime contract: constructed from `RHI::IDevice& + RHI::BufferManager&`;
    // the device and manager pointers are non-null for the helper's lifetime
    // (the renderer owns both and resets the helper before the manager in
    // `Shutdown()`). The `Upload*` methods therefore only guard the device's
    // operational state and the empty-input case, not the member pointers.
    class VisualizationOverlayUploadHelper final : public IVisualizationOverlayUploadHelper
    {
    public:
        VisualizationOverlayUploadHelper(RHI::IDevice& device, RHI::BufferManager& bufferManager);
        ~VisualizationOverlayUploadHelper() override;

        void BeginFrame() override;

        [[nodiscard]] VisualizationVectorFieldUploadResult UploadVectorFields(
            std::span<const VectorFieldOverlayPacket> vectorFields) override;

        [[nodiscard]] VisualizationIsolineUploadResult UploadIsolines(
            std::span<const IsolineOverlayPacket> isolines) override;

        [[nodiscard]] std::uint64_t GetBufferAllocationCount() const noexcept override
        {
            return m_BufferAllocationCount;
        }

    private:
        RHI::IDevice*       m_Device{nullptr};
        RHI::BufferManager* m_BufferManager{nullptr};

        std::optional<RHI::BufferManager::BufferLease> m_VectorFieldVertexBuffer{};
        std::uint64_t  m_VectorFieldVertexBufferCapacityBytes{0u};

        // GRAPHICS-078 Slice C — independent per-lane buffer lease for
        // the isoline lane. Grows independently of the vector-field
        // lane and is reset before the `BufferManager` in the
        // renderer's `Shutdown()` (via `m_VisualizationOverlayUploadHelper.reset()`
        // before `m_BufferManager.reset()`).
        std::optional<RHI::BufferManager::BufferLease> m_IsolineVertexBuffer{};
        std::uint64_t  m_IsolineVertexBufferCapacityBytes{0u};

        std::uint64_t  m_BufferAllocationCount{0u};
    };
}
