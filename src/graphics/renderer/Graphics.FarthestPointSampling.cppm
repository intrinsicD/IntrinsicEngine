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

extern "C++"
{
    namespace Extrinsic::RHI
    {
        class IDevice;
        class ICommandContext;
        class BufferManager;
    }
}

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
        // Device buffers are cached scratch leases of `scratch`, which must outlive the workspace.
        FarthestPointSamplingWorkspace(RHI::IDevice& device, RHI::BufferManager& scratch);
        ~FarthestPointSamplingWorkspace();
        // Kernel pairs (points x rounds) one submission may evaluate.
        static constexpr std::uint64_t MaxPairsPerSubmission = std::uint64_t{1} << 26;
        // One workgroup of 256 points per group, within the guaranteed 65535 groups per dispatch.
        static constexpr std::uint32_t MaxPoints = 65535u * 256u;
        // Readback: Count clearance doubles (+inf for the first), then Count order uints.
        [[nodiscard]] static std::size_t ReadbackBytes(std::uint32_t count) noexcept;
        // Retains resident views and copies row/transform metadata; refuses invalid layouts.
        // Call only after the previous run completed; scratch capacity and pipeline are reused.
        [[nodiscard]] bool Begin(const FarthestPointGpuInput& input);
        // ARCH-023, device-owner thread after Begin: creates the pipeline and every buffer of the run (its row map and
        // transform uploaded) and resolves their addresses, so the run's chunks may then be recorded on a worker
        // (RecordNext then makes no device call and creates nothing). Without it the first RecordNext allocates
        // (device-owner thread only). False on refusal.
        [[nodiscard]] bool Prepare();
        // Gathers and converts resident inputs on the first call, records bounded rounds and returns the
        // result buffer, ready for transfer reads; invalid on refusal (non-operational device,
        // no shader float64). The caller keeps the workspace alive, and does not record again,
        // until that submission has completed. ARCH-023: the chunk adds at most `maxSamples` samples
        // (a step run's remaining steps; the first chunk's initial sample counts); 0 records no round
        // and only makes the result buffer ready for a transfer read (a stopped run's Accept), refused
        // before the first sample.
        [[nodiscard]] RHI::BufferHandle RecordNext(
            RHI::ICommandContext& commands, std::uint32_t maxSamples = 0xffffffffu);
        [[nodiscard]] std::uint32_t Produced() const noexcept; // samples in the last recorded result
        // The samples the next RecordNext with `maxSamples` adds: what the pair budget allows, capped.
        [[nodiscard]] std::uint32_t NextChunkSamples(std::uint32_t maxSamples = 0xffffffffu) const noexcept;
        // Whether the next RecordNext produces the last samples (GRAPHICS-153): only that chunk
        // needs the full readback; earlier chunks require completion only.
        [[nodiscard]] bool NextChunkFinishes(std::uint32_t maxSamples = 0xffffffffu) const noexcept;
        [[nodiscard]] bool Finished() const noexcept;
        // Returns every buffer to its cache and ends the run (Begin starts the next); the pipeline stays.
        void ReleaseScratch();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
