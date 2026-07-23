module;

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
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

        [[nodiscard]] const char* DomainName(const ClusteringDomain domain) noexcept
        {
            switch (domain)
            {
            case ClusteringDomain::MeshVertices: return "mesh vertices";
            case ClusteringDomain::GraphVertices: return "graph vertices";
            case ClusteringDomain::PointCloudPoints: return "point-cloud points";
            }
            return "unknown";
        }

        [[nodiscard]] const char* BackendId(const ClusteringBackend backend) noexcept
        {
            switch (backend)
            {
            case ClusteringBackend::CpuReference: return "cpu_reference";
            case ClusteringBackend::VulkanCompute: return "gpu_vulkan_compute";
            }
            return "cpu_reference";
        }

        [[nodiscard]] GK::Backend ToGeometryBackend(
            const ClusteringBackend backend) noexcept
        {
            switch (backend)
            {
            case ClusteringBackend::CpuReference: return GK::Backend::CPU;
            case ClusteringBackend::VulkanCompute: return GK::Backend::GPU;
            }
            return GK::Backend::CPU;
        }

        [[nodiscard]] bool IsExecutionDomain(const ClusteringDomain domain) noexcept
        {
            switch (domain)
            {
            case ClusteringDomain::MeshVertices:
            case ClusteringDomain::GraphVertices:
            case ClusteringDomain::PointCloudPoints:
                return true;
            }
            return false;
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
                .Domain = command.Domain,
                .StableEntityId = command.StableEntityId,
                .RequestedBackend = command.Backend,
                .ActualBackend = ClusteringBackend::CpuReference,
                .FellBackToCpu =
                    command.Backend == ClusteringBackend::VulkanCompute,
                .Error = error,
                .Message = std::move(message),
            };
        }

        [[nodiscard]] bool SourceViewSupportsDomain(
            const GS::MutableSourceView& view,
            const ClusteringDomain domain) noexcept
        {
            const GS::SourceAvailability sources =
                GS::BuildSourceAvailability(view);
            switch (domain)
            {
            case ClusteringDomain::MeshVertices:
                return sources.ProvenanceDomain == GS::Domain::Mesh &&
                       sources.Has(GS::SourceCapability::VertexPoints);
            case ClusteringDomain::GraphVertices:
                return sources.ProvenanceDomain == GS::Domain::Graph &&
                       sources.Has(GS::SourceCapability::NodePoints);
            case ClusteringDomain::PointCloudPoints:
                return sources.ProvenanceDomain == GS::Domain::PointCloud &&
                       sources.Has(GS::SourceCapability::VertexPoints);
            }
            return false;
        }

        [[nodiscard]] Geometry::PropertySet* TargetProperties(
            GS::MutableSourceView& view,
            const ClusteringDomain domain) noexcept
        {
            switch (domain)
            {
            case ClusteringDomain::MeshVertices:
            case ClusteringDomain::PointCloudPoints:
                return view.VertexSource != nullptr
                    ? &view.VertexSource->Properties
                    : nullptr;
            case ClusteringDomain::GraphVertices:
                return view.NodeSource != nullptr ? &view.NodeSource->Properties
                                                  : nullptr;
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
            const Geometry::PropertySet& properties)
        {
            const auto positions =
                properties.Get<glm::vec3>(GS::PropertyNames::kPosition);
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
            const ClusteringDomain domain,
            const GK::KMeansResult& result)
        {
            if (result.Labels.empty() ||
                result.Labels.size() != properties.Size())
            {
                return false;
            }

            const bool pointCloud =
                domain == ClusteringDomain::PointCloudPoints;
            const std::string labelName =
                pointCloud ? "p:kmeans_label" : "v:kmeans_label";
            const std::string colorName =
                pointCloud ? "p:kmeans_color" : "v:kmeans_color";

            auto labels = properties.GetOrAdd<std::uint32_t>(labelName, 0u);
            auto colors =
                properties.GetOrAdd<glm::vec4>(colorName, glm::vec4{1.0f});
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

            if (!pointCloud)
            {
                auto labelFloats =
                    properties.GetOrAdd<float>("v:kmeans_label_f", 0.0f);
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
            const ClusteringDomain domain,
            const KMeansRunCompleted& completion)
        {
            std::string message = "K-Means (requested ";
            message += BackendId(completion.RequestedBackend);
            message += ", actual ";
            message += BackendId(completion.ActualBackend);
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
            if (!IsExecutionDomain(command.Domain) ||
                command.ClusterCount == 0u ||
                command.MaxIterations == 0u)
            {
                failure = MakeCompletion(
                    command,
                    world,
                    correlation,
                    KMeansRunStatus::InvalidProcessingParameters,
                    Core::ErrorCode::InvalidArgument,
                    "K-Means requires mesh vertices, graph nodes, or point-cloud points with positive cluster and iteration counts.");
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

            GS::MutableSourceView view = GS::BuildMutableView(raw, entity);
            if (!view.Valid() || !SourceViewSupportsDomain(view, command.Domain))
            {
                failure = MakeCompletion(
                    command,
                    world,
                    correlation,
                    KMeansRunStatus::UnsupportedGeometryDomain,
                    Core::ErrorCode::InvalidArgument,
                    "Selected entity does not expose the requested K-Means GeometrySources domain.");
                return std::nullopt;
            }

            Geometry::PropertySet* properties =
                TargetProperties(view, command.Domain);
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

            std::optional<std::vector<glm::vec3>> points =
                CollectPositions(*properties);
            if (!points.has_value())
            {
                failure = MakeCompletion(
                    command,
                    world,
                    correlation,
                    KMeansRunStatus::InvalidProcessingParameters,
                    Core::ErrorCode::InvalidArgument,
                    "K-Means requires a non-empty finite v:position property on the requested domain.");
                return std::nullopt;
            }

            GK::KMeansParams params{};
            params.ClusterCount = command.ClusterCount;
            params.MaxIterations = command.MaxIterations;
            params.Seed = command.Seed;
            params.Init = command.UseHierarchicalInitialization
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
                return result;

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
                !SourceViewSupportsDomain(view, job.Snapshot.Command.Domain))
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
                TargetProperties(view, job.Snapshot.Command.Domain);
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
                CollectPositions(*properties);
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
                    job.Snapshot.Command.Domain,
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
                BuildSuccessMessage(job.Snapshot.Command.Domain, completed);
            stats.LabelsCommitted += 1u;
            PublishCompletion(events, completed);

            if (events != nullptr)
            {
                events->Publish(ClusterLabelsChanged{
                    .Correlation = completed.Correlation,
                    .World = completed.World,
                    .Domain = completed.Domain,
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
