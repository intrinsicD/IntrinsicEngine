// Device-owned double-precision kernels for the explicit graph property filters (averaging,
// Taubin, bilateral, spectral heat). Inputs are plain arrays prepared by the caller from the
// CPU reference plan, so this layer stays free of geometry and ECS types.
module;
#include <array>
#include <cstdint>
#include <memory>
#include <span>
export module Extrinsic.Graphics.PropertyFilter;
import Extrinsic.RHI.Handles;

extern "C++" { namespace Extrinsic::RHI { class IDevice; class ICommandContext; } }

export namespace Extrinsic::Graphics
{
    enum class PropertyFilterGpuMethod : std::uint8_t { Averaging, SpectralHeat, Taubin, Bilateral };

    struct PropertyFilterGpuParams
    {
        PropertyFilterGpuMethod Method{PropertyFilterGpuMethod::Averaging};
        bool RandomWalk{true};
        std::uint32_t Iterations{1}; // 0 when the reference would not iterate (zero rate)
        double Step{}, TaubinStep{};  // lambda and mu, already divided by the combinatorial rate
        double RangeSigma{1.0};       // bilateral
        double HeatStep{};            // spectral heat: 1 / rate
        std::uint32_t HeatSplits{};
        std::array<double, 19> HeatCoefficients{};
        double HeatMass{1.0};
    };

    struct PropertyFilterGpuInput
    {
        std::span<const double> Values;       // rows x channels
        std::uint32_t Channels{1};
        std::span<const std::uint32_t> Edges; // endpoint pairs, one per undirected edge
        std::span<const double> Weights;      // one per edge
        std::span<const double> Degree;       // initial weighted degree per row
        std::span<const std::uint32_t> Fixed; // nonzero: the row keeps its value
    };

    class PropertyFilterWorkspace
    {
    public:
        explicit PropertyFilterWorkspace(RHI::IDevice& device);
        ~PropertyFilterWorkspace();
        // Dispatches Record would issue; runs above MaxDispatches are refused.
        [[nodiscard]] static std::uint64_t DispatchCount(const PropertyFilterGpuParams& params);
        static constexpr std::uint64_t MaxDispatches = 1u << 20;
        // Uploads the inputs, records every iteration on the device and returns the buffer holding
        // the filtered rows x channels doubles, ready for transfer reads; invalid on refusal
        // (non-operational device, no shader double support, bad shape or dispatch budget).
        // The caller keeps the workspace alive until the readback completes.
        [[nodiscard]] RHI::BufferHandle Record(RHI::ICommandContext& commands,
            const PropertyFilterGpuInput& input, const PropertyFilterGpuParams& params);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
