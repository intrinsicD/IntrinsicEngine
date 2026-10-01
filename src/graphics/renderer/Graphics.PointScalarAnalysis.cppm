// Resident LBVH density, spacing and compact-support weights; no CPU neighborhood traffic.
module;
#include <cstdint>
#include <memory>
export module Extrinsic.Graphics.PointScalarAnalysis;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.RHI.Handles;
extern "C++" { namespace Extrinsic::RHI { class IDevice; class ICommandContext; } }
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
        explicit PointScalarWorkspace(RHI::IDevice&);
        ~PointScalarWorkspace();
        // One run per call. Reusable once the previous run's submission completed: the pipeline is
        // kept and only undersized scratch is replaced.
        [[nodiscard]] RHI::BufferHandle Record(RHI::ICommandContext&, const PointScalarGpuParams&, const PointScalarResidentIo&);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
