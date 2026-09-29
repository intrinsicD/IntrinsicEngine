// Vulkan execution seam of Geometry.PointSampling (RUNTIME-290). Geometry stays CPU-only; this
// module decides whether a request can run on the device, drives it in bounded chunks
// through SpatialIndexCache::QueueGpuCompute (main thread, immediate submits), keeps the growing order prefix
// (published entries never change) and checks it against the CPU reference before the
// result may report `gpu_vulkan_compute`. Methods with a device kernel: exact (weighted)
// farthest point (Graphics.FarthestPointSampling); METHOD-014/055/060-062 plug in here.
module;
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <glm/glm.hpp>

export module Extrinsic.Runtime.PointSamplingGpu;

export import Geometry.PointSampling;
export import Extrinsic.Runtime.SpatialIndexCache;

extern "C++"
{
    namespace Extrinsic::RHI { class IDevice; }
}

export namespace Extrinsic::Runtime
{
    inline constexpr std::string_view kPointSamplingGpuBackendId = "gpu_vulkan_compute";
    inline constexpr std::string_view kPointSamplingCpuBackendId = "cpu_reference";
    // Samples of every device order recomputed on the CPU (brute force, on the main thread)
    // before the device result is accepted: up to 64, fewer on large inputs so the check stays
    // within kPointSamplingVerifiedPairs point pairs.
    inline constexpr std::size_t kPointSamplingVerifiedPrefix = 64u;
    inline constexpr std::size_t kPointSamplingVerifiedPairs = std::size_t{1} << 24;

    // Empty when the request can run on `device`; otherwise why it runs on the CPU.
    [[nodiscard]] std::string PointSamplingGpuUnsupportedReason(const Geometry::PointSampling::Params& params,
                                                                std::size_t points, std::size_t count,
                                                                const RHI::IDevice* device);

    class PointSamplingGpuRun
    {
    public:
        // Copies the points and weights; the request must be supported (see above).
        PointSamplingGpuRun(RHI::IDevice& device, std::span<const glm::vec3> points,
                            const Geometry::PointSampling::Params& params, std::size_t count);
        ~PointSamplingGpuRun();
        PointSamplingGpuRun(const PointSamplingGpuRun&) = delete;
        PointSamplingGpuRun& operator=(const PointSamplingGpuRun&) = delete;

        // Main thread: queues the next bounded chunk (null when the request was refused).
        [[nodiscard]] std::shared_ptr<SpatialGpuResult> QueueNext(SpatialIndexCache& cache);
        // Main thread, on a ready chunk: extends the prefix; true once every sample is in.
        [[nodiscard]] bool Observe(const SpatialGpuResult& chunk);
        // The order sampled so far (a prefix of the final order).
        [[nodiscard]] const Geometry::PointSampling::Result& Current() const noexcept;
        // Recomputes the leading samples on the CPU (see kPointSamplingVerifiedPrefix); false,
        // with a diagnostic, when any order entry or clearance differs.
        [[nodiscard]] bool VerifyPrefix(std::string& diagnostic) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
