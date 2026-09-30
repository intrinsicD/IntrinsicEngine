// Resident LBVH PCA normals: device covariance/eigenvectors into a float3 output ring.
module;
#include <array>
#include <cstdint>
#include <memory>
export module Extrinsic.Graphics.PointNormals;
import Extrinsic.Graphics.GpuPropertyResidency;
import Extrinsic.RHI.Handles;
extern "C++" { namespace Extrinsic::RHI { class IDevice; class ICommandContext; } }
export namespace Extrinsic::Graphics
{
    struct PointNormalsGpuParams
    {
        std::uint32_t K{15}, MinimumNeighbors{2}, BatchSize{4096};
        bool RadiusSearch{};
        float Radius{};
        double Epsilon{1e-12}, CollinearRatio{1e-5};
        std::array<float, 3> Fallback{0, 0, 1};
    };
    struct PointNormalsResidentIo
    {
        GpuPropertyView Positions{}, Output{}, Base{};
        std::uint64_t Nodes{}, LiveSlots{};
        std::uint32_t LiveCount{};
    };
    struct PointNormalsGpuStats
    {
        std::uint32_t Overflow{}, Valid{}, Fallback{}, TooFew{}, Collinear{}, Degenerate{}, Duplicates{}, FallbackRepaired{};
    };
    class PointNormalsWorkspace
    {
    public:
        explicit PointNormalsWorkspace(RHI::IDevice&);
        ~PointNormalsWorkspace();
        // Radius traversal and quadratic diagnostics have bounded work per submission.
        [[nodiscard]] static std::uint32_t RowsPerSubmission(bool radiusSearch, std::uint32_t batchSize);
        // Sequential pages share scratch/stats/output; caller waits for each page before
        // recording the next, and publishes the output only after the last page succeeds.
        [[nodiscard]] RHI::BufferHandle Record(RHI::ICommandContext&, const PointNormalsGpuParams&,
                                               const PointNormalsResidentIo&, std::uint32_t first = 0);
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
