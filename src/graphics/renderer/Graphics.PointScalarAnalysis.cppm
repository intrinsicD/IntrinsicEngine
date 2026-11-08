// Resident LBVH density, spacing and compact-support weights; no CPU neighborhood traffic.
module;
#include <cstdint>
#include <memory>
export module Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.Graphics.GpuPropertyResidency;
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
    struct PointScalarGpuParams
    {
        std::uint32_t Method{}, K{}, Capacity{}, Kernel{}, Inverse{};
        float Bandwidth{}, Scale{1}, QueryRadius{};
        double SupportRadius{1};
    };
    [[nodiscard]] std::uint32_t PointScalarNeighborWidth(const PointScalarGpuParams&, std::uint32_t count);
    struct PointScalarResidentIo
    {
        GpuPropertyView Positions{}, Output{}, Base{};
        std::uint64_t Nodes{}, LiveSlots{};
        std::uint32_t LiveCount{};
    };
    struct PointScalarGpuStats
    {
        std::uint32_t Invalid{}, MaximumNeighbors{}, Contributions{}, Reserved{};
        double Bandwidth{}, Mean{}, Minimum{}, Maximum{};
        double AverageSpacing{}, MinSpacing{}, MaxSpacing{}, Diagonal{};
        double CentroidX{}, CentroidY{}, CentroidZ{}, Padding{};
    };
    class PointScalarWorkspace
    {
    public:
        // Scratch buffers are cached leases of `scratch`, which must outlive the workspace.
        PointScalarWorkspace(RHI::IDevice&, RHI::BufferManager& scratch);
        ~PointScalarWorkspace();
        // One run per call. Reusable once the previous run's submission completed: the pipeline is
        // kept and only undersized scratch is replaced.
        [[nodiscard]] RHI::BufferHandle Record(RHI::ICommandContext&, const PointScalarGpuParams&, const PointScalarResidentIo&);
        // ARCH-023, device-owner thread (a run's Init): checks the device, creates the pipeline and sizes the scratch for
        // `liveCount` rows of `p`, resolving their addresses, so a Record of that shape then makes no device call (a
        // method worker records it). Record of another shape fails until the next Prepare or ReleaseScratch.
        [[nodiscard]] bool Prepare(const PointScalarGpuParams& p, std::uint32_t liveCount);
        // Returns every scratch buffer to its cache, keeping the pipeline; the next Record reallocates.
        void ReleaseScratch();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
