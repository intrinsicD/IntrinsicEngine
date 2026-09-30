// Drives resident farthest-point sampling in bounded GPU submissions and checks the terminal
// order against the CPU reference before runtime publishes it.
module;
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <glm/glm.hpp>

export module Extrinsic.Runtime.PointSamplingGpu;

export import Geometry.PointSampling;
export import Extrinsic.Graphics.FarthestPointSampling;
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
        // Device input is canonical residency. CPU points/weights are retained only for the parity check.
        PointSamplingGpuRun(RHI::IDevice& device, const Graphics::FarthestPointGpuInput& input,
                            std::span<const glm::vec3> referencePoints, const Geometry::PointSampling::Params& params);
        ~PointSamplingGpuRun();
        PointSamplingGpuRun(const PointSamplingGpuRun&) = delete;
        PointSamplingGpuRun& operator=(const PointSamplingGpuRun&) = delete;

        // Main thread: queues the next bounded chunk (null when the request was refused).
        [[nodiscard]] std::shared_ptr<SpatialGpuResult> QueueNext(SpatialIndexCache& cache);
        // Main thread, on a ready chunk: true once every sample is in (only the last chunk carries
        // the order, GRAPHICS-153).
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
