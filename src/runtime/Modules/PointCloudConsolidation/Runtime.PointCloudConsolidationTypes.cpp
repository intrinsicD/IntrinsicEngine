module;
#include <functional>
#include <utility>
#include <optional>
#include <string>
#include <string_view>
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

    bool PointCloudConsolidationService::Available() const noexcept
    {
        return m_Commands != nullptr && m_Events != nullptr;
    }

    CommandCorrelationId PointCloudConsolidationService::Run(
        PointCloudConsolidationRequest request)
    {
        return m_Commands != nullptr
            ? m_Commands->Enqueue(std::move(request))
            : CommandCorrelationId{};
    }

    KernelEventSubscription
    PointCloudConsolidationService::SubscribeCompleted(
        std::function<void(const PointCloudConsolidationResult&)> listener)
    {
        return m_Events != nullptr && listener
            ? m_Events->Subscribe<PointCloudConsolidationResult>(
                  std::move(listener))
            : KernelEventSubscription{};
    }

    void PointCloudConsolidationService::Unsubscribe(
        const KernelEventSubscription subscription)
    {
        if (m_Events != nullptr && subscription.IsValid())
            m_Events->Unsubscribe(subscription);
    }

    PointCloudConsolidationModuleStats
    PointCloudConsolidationService::Stats() const noexcept
    {
        return m_Stats != nullptr
            ? *m_Stats
            : PointCloudConsolidationModuleStats{};
    }

    void PointCloudConsolidationService::Bind(
        CommandBus* commands,
        KernelEventBus* events,
        const PointCloudConsolidationModuleStats* stats) noexcept
    {
        m_Commands = commands;
        m_Events = events;
        m_Stats = stats;
    }

}
