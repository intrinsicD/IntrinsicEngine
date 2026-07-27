module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.ClusteringModule;

import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.KMeans;
import Geometry.Properties;

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Dirty = ECS::Components::DirtyTags;
        namespace GS = ECS::Components::GeometrySources;
        namespace GK = Geometry::KMeans;

        struct KMeansSnapshot
        {
            RunKMeans Command{};
            WorldHandle World{};
            CommandCorrelationId Correlation{};
            std::vector<glm::vec3> Points{};
            GK::KMeansParams Params{};
        };

        struct KMeansJobResult
        {
            KMeansSnapshot Snapshot{};
            KMeansRunCompleted Completion{};
            std::optional<GK::KMeansResult> Clustered{};
        };

        struct KMeansJobCompleted
        {
            KMeansJobResult Result{};
        };

        [[nodiscard]] const char* DomainName(
            const GeometryElementDomain domain) noexcept
        {
            switch (domain)
            {
            case GeometryElementDomain::MeshVertex: return "mesh vertices";
            case GeometryElementDomain::GraphNode: return "graph nodes";
            case GeometryElementDomain::PointCloudPoint:
                return "point-cloud points";
            default: break;
            }
            return "unknown";
        }

        [[nodiscard]] GK::Backend ToGeometryBackend(
            const ClusteringBackend backend) noexcept
        {
            switch (backend)
            {
            case ClusteringBackend::CpuReference: return GK::Backend::CPU;
            case ClusteringBackend::VulkanCompute: return GK::Backend::GPU;
            case ClusteringBackend::None: break;
            }
            return GK::Backend::CPU;
        }

        [[nodiscard]] bool IsExecutionDomain(
            const GeometryElementDomain domain) noexcept
        {
            switch (domain)
            {
            case GeometryElementDomain::MeshVertex:
            case GeometryElementDomain::GraphNode:
            case GeometryElementDomain::PointCloudPoint:
                return true;
            default: break;
            }
            return false;
        }

        [[nodiscard]] bool HasExpectedPropertyKinds(
            const KMeansPropertyRefs& refs) noexcept
        {
            const GeometryElementDomain domain = refs.InputPositions.Domain;
            return IsExecutionDomain(domain) &&
                   refs.InputPositions.HasName() &&
                   refs.InputPositions.ValueKind ==
                       Geometry::PropertyValueKind::Vec3 &&
                   refs.OutputLabels.Domain == domain &&
                   refs.OutputLabels.HasName() &&
                   refs.OutputLabels.ValueKind ==
                       Geometry::PropertyValueKind::UInt32 &&
                   refs.OutputColors.Domain == domain &&
                   refs.OutputColors.HasName() &&
                   refs.OutputColors.ValueKind ==
                       Geometry::PropertyValueKind::Vec4 &&
                   (!refs.OutputScalarLabels.has_value() ||
                    (refs.OutputScalarLabels->Domain == domain &&
                     refs.OutputScalarLabels->HasName() &&
                     refs.OutputScalarLabels->ValueKind ==
                         Geometry::PropertyValueKind::Float));
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

        [[nodiscard]] KMeansRunCompleted MakeCompletion(
            const RunKMeans& command,
            const WorldHandle world,
            const CommandCorrelationId correlation,
            const KMeansRunStatus status,
            const Core::ErrorCode error,
            std::string message)
        {
            return KMeansRunCompleted{
                .Correlation = correlation,
                .World = world,
                .Status = status,
                .StableEntityId = command.StableEntityId,
                .Properties = command.Properties,
                .Parameters = command.Parameters,
                .RequestedBackend = command.Backend,
                .ActualBackend = ClusteringBackend::None,
                .Error = error,
                .Message = std::move(message),
            };
        }

        [[nodiscard]] bool SourceViewSupportsDomain(
            const GS::MutableSourceView& view,
            const GeometryElementDomain domain) noexcept
        {
            const GS::SourceAvailability sources =
                GS::BuildSourceAvailability(view);
            switch (domain)
            {
            case GeometryElementDomain::MeshVertex:
                return sources.ProvenanceDomain == GS::Domain::Mesh &&
                       sources.Has(GS::SourceCapability::VertexPoints);
            case GeometryElementDomain::GraphNode:
                return sources.ProvenanceDomain == GS::Domain::Graph &&
                       sources.Has(GS::SourceCapability::NodePoints);
            case GeometryElementDomain::PointCloudPoint:
                return sources.ProvenanceDomain == GS::Domain::PointCloud &&
                       sources.Has(GS::SourceCapability::VertexPoints);
            default: break;
            }
            return false;
        }

        [[nodiscard]] Geometry::PropertySet* TargetProperties(
            GS::MutableSourceView& view,
            const GeometryElementDomain domain) noexcept
        {
            switch (domain)
            {
            case GeometryElementDomain::MeshVertex:
            case GeometryElementDomain::PointCloudPoint:
                return view.VertexSource != nullptr
                    ? &view.VertexSource->Properties
                    : nullptr;
            case GeometryElementDomain::GraphNode:
                return view.NodeSource != nullptr ? &view.NodeSource->Properties
                                                  : nullptr;
            default: break;
            }
            return nullptr;
        }

        [[nodiscard]] bool IsFinitePosition(const glm::vec3& position) noexcept
        {
            return std::isfinite(position.x) &&
                   std::isfinite(position.y) &&
                   std::isfinite(position.z);
        }

        [[nodiscard]] std::optional<std::vector<glm::vec3>> CollectPositions(
            const Geometry::PropertySet& properties,
            const std::string_view propertyName)
        {
            const auto positions =
                properties.Get<glm::vec3>(propertyName);
            if (!positions || positions.Vector().empty())
                return std::nullopt;
            if (positions.Vector().size() != properties.Size())
                return std::nullopt;

            std::vector<glm::vec3> points{};
            points.reserve(positions.Vector().size());
            for (const glm::vec3& position : positions.Vector())
            {
                if (!IsFinitePosition(position))
                    return std::nullopt;
                points.push_back(position);
            }
            return points;
        }

        [[nodiscard]] bool SamePositions(
            const std::vector<glm::vec3>& lhs,
            const std::vector<glm::vec3>& rhs) noexcept
        {
            if (lhs.size() != rhs.size())
                return false;
            for (std::size_t i = 0u; i < lhs.size(); ++i)
            {
                if (lhs[i].x != rhs[i].x ||
                    lhs[i].y != rhs[i].y ||
                    lhs[i].z != rhs[i].z)
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] glm::vec4 LabelColor(const std::uint32_t label)
        {
            const float h =
                std::fmod(0.61803398875f * static_cast<float>(label), 1.0f);
            constexpr float s = 0.65f;
            constexpr float v = 0.95f;

            const float hh = h * 6.0f;
            const float c = v * s;
            const float x =
                c * (1.0f - std::fabs(std::fmod(hh, 2.0f) - 1.0f));
            const float m = v - c;

            glm::vec3 rgb{0.0f};
            if (hh < 1.0f)
                rgb = {c, x, 0.0f};
            else if (hh < 2.0f)
                rgb = {x, c, 0.0f};
            else if (hh < 3.0f)
                rgb = {0.0f, c, x};
            else if (hh < 4.0f)
                rgb = {0.0f, x, c};
            else if (hh < 5.0f)
                rgb = {x, 0.0f, c};
            else
                rgb = {c, 0.0f, x};
            return glm::vec4(rgb + glm::vec3(m), 1.0f);
        }

        [[nodiscard]] bool PublishProperties(
            Geometry::PropertySet& properties,
            const KMeansPropertyRefs& refs,
            const GK::KMeansResult& result)
        {
            if (result.Labels.empty() ||
                result.Labels.size() != properties.Size())
            {
                return false;
            }

            auto labels = properties.GetOrAdd<std::uint32_t>(
                refs.OutputLabels.Name, 0u);
            auto colors =
                properties.GetOrAdd<glm::vec4>(
                    refs.OutputColors.Name, glm::vec4{1.0f});
            if (!labels || !colors)
                return false;
            if (labels.Vector().size() != result.Labels.size() ||
                colors.Vector().size() != result.Labels.size())
            {
                return false;
            }

            for (std::size_t i = 0u; i < result.Labels.size(); ++i)
            {
                labels.Vector()[i] = result.Labels[i];
                colors.Vector()[i] = LabelColor(result.Labels[i]);
            }

            if (refs.OutputScalarLabels.has_value())
            {
                auto labelFloats =
                    properties.GetOrAdd<float>(
                        refs.OutputScalarLabels->Name, 0.0f);
                if (!labelFloats ||
                    labelFloats.Vector().size() != result.Labels.size())
                {
                    return false;
                }
                for (std::size_t i = 0u; i < result.Labels.size(); ++i)
                    labelFloats.Vector()[i] =
                        static_cast<float>(result.Labels[i]);
            }
            return true;
        }

        [[nodiscard]] std::string BuildSuccessMessage(
            const GeometryElementDomain domain,
            const KMeansRunCompleted& completion)
        {
            std::string message = "K-Means (requested ";
            message += ToString(completion.RequestedBackend);
            message += ", actual ";
            message += ToString(completion.ActualBackend);
            message += ") completed for ";
            message += DomainName(domain);
            message += " (labels=";
            message += std::to_string(completion.LabelCount);
            message += ", clusters=";
            message += std::to_string(completion.ClusterCount);
            message += ", iterations=";
            message += std::to_string(completion.Iterations);
            message += ").";
            return message;
        }

        [[nodiscard]] std::optional<KMeansSnapshot> TryBuildSnapshot(
            ECS::Scene::Registry& scene,
            const WorldHandle world,
            const CommandCorrelationId correlation,
            const RunKMeans& command,
            KMeansRunCompleted& failure)
        {
            const GeometryElementDomain domain =
                command.Properties.InputPositions.Domain;
            if (!HasExpectedPropertyKinds(command.Properties) ||
                !HasDistinctPropertyNames(command.Properties) ||
                command.Parameters.ClusterCount == 0u ||
                command.Parameters.MaxIterations == 0u ||
                command.Backend == ClusteringBackend::None)
            {
                failure = MakeCompletion(
                    command,
                    world,
                    correlation,
                    KMeansRunStatus::InvalidProcessingParameters,
                    Core::ErrorCode::InvalidArgument,
                    "K-Means requires one supported input/output property domain, the canonical vec3/uint32/vec4/float value kinds, distinct property names, a concrete backend, and positive cluster and iteration counts.");
                return std::nullopt;
            }

            entt::registry& raw = scene.Raw();
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(command.StableEntityId);
            if (entity == ECS::InvalidEntityHandle || !raw.valid(entity))
            {
                failure = MakeCompletion(
                    command,
                    world,
                    correlation,
                    KMeansRunStatus::StaleEntity,
                    Core::ErrorCode::ResourceNotFound,
                    "K-Means target entity is stale or no longer live.");
                return std::nullopt;
            }

            const GeometryEntityAvailability availability =
                BuildGeometryAvailability(raw, entity);
            const GeometryPropertyResolution inputResolution =
                ResolveGeometryProperty(
                    availability,
                    command.Properties.InputPositions,
                    std::nullopt,
                    true);
            if (!inputResolution.Resolved() || inputResolution.ElementCount == 0u)
            {
                failure = MakeCompletion(
                    command,
                    world,
                    correlation,
                    KMeansRunStatus::UnsupportedGeometryDomain,
                    Core::ErrorCode::InvalidArgument,
                    "Selected entity does not expose the requested non-empty, finite K-Means input position property.");
                return std::nullopt;
            }

            GS::MutableSourceView view = GS::BuildMutableView(raw, entity);
            if (!view.Valid() || !SourceViewSupportsDomain(view, domain))
            {
                failure = MakeCompletion(
                    command,
                    world,
                    correlation,
                    KMeansRunStatus::UnsupportedGeometryDomain,
                    Core::ErrorCode::InvalidArgument,
                    "Selected entity no longer exposes the requested writable K-Means GeometrySources domain.");
                return std::nullopt;
            }

            Geometry::PropertySet* properties =
                TargetProperties(view, domain);
            if (properties == nullptr)
            {
                failure = MakeCompletion(
                    command,
                    world,
                    correlation,
                    KMeansRunStatus::UnsupportedGeometryDomain,
                    Core::ErrorCode::InvalidArgument,
                    "Requested K-Means GeometrySources domain has no writable property set.");
                return std::nullopt;
            }

            if (!CanWriteOutputs(*properties, command.Properties))
            {
                failure = MakeCompletion(
                    command,
                    world,
                    correlation,
                    KMeansRunStatus::InvalidProcessingParameters,
                    Core::ErrorCode::TypeMismatch,
                    "One or more requested K-Means output properties already exist with incompatible value kinds.");
                return std::nullopt;
            }

            std::optional<std::vector<glm::vec3>> points =
                CollectPositions(
                    *properties,
                    command.Properties.InputPositions.Name);
            if (!points.has_value())
            {
                failure = MakeCompletion(
                    command,
                    world,
                    correlation,
                    KMeansRunStatus::InvalidProcessingParameters,
                    Core::ErrorCode::InvalidArgument,
                    "K-Means requires a non-empty finite vec3 input position property on the requested domain.");
                return std::nullopt;
            }

            GK::KMeansParams params{};
            params.ClusterCount = command.Parameters.ClusterCount;
            params.MaxIterations = command.Parameters.MaxIterations;
            params.Seed = command.Parameters.Seed;
            params.Init = command.Parameters.Initialization ==
                    KMeansInitialization::Hierarchical
                ? GK::Initialization::Hierarchical
                : GK::Initialization::Random;
            params.Compute = GK::Backend::CPU;

            return KMeansSnapshot{
                .Command = command,
                .World = world,
                .Correlation = correlation,
                .Points = std::move(*points),
                .Params = params,
            };
        }

        [[nodiscard]] KMeansJobResult RunKMeansWorker(
            KMeansSnapshot snapshot,
            const JobCancellation& cancellation)
        {
            KMeansJobResult result{};
            result.Snapshot = std::move(snapshot);
            result.Completion = MakeCompletion(
                result.Snapshot.Command,
                result.Snapshot.World,
                result.Snapshot.Correlation,
                KMeansRunStatus::GeometryProcessingFailed,
                Core::ErrorCode::Unknown,
                "Geometry.KMeans returned no result for the requested points.");

            if (cancellation.IsCancelled())
            {
                result.Completion.Status = KMeansRunStatus::Cancelled;
                result.Completion.Error = Core::ErrorCode::InvalidState;
                result.Completion.Message =
                    "K-Means CPU work was cancelled before execution.";
                return result;
            }

            std::optional<GK::KMeansResult> clustered = GK::Cluster(
                std::span<const glm::vec3>{
                    result.Snapshot.Points.data(),
                    result.Snapshot.Points.size()},
                result.Snapshot.Params);
            if (!clustered.has_value())
                return result;

            clustered->RequestedBackend =
                ToGeometryBackend(result.Snapshot.Command.Backend);
            clustered->ActualBackend = GK::Backend::CPU;
            clustered->FellBackToCPU =
                result.Snapshot.Command.Backend ==
                ClusteringBackend::VulkanCompute;

            result.Completion.Status = KMeansRunStatus::Applied;
            result.Completion.Error = Core::ErrorCode::Success;
            result.Completion.Message.clear();
            result.Completion.LabelCount =
                static_cast<std::uint32_t>(clustered->Labels.size());
            result.Completion.ClusterCount =
                static_cast<std::uint32_t>(clustered->Centroids.size());
            result.Completion.Iterations = clustered->Iterations;
            result.Completion.Converged = clustered->Converged;
            result.Completion.Inertia = clustered->Inertia;
            result.Completion.MaxDistanceIndex = clustered->MaxDistanceIndex;
            result.Completion.ActualBackend = ClusteringBackend::CpuReference;
            result.Completion.FellBackToCpu = clustered->FellBackToCPU;
            if (result.Completion.FellBackToCpu)
            {
                result.Completion.BackendDiagnostic =
                    "Vulkan compute execution was unavailable; the CPU reference completed the request.";
            }
            result.Clustered = std::move(*clustered);
            return result;
        }

        void PublishCompletion(KernelEventBus* events,
                               KMeansRunCompleted completion)
        {
            if (events != nullptr)
                events->Publish(std::move(completion));
        }

        [[nodiscard]] ECS::EntityHandle ResolveEntity(
            entt::registry& raw,
            const std::uint32_t stableEntityId) noexcept
        {
            const ECS::EntityHandle entity =
                SelectionController::ToEntityHandle(stableEntityId);
            if (entity != ECS::InvalidEntityHandle && raw.valid(entity))
                return entity;
            return ECS::InvalidEntityHandle;
        }

        void HandleJobCompletedEvent(
            const KMeansJobCompleted& event,
            WorldRegistry* worlds,
            KernelEventBus* events,
            ClusteringModuleStats& stats)
        {
            stats.CompletionEvents += 1u;

            const KMeansJobResult& job = event.Result;
            if (!job.Completion.Succeeded())
            {
                stats.CommitsDropped += 1u;
                PublishCompletion(events, job.Completion);
                return;
            }

            if (worlds == nullptr ||
                !job.Snapshot.World.IsValid() ||
                !worlds->Contains(job.Snapshot.World) ||
                worlds->ActiveWorld() != job.Snapshot.World)
            {
                stats.CommitsDropped += 1u;
                KMeansRunCompleted dropped = job.Completion;
                dropped.Status = KMeansRunStatus::StaleWorld;
                dropped.Error = Core::ErrorCode::InvalidState;
                dropped.Message =
                    "K-Means result was dropped because its world is no longer active.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            ECS::Scene::Registry* scene = worlds->Get(job.Snapshot.World);
            if (scene == nullptr || !job.Clustered.has_value())
            {
                stats.CommitsDropped += 1u;
                KMeansRunCompleted dropped = job.Completion;
                dropped.Status = KMeansRunStatus::MissingScene;
                dropped.Error = Core::ErrorCode::InvalidState;
                dropped.Message =
                    "Scene registry is unavailable for completed K-Means publication.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity =
                ResolveEntity(raw, job.Snapshot.Command.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
            {
                stats.CommitsDropped += 1u;
                KMeansRunCompleted dropped = job.Completion;
                dropped.Status = KMeansRunStatus::StaleEntity;
                dropped.Error = Core::ErrorCode::ResourceNotFound;
                dropped.Message =
                    "K-Means target entity is stale or no longer live.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            GS::MutableSourceView view = GS::BuildMutableView(raw, entity);
            if (!view.Valid() ||
                !SourceViewSupportsDomain(
                    view,
                    job.Snapshot.Command.Properties.InputPositions.Domain))
            {
                stats.CommitsDropped += 1u;
                KMeansRunCompleted dropped = job.Completion;
                dropped.Status = KMeansRunStatus::UnsupportedGeometryDomain;
                dropped.Error = Core::ErrorCode::InvalidArgument;
                dropped.Message =
                    "Completed K-Means job no longer matches a writable GeometrySources domain.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            Geometry::PropertySet* properties =
                TargetProperties(
                    view,
                    job.Snapshot.Command.Properties.InputPositions.Domain);
            if (properties == nullptr)
            {
                stats.CommitsDropped += 1u;
                KMeansRunCompleted dropped = job.Completion;
                dropped.Status = KMeansRunStatus::UnsupportedGeometryDomain;
                dropped.Error = Core::ErrorCode::InvalidArgument;
                dropped.Message =
                    "Completed K-Means domain has no writable property set.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            std::optional<std::vector<glm::vec3>> current =
                CollectPositions(
                    *properties,
                    job.Snapshot.Command.Properties.InputPositions.Name);
            if (!current.has_value() ||
                !SamePositions(*current, job.Snapshot.Points))
            {
                stats.CommitsDropped += 1u;
                KMeansRunCompleted dropped = job.Completion;
                dropped.Status = KMeansRunStatus::StaleSource;
                dropped.Error = Core::ErrorCode::InvalidState;
                dropped.Message =
                    "K-Means result was dropped because source positions changed before commit.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            if (!PublishProperties(
                    *properties,
                    job.Snapshot.Command.Properties,
                    *job.Clustered))
            {
                stats.CommitsDropped += 1u;
                KMeansRunCompleted dropped = job.Completion;
                dropped.Status = KMeansRunStatus::GeometryProcessingFailed;
                dropped.Error = Core::ErrorCode::TypeMismatch;
                dropped.Message =
                    "K-Means result publication failed because output properties have incompatible types or sizes.";
                PublishCompletion(events, std::move(dropped));
                return;
            }

            KMeansRunCompleted completed = job.Completion;
            completed.Message =
                BuildSuccessMessage(
                    job.Snapshot.Command.Properties.InputPositions.Domain,
                    completed);
            stats.LabelsCommitted += 1u;
            PublishCompletion(events, completed);

            if (events != nullptr)
            {
                events->Publish(ClusterLabelsChanged{
                    .Correlation = completed.Correlation,
                    .World = completed.World,
                    .Labels = completed.Properties.OutputLabels,
                    .Colors = completed.Properties.OutputColors,
                    .StableEntityId = completed.StableEntityId,
                    .LabelCount = completed.LabelCount,
                });
            }
        }

        void ReactToClusterLabelsChanged(
            const ClusterLabelsChanged& event,
            WorldRegistry* worlds,
            ClusteringModuleStats& stats)
        {
            stats.ClusterLabelsChangedEvents += 1u;
            if (worlds == nullptr ||
                !event.World.IsValid() ||
                !worlds->Contains(event.World))
            {
                return;
            }

            ECS::Scene::Registry* scene = worlds->Get(event.World);
            if (scene == nullptr)
                return;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity =
                ResolveEntity(raw, event.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return;

            Dirty::MarkVertexAttributesDirty(raw, entity);
            stats.VisualizationRefreshReactions += 1u;
        }

        [[nodiscard]] CommandOutcome HandleRunKMeansCommand(
            CommandContext& context,
            const RunKMeans& command,
            ClusteringModuleStats& stats)
        {
            stats.CommandsHandled += 1u;
            if (context.Jobs == nullptr ||
                context.Worlds == nullptr ||
                context.Events == nullptr)
            {
                return CommandOutcome::Fail(
                    "RunKMeans requires JobService, WorldRegistry, and KernelEventBus services.");
            }

            const WorldHandle world = context.Worlds->ActiveWorld();
            if (!world.IsValid() ||
                context.Worlds->Get(world) != &context.ActiveWorld)
            {
                KMeansRunCompleted failure = MakeCompletion(
                    command,
                    world,
                    context.Correlation,
                    KMeansRunStatus::MissingScene,
                    Core::ErrorCode::InvalidState,
                    "Active world is unavailable for K-Means.");
                PublishCompletion(context.Events, std::move(failure));
                return CommandOutcome::Fail("Active world is unavailable.");
            }

            KMeansRunCompleted failure{};
            std::optional<KMeansSnapshot> snapshot = TryBuildSnapshot(
                context.ActiveWorld,
                world,
                context.Correlation,
                command,
                failure);
            if (!snapshot.has_value())
            {
                PublishCompletion(context.Events, failure);
                return CommandOutcome::Fail(failure.Message);
            }

            const JobToken token = context.Jobs->Submit(
                MakeCpuJobDesc<KMeansJobResult>(
                    "Runtime.Clustering.KMeans.CPU",
                    world,
                    [snapshot = std::move(*snapshot)](
                        const JobCancellation& cancellation) mutable
                    {
                        return RunKMeansWorker(
                            std::move(snapshot),
                            cancellation);
                    },
                    [](const KMeansJobResult& result)
                    {
                        return KMeansJobCompleted{.Result = result};
                    }));

            if (!token.IsValid())
            {
                stats.JobSubmissionFailures += 1u;
                KMeansRunCompleted rejected = MakeCompletion(
                    command,
                    world,
                    context.Correlation,
                    KMeansRunStatus::GeometryProcessingFailed,
                    Core::ErrorCode::InvalidState,
                    "K-Means CPU job submission was rejected by JobService.");
                PublishCompletion(context.Events, std::move(rejected));
                return CommandOutcome::Fail(
                    "K-Means CPU job submission was rejected by JobService.");
            }

            stats.JobsSubmitted += 1u;
            return CommandOutcome::Ok();
        }
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
        struct RunKMeans command)
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

    std::string_view ClusteringModule::Name() const noexcept
    {
        return "Runtime.ClusteringModule";
    }

    Core::Result ClusteringModule::OnRegister(EngineSetup& setup)
    {
        m_Events = &setup.Events();
        m_Jobs = &setup.Jobs();
        m_Worlds = &setup.Worlds();
        m_Service.Bind(&setup.Commands(), m_Events, &m_Stats);

        if (Core::Result provided =
                setup.Services().Provide<ClusteringService>(
                    m_Service, Name());
            !provided.has_value())
        {
            return provided;
        }

        setup.RegisterCommandHandler<RunKMeans>(
            [this](CommandContext& context,
                   const RunKMeans& command) -> CommandOutcome
            {
                return HandleRunKMeansCommand(
                    context,
                    command,
                    m_Stats);
            });

        m_JobCompletedSubscription =
            setup.Subscribe<KMeansJobCompleted>(
                [this](const KMeansJobCompleted& event)
                {
                    HandleJobCompletedEvent(
                        event,
                        m_Worlds,
                        m_Events,
                        m_Stats);
                });

        m_ClusterLabelsChangedSubscription =
            setup.Subscribe<ClusterLabelsChanged>(
                [this](const ClusterLabelsChanged& event)
                {
                    ReactToClusterLabelsChanged(
                        event,
                        m_Worlds,
                        m_Stats);
                });

        return Core::Ok();
    }

    void ClusteringModule::OnShutdown(RuntimeModuleShutdownContext& context)
    {
        if (m_JobCompletedSubscription.IsValid())
            context.Events.Unsubscribe(m_JobCompletedSubscription);
        if (m_ClusterLabelsChangedSubscription.IsValid())
            context.Events.Unsubscribe(m_ClusterLabelsChangedSubscription);
        m_JobCompletedSubscription = {};
        m_ClusterLabelsChangedSubscription = {};
        m_Service.Bind(nullptr, nullptr, nullptr);
        m_Events = nullptr;
        m_Jobs = nullptr;
        m_Worlds = nullptr;
    }
}
