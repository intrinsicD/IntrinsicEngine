module;
#include <functional>
#include <utility>
#include <string_view>
#include <string>
#include <optional>
#include <entt/entity/registry.hpp>
#include <glm/vec3.hpp>
module Extrinsic.Runtime.ClusteringTypes;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.SelectionController;
namespace Extrinsic::Runtime
{
    namespace
    {
        [[nodiscard]] bool HasExpectedPropertyKinds(
            const KMeansPropertyRefs& refs) noexcept
        {
            const GeometryElementDomain domain = refs.InputPositions.Domain;
            return domain >= GeometryElementDomain::MeshVertex &&
                   domain <= GeometryElementDomain::PointCloudPoint &&
                   refs.InputPositions.HasName() &&
                   refs.InputPositions.ValueKind ==
                       Geometry::PropertyValueKind::Vec3 &&
                   refs.OutputLabels.Domain == domain &&
                   refs.OutputLabels.HasName() &&
                   GeometryPropertyComponentCount(refs.OutputLabels.ValueKind) == 1 &&
                   refs.OutputColors.Domain == domain &&
                   refs.OutputColors.HasName() &&
                   refs.OutputColors.ValueKind ==
                       Geometry::PropertyValueKind::Vec4 &&
                   (!refs.OutputScalarLabels.has_value() ||
                    (refs.OutputScalarLabels->Domain == domain &&
                     refs.OutputScalarLabels->HasName() &&
                     GeometryPropertyComponentCount(refs.OutputScalarLabels->ValueKind) == 1));
        }

        [[nodiscard]] bool HasDistinctPropertyNames(
            const KMeansPropertyRefs& refs) noexcept
        {
            if (refs.InputPositions.Name == refs.OutputLabels.Name ||
                refs.InputPositions.Name == refs.OutputColors.Name ||
                refs.OutputLabels.Name == refs.OutputColors.Name)
            {
                return false;
            }
            if (!refs.OutputScalarLabels.has_value())
                return true;
            return refs.OutputScalarLabels->Name != refs.InputPositions.Name &&
                   refs.OutputScalarLabels->Name != refs.OutputLabels.Name &&
                   refs.OutputScalarLabels->Name != refs.OutputColors.Name;
        }

        [[nodiscard]] bool CanWriteProperty(
            const Geometry::PropertySet& properties,
            const GeometryPropertyRef& ref) noexcept
        {
            const Geometry::PropertyValueKind actual =
                DetectGeometryPropertyValueKind(properties, ref.Name);
            return actual == Geometry::PropertyValueKind::Unknown ||
                   actual == ref.ValueKind;
        }

        [[nodiscard]] bool CanWriteOutputs(
            const Geometry::PropertySet& properties,
            const KMeansPropertyRefs& refs) noexcept
        {
            return CanWriteProperty(properties, refs.OutputLabels) &&
                   CanWriteProperty(properties, refs.OutputColors) &&
                   (!refs.OutputScalarLabels.has_value() ||
                    CanWriteProperty(properties, *refs.OutputScalarLabels));
        }

    }

    bool IsValidKMeansPropertyBindings(const KMeansPropertyRefs& properties) noexcept
    {
        const auto writable = [](const GeometryPropertyRef& ref) {
            return ref.Name.find('\0') == std::string::npos &&
                   !IsTopologyProperty(ref.Domain, ref.Name) &&
                   !(ref.Name == "v:position" &&
                     (ref.Domain == GeometryElementDomain::MeshVertex ||
                      ref.Domain == GeometryElementDomain::GraphNode ||
                      ref.Domain == GeometryElementDomain::PointCloudPoint));
        };
        return HasExpectedPropertyKinds(properties) && HasDistinctPropertyNames(properties) &&
               properties.InputPositions.Name.find('\0') == std::string::npos &&
               writable(properties.OutputLabels) && writable(properties.OutputColors) &&
               (!properties.OutputScalarLabels || writable(*properties.OutputScalarLabels));
    }

    std::optional<KMeansRunCompleted> ValidateKMeansRequest(
        const entt::registry* registry, const RunKMeans& command)
    {
        const auto reject = [&](KMeansRunStatus status, Core::ErrorCode error, const char* message)
        {
            return std::optional<KMeansRunCompleted>{KMeansRunCompleted{
                .Status = status,
                .StableEntityId = command.StableEntityId,
                .Properties = command.Properties,
                .Parameters = command.Parameters,
                .RequestedBackend = command.Backend,
                .ActualBackend = ClusteringBackend::None,
                .Error = error,
                .Message = message,
            }};
        };
        if (registry == nullptr)
            return reject(KMeansRunStatus::MissingScene, Core::ErrorCode::InvalidState,
                "Active world is unavailable for K-Means.");
        if (!IsValidKMeansPropertyBindings(command.Properties) ||
            command.Parameters.ClusterCount == 0u || command.Parameters.MaxIterations == 0u ||
            command.Backend == ClusteringBackend::None)
            return reject(KMeansRunStatus::InvalidProcessingParameters, Core::ErrorCode::InvalidArgument,
                "K-Means requires one supported input/output property domain, vec3 positions, scalar label storage, vec4 colors, distinct property names, a concrete backend, and positive cluster and iteration counts.");

        const auto entity = SelectionController::ToEntityHandle(command.StableEntityId);
        if (!registry->valid(entity))
            return reject(KMeansRunStatus::StaleEntity, Core::ErrorCode::ResourceNotFound,
                command.StableEntityId == 0u ? "Choose an entity to run K-Means."
                                           : "K-Means target entity is stale or no longer live.");
        const auto availability = BuildGeometryAvailability(*registry, entity);
        const auto& refs = command.Properties;
        const auto resolution = ResolveGeometryProperty(availability, refs.InputPositions);
        if (!resolution.Resolved() || resolution.ElementCount == 0u)
            return reject(KMeansRunStatus::UnsupportedGeometryDomain, Core::ErrorCode::InvalidArgument,
                "Selected entity does not expose the requested non-empty K-Means input position property.");
        const auto* properties = ResolveGeometryPropertySet(availability, refs.InputPositions.Domain);
        const auto positions = properties->Get<glm::vec3>(refs.InputPositions.Name);
        if (positions.Vector().size() != properties->Size())
            return reject(KMeansRunStatus::InvalidProcessingParameters, Core::ErrorCode::InvalidArgument,
                "K-Means requires a count-matched vec3 input position property on the requested domain.");
        if (!CanWriteOutputs(*properties, refs))
            return reject(KMeansRunStatus::InvalidProcessingParameters, Core::ErrorCode::TypeMismatch,
                "One or more requested K-Means output properties already exist with incompatible value kinds.");
        return std::nullopt;
    }

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

    bool ClusteringService::Available() const noexcept
    {
        return m_Commands != nullptr && m_Events != nullptr;
    }

    CommandCorrelationId ClusteringService::RunKMeans(
        ::Extrinsic::Runtime::RunKMeans command)
    {
        if (m_Commands == nullptr)
            return {};
        return m_Commands->Enqueue(std::move(command));
    }

    KernelEventSubscription ClusteringService::SubscribeRunCompleted(
        std::function<void(const KMeansRunCompleted&)> listener)
    {
        if (m_Events == nullptr || !listener)
            return {};
        return m_Events->Subscribe<KMeansRunCompleted>(std::move(listener));
    }

    KernelEventSubscription ClusteringService::SubscribeClusterLabelsChanged(
        std::function<void(const ClusterLabelsChanged&)> listener)
    {
        if (m_Events == nullptr || !listener)
            return {};
        return m_Events->Subscribe<ClusterLabelsChanged>(std::move(listener));
    }

    void ClusteringService::Unsubscribe(KernelEventSubscription subscription)
    {
        if (m_Events != nullptr && subscription.IsValid())
            m_Events->Unsubscribe(subscription);
    }

    ClusteringModuleStats ClusteringService::Stats() const noexcept
    {
        return m_Stats != nullptr ? *m_Stats : ClusteringModuleStats{};
    }

    void ClusteringService::Bind(CommandBus* commands,
                                 KernelEventBus* events,
                                 const ClusteringModuleStats* stats) noexcept
    {
        m_Commands = commands;
        m_Events = events;
        m_Stats = stats;
    }

}
