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

// GRAPHICS-078 Slice B — per-frame host-visible upload helper for the
// visualization overlay pass. The helper packs sanitized
// `VectorFieldOverlayPacket` (Slice B) and `IsolineOverlayPacket`
// (Slice C, deferred) spans into per-lane host-visible vertex buffers
// that survive across frames (geometric growth on demand) and reports
// per-lane upload results so the executor's
// `RecordVisualizationOverlayPass(...)` helper can record deterministic
// `BindPipeline + PushConstants + Draw(N, 1, 0, 0)` shapes.
//
// Mirrors `Extrinsic.Graphics.TransientDebugUploadHelper` exactly,
// substituting visualization lanes for transient-debug lanes:
//   - Slice B wires the vector-field lane (one glyph = one line
//     segment = two packed vertices).
//   - Slice C extends the interface to the isoline lane.
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
// `BufferManager` + `IDevice::WriteBuffer(...)`. The Vulkan-tuned
// concrete implementation (which expands the source position/vector
// BDAs into actual per-glyph world-space endpoints) lands with the
// optional Slice D `gpu;vulkan` smoke.

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

    class IVisualizationOverlayUploadHelper
    {
    public:
        virtual ~IVisualizationOverlayUploadHelper() = default;

        IVisualizationOverlayUploadHelper(const IVisualizationOverlayUploadHelper&)            = delete;
        IVisualizationOverlayUploadHelper& operator=(const IVisualizationOverlayUploadHelper&) = delete;

        virtual void BeginFrame() = 0;

        [[nodiscard]] virtual VisualizationVectorFieldUploadResult UploadVectorFields(
            std::span<const VectorFieldOverlayPacket> vectorFields) = 0;

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
    // (those are GPU pointers), so the CPU/null path writes zeroed
    // positions and the packet's packed color into each packed vertex.
    // The bind/push/draw shape is the only behavior validated by the
    // contract tests; per-pixel correctness on a real Vulkan device is
    // owned by the optional Slice D `gpu;vulkan` smoke and the future
    // Vulkan-tuned helper variant that expands actual per-glyph
    // endpoints from the source BDAs.
    //
    // Buffer recycling: a single growing buffer is reused across
    // frames per lane. `GetBufferAllocationCount()` returns the
    // cumulative number of underlying `BufferManager::Create(...)`
    // calls across all lanes. The
    // `PerFrameBufferRecyclingDoesNotLeakVectorField` contract test
    // pins this to the post-frame-1 baseline across N frames with
    // constant payload (no per-frame leak).
    class VisualizationOverlayUploadHelper final : public IVisualizationOverlayUploadHelper
    {
    public:
        VisualizationOverlayUploadHelper(RHI::IDevice& device, RHI::BufferManager& bufferManager);
        ~VisualizationOverlayUploadHelper() override;

        void BeginFrame() override;

        [[nodiscard]] VisualizationVectorFieldUploadResult UploadVectorFields(
            std::span<const VectorFieldOverlayPacket> vectorFields) override;

        [[nodiscard]] std::uint64_t GetBufferAllocationCount() const noexcept override
        {
            return m_BufferAllocationCount;
        }

    private:
        RHI::IDevice*       m_Device{nullptr};
        RHI::BufferManager* m_BufferManager{nullptr};

        std::optional<RHI::BufferManager::BufferLease> m_VectorFieldVertexBuffer{};
        std::uint64_t  m_VectorFieldVertexBufferCapacityBytes{0u};

        std::uint64_t  m_BufferAllocationCount{0u};
    };
}
