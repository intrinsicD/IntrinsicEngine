module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

export module Extrinsic.Graphics.TransientDebugUploadHelper;

import Extrinsic.Graphics.RenderWorld;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;

// GRAPHICS-077 Slices B + C — per-frame host-visible upload helper for
// the transient debug surface pass. The helper packs sanitized
// `DebugTrianglePacket` / `DebugLinePacket` / `DebugPointPacket` spans
// into a small set of host-visible vertex buffers that survive across
// frames (geometric growth on demand) and reports per-lane upload
// results so the executor's `RecordTransientDebugSurfacePass(...)`
// helper can record deterministic `BindPipeline + PushConstants +
// Draw(...)` shapes.
//
// Slice B wired the triangle lane; Slice C extends the interface to
// the line + point lanes with the same shape (per-lane buffer lease,
// per-lane growth, per-lane upload result). All three lanes share the
// `position(vec3) + packed RGBA8 color(uint32)` 16-byte packed-vertex
// layout consumed by `assets/shaders/transient_debug_*.{vert,frag}`.
//
// Lifetime contract: the helper is owned by the renderer. It holds a
// `RHI::BufferManager::BufferLease` per lane; leases reset before the
// `BufferManager` is destroyed in `Shutdown()`.
//
// Backend policy: per `GRAPHICS-077` and the task non-goals, the helper
// must not retain GPU resources on `GpuWorld`, must not expose itself
// through RHI or renderer module surfaces (only the
// `IRenderer`-internal `Pass.TransientDebug.Surface` consumes the
// upload result), and must not route through the retained
// `GpuRender_Line` / `GpuRender_Point` cull buckets. The helper is
// declared here in the renderer module so the abstract interface is
// reachable from CPU contract tests; the Vulkan-tuned concrete
// implementation lands with Slice D (see the task file).

export namespace Extrinsic::Graphics
{
    // GRAPHICS-077 — deterministic CPU diagnostics for the
    // `TransientDebugSurfacePass` upload + recording path. All counters
    // stay at zero in Slice A (no pipelines, scaffold executor branch
    // only). Slice B populates the triangle counters; Slice C populates
    // the line + point counters. `MissingPipelineSkipCount` increments
    // when the executor reaches the pass branch with the device
    // operational but at least one required pipeline lease is missing
    // (so the pass returns `SkippedUnavailable`); useful for
    // distinguishing "feature off" (counter stays zero, pass not in
    // stats) from "feature on but pipeline missing" (counter increments).
    // `UploadOverflowCount` reports transient-buffer-allocator capacity
    // exhaustion from the upload helper.
    //
    // Reset per-frame through the renderer's existing
    // `m_LastRenderGraphStats = {}` cadence in `ExecuteFrame()`.
    struct TransientDebugUploadDiagnostics
    {
        std::uint64_t UploadOverflowCount = 0;
        std::uint64_t LineRecordsSubmitted = 0;
        std::uint64_t PointRecordsSubmitted = 0;
        std::uint64_t TriangleRecordsSubmitted = 0;
        std::uint64_t LineRecordsRecorded = 0;
        std::uint64_t PointRecordsRecorded = 0;
        std::uint64_t TriangleRecordsRecorded = 0;
        std::uint64_t MissingPipelineSkipCount = 0;
    };

    struct TransientDebugTriangleUploadResult
    {
        RHI::BufferHandle VertexBuffer{};
        std::uint64_t     VertexBufferBDA{0u};
        std::uint32_t     VertexCount{0u};
        std::uint32_t     PacketCount{0u};
        bool              Uploaded{false};
        bool              Overflow{false};
    };

    // GRAPHICS-077 Slice C — line-lane upload result. Mirrors the
    // triangle result: BDA + vertex count + per-frame `Uploaded` flag.
    // `VertexCount = 2 * PacketCount` (one segment = two vertices).
    struct TransientDebugLineUploadResult
    {
        RHI::BufferHandle VertexBuffer{};
        std::uint64_t     VertexBufferBDA{0u};
        std::uint32_t     VertexCount{0u};
        std::uint32_t     PacketCount{0u};
        bool              Uploaded{false};
        bool              Overflow{false};
    };

    // GRAPHICS-077 Slice C — point-lane upload result. Mirrors the
    // triangle result. `VertexCount = PacketCount` (one point = one
    // vertex).
    struct TransientDebugPointUploadResult
    {
        RHI::BufferHandle VertexBuffer{};
        std::uint64_t     VertexBufferBDA{0u};
        std::uint32_t     VertexCount{0u};
        std::uint32_t     PacketCount{0u};
        bool              Uploaded{false};
        bool              Overflow{false};
    };

    class ITransientDebugUploadHelper
    {
    public:
        virtual ~ITransientDebugUploadHelper() = default;

        ITransientDebugUploadHelper(const ITransientDebugUploadHelper&)            = delete;
        ITransientDebugUploadHelper& operator=(const ITransientDebugUploadHelper&) = delete;

        virtual void BeginFrame(std::uint32_t frameIndex,
                                std::uint32_t framesInFlight) = 0;

        [[nodiscard]] virtual TransientDebugTriangleUploadResult UploadTriangles(
            std::span<const DebugTrianglePacket> triangles) = 0;

        // GRAPHICS-077 Slice C — line + point lane uploads. Same per-
        // lane buffer-lease + geometric-growth shape as the triangle
        // lane. Returns `Uploaded = false` when the lane has no
        // packets, the device is non-operational, or no manager is
        // attached; `Overflow = true` when the requested vertex count
        // exceeds the per-lane cap or buffer creation fails.
        [[nodiscard]] virtual TransientDebugLineUploadResult UploadLines(
            std::span<const DebugLinePacket> lines) = 0;

        [[nodiscard]] virtual TransientDebugPointUploadResult UploadPoints(
            std::span<const DebugPointPacket> points) = 0;

        [[nodiscard]] virtual std::uint64_t GetBufferAllocationCount() const noexcept = 0;

    protected:
        ITransientDebugUploadHelper() = default;
    };

    // Default in-renderer implementation. Pairs `RHI::BufferManager` with
    // the device's `WriteBuffer(...)` path: per frame the helper resets
    // its bookkeeping, the renderer calls
    // `UploadTriangles(...)`/`UploadLines(...)`/`UploadPoints(...)` once
    // per draw stream, the helper ensures the per-lane host-visible
    // vertex buffer has capacity for the requested vertex count
    // (geometric growth ×2 up to the per-lane cap), copies the packed
    // `position(vec3) + packed RGBA8 color(uint32)` vertices through
    // `device.WriteBuffer(...)`, and returns the per-lane vertex
    // buffer handle + BDA the pass uses for `BindPipeline +
    // PushConstants(BDA + FirstVertex) + Draw(N, 1, 0, 0)` per packet
    // (N = 3 for triangles, 2 for lines, 1 for points).
    //
    // Buffer recycling: one growing buffer is reused per lane and per
    // frame-in-flight slot. `GetBufferAllocationCount()` returns the
    // cumulative number of underlying `BufferManager::Create(...)` calls across
    // all lanes/slots. The `PerFrameBufferRecycling` contract test pins this
    // after slot warm-up across N frames with constant payload.
    // Lifetime contract: constructed from `RHI::IDevice& + RHI::BufferManager&`;
    // the device and manager pointers are non-null for the helper's lifetime
    // (the renderer owns both and resets the helper before the manager in
    // `Shutdown()`). The `Upload*` methods therefore only guard the device's
    // operational state and the empty-input case, not the member pointers.
    class TransientDebugUploadHelper final : public ITransientDebugUploadHelper
    {
    public:
        TransientDebugUploadHelper(RHI::IDevice& device, RHI::BufferManager& bufferManager);
        ~TransientDebugUploadHelper() override;

        void BeginFrame(std::uint32_t frameIndex,
                        std::uint32_t framesInFlight) override;

        [[nodiscard]] TransientDebugTriangleUploadResult UploadTriangles(
            std::span<const DebugTrianglePacket> triangles) override;

        [[nodiscard]] TransientDebugLineUploadResult UploadLines(
            std::span<const DebugLinePacket> lines) override;

        [[nodiscard]] TransientDebugPointUploadResult UploadPoints(
            std::span<const DebugPointPacket> points) override;

        [[nodiscard]] std::uint64_t GetBufferAllocationCount() const noexcept override
        {
            return m_BufferAllocationCount;
        }

    private:
        RHI::IDevice*       m_Device{nullptr};
        RHI::BufferManager* m_BufferManager{nullptr};

        struct UploadBufferSlot
        {
            std::optional<RHI::BufferManager::BufferLease> Buffer{};
            std::uint64_t CapacityBytes{0u};
        };

        void EnsureFrameSlots(std::uint32_t framesInFlight);

        std::vector<UploadBufferSlot> m_TriangleVertexBufferSlots{};

        // GRAPHICS-077 Slice C — independent per-lane buffer leases
        // for the line + point lanes. Each lane grows independently
        // and is reset before the `BufferManager` in the renderer's
        // `Shutdown()` (via `m_TransientDebugUploadHelper.reset()`
        // before `m_BufferManager.reset()`).
        std::vector<UploadBufferSlot> m_LineVertexBufferSlots{};

        std::vector<UploadBufferSlot> m_PointVertexBufferSlots{};

        std::uint32_t  m_ActiveSlot{0u};
        std::uint64_t  m_BufferAllocationCount{0u};
    };
}
