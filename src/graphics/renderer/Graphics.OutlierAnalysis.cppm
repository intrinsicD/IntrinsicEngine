// LBVH outlier scoring and fixed-order classification into resident property rings.
module;
#include <cstdint>
#include <memory>
export module Extrinsic.Graphics.OutlierAnalysis;
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
        double Mean{}, StdDev{}, Threshold{};
    };
    class OutlierWorkspace
    {
    public:
        // Scratch buffers are cached leases of `scratch`, which must outlive the workspace.
        OutlierWorkspace(RHI::IDevice&, RHI::BufferManager& scratch);
        ~OutlierWorkspace();
        // Inputs and outputs remain leased by the caller until the returned stats are read.
        // Reusable once that submission completed: the pipeline is kept, undersized scratch replaced.
        [[nodiscard]] RHI::BufferHandle Record(RHI::ICommandContext&, const OutlierGpuParams&, const OutlierResidentIo&);
        // ARCH-023, device-owner thread (a run's Init): checks the device, creates the pipeline and sizes the scratch for
        // `liveCount` rows of `p` and a score ring of `scoreBytes`, resolving their addresses, so a Record of that shape
        // then makes no device call (a method worker records it).
        [[nodiscard]] bool Prepare(const OutlierGpuParams& p, std::uint32_t liveCount, std::uint64_t scoreBytes);
        // Returns every scratch buffer to its cache, keeping the pipeline; the next Record reallocates.
        void ReleaseScratch();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
