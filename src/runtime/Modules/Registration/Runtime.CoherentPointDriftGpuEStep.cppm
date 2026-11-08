// Device E-step of one Coherent Point Drift run (METHOD-056). ARCH-023: the run's Init (main thread) leases a pooled
// Graphics::CoherentPointDriftEStepWorkspace (SpatialIndexCache::LeaseGpuWorkspace) and prepares its pipelines and
// buffers for the run's largest sets; each EM iteration's E-step is then evaluated on the step's own worker: the
// request is converted to float-float pairs in staging the run reuses, written into the prepared host-visible
// buffers, recorded and submitted as one bounded page (GRAPHICS-150) whose statistics are pulled back and waited for
// there (Runtime.WorkerGpuPages.hpp), with no frame or main-thread pump. The E-step alone runs on the device: the
// M-step, the objective and rows flagged for exact evaluation stay on the CPU, so every iteration uploads the moved
// source and reads back the row and source statistics (the existing hybrid transfer).
//
// Evaluate returns false, and an uncancelled solver runs that E-step on the CPU, when the broker is closed, the page
// is refused or fails (which closes the broker, so later iterations do not wait for a device that cannot help), or
// the request's timeout elapsed.
module;
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

export module Extrinsic.Runtime.CoherentPointDriftGpuEStep;

import Geometry.Registration.CoherentPointDrift.EStep;

extern "C++"
{
    namespace Extrinsic::Runtime { class SpatialIndexCache; }
    namespace Extrinsic::RHI
    {
        class IDevice;
    }
}

export namespace Extrinsic::Runtime
{
    struct CoherentPointDriftGpuEStepStats
    {
        std::uint64_t Requests{0u}, Completed{0u}, Failed{0u};
        double WaitSeconds{0.0}; // elapsed request time through completion, failure or cancellation
    };

    class CoherentPointDriftGpuEStep
    {
    public:
        // The cache (and `device`) must outlive the broker's workspace lease: release or destroy the broker before
        // the cache shuts down.
        CoherentPointDriftGpuEStep(SpatialIndexCache& cache,
            RHI::IDevice& device,
            std::chrono::milliseconds timeout = std::chrono::seconds{60});
        ~CoherentPointDriftGpuEStep();
        CoherentPointDriftGpuEStep(const CoherentPointDriftGpuEStep&) = delete;
        CoherentPointDriftGpuEStep& operator=(const CoherentPointDriftGpuEStep&) = delete;

        // Main thread, the run's Init: leases the workspace and prepares it for at most `targets` x `sources` points
        // (every later request of the run fits), held across every step, pause and restart until the terminal
        // release, Close or destruction. False, closed with the reason: no device workspace or a refused preparation.
        [[nodiscard]] bool Prepare(std::size_t targets, std::size_t sources);
        // Worker (the step's own thread), one request at a time: evaluates the E-step on the device and copies its
        // statistics into the solver's spans. False without writing them: closed, refused, failed, timed out or a
        // shape beyond Prepare's.
        [[nodiscard]] bool Evaluate(const Geometry::CoherentPointDrift::EStep::ExternalRequest& request);
        // Main thread, once the run is applied, discarded or otherwise done: no page is submitted any more and the
        // workspace lease returns to the pool (a page still on the device is covered by the pool's compute fence).
        void ReleaseDeviceResources();
        // Any thread: fails every later request; a waiting one gives up.
        void Close(std::string diagnostic = {});
        [[nodiscard]] bool Closed() const;

        // Why the last iteration did not run on the device (empty while none failed).
        [[nodiscard]] std::string Diagnostic() const;
        [[nodiscard]] CoherentPointDriftGpuEStepStats Stats() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
