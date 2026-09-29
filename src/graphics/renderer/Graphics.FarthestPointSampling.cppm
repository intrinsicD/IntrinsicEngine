// Device-owned exact (weighted) farthest-point sampling (RUNTIME-290): the GPU twin of the
// Geometry.PointSampling brute-force order (point_sampling_farthest.comp). A run is begun
// once and recorded in bounded chunks of rounds, one framed submission each; every readback
// holds the order prefix sampled so far, and earlier entries never change.
module;
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
export module Extrinsic.Graphics.FarthestPointSampling;
import Extrinsic.RHI.Handles;

extern "C++" { namespace Extrinsic::RHI { class IDevice; class ICommandContext; } }

export namespace Extrinsic::Graphics
{
    struct FarthestPointGpuInput
    {
        std::span<const double> X, Y, Z;  // one entry per point (the CPU reference's doubles)
        std::span<const double> Weights;  // empty: unweighted
        std::uint32_t FirstIndex{0u};
        std::uint32_t Count{0u};          // samples to produce, <= points
    };

    class FarthestPointSamplingWorkspace
    {
    public:
        explicit FarthestPointSamplingWorkspace(RHI::IDevice& device);
        ~FarthestPointSamplingWorkspace();
        // Kernel pairs (points x rounds) one submission may evaluate.
        static constexpr std::uint64_t MaxPairsPerSubmission = std::uint64_t{1} << 26;
        static constexpr std::uint32_t MaxPoints = 1u << 24;
        // Readback: Count clearance doubles (+inf for the first), then Count order uints.
        [[nodiscard]] static std::size_t ReadbackBytes(std::uint32_t count) noexcept;
        // Copies the input; false for an invalid shape (the device is checked when recording).
        [[nodiscard]] bool Begin(const FarthestPointGpuInput& input);
        // Uploads on the first call, records the next bounded chunk of rounds and returns the
        // result buffer, ready for transfer reads; invalid on refusal (non-operational device,
        // no shader float64). The caller keeps the workspace alive, and does not record again,
        // until that readback has completed.
        [[nodiscard]] RHI::BufferHandle RecordNext(RHI::ICommandContext& commands);
        [[nodiscard]] std::uint32_t Produced() const noexcept; // samples in the last recorded result
        [[nodiscard]] bool Finished() const noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
