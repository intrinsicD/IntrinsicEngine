#pragma once

// Include-only Engine frame-loop helpers. Include after module imports in
// Runtime.Engine.cpp so these declarations remain implementation-local.

namespace Extrinsic::Runtime
{
    constexpr double kIdleSleepSeconds = 0.016; // ~60 Hz event wake

    [[nodiscard]] std::uint64_t ElapsedMicros(
        const std::chrono::steady_clock::time_point start) noexcept
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start).count());
    }

    struct RuntimeFrameContext
    {
        double FrameDeltaSeconds{0.0};
        double FixedStepAlpha{0.0};
        std::uint64_t FrameIndex{0};
        Graphics::RenderFrameInput RenderInput{};
        RuntimeRenderExtractionStats ExtractionStats{};
        EditorInputCaptureSnapshot EditorCapture{};
        std::uint32_t PooledFrontSlot{RenderWorldPool::kInvalidSlot};
    };

    [[nodiscard]] bool HasPendingPreRenderTransformFlush(
        const ECS::Scene::Registry& scene)
    {
        const entt::registry& raw = scene.Raw();
        const auto dirtyTransforms =
            raw.view<ECS::Components::Transform::IsDirtyTag>();
        if (dirtyTransforms.begin() != dirtyTransforms.end())
        {
            return true;
        }

        const auto worldUpdated =
            raw.view<ECS::Components::Transform::WorldUpdatedTag>();
        return worldUpdated.begin() != worldUpdated.end();
    }

    void RunFixedStepSimulationTicks(Core::FrameGraph& frameGraph,
                                     ECS::Scene::Registry& scene,
                                     double& accumulator,
                                     const double fixedDt,
                                     const int maxSubSteps)
    {
        int substeps = 0;
        while (accumulator >= fixedDt && substeps < maxSubSteps)
        {
            // The promoted ECS systems are the complete production fixed-step
            // schedule. Their declared read/write and named-signal edges
            // preserve TransformHierarchy -> BoundsPropagation -> RenderSync
            // while Engine owns only composition and replay cadence.
            ECS::Systems::TransformHierarchy::RegisterSystem(
                frameGraph, scene.Raw());
            ECS::Systems::BoundsPropagation::RegisterSystem(
                frameGraph, scene.Raw());
            ECS::Systems::RenderSync::RegisterSystem(
                frameGraph, scene.Raw());

            if (frameGraph.PassCount() > 0)
            {
                if (auto r = frameGraph.Compile(); r.has_value())
                {
                    if (auto exec = frameGraph.Execute(); !exec.has_value())
                    {
                        Core::Log::Error("[Runtime] FrameGraph Execute() failed: error={}",
                                         static_cast<int>(exec.error()));
                    }
                }
                else
                {
                    Core::Log::Error("[Runtime] FrameGraph Compile() failed: error={}",
                                     static_cast<int>(r.error()));
                }
            }

            if (auto reset = frameGraph.ResetForReplay();
                !reset.has_value())
            {
                Core::Log::Error(
                    "[Runtime] FrameGraph ResetForReplay() failed: error={}",
                    static_cast<int>(reset.error()));
            }

            accumulator -= fixedDt;
            ++substeps;
        }
    }
}
