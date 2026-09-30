// Private resident-input k-means workspace and bounded dispatch recorder.
module;
#include <cstdint>
#include <memory>
#include <span>
#include <glm/vec3.hpp>
module Extrinsic.Runtime.ClusteringModule:GpuBackend;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Handles;
import Extrinsic.Graphics.GpuPropertyResidency;
namespace Extrinsic::Runtime
{
    enum class KMeansGpuPhase : std::uint32_t { Initialize, Assign, Update, Reduce, Preview, Presentation };
    struct KMeansGpuDiagnostics
    {
        float Inertia{}, MaxDistance{-1}, MaxShift{};
        std::uint32_t MaxDistanceIndex{}, Changed{}, Invalid{};
        double InertiaSum{};
    };
    struct KMeansGpuPage
    {
        KMeansGpuPhase Phase{};
        std::uint32_t First{}, Count{}, InnerFirst{}, InnerCount{};
    };
    class KMeansGpuWorkspace
    {
    public:
        explicit KMeansGpuWorkspace(RHI::IDevice&);
        ~KMeansGpuWorkspace();
        bool Prepare(std::uint32_t count, std::span<const glm::vec3> seeds,
                     std::span<const std::uint32_t> slots);
        RHI::BufferHandle Record(RHI::ICommandContext&, const Graphics::GpuPropertyView& input,
            const KMeansGpuPage&, const Graphics::GpuPropertyView& labels = {},
            const Graphics::GpuPropertyView& presentation = {});
        [[nodiscard]] RHI::BufferHandle Labels() const;
        [[nodiscard]] RHI::BufferHandle Centroids() const;
        [[nodiscard]] RHI::BufferHandle Distances() const;
        [[nodiscard]] std::uint64_t CpuUploadBytes() const;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
