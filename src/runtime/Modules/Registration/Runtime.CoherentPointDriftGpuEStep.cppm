// Device E-step broker of one Coherent Point Drift run (METHOD-056). The solver runs on a
// JobService worker, but GPU work is recorded on the device-owner thread: the worker's Evaluate
// (the solver's EStep::ExternalEvaluator) converts the request to float-float pairs in staging the
// run reuses, queues it and waits; Pump, called on the main thread every drain while a step job
// runs, records it through SpatialIndexCache::QueueGpuCompute into a pooled
// Graphics::CoherentPointDriftEStepWorkspace (SpatialIndexCache::LeaseGpuWorkspace) as an
// immediate submit (GRAPHICS-150) and hands the readback back, usually in the frame that
// submitted it. The E-step alone runs on the device: the M-step, the objective and rows flagged
// for exact evaluation stay on the CPU, so every iteration uploads the moved source and reads
// back the row and source statistics.
//
// Evaluate returns false, and the solver runs that iteration on the CPU, when the broker is
// closed, the computation is refused or fails (which closes the broker, so later iterations do
// not wait for a device that cannot help), or no result arrives within Timeout.
module;
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

export module Extrinsic.Runtime.CoherentPointDriftGpuEStep;

import Geometry.Registration.CoherentPointDrift.EStep;

extern "C++"
{
    namespace Extrinsic::Runtime { class SpatialIndexCache; }
}

export namespace Extrinsic::Runtime
{
    struct CoherentPointDriftGpuEStepStats
    {
        std::uint64_t Requests{0u}, Completed{0u}, Failed{0u};
        double WaitSeconds{0.0}; // worker time spent waiting for results
    };

    class CoherentPointDriftGpuEStep
    {
    public:
        // The cache (and its device) must outlive every Pump call and the last computation.
        explicit CoherentPointDriftGpuEStep(SpatialIndexCache& cache,
                                            std::chrono::milliseconds timeout = std::chrono::seconds{60});
        ~CoherentPointDriftGpuEStep();
        CoherentPointDriftGpuEStep(const CoherentPointDriftGpuEStep&) = delete;
        CoherentPointDriftGpuEStep& operator=(const CoherentPointDriftGpuEStep&) = delete;

        // Worker thread; blocks until the result, a failure, Close or the timeout.
        [[nodiscard]] bool Evaluate(const Geometry::CoherentPointDrift::EStep::ExternalRequest& request);
        // Device-owner (main) thread: queues a waiting request, delivers a finished one.
        void Pump();
        // Main thread, once no step job runs: returns the workspace lease to the cache's pool, so a
        // run that outlives the device never frees into a destroyed one. The next request leases
        // again; a matured pooled workspace keeps its pipelines and buffers, and uploads the target
        // again only when it last held another target generation.
        void ReleaseDeviceResources();
        // Any thread: fails the waiting request and every later one.
        void Close(std::string diagnostic = {});
        [[nodiscard]] bool Closed() const;

        // Set while a step job may still call Evaluate; the pump job ends once it is cleared.
        void SetWorkerActive(bool active);
        [[nodiscard]] bool WorkerActive() const;

        // Why the last iteration did not run on the device (empty while none failed).
        [[nodiscard]] std::string Diagnostic() const;
        [[nodiscard]] CoherentPointDriftGpuEStepStats Stats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
