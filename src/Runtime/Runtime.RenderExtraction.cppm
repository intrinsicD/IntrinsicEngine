module;
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>
#include <glm/glm.hpp>

export module Runtime.RenderExtraction;

import Core.InplaceFunction;
import Core.Memory;
import Graphics.Camera;
import Graphics.DebugDraw;
import Graphics.RenderPipeline;
import RHI.Profiler;
import Runtime.SceneManager;

export namespace Runtime
{
    inline constexpr uint32_t MinFrameContexts = 2;
    inline constexpr uint32_t DefaultFrameContexts = 2;
    inline constexpr uint32_t MaxFrameContexts = 3;
    inline constexpr uint64_t InvalidFrameNumber = std::numeric_limits<uint64_t>::max();

    struct RenderViewport
    {
        uint32_t Width = 0;
        uint32_t Height = 0;

        [[nodiscard]] bool IsValid() const
        {
            return Width > 0 && Height > 0;
        }
    };

    struct RenderViewPacket
    {
        Graphics::CameraComponent Camera{};
        glm::mat4 ViewMatrix{1.0f};
        glm::mat4 ProjectionMatrix{1.0f};
        glm::mat4 ViewProjectionMatrix{1.0f};
        glm::mat4 InverseViewMatrix{1.0f};
        glm::mat4 InverseProjectionMatrix{1.0f};
        glm::mat4 InverseViewProjectionMatrix{1.0f};
        glm::vec3 CameraPosition{0.0f};
        float NearPlane = 0.0f;
        glm::vec3 CameraForward{0.0f, 0.0f, -1.0f};
        float FarPlane = 0.0f;
        RenderViewport Viewport{};
        float AspectRatio = 1.0f;
        float VerticalFieldOfViewDegrees = 45.0f;

        [[nodiscard]] bool IsValid() const
        {
            return Viewport.IsValid();
        }
    };

    struct RenderFrameInput
    {
        double Alpha = 0.0;
        RenderViewPacket View{};
        WorldSnapshot World{};

        [[nodiscard]] bool IsValid() const
        {
            return World.IsValid() && View.IsValid();
        }
    };

    struct RenderWorld
    {
        double Alpha = 0.0;
        RenderViewPacket View{};
        WorldSnapshot World{};
        Graphics::LightEnvironmentPacket Lighting{};
        bool HasSelectionWork = false;
        Graphics::SelectionOutlinePacket SelectionOutline{};
        std::vector<Graphics::PickingSurfacePacket> SurfacePicking{};
        std::vector<Graphics::PickingLinePacket> LinePicking{};
        std::vector<Graphics::PickingPointPacket> PointPicking{};
        std::vector<Graphics::SurfaceDrawPacket> SurfaceDraws{};
        std::vector<Graphics::LineDrawPacket> LineDraws{};
        std::vector<Graphics::PointDrawPacket> PointDraws{};
        std::optional<Graphics::HtexPatchPreviewPacket> HtexPatchPreview{};

        // Editor UI overlay state (ImGui draw-data readiness).
        Graphics::EditorOverlayPacket EditorOverlay{};

        // Debug draw snapshots — immutable copies extracted from the DebugDraw accumulator.
        std::vector<Graphics::DebugDraw::LineSegment> DebugDrawLines{};
        std::vector<Graphics::DebugDraw::LineSegment> DebugDrawOverlayLines{};
        std::vector<Graphics::DebugDraw::PointMarker> DebugDrawPoints{};
        std::vector<Graphics::DebugDraw::TriangleVertex> DebugDrawTriangles{};

        // Interaction state resolved during extraction (not late in BuildGraph).
        Graphics::PickRequestSnapshot PickRequest{};
        Graphics::DebugViewSnapshot DebugView{};
        Graphics::GpuSceneSnapshot GpuScene{};

        [[nodiscard]] bool IsValid() const
        {
            return World.IsValid() && View.IsValid();
        }
    };

    inline constexpr size_t DefaultFrameArenaSize = 1024 * 1024;

    struct FrameContext
    {
        uint64_t FrameNumber = 0;
        uint64_t PreviousFrameNumber = InvalidFrameNumber;
        uint64_t LastSubmittedTimelineValue = 0;
        uint32_t SlotIndex = 0;
        uint32_t FramesInFlight = DefaultFrameContexts;
        RenderViewport Viewport{};
        std::optional<RenderWorld> PreparedRenderWorld{};
        bool Prepared = false;
        bool Submitted = false;
        bool ReusedSubmittedSlot = false;

        // Per-frame-context scratch allocators for render-graph data.
        // Owned by the FrameContext so each in-flight frame has independent memory.
        std::unique_ptr<Core::Memory::LinearArena> RenderArena{};
        std::unique_ptr<Core::Memory::ScopeStack> RenderScope{};

        // Per-frame-context resolved GPU profiling sample (B4.9).
        // Populated during EndFrame after GPU work for this slot completes,
        // consumed by the telemetry system on the next reuse of this slot.
        // Keeps profiling results under frame-context ownership rather than
        // keyed by swapchain image count.
        std::optional<RHI::GpuTimestampFrame> ResolvedGpuProfile{};

        // Per-frame-context deferred deletion queue (B4.9).
        // Resources whose lifetime is tied to THIS frame slot push cleanup
        // callbacks here.  The queue is flushed after the GPU confirms
        // completion of this slot's work (timeline wait in BeginFrame),
        // but BEFORE render allocators are reused for the next frame.
        //
        // Use this for per-frame transient resources (staging buffers,
        // descriptor sets, scratch allocations).  For resources with
        // cross-frame or unknown-frame lifetimes, continue using
        // VulkanDevice::SafeDestroy / SafeDestroyAfter.
        std::vector<Core::InplaceFunction<void()>> DeferredDeletions{};

        // Enqueue a cleanup callback to run when this slot is next reused
        // (i.e. after the GPU has finished executing this slot's commands).
        //
        // Threading contract: call only from the main thread.
        // Timing: the callback will fire when FlushDeferredDeletions() is
        // called — typically in RenderOrchestrator::BeginFrame() after the
        // GPU timeline wait confirms this slot's prior work is complete.
        // Unlike VulkanDevice::SafeDestroy (which delays by N frames from
        // the call site), this delays until the specific slot wraps around
        // (i.e. FramesInFlight frames later).
        void DeferDeletion(Core::InplaceFunction<void()>&& fn)
        {
            DeferredDeletions.push_back(std::move(fn));
        }

        // Execute and clear all deferred deletion callbacks.
        // Caller must ensure the GPU has completed this slot's work first.
        //
        // Re-entrancy safe: callbacks that call DeferDeletion() during flush
        // will enqueue into the next cycle (the vector is swapped out before
        // iteration so new pushes go to the live vector, not the batch being
        // drained).
        void FlushDeferredDeletions()
        {
            if (DeferredDeletions.empty())
                return;

            // Swap-and-drain: avoids iterator invalidation if a callback
            // enqueues another deletion during flush.
            std::vector<Core::InplaceFunction<void()>> batch;
            batch.swap(DeferredDeletions);
            for (auto& fn : batch)
                fn();
        }

        [[nodiscard]] bool HasAllocators() const
        {
            return RenderArena != nullptr && RenderScope != nullptr;
        }

        [[nodiscard]] Core::Memory::LinearArena& GetRenderArena()
        {
            return *RenderArena;
        }

        [[nodiscard]] Core::Memory::ScopeStack& GetRenderScope()
        {
            return *RenderScope;
        }

        // Explicit allocator reset for callers outside the render-graph path.
        // Note: RenderGraph::Reset() already resets these during BuildGraph, so this
        // is only needed for non-standard frame flows (e.g. error recovery, testing).
        void ResetRenderAllocators()
        {
            if (RenderScope) RenderScope->Reset();
            if (RenderArena) RenderArena->Reset();
        }

        [[nodiscard]] const RenderWorld* GetPreparedRenderWorld() const
        {
            return PreparedRenderWorld ? &*PreparedRenderWorld : nullptr;
        }

        [[nodiscard]] RenderWorld* GetPreparedRenderWorld()
        {
            return PreparedRenderWorld ? &*PreparedRenderWorld : nullptr;
        }

        void ResetPreparedState()
        {
            PreparedRenderWorld.reset();
            Prepared = false;
        }
    };

    [[nodiscard]] uint32_t SanitizeFrameContextCount(uint32_t requestedCount);

    class FrameContextRing
    {
    public:
        explicit FrameContextRing(uint32_t framesInFlight = DefaultFrameContexts,
                                  size_t renderArenaSize = DefaultFrameArenaSize);

        void Configure(uint32_t framesInFlight, size_t renderArenaSize = DefaultFrameArenaSize);

        [[nodiscard]] uint32_t GetFramesInFlight() const
        {
            return m_FramesInFlight;
        }

        [[nodiscard]] FrameContext& BeginFrame(uint64_t frameNumber, RenderViewport viewport) &;

        // Reset timeline/submitted state on all frame-context slots after the GPU
        // has been fully drained (e.g. during resize). This prevents the next
        // BeginFrame from double-waiting on already-completed timeline values and
        // clears stale per-frame state that referenced the old swapchain.
        void InvalidateAfterResize();

    private:
        uint32_t m_FramesInFlight = DefaultFrameContexts;
        size_t m_RenderArenaSize = DefaultFrameArenaSize;
        std::vector<FrameContext> m_Contexts{};
    };

    [[nodiscard]] RenderFrameInput MakeRenderFrameInput(const Graphics::CameraComponent& camera,
                                                        WorldSnapshot world,
                                                        RenderViewport viewport,
                                                        double alpha = 0.0);

    [[nodiscard]] RenderViewPacket MakeRenderViewPacket(const Graphics::CameraComponent& camera,
                                                        RenderViewport viewport);

    [[nodiscard]] RenderWorld ExtractRenderWorld(const RenderFrameInput& input);
}
