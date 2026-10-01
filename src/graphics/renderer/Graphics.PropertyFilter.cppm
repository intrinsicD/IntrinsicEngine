// Device-owned double-precision kernels for the explicit graph property filters (averaging,
// Taubin, bilateral, spectral heat). Inputs are plain arrays prepared by the caller from the
// CPU reference plan, so this layer stays free of geometry and ECS types. A run may read its
// values from, and write its result into, resident property buffers (ADR 0030): the kernel
// converts the property's own scalar type to its double working layout at the start and back
// at the end, so no CPU copy crosses the boundary.
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

    // A resident property buffer as a kernel endpoint: the property's own scalar type (float
    // or double), `Channels` tightly packed per row, over every property row.
    struct PropertyFilterResidentView
    {
        RHI::BufferHandle Buffer{};
        std::uint64_t Address{};
        bool Double{};
        [[nodiscard]] bool Valid() const noexcept { return Buffer.IsValid() && Address != 0u; }
    };
    // Resident endpoints of a run. `Slots` maps working row i to its property row (empty:
    // row i); `Rows` is the working row count when `Slots` is empty. `Input` replaces the
    // uploaded values (they are gathered on the device). `Output` receives the result: it is
    // first filled from `Base` (the output property's current bytes, so rows outside `Slots`
    // keep their published values) or zeroed over `OutputBytes` without a base, then rows
    // flagged in `RestoreMask` (one per working row) take the input's own value instead of
    // the kernel's, matching the CPU publication's fixed and isolated rows. `Presentation` is
    // an optional float copy of the output (a double property's colormap view).
    struct PropertyFilterResidentIo
    {
        PropertyFilterResidentView Input{};
        PropertyFilterResidentView Output{};
        std::uint64_t OutputBytes{};
        PropertyFilterResidentView Base{};
        PropertyFilterResidentView Presentation{};
        std::uint64_t PresentationBytes{};
        std::span<const std::uint32_t> Slots{};
        std::span<const std::uint32_t> RestoreMask{};
        std::uint32_t Rows{};
    };

    class PropertyFilterWorkspace
    {
    public:
        explicit PropertyFilterWorkspace(RHI::IDevice& device);
        ~PropertyFilterWorkspace();
        // Dispatches Record would issue; runs above MaxDispatches are refused.
        [[nodiscard]] static std::uint64_t DispatchCount(const PropertyFilterGpuParams& params);
        static constexpr std::uint64_t MaxDispatches = 1u << 20;
        // Starts a run: uploads the inputs, records every iteration on the device and returns the
        // buffer holding the filtered rows x channels doubles at its head, ready for transfer
        // reads (read rows x channels doubles, not the buffer's capacity); invalid on refusal
        // (non-operational device, no shader double support, bad shape or dispatch budget).
        // With `resident`, the values come from its input (input.Values may be empty, or equal
        // in shape) and the result is also stored into its output. The caller keeps the
        // workspace alive until the readback completes. A workspace is reusable once its
        // previous run's submissions completed: the pipeline and buffers are kept, only an
        // undersized buffer is replaced, and every input of the run is rewritten.
        [[nodiscard]] RHI::BufferHandle Record(RHI::ICommandContext& commands,
            const PropertyFilterGpuInput& input, const PropertyFilterGpuParams& params,
            const PropertyFilterResidentIo* resident = nullptr);
        // Stores rows x `channels` doubles held elsewhere on the device (element (i, c) at
        // `source` + (i * rowStride + c * channelStride) * 8) into the resident output and
        // presentation, e.g. a conjugate-gradient solution block. Uses the row map and restore
        // mask uploaded by the run's Record or RecordLoad; false on refusal, including endpoints
        // whose map or mask length differs from that run's.
        [[nodiscard]] bool RecordStore(RHI::ICommandContext& commands, const PropertyFilterResidentIo& resident,
            std::uint32_t channels, RHI::BufferHandle source, std::uint64_t sourceAddress,
            std::uint32_t rowStride, std::uint32_t channelStride);
        // Starts a run (uploading its row map and restore mask) and gathers the resident input
        // into rows x `channels` doubles held elsewhere on the device (the same element layout),
        // e.g. a solver's seed block. False on refusal.
        [[nodiscard]] bool RecordLoad(RHI::ICommandContext& commands, const PropertyFilterResidentIo& resident,
            std::uint32_t channels, RHI::BufferHandle target, std::uint64_t targetAddress,
            std::uint32_t rowStride, std::uint32_t channelStride);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
