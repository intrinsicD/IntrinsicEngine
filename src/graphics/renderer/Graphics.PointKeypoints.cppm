// Device-owned centroid-PCA keypoint kernels over a caller-supplied cached LBVH.
module;
#include <cstdint>
#include <memory>
export module Extrinsic.Graphics.PointKeypoints;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;

export namespace Extrinsic::Graphics
{
    struct PointKeypointParams
    {
        float SalientRadius{}, NonMaxRadius{};
        double Gamma21{.975}, Gamma32{.975};
        std::uint32_t MinimumNeighbors{5}, RadiusCapacity{256}, BatchSize{4096};
    };
    // Error bits: 1 invalid scale, 2 support overflow, 4 nonfinite result, 8 traversal overflow.
    // MaximumNeighbors is a lower bound on overflow; no partial result is usable.
    struct PointKeypointHeader
    {
        std::uint32_t Error{}, MaximumNeighbors{}, KeypointCount{}, Reserved{};
        float MeanSpacing{}, SalientRadius{}, NonMaxRadius{}, ReservedFloat{};
    };
    struct PointKeypointValue { float Saliency{}; std::uint32_t Mask{}; };
    class PointKeypointWorkspace
    {
    public:
        explicit PointKeypointWorkspace(RHI::IDevice& device);
        ~PointKeypointWorkspace();
        // Points are compact float3; slots maps them to original IDs in LBVH leaves.
        // Inputs must be shader-readable (including a barrier after a fresh build).
        // Caller retains the index/workspace through readback and must not record
        // another computation into this workspace until that readback completes.
        [[nodiscard]] RHI::BufferHandle Record(RHI::ICommandContext& commands,
            std::uint64_t nodes, std::uint64_t points, std::uint64_t slots,
            std::uint32_t count, const PointKeypointParams& params);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
