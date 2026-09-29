// A JobService job whose work is framed GPU computation: SpatialIndexCache::QueueGpuCompute
// is queued and observed on the main thread through IsReadyToApply (the worker does nothing),
// so no thread blocks on the frame loop. Chunked work queues again until Observe says done.
// Include after the JobService, WorldHandle and SpatialIndexCache imports.
#pragma once
extern "C++"
{
namespace Extrinsic::Runtime::EditorFeatureDetail
{
    struct FramedGpuJob
    {
        std::string DebugName{};
        WorldHandle Scope{DefaultWorldHandle};
        // Inputs still current; a stale job skips its remaining chunks and does not publish.
        std::function<bool()> Current{};
        // Queues the next chunk (null: nothing could be queued; the job then publishes).
        std::function<std::shared_ptr<SpatialGpuResult>()> Queue{};
        // Absorbs a ready chunk; true once the work is complete. Empty: one chunk.
        std::function<bool(const SpatialGpuResult&)> Observe{};
        // Publishes with the last chunk (null or not Ready when the device failed or refused).
        std::function<bool(const SpatialGpuResult*)> Publish{};
        // Cancelled, stale or discarded without publishing.
        std::function<void()> Abandon{};
    };

    [[nodiscard]] inline JobDesc MakeFramedGpuJobDesc(FramedGpuJob job)
    {
        struct State
        {
            FramedGpuJob Job;
            std::shared_ptr<SpatialGpuResult> Gpu{};
        };
        auto state = std::make_shared<State>(State{.Job = std::move(job)});
        return JobDesc{
            .DebugName = state->Job.DebugName,
            .Scope = state->Job.Scope,
            .Kind = RuntimeTaskKinds::GeometryProcess,
            .Work = [](const JobCancellation&) { return JobResultEnvelope::Make(true); },
            .IsReadyToApply = [state] {
                auto& s = *state;
                if (!s.Job.Current()) return true;
                if (!s.Gpu) s.Gpu = s.Job.Queue();
                if (!s.Gpu || s.Gpu->State == SpatialQueryState::Failed) return true;
                if (s.Gpu->State != SpatialQueryState::Ready) return false;
                if (!s.Job.Observe || s.Job.Observe(*s.Gpu)) return true;
                s.Gpu = s.Job.Queue();
                return !s.Gpu;
            },
            .ValidateBeforeApply = [state] {
                return state->Job.Current() ? JobApplyValidation::Current : JobApplyValidation::StaleGeneration;
            },
            .PublishCompletion = [state](KernelEventBus&, const JobResultEnvelope&) {
                return state->Job.Publish(state->Gpu.get());
            },
            .FinalizeUnpublishedOnMainThread = [state] {
                if (state->Job.Abandon) state->Job.Abandon();
            },
        };
    }
}
}
