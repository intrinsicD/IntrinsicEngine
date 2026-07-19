#pragma once

// Include-only Engine module glue. Include after the required graphics and
// runtime module imports in Runtime.Engine.cppm.

namespace Extrinsic::Runtime
{
    // Runtime-owned boundary between the live ECS scene and Graphics.
    //
    // The service owns the extraction-side state as one unit:
    //   - `RenderExtractionCache` — persistent GPU sidecars, geometry
    //     residency and deferred-retire queues; performs the actual
    //     extraction in `ExtractAndSubmit`.
    //   - `RenderWorldPool` — snapshot slot lifecycle (synchronous
    //     single-slot or pipelined render-N-1 buffering; `ConfigurePool`).
    //   - Last-frame extraction stats and the extraction frame index.
    //
    // Contract:
    //   - Extraction performs semantic filtering (render hints,
    //     geometry/asset/material source validation) and residency
    //     reconciliation. Entities that are not renderable or whose sources
    //     are not yet resident are never published as draw candidates.
    //   - View-frustum/HZB/occlusion and pass-specific culling stay in
    //     `Graphics::CullingSystem`; extraction publishes candidates plus
    //     bounds/flags, not visibility decisions.
    //   - The published `RenderWorld` is an immutable snapshot; graphics
    //     never receives references into live ECS storage (the renderer
    //     copies the batch during `SubmitRuntimeSnapshots`).
    //   - Persistent sidecars may outlive a temporarily non-resident
    //     renderable so a later frame can retry residency without a full
    //     re-upload; still-broken sources stay fail-closed and unpublished.
    //   - The visible frame-phase order (AcquireBack → ExtractAndSubmit →
    //     PublishFront → AcquireFront/AcquirePreviousFront →
    //     ExtractRenderWorld → PrepareFrame → ExecuteFrame) is owned by the
    //     Engine frame loop, not by this service.
    class RenderExtractionService
    {
    public:
        RenderExtractionService() = default;

        RenderExtractionService(const RenderExtractionService&) = delete;
        RenderExtractionService& operator=(const RenderExtractionService&) = delete;

        void ConfigurePool(bool synchronousExtraction);

        [[nodiscard]] RenderExtractionCache& Cache() noexcept;
        [[nodiscard]] const RenderExtractionCache& Cache() const noexcept;

        [[nodiscard]] RenderWorldPool& Pool() noexcept;
        [[nodiscard]] const RenderWorldPool& Pool() const noexcept;

        [[nodiscard]] const RuntimeRenderExtractionStats& LastStats() const noexcept;
        void PublishLastStats(const RuntimeRenderExtractionStats& stats) noexcept;

        [[nodiscard]] std::uint64_t CurrentFrameIndex() const noexcept;
        [[nodiscard]] std::uint64_t ConsumeFrameIndex() noexcept;

        void ReleaseFrontSlot(std::uint32_t slot) noexcept;
        void Shutdown(Graphics::IRenderer& renderer);

        void SetVisualizationAdapterBinding(
            std::uint32_t stableEntityId,
            RenderExtractionCache::VisualizationAdapterBinding binding);
        void ClearVisualizationAdapterBinding(std::uint32_t stableEntityId) noexcept;
        [[nodiscard]] std::optional<RenderExtractionCache::VisualizationAdapterBinding>
            GetVisualizationAdapterBinding(std::uint32_t stableEntityId) const noexcept;
        [[nodiscard]] std::uint64_t
            GetVisualizationAdapterBindingRevision() const noexcept;

    private:
        RenderExtractionCache m_Cache{};
        std::unique_ptr<RenderWorldPool> m_Pool{};
        RuntimeRenderExtractionStats m_LastStats{};
        std::uint64_t m_FrameIndex{0u};
    };
}
