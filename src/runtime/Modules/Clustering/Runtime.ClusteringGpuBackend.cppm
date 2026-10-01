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
        // Reuses retained pipelines and any buffer with enough capacity. Call
        // only after every submission that recorded this workspace completed.
        bool Prepare(std::uint32_t count, std::span<const glm::vec3> seeds,
                     std::span<const std::uint32_t> slots);
        RHI::BufferHandle Record(RHI::ICommandContext&, const Graphics::GpuPropertyView& input,
            const KMeansGpuPage&, const Graphics::GpuPropertyView& labels = {},
            const Graphics::GpuPropertyView& presentation = {});
        [[nodiscard]] RHI::BufferHandle Labels() const;
        [[nodiscard]] RHI::BufferHandle Centroids() const;
        [[nodiscard]] RHI::BufferHandle Distances() const;
        [[nodiscard]] RHI::BufferHandle Diagnostics() const;
        [[nodiscard]] std::uint64_t CpuUploadBytes() const;
        // Device objects created by the last Prepare; zero when fully reused.
        [[nodiscard]] std::uint32_t PreparedBufferCreations() const;
        [[nodiscard]] std::uint32_t PreparedPipelineCreations() const;
    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };
}
