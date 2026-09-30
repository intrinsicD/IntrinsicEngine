// Exact weighted farthest-point sampling from resident properties, recorded in bounded
// completion-gated chunks; only the final chunk reads back the order and clearances.
module;
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <array>
export module Extrinsic.Graphics.FarthestPointSampling;
import Extrinsic.RHI.Handles;
export import Extrinsic.Graphics.GpuPropertyResidency;

extern "C++" { namespace Extrinsic::RHI { class IDevice; class ICommandContext; } }

export namespace Extrinsic::Graphics
{
    struct FarthestPointGpuInput
    {
        GpuPropertyView Positions{}; // canonical float3 property, retained through completion
        GpuPropertyView Weights{};   // optional canonical float or double scalar
        std::span<const std::uint32_t> Rows{}; // live rows in sampling order; empty means all
        std::array<double, 16> Model{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
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
        // One workgroup of 256 points per group, within the guaranteed 65535 groups per dispatch.
        static constexpr std::uint32_t MaxPoints = 65535u * 256u;
        // Readback: Count clearance doubles (+inf for the first), then Count order uints.
        [[nodiscard]] static std::size_t ReadbackBytes(std::uint32_t count) noexcept;
        // Retains the resident views and copies row/transform metadata; refuses invalid layouts.
        [[nodiscard]] bool Begin(const FarthestPointGpuInput& input);
        // Gathers and converts resident inputs on the first call, records bounded rounds and returns the
        // result buffer, ready for transfer reads; invalid on refusal (non-operational device,
        // no shader float64). The caller keeps the workspace alive, and does not record again,
        // until that submission has completed.
        [[nodiscard]] RHI::BufferHandle RecordNext(RHI::ICommandContext& commands);
        [[nodiscard]] std::uint32_t Produced() const noexcept; // samples in the last recorded result
        // Whether the next RecordNext produces the last samples (GRAPHICS-153): only that chunk
        // needs the full readback; earlier chunks require completion only.
        [[nodiscard]] bool NextChunkFinishes() const noexcept;
        [[nodiscard]] bool Finished() const noexcept;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
