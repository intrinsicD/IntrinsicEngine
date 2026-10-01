// Device-owned centroid-PCA keypoint kernels over a caller-supplied cached LBVH.
module;
#include <cstdint>
#include <memory>
export module Extrinsic.Graphics.PointKeypoints;
import Extrinsic.RHI.Handles;
import Extrinsic.Graphics.GpuPropertyResidency;

extern "C++" { namespace Extrinsic::RHI { class IDevice; class ICommandContext; } }

export namespace Extrinsic::Graphics
{
    struct PointKeypointParams
    {
        float SalientRadius{}, NonMaxRadius{};
        double Gamma21{.975}, Gamma32{.975};
        std::uint32_t MinimumNeighbors{5};
    };
    // Error bits: 1 invalid scale, 4 nonfinite result, 8 traversal overflow.
    // MaximumNeighbors reports salient-radius support; it does not limit traversal.
    struct PointKeypointHeader
    {
        std::uint32_t Error{}, MaximumNeighbors{}, KeypointCount{}, Reserved{};
        float MeanSpacing{}, SalientRadius{}, NonMaxRadius{}, ReservedFloat{};
    };
    // One completion-gated page. Visits bounds serial traversal per row; the caller
    // bounds Rows*Visits and retries unfinished rows with Resume. Modes: spacing,
    // reduction, score, suppression, ring copy, partial spacing reduction.
    struct PointKeypointPage
    {
        std::uint32_t Mode{}, First{}, Rows{}, Visits{1024}, Resume{};
    };
    class PointKeypointWorkspace
    {
    public:
        explicit PointKeypointWorkspace(RHI::IDevice& device);
        ~PointKeypointWorkspace();
        // Resident canonical stride-12 positions and output leases stay held through
        // completion. The index maps compact rows to original property slots.
        [[nodiscard]] RHI::BufferHandle RecordPage(RHI::ICommandContext& commands,
            std::uint64_t nodes, const GpuPropertyView& positions, std::uint64_t slots,
            std::uint32_t count, const PointKeypointParams& params, const PointKeypointPage& page,
            const GpuPropertyView& score, const GpuPropertyView& mask);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
