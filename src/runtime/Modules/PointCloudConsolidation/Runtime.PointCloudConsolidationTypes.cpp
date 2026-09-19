module;
#include <functional>
#include <utility>
#include <optional>
#include <string>
#include <string_view>
module Extrinsic.Runtime.PointCloudConsolidationTypes;
namespace Extrinsic::Runtime
{
    bool IsValidPointCloudConsolidationPropertyRefs(
        const PointCloudConsolidationPropertyRefs& refs) noexcept
    {
        const GeometryElementDomain domain = refs.InputPositions.Domain;
        if (domain == GeometryElementDomain::Unknown ||
            !refs.InputPositions.HasName() ||
            refs.InputPositions.ValueKind !=
                Geometry::PropertyValueKind::Vec3 ||
            refs.OutputPositions.Domain != domain ||
            !refs.OutputPositions.HasName() ||
            refs.OutputPositions.ValueKind !=
                Geometry::PropertyValueKind::Vec3)
        {
            return false;
        }

        const auto validOptional = [domain](
            const std::optional<GeometryPropertyRef>& ref)
        {
            return !ref.has_value() ||
                   (ref->Domain == domain && ref->HasName() &&
                    ref->ValueKind ==
                        Geometry::PropertyValueKind::Vec3);
        };
        if (!validOptional(refs.InputNormals) ||
            !validOptional(refs.OutputNormals))
        {
            return false;
        }

        if (refs.InputNormals.has_value() &&
            refs.InputNormals->Name == refs.InputPositions.Name)
        {
            return false;
        }
        if (refs.OutputNormals.has_value() &&
            refs.OutputNormals->Name == refs.OutputPositions.Name)
        {
            return false;
        }
        if (refs.OutputNormals.has_value() &&
            refs.OutputNormals->Name == refs.InputPositions.Name)
        {
            return false;
        }
        return !refs.InputNormals.has_value() ||
               refs.OutputPositions.Name != refs.InputNormals->Name;
    }

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

    PointCloudConsolidationAvailability
    PointCloudConsolidationService::PrepareAvailability(
        const WorldHandle world, const PointCloudConsolidationRequest& request)
    {
        return m_PrepareAvailability ? m_PrepareAvailability(world, request)
            : PointCloudConsolidationAvailability{
                .Error = Core::ErrorCode::InvalidState,
                .Message = "Point-set consolidation service is unavailable."};
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
        const PointCloudConsolidationModuleStats* stats,
        std::function<PointCloudConsolidationAvailability(
            WorldHandle, const PointCloudConsolidationRequest&)> prepare) noexcept
    {
        m_PrepareAvailability = std::move(prepare);
        m_Commands = commands;
        m_Events = events;
        m_Stats = stats;
    }

}
