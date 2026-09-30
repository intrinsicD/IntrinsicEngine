// LBVH outlier scoring and fixed-order classification into resident property rings.
module;
#include <cstdint>
#include <memory>
export module Extrinsic.Graphics.OutlierAnalysis;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.RHI.Handles;
extern "C++" { namespace Extrinsic::RHI { class IDevice; class ICommandContext; } }
export namespace Extrinsic::Graphics
{
    struct OutlierGpuParams
    {
        std::uint32_t Method{}, K{}, MinimumNeighbors{};
        float Radius{}, Multiplier{}, ScoreThreshold{};
    };
    // Shared admission/allocation width: statistical=0, radius=1, LDR=2.
    [[nodiscard]] std::uint32_t OutlierNeighborWidth(std::uint32_t method, std::uint32_t k, std::uint32_t liveCount);
    struct OutlierResidentIo
    {
        GpuPropertyView Positions{}, Score{}, Mask{}, Presentation{}, ScoreBase{}, MaskBase{};
        std::uint64_t Nodes{}, LiveSlots{};
        std::uint32_t LiveCount{};
    };
    struct OutlierGpuStats
    {
        std::uint32_t Rejected{}, Invalid{};
        float Mean{}, StdDev{}, Threshold{};
    };
    class OutlierWorkspace
    {
    public:
        explicit OutlierWorkspace(RHI::IDevice&);
        ~OutlierWorkspace();
        // Inputs and outputs remain leased by the caller until the returned stats are read.
        [[nodiscard]] RHI::BufferHandle Record(RHI::ICommandContext&, const OutlierGpuParams&, const OutlierResidentIo&);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
