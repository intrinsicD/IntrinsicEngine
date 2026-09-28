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

extern "C++" { namespace Extrinsic::RHI { class IDevice; class ICommandContext; } }

export namespace Extrinsic::Graphics
{
    struct CoherentPointDriftEStepGpuInput
    {
        std::span<const float> Target;       // n x 4: x, y, z, unused
        std::uint64_t TargetGeneration{0u};  // the target is uploaded again when this changes
        std::span<const float> Source;       // m x 4: x, y, z, log-weight (<= 0)
        double Sigma2{1.0};
        double LogOutlier{0.0};              // -inf: no uniform component
    };

    class CoherentPointDriftEStepWorkspace
    {
    public:
        explicit CoherentPointDriftEStepWorkspace(RHI::IDevice& device);
        ~CoherentPointDriftEStepWorkspace();
        // Readback layout, all doubles: LogDenominator (n), Pt1 (n), P1, PXx, PXy, PXz (m each).
        [[nodiscard]] static std::size_t ReadbackBytes(std::size_t targets, std::size_t sources) noexcept;
        // Kernel pairs one dispatch may evaluate, so no dispatch runs long enough for a watchdog.
        static constexpr std::uint64_t MaxPairsPerDispatch = std::uint64_t{1} << 30;
        static constexpr std::size_t MaxPoints = std::size_t{1} << 24;
        // Records both passes and returns the buffer to read back, ready for transfer reads;
        // invalid on refusal (non-operational device, no shader double support, bad shape).
        // The caller keeps the workspace alive, and does not record again, until the readback
        // has completed.
        [[nodiscard]] RHI::BufferHandle Record(RHI::ICommandContext& commands, const CoherentPointDriftEStepGpuInput& input);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
