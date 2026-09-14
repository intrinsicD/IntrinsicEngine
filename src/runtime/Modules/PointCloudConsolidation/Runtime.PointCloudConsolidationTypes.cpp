module;
#include <optional>
#include <string>
#include <string_view>
#include <utility>
module Extrinsic.Runtime.PointCloudConsolidationTypes;
namespace Extrinsic::Runtime
{
    PointCloudConsolidationPropertyRefs
    MakePointCloudConsolidationPropertyRefs(
        const GeometryElementDomain domain,
        std::string positionPropertyName,
        std::optional<std::string> normalPropertyName)
    {
        PointCloudConsolidationPropertyRefs refs{
            .InputPositions = GeometryPropertyRef{
                .Domain = domain,
                .Name = positionPropertyName,
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            },
            .OutputPositions = GeometryPropertyRef{
                .Domain = domain,
                .Name = std::move(positionPropertyName),
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            },
        };
        if (normalPropertyName.has_value())
        {
            refs.InputNormals = GeometryPropertyRef{
                .Domain = domain,
                .Name = *normalPropertyName,
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            };
            refs.OutputNormals = GeometryPropertyRef{
                .Domain = domain,
                .Name = std::move(*normalPropertyName),
                .ValueKind = Geometry::PropertyValueKind::Vec3,
            };
        }
        else
        {
            refs.InputNormals.reset();
            refs.OutputNormals.reset();
        }
        return refs;
    }

    std::string_view ToString(
        const PointCloudConsolidationRunStatus status) noexcept
    {
        switch (status)
        {
        case PointCloudConsolidationRunStatus::Queued: return "Queued";
        case PointCloudConsolidationRunStatus::Applied: return "Applied";
        case PointCloudConsolidationRunStatus::MissingScene:
            return "MissingScene";
        case PointCloudConsolidationRunStatus::InvalidProcessingParameters:
            return "InvalidProcessingParameters";
        case PointCloudConsolidationRunStatus::StaleEntity:
            return "StaleEntity";
        case PointCloudConsolidationRunStatus::UnsupportedPropertySource:
            return "UnsupportedPropertySource";
        case PointCloudConsolidationRunStatus::UnsafeSupportRadius:
            return "UnsafeSupportRadius";
        case PointCloudConsolidationRunStatus::GeometryProcessingFailed:
            return "GeometryProcessingFailed";
        case PointCloudConsolidationRunStatus::Cancelled: return "Cancelled";
        case PointCloudConsolidationRunStatus::StaleSource:
            return "StaleSource";
        case PointCloudConsolidationRunStatus::StaleWorld:
            return "StaleWorld";
        case PointCloudConsolidationRunStatus::ModuleUnavailable:
            return "ModuleUnavailable";
        }
        return "Unknown";
    }

}
