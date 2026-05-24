module;

#include <cstdint>
#include <memory>
#include <optional>
#include <span>

export module Extrinsic.Graphics.TransientDebugUploadHelper;

import Extrinsic.Graphics.RenderWorld;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.Handles;

// GRAPHICS-077 Slice B — per-frame host-visible upload helper for the
// transient debug surface pass. The helper packs sanitized
// `DebugTrianglePacket` / `DebugLinePacket` / `DebugPointPacket` spans
// into a small set of host-visible vertex buffers that survive across
// frames (geometric growth on demand) and reports per-lane upload
// results so the executor's `RecordTransientDebugSurfacePass(...)`
// helper can record deterministic `BindPipeline + PushConstants +
// Draw(...)` shapes.
//
// Slice B wires the triangle lane only; line + point lanes are routed
// through the same interface but the default implementation's
// per-lane uploaders remain inert until Slice C.
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

    class ITransientDebugUploadHelper
    {
    public:
        virtual ~ITransientDebugUploadHelper() = default;

        ITransientDebugUploadHelper(const ITransientDebugUploadHelper&)            = delete;
        ITransientDebugUploadHelper& operator=(const ITransientDebugUploadHelper&) = delete;

        virtual void BeginFrame() = 0;

        [[nodiscard]] virtual TransientDebugTriangleUploadResult UploadTriangles(
            std::span<const DebugTrianglePacket> triangles) = 0;

        [[nodiscard]] virtual std::uint64_t GetBufferAllocationCount() const noexcept = 0;

    protected:
        ITransientDebugUploadHelper() = default;
    };

    // Default in-renderer implementation. Pairs `RHI::BufferManager` with
    // the device's `WriteBuffer(...)` path: per frame the helper resets
    // its bookkeeping, the renderer calls `UploadTriangles(...)` once
    // per draw stream, the helper ensures the host-visible vertex buffer
    // has capacity for the requested vertex count (geometric growth ×2
    // up to `kMaxTriangleVertexCount`), copies the packed
    // `position(vec3) + packed RGBA8 color(uint32)` vertices through
    // `device.WriteBuffer(...)`, and returns the per-lane vertex buffer
    // handle + BDA the pass uses for `BindPipeline + PushConstants(BDA)
    // + Draw(3, 1, 0, 0)` per packet.
    //
    // Buffer recycling: a single growing buffer is reused across frames.
    // `GetBufferAllocationCount()` returns the number of underlying
    // `BufferManager::Create(...)` calls the helper has issued. The
    // `PerFrameBufferRecycling` contract test pins this to <= 1 across
    // N frames with constant payload (no per-frame leak).
    class TransientDebugUploadHelper final : public ITransientDebugUploadHelper
    {
    public:
        TransientDebugUploadHelper(RHI::IDevice& device, RHI::BufferManager& bufferManager);
        ~TransientDebugUploadHelper() override;

        void BeginFrame() override;

        [[nodiscard]] TransientDebugTriangleUploadResult UploadTriangles(
            std::span<const DebugTrianglePacket> triangles) override;

        [[nodiscard]] std::uint64_t GetBufferAllocationCount() const noexcept override
        {
            return m_BufferAllocationCount;
        }

    private:
        RHI::IDevice*       m_Device{nullptr};
        RHI::BufferManager* m_BufferManager{nullptr};

        std::optional<RHI::BufferManager::BufferLease> m_TriangleVertexBuffer{};
        std::uint64_t  m_TriangleVertexBufferCapacityBytes{0u};
        std::uint64_t  m_BufferAllocationCount{0u};
    };
}
