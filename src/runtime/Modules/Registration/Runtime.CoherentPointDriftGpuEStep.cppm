// Device E-step broker of one Coherent Point Drift run (METHOD-056). The solver runs on a
// JobService worker, but GPU work is recorded on the device-owner thread: the worker's Evaluate
// (the solver's EStep::ExternalEvaluator) converts the request to fp32, queues it and waits; Pump,
// called on the main thread every drain while a step job runs, records it through
// SpatialIndexCache::QueueGpuCompute (Graphics::CoherentPointDriftEStepWorkspace) and hands the
// readback back. A result arrives two to three frames after the request.
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
    namespace Extrinsic::RHI { class IDevice; }
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
        // The cache and device must outlive every Pump call and the last computation.
        CoherentPointDriftGpuEStep(SpatialIndexCache& cache, RHI::IDevice& device,
                                   std::chrono::milliseconds timeout = std::chrono::seconds{60});
        ~CoherentPointDriftGpuEStep();
        CoherentPointDriftGpuEStep(const CoherentPointDriftGpuEStep&) = delete;
        CoherentPointDriftGpuEStep& operator=(const CoherentPointDriftGpuEStep&) = delete;

        // Worker thread; blocks until the result, a failure, Close or the timeout.
        [[nodiscard]] bool Evaluate(const Geometry::CoherentPointDrift::EStep::ExternalRequest& request);
        // Device-owner (main) thread: queues a waiting request, delivers a finished one.
        void Pump();
        // Main thread, once no step job runs: drops the device buffers (the next request uploads
        // again), so a run that outlives the device never frees into a destroyed one.
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
