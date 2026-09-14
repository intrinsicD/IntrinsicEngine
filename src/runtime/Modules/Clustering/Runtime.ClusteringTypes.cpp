module;
#include <string_view>
#include <string>
#include <optional>
module Extrinsic.Runtime.ClusteringTypes;
namespace Extrinsic::Runtime
{
    std::string_view ToString(const ClusteringBackend backend) noexcept
    {
        switch (backend)
        {
        case ClusteringBackend::None: return "none";
        case ClusteringBackend::CpuReference: return "cpu_reference";
        case ClusteringBackend::VulkanCompute: return "vulkan_compute";
        }
        return "none";
    }

    std::string_view ToString(const KMeansRunStatus status) noexcept
    {
        switch (status)
        {
        case KMeansRunStatus::Queued: return "Queued";
        case KMeansRunStatus::Applied: return "Applied";
        case KMeansRunStatus::MissingScene: return "MissingScene";
        case KMeansRunStatus::InvalidProcessingParameters:
            return "InvalidProcessingParameters";
        case KMeansRunStatus::StaleEntity: return "StaleEntity";
        case KMeansRunStatus::UnsupportedGeometryDomain:
            return "UnsupportedGeometryDomain";
        case KMeansRunStatus::GeometryProcessingFailed:
            return "GeometryProcessingFailed";
        case KMeansRunStatus::Cancelled: return "Cancelled";
        case KMeansRunStatus::StaleSource: return "StaleSource";
        case KMeansRunStatus::StaleWorld: return "StaleWorld";
        case KMeansRunStatus::ModuleUnavailable:
            return "ModuleUnavailable";
        }
        return "Unknown";
    }

    KMeansPropertyRefs MakeKMeansPropertyRefs(
        const GeometryElementDomain domain)
    {
        const bool pointCloud =
            domain == GeometryElementDomain::PointCloudPoint;
        KMeansPropertyRefs refs{
            .InputPositions = GeometryPropertyRef{
                .Domain = domain,
                .Name = "v:position",
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            },
            .OutputLabels = GeometryPropertyRef{
                .Domain = domain,
                .Name = pointCloud ? "p:kmeans_label" : "v:kmeans_label",
                .ValueKind = Geometry::PropertyValueKind::UInt32,
            },
            .OutputColors = GeometryPropertyRef{
                .Domain = domain,
                .Name = pointCloud ? "p:kmeans_color" : "v:kmeans_color",
                .ValueKind = Geometry::PropertyValueKind::Vec4,
            },
        };
        if (!pointCloud)
        {
            refs.OutputScalarLabels = GeometryPropertyRef{
                .Domain = domain,
                .Name = "v:kmeans_label_f",
                .ValueKind = Geometry::PropertyValueKind::Float,
            };
        }
        return refs;
    }

}
