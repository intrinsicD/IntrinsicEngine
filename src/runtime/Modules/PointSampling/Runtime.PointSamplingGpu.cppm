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
        PointSamplingGpuRun(SpatialIndexCache& cache, const Graphics::FarthestPointGpuInput& input,
                            std::span<const glm::vec3> referencePoints, const Geometry::PointSampling::Params& params);
        ~PointSamplingGpuRun();
        PointSamplingGpuRun(const PointSamplingGpuRun&) = delete;
        PointSamplingGpuRun& operator=(const PointSamplingGpuRun&) = delete;

        // Main thread: queues the next bounded chunk (null when the request was refused). ARCH-023: it adds at most
        // `maxSamples` samples (a step run's remaining steps; 0: none, a readback of the samples so far) and reads
        // the order back when it adds the last sample or `readback` asks (a stopped run's Accept); earlier chunks
        // move nothing to the CPU.
        [[nodiscard]] std::shared_ptr<SpatialGpuResult> QueueNext(
            SpatialIndexCache& cache, std::uint32_t maxSamples = 0xffffffffu, bool readback = false);
        // The samples the next QueueNext with `maxSamples` adds (0 once refused or released).
        [[nodiscard]] std::uint32_t NextChunkSamples(std::uint32_t maxSamples) const noexcept;
        // Main thread, on a ready chunk: true once every sample is in (only the last chunk carries
        // the order, GRAPHICS-153).
        [[nodiscard]] bool Observe(const SpatialGpuResult& chunk);
        // ARCH-023: the same for a chunk's bytes a worker step took (main thread, once no step runs).
        [[nodiscard]] bool Observe(std::span<const std::byte> chunk);
        // ARCH-023, main thread after construction or Restart: the run's pipeline and buffers (the worker's chunks
        // create nothing); false when the workspace refuses.
        [[nodiscard]] bool Prepare();
        // ARCH-023: the run's device workspace for a worker step (its own reference: Release drops this run's).
        [[nodiscard]] std::shared_ptr<Graphics::FarthestPointSamplingWorkspace> Workspace() const noexcept;
        // Main thread, once no chunk is in flight: the run from its first sample again (a step run's restart), with
        // the device workspace kept (or leased again); false when the workspace refuses.
        [[nodiscard]] bool Restart(SpatialIndexCache& cache);
        // Returns the device workspace (call once no chunk is in flight); the order read back stays.
        void Release() noexcept;
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
