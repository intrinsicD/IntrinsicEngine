// Device-owned dense E-step of Coherent Point Drift (METHOD-056): the target pass
// (cpd_estep_target_pass.comp) computes every row's log-denominator and Pt1, the source pass
// (cpd_estep_source_pass.comp) every source's P1 and PX, with fp32 kernel terms and fp64 sums.
// Inputs are plain float arrays prepared by the caller from the solver's normalized
// coordinates, so this layer stays free of geometry types.
module;
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
export module Extrinsic.Graphics.CoherentPointDriftEStep;
import Extrinsic.RHI.Handles;

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
    struct CoherentPointDriftEStepGpuInput
    {
        // Float-float coordinates: per point hi (x, y, z, w) then lo (x, y, z, unused), where
        // lo = float(x - hi); the target's w is unused, the source's w is its log-weight (<= 0).
        std::span<const float> Target{};     // n x 8
        std::uint64_t TargetGeneration{0u};  // the target is uploaded again when this changes (always when 0)
        std::span<const float> Source{};     // m x 8
        std::span<const std::uint32_t> SkipRows{}; // n flags (nonzero: row evaluated on the CPU) or empty
        double Sigma2{1.0};
        double LogOutlier{0.0};              // -inf: no uniform component
    };

    class CoherentPointDriftEStepWorkspace
    {
    public:
        // Device buffers are cached scratch leases of `scratch`, which must outlive the workspace.
        CoherentPointDriftEStepWorkspace(RHI::IDevice& device, RHI::BufferManager& scratch);
        ~CoherentPointDriftEStepWorkspace();
        // Readback layout, all doubles: LogDenominator (n), Pt1 (n), P1, PXx, PXy, PXz (m each).
        [[nodiscard]] static std::size_t ReadbackBytes(std::size_t targets, std::size_t sources) noexcept;
        // Kernel pairs one dispatch may evaluate, so no dispatch runs long enough for a watchdog.
        static constexpr std::uint64_t MaxPairsPerDispatch = std::uint64_t{1} << 30;
        static constexpr std::size_t MaxPoints = std::size_t{1} << 24;
        // Records both passes and returns the buffer to read back (its first ReadbackBytes(n, m)
        // bytes), ready for transfer reads; invalid on refusal (non-operational device, no shader
        // double support, bad shape). Buffers only grow and pipelines persist, so the workspace
        // may serve later iterations and runs (a pooled SpatialIndexCache lease); the target is
        // uploaded only when its generation or size differs from the last upload. The caller
        // keeps the workspace alive, and does not record again, until the readback has completed.
        [[nodiscard]] RHI::BufferHandle Record(RHI::ICommandContext& commands, const CoherentPointDriftEStepGpuInput& input);
        // ARCH-023, device-owner thread: creates both pipelines, device-local inputs of `targets` x `sources` (the row
        // flags included) with one retained host-visible staging buffer each, and resolves every address. A later
        // Record of at most that shape then creates nothing and makes no device call but IDevice::WriteMappedBuffer
        // (a method worker records it): it writes the rows into the staging and copies them on the compute lane.
        // False on refusal (no device, no float64, budget).
        [[nodiscard]] bool Prepare(std::size_t targets, std::size_t sources);
        // Returns every buffer to its cache and forgets the uploaded target; the pipelines stay.
        void ReleaseScratch();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        [[nodiscard]] RHI::BufferHandle RecordPrepared(
            RHI::ICommandContext& commands, const CoherentPointDriftEStepGpuInput& input, std::size_t n, std::size_t m);
    };
}
