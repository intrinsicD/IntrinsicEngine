module;
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <functional>
#include <chrono>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>

module Extrinsic.Runtime.ClusteringModule;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorJobProjection;

import Extrinsic.Runtime.Module;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Graphics.Renderer;
import Extrinsic.RHI.BufferManager;
import Extrinsic.RHI.CommandContext;
import Extrinsic.RHI.Device;
import Extrinsic.RHI.TransferQueue;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.SelectionController;
import Extrinsic.Runtime.WorldRegistry;
import Geometry.KMeans;
import Geometry.Properties;

#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Modules/Clustering/Runtime.ClusteringGpuState.Internal.hpp"
#include "Editor/internal/Runtime.EditorMutation.Internal.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace Dirty = ECS::Components::DirtyTags;
        namespace GS = ECS::Components::GeometrySources;
        namespace GK = Geometry::KMeans;

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
            case GeometryElementDomain::MeshEdge: return "mesh edges";
            case GeometryElementDomain::MeshHalfedge: return "mesh halfedges";
            case GeometryElementDomain::MeshFace: return "mesh faces";
            case GeometryElementDomain::GraphEdge: return "graph edges";
            case GeometryElementDomain::GraphHalfedge: return "graph halfedges";
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

        using GeometryProcessingDetail::CapturePointInput;
        using GeometryProcessingDetail::PointInputCapture;
        using GeometryProcessingDetail::MutableGeometryProperties;

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

        template <typename T>
        [[nodiscard]] bool CaptureOutputProperty(
            const Geometry::PropertySet& properties,
            const std::string_view name,
            bool& hadProperty,
            std::vector<T>& values)
        {
            hadProperty = false;
            values.clear();
            if (!properties.Exists(name))
                return true;

            const auto property = properties.Get<T>(name);
            if (!property ||
                property.Vector().size() != properties.Size())
            {
                return false;
            }

            hadProperty = true;
            values = property.Vector();
            return true;
        }

        [[nodiscard]] bool CaptureKMeansOutputPropertyState(
            const Geometry::PropertySet& properties,
            const KMeansPropertyRefs& refs,
            KMeansOutputPropertyState& out)
        {
            out = {};
            out.Labels = CaptureGeometryScalarProperty(properties, refs.OutputLabels);
            if (properties.Exists(refs.OutputLabels.Name) &&
                (!out.Labels.Exists || GeometryScalarPropertySize(out.Labels) != properties.Size()))
                return false;
            if (!CaptureOutputProperty<glm::vec4>(properties, refs.OutputColors.Name,
                                                 out.HadColors, out.Colors)) return false;
            if (!refs.OutputScalarLabels) return true;
            out.ScalarLabels = CaptureGeometryScalarProperty(properties, *refs.OutputScalarLabels);
            return !properties.Exists(refs.OutputScalarLabels->Name) ||
                   (out.ScalarLabels.Exists && GeometryScalarPropertySize(out.ScalarLabels) == properties.Size());
        }

        using GeometryValueComparison::BitEqual;

        [[nodiscard]] bool SameKMeansOutputPropertyState(
            const KMeansOutputPropertyState& lhs,
            const KMeansOutputPropertyState& rhs) noexcept
        {
            return lhs.HadColors == rhs.HadColors && BitEqual(lhs.Colors, rhs.Colors) &&
                   SameGeometryScalarPropertySnapshot(lhs.Labels, rhs.Labels) &&
                   SameGeometryScalarPropertySnapshot(lhs.ScalarLabels, rhs.ScalarLabels);
        }

        [[nodiscard]] std::optional<KMeansOutputPropertyState>
        BuildKMeansOutputPropertyState(
            const Geometry::PropertySet& properties,
            const KMeansPropertyRefs& refs,
            const GK::KMeansResult& result,
            const std::span<const std::uint32_t> slots)
        {
            if (result.Labels.empty() || result.Labels.size() != slots.size()) return std::nullopt;
            std::vector<std::uint32_t> labels(properties.Size());
            for (std::size_t i = 0; i < slots.size(); ++i)
            {
                if (slots[i] >= labels.size()) return std::nullopt;
                labels[slots[i]] = result.Labels[i];
            }
            KMeansOutputPropertyState state;
            if (!CaptureKMeansOutputPropertyState(properties, refs, state) ||
                !PrepareGeometryScalarProperty(state.Labels, refs.OutputLabels.ValueKind,
                    properties.Size(), slots, labels)) return std::nullopt;
            if (refs.OutputScalarLabels &&
                !PrepareGeometryScalarProperty(state.ScalarLabels, refs.OutputScalarLabels->ValueKind,
                    properties.Size(), slots, labels)) return std::nullopt;
            state.HadColors = true;
            state.Colors.resize(properties.Size(), glm::vec4(1.0f));
            for (std::size_t i = 0; i < slots.size(); ++i)
                state.Colors[slots[i]] = LabelColor(result.Labels[i]);
            return state;
        }

        template <typename T>
        [[nodiscard]] bool ApplyOutputProperty(
            Geometry::PropertySet& properties,
            const std::string_view name,
            const bool hasProperty,
            const std::vector<T>& values,
            const T& defaultValue)
        {
            if (!hasProperty)
            {
                if (!values.empty())
                    return false;
                if (!properties.Exists(name))
                    return true;
                auto property = properties.Get<T>(name);
                if (!property)
                    return false;
                properties.Remove(property);
                return true;
            }

            if (values.size() != properties.Size())
                return false;
            auto property =
                properties.GetOrAdd<T>(std::string{name}, defaultValue);
            if (!property || property.Vector().size() != values.size())
                return false;
            property.Vector() = values;
            return true;
        }

        [[nodiscard]] bool ApplyKMeansOutputPropertyState(
            Geometry::PropertySet& properties,
            const KMeansPropertyRefs& refs,
            const KMeansOutputPropertyState& state)
        {
            Geometry::PropertySet staged = properties;
            ApplyGeometryScalarProperty(staged, refs.OutputLabels, state.Labels);
            if (!ApplyOutputProperty<glm::vec4>(staged, refs.OutputColors.Name,
                    state.HadColors, state.Colors, glm::vec4{1.0f})) return false;
            if (refs.OutputScalarLabels)
                ApplyGeometryScalarProperty(staged, *refs.OutputScalarLabels, state.ScalarLabels);
            else if (state.ScalarLabels.Exists) return false;

            properties = std::move(staged);
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
            if (auto rejected = ValidateKMeansRequest(&scene.Raw(), command))
            {
                failure = std::move(*rejected);
                failure.World = world;
                failure.Correlation = correlation;
                return std::nullopt;
            }

            const auto availability = BuildGeometryAvailability(
                scene.Raw(), SelectionController::ToEntityHandle(command.StableEntityId));
            const auto* properties = ResolveGeometryPropertySet(
                availability, command.Properties.InputPositions.Domain);
            PointInputCapture input;
            auto positionRef = command.Properties.InputPositions;
            std::string diagnostic;
            if (!CapturePointInput(availability, positionRef, true, input, diagnostic) || input.Points.empty())
            {
                failure = MakeCompletion(command, world, correlation,
                    KMeansRunStatus::UnsupportedGeometryDomain, Core::ErrorCode::InvalidArgument,
                    "K-Means input: " + diagnostic);
                return std::nullopt;
            }

            KMeansOutputPropertyState beforeOutputs{};
            if (!CaptureKMeansOutputPropertyState(
                    *properties,
                    command.Properties,
                    beforeOutputs))
            {
                failure = MakeCompletion(
                    command,
                    world,
                    correlation,
                    KMeansRunStatus::InvalidProcessingParameters,
                    Core::ErrorCode::TypeMismatch,
                    "K-Means output properties could not be captured as exact count-matched typed state.");
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
                .Points = std::move(input.Points),
                .Slots = std::move(input.Slots),
                .SlotCount = input.SlotCount,
                .BeforeOutputs = std::move(beforeOutputs),
                .Params = params,
            };
        }

        [[nodiscard]] KMeansJobResult MakeCompletedResult(
            KMeansSnapshot snapshot,
            GK::KMeansResult clustered,
            const ClusteringBackend actualBackend)
        {
            KMeansJobResult result{};
            result.Snapshot = std::move(snapshot);
            result.Completion = MakeCompletion(
                result.Snapshot.Command,
                result.Snapshot.World,
                result.Snapshot.Correlation,
                KMeansRunStatus::Applied,
                Core::ErrorCode::Success,
                {});

            clustered.RequestedBackend =
                ToGeometryBackend(result.Snapshot.Command.Backend);
            clustered.ActualBackend = actualBackend ==
                    ClusteringBackend::VulkanCompute
                ? GK::Backend::GPU
                : GK::Backend::CPU;
            clustered.FellBackToCPU =
                result.Snapshot.Command.Backend ==
                    ClusteringBackend::VulkanCompute &&
                actualBackend == ClusteringBackend::CpuReference;

            result.Completion.LabelCount =
                static_cast<std::uint32_t>(clustered.Labels.size());
            result.Completion.ClusterCount =
                static_cast<std::uint32_t>(clustered.Centroids.size());
            result.Completion.Iterations = clustered.Iterations;
            result.Completion.Converged = clustered.Converged;
            result.Completion.Inertia = clustered.Inertia;
            result.Completion.MaxDistanceIndex = clustered.MaxDistanceIndex;
            result.Completion.ActualBackend = actualBackend;
            result.Completion.FellBackToCpu = clustered.FellBackToCPU;
            result.Completion.BackendDiagnostic = result.Snapshot.BackendDiagnostic;
            if (result.Completion.FellBackToCpu)
            {
                result.Completion.BackendDiagnostic =
                    result.Snapshot.BackendDiagnostic.empty()
                    ? "Vulkan compute execution was unavailable; the CPU reference completed the request."
                    : result.Snapshot.BackendDiagnostic;
            }
            result.Clustered = std::move(clustered);
            return result;
        }

        [[nodiscard]] KMeansJobResult RunKMeansWorker(
            KMeansSnapshot snapshot,
            const JobCancellation& cancellation)
        {
            if (cancellation.IsCancelled())
            {
                KMeansJobResult cancelled{};
                cancelled.Snapshot = std::move(snapshot);
                cancelled.Completion = MakeCompletion(
                    cancelled.Snapshot.Command,
                    cancelled.Snapshot.World,
                    cancelled.Snapshot.Correlation,
                    KMeansRunStatus::Cancelled,
                    Core::ErrorCode::InvalidState,
                    "K-Means CPU work was cancelled before execution.");
                return cancelled;
            }

            std::optional<GK::KMeansResult> clustered = GK::Cluster(
                std::span<const glm::vec3>{
                    snapshot.Points.data(), snapshot.Points.size()},
                snapshot.Params);
            if (!clustered.has_value())
            {
                KMeansJobResult failed{};
                failed.Snapshot = std::move(snapshot);
                failed.Completion = MakeCompletion(
                    failed.Snapshot.Command,
                    failed.Snapshot.World,
                    failed.Snapshot.Correlation,
                    KMeansRunStatus::GeometryProcessingFailed,
                    Core::ErrorCode::Unknown,
                    "Geometry.KMeans returned no result for the requested points.");
                return failed;
            }
            return MakeCompletedResult(
                std::move(snapshot),
                std::move(*clustered),
                ClusteringBackend::CpuReference);
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

        using KMeansPointSnapshot =
            std::shared_ptr<const PointInputCapture>;
        using KMeansOutputPropertySnapshot =
            std::shared_ptr<const KMeansOutputPropertyState>;

        struct KMeansMutationIdentity
        {
            ECS::Scene::Registry* Scene{nullptr};
            WorldHandle World{};
            std::uint32_t StableEntityId{0u};
            KMeansPropertyRefs Properties{};
        };

        struct KMeansMutationGeneration
        {
            KMeansPointSnapshot Points{};
            KMeansOutputPropertySnapshot Outputs{};
        };

        [[nodiscard]] EditorCommandHistoryStatus ApplyKMeansOutputState(
            const KMeansMutationIdentity& identity,
            const KMeansOutputPropertyState& state)
        {
            if (identity.Scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = identity.Scene->Raw();
            const ECS::EntityHandle entity =
                ResolveEntity(raw, identity.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return EditorCommandHistoryStatus::StaleEntity;

            GS::MutableSourceView view = GS::BuildMutableView(raw, entity);
            const GeometryElementDomain domain =
                identity.Properties.InputPositions.Domain;
            if (!view.Valid() ||
                !SupportsGeometryElementDomain(BuildGeometryAvailability(raw, entity), domain))
            {
                return EditorCommandHistoryStatus::UnsupportedOperation;
            }
            Geometry::PropertySet* properties =
                MutableGeometryProperties(raw, entity, domain);
            if (properties == nullptr)
                return EditorCommandHistoryStatus::UnsupportedOperation;

            return ApplyKMeansOutputPropertyState(
                       *properties,
                       identity.Properties,
                       state)
                ? EditorCommandHistoryStatus::Applied
                : EditorCommandHistoryStatus::CommandFailed;
        }

        [[nodiscard]] EditorCommandHistoryStatus CommitKMeansOutputs(
            ECS::Scene::Registry* scene,
            const WorldHandle world,
            EditorCommandHistory* history,
            const KMeansSnapshot& snapshot,
            const GK::KMeansResult& clustered)
        {
            if (scene == nullptr)
                return EditorCommandHistoryStatus::MissingScene;

            entt::registry& raw = scene->Raw();
            const ECS::EntityHandle entity =
                ResolveEntity(
                    raw,
                    snapshot.Command.StableEntityId);
            if (entity == ECS::InvalidEntityHandle)
                return EditorCommandHistoryStatus::StaleEntity;

            Geometry::PropertySet* properties =
                MutableGeometryProperties(raw, entity, snapshot.Command.Properties.InputPositions.Domain);
            if (properties == nullptr)
                return EditorCommandHistoryStatus::UnsupportedOperation;

            std::optional<KMeansOutputPropertyState> after =
                BuildKMeansOutputPropertyState(
                    *properties,
                    snapshot.Command.Properties,
                    clustered, snapshot.Slots);
            if (!after.has_value())
                return EditorCommandHistoryStatus::CommandFailed;

            const KMeansMutationIdentity identity{
                .Scene = scene,
                .World = world,
                .StableEntityId = snapshot.Command.StableEntityId,
                .Properties = snapshot.Command.Properties,
            };
            if (history != nullptr)
            {
                const KMeansPointSnapshot points =
                    std::make_shared<PointInputCapture>(PointInputCapture{
                        .Points = snapshot.Points, .Slots = snapshot.Slots, .SlotCount = snapshot.SlotCount});
                const KMeansOutputPropertySnapshot beforeState =
                    std::make_shared<KMeansOutputPropertyState>(
                        snapshot.BeforeOutputs);
                const KMeansOutputPropertySnapshot afterState =
                    std::make_shared<KMeansOutputPropertyState>(
                        std::move(*after));
                return Internal::ExecuteUndoableEntityMutation(
                           *history,
                           "Run K-Means clustering",
                           identity,
                           KMeansMutationGeneration{
                               .Points = points,
                               .Outputs = beforeState,
                           },
                           beforeState,
                           afterState,
                           [](
                               const KMeansMutationIdentity& mutation,
                               const KMeansMutationGeneration& expected,
                               const KMeansOutputPropertySnapshot& target)
                           {
                               if (mutation.Scene == nullptr ||
                                   !mutation.World.IsValid())
                               {
                                   return EditorCommandHistoryStatus::
                                       MissingScene;
                               }
                               if (expected.Points == nullptr ||
                                   expected.Outputs == nullptr ||
                                   target == nullptr)
                               {
                                   return EditorCommandHistoryStatus::
                                       CommandFailed;
                               }

                               entt::registry& currentRaw =
                                   mutation.Scene->Raw();
                               const ECS::EntityHandle currentEntity =
                                   ResolveEntity(
                                       currentRaw,
                                       mutation.StableEntityId);
                               if (currentEntity ==
                                   ECS::InvalidEntityHandle)
                               {
                                   return EditorCommandHistoryStatus::
                                       StaleEntity;
                               }

                               GS::MutableSourceView currentView =
                                   GS::BuildMutableView(
                                       currentRaw,
                                       currentEntity);
                               const GeometryElementDomain domain =
                                   mutation.Properties.InputPositions.Domain;
                               if (!currentView.Valid() ||
                                   !SupportsGeometryElementDomain(BuildGeometryAvailability(currentRaw, currentEntity), domain))
                               {
                                   return EditorCommandHistoryStatus::
                                       UnsupportedOperation;
                               }
                               Geometry::PropertySet* currentProperties =
                                   MutableGeometryProperties(currentRaw, currentEntity, domain);
                               if (currentProperties == nullptr)
                               {
                                   return EditorCommandHistoryStatus::
                                       UnsupportedOperation;
                               }

                               PointInputCapture currentPoints;
                               auto positionRef = mutation.Properties.InputPositions;
                               std::string diagnostic;
                               KMeansOutputPropertyState currentOutputs{};
                               if (!CapturePointInput(BuildGeometryAvailability(currentRaw, currentEntity),
                                       positionRef, true, currentPoints, diagnostic) ||
                                   currentPoints.SlotCount != expected.Points->SlotCount ||
                                   currentPoints.Slots != expected.Points->Slots ||
                                   !BitEqual(currentPoints.Points, expected.Points->Points) ||
                                   !CaptureKMeansOutputPropertyState(
                                       *currentProperties,
                                       mutation.Properties,
                                       currentOutputs) ||
                                   !SameKMeansOutputPropertyState(
                                       currentOutputs,
                                       *expected.Outputs))
                               {
                                   return EditorCommandHistoryStatus::
                                       StaleEntity;
                               }
                               if (target->Labels.Exists &&
                                   GeometryScalarPropertySize(target->Labels) !=
                                       currentProperties->Size())
                               {
                                   return EditorCommandHistoryStatus::
                                       CommandFailed;
                               }
                               return EditorCommandHistoryStatus::Applied;
                           },
                           [](
                               const KMeansMutationIdentity& mutation,
                               const KMeansOutputPropertySnapshot& target)
                           {
                               if (target == nullptr)
                               {
                                   return EditorCommandHistoryStatus::
                                       CommandFailed;
                               }
                               return ApplyKMeansOutputState(
                                   mutation,
                                   *target);
                           },
                           [](
                               const KMeansMutationIdentity& mutation,
                               const KMeansMutationGeneration& expected,
                               const KMeansOutputPropertySnapshot& target)
                           {
                               entt::registry& currentRaw =
                                   mutation.Scene->Raw();
                               const ECS::EntityHandle currentEntity =
                                   ResolveEntity(
                                       currentRaw,
                                       mutation.StableEntityId);
                               if (currentEntity !=
                                   ECS::InvalidEntityHandle)
                               {
                                   Dirty::MarkVertexAttributesDirty(
                                       currentRaw,
                                       currentEntity);
                               }
                               return KMeansMutationGeneration{
                                   .Points = expected.Points,
                                   .Outputs = target,
                               };
                           })
                    .Status;
            }

            const EditorCommandHistoryStatus applied =
                ApplyKMeansOutputState(identity, *after);
            if (applied == EditorCommandHistoryStatus::Applied)
                Dirty::MarkVertexAttributesDirty(raw, entity);
            return applied;
        }

        void HandleJobCompletedEvent(
            const KMeansJobCompleted& event,
            WorldRegistry* worlds,
            KernelEventBus* events,
            EditorCommandHistory* history,
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
                !SupportsGeometryElementDomain(BuildGeometryAvailability(raw, entity),
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
                MutableGeometryProperties(raw, entity, job.Snapshot.Command.Properties.InputPositions.Domain);
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

            PointInputCapture current;
            auto positionRef = job.Snapshot.Command.Properties.InputPositions;
            std::string diagnostic;
            if (!CapturePointInput(BuildGeometryAvailability(raw, entity), positionRef, true, current, diagnostic) ||
                current.SlotCount != job.Snapshot.SlotCount || current.Slots != job.Snapshot.Slots ||
                !BitEqual(current.Points, job.Snapshot.Points))
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

            const EditorCommandHistoryStatus commitStatus =
                CommitKMeansOutputs(
                    scene,
                    job.Snapshot.World,
                    history,
                    job.Snapshot,
                    *job.Clustered);
            if (commitStatus != EditorCommandHistoryStatus::Applied)
            {
                stats.CommitsDropped += 1u;
                KMeansRunCompleted dropped = job.Completion;
                switch (commitStatus)
                {
                case EditorCommandHistoryStatus::MissingScene:
                    dropped.Status = KMeansRunStatus::MissingScene;
                    dropped.Error = Core::ErrorCode::InvalidState;
                    break;
                case EditorCommandHistoryStatus::UnsupportedOperation:
                    dropped.Status =
                        KMeansRunStatus::UnsupportedGeometryDomain;
                    dropped.Error = Core::ErrorCode::InvalidArgument;
                    break;
                case EditorCommandHistoryStatus::StaleEntity:
                    dropped.Status = KMeansRunStatus::StaleSource;
                    dropped.Error = Core::ErrorCode::InvalidState;
                    break;
                default:
                    dropped.Status =
                        KMeansRunStatus::GeometryProcessingFailed;
                    dropped.Error = Core::ErrorCode::TypeMismatch;
                    break;
                }
                dropped.Message = "K-Means result publication failed during "
                                  "the editor mutation transaction.";
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

        [[nodiscard]] CommandOutcome SubmitCpuSnapshot(
            JobService& jobs,
            KernelEventBus* events,
            KMeansSnapshot snapshot,
            ClusteringModuleStats& stats)
        {
            const RunKMeans command = snapshot.Command;
            const WorldHandle world = snapshot.World;
            const CommandCorrelationId correlation = snapshot.Correlation;
            JobDesc job = MakeCpuJobDesc<KMeansJobResult>(
                "Runtime.Clustering.KMeans.CPU",
                world,
                [snapshot = std::move(snapshot)](
                    const JobCancellation& cancellation) mutable
                {
                    return RunKMeansWorker(
                        std::move(snapshot), cancellation);
                },
                [](const KMeansJobResult& result)
                {
                    return KMeansJobCompleted{.Result = result};
                });
            job.FinalizeUnpublishedOnMainThread =
                [events, command, world, correlation]() mutable
                {
                    KMeansRunCompleted cancelled = MakeCompletion(
                        command,
                        world,
                        correlation,
                        KMeansRunStatus::Cancelled,
                        Core::ErrorCode::InvalidState,
                        "K-Means work was cancelled before its result could be committed.");
                    PublishCompletion(events, std::move(cancelled));
                };
            const JobToken token = jobs.Submit(std::move(job));
            if (!token.IsValid())
            {
                stats.JobSubmissionFailures += 1u;
                KMeansRunCompleted rejected = MakeCompletion(
                    command,
                    world,
                    correlation,
                    KMeansRunStatus::GeometryProcessingFailed,
                    Core::ErrorCode::InvalidState,
                    "K-Means CPU job submission was rejected by JobService.");
                PublishCompletion(events, std::move(rejected));
                return CommandOutcome::Fail(
                    "K-Means CPU job submission was rejected by JobService.");
            }
            stats.JobsSubmitted += 1u;
            return CommandOutcome::Ok();
        }

        void HandleGpuResult(
            ClusteringGpuResult result,
            JobService* jobs,
            WorldRegistry* worlds,
            KernelEventBus* events,
            EditorCommandHistory* history,
            ClusteringModuleStats& stats)
        {
            if (result.Succeeded())
            {
                stats.GpuCompletions += 1u;
                KMeansJobResult completed = MakeCompletedResult(
                    std::move(result.Snapshot),
                    std::move(*result.Clustered),
                    ClusteringBackend::VulkanCompute);
                HandleJobCompletedEvent(
                    KMeansJobCompleted{.Result = std::move(completed)},
                    worlds,
                    events,
                    history,
                    stats);
                return;
            }

            stats.GpuFallbacks += 1u;
            result.Snapshot.BackendDiagnostic = result.Diagnostic.empty()
                ? "K-Means Vulkan execution failed; the CPU reference completed the request."
                : result.Diagnostic +
                      " The CPU reference completed the request.";
            if (jobs == nullptr)
            {
                KMeansRunCompleted failed = MakeCompletion(
                    result.Snapshot.Command,
                    result.Snapshot.World,
                    result.Snapshot.Correlation,
                    KMeansRunStatus::GeometryProcessingFailed,
                    Core::ErrorCode::InvalidState,
                    "K-Means Vulkan execution failed and JobService is unavailable for CPU fallback.");
                PublishCompletion(events, std::move(failed));
                return;
            }
            (void)SubmitCpuSnapshot(
                *jobs, events, std::move(result.Snapshot), stats);
        }

        [[nodiscard]] CommandOutcome HandleRunKMeansCommand(
            CommandContext& context,
            const RunKMeans& command,
            ClusteringGpuState* gpuState,
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

            if (command.Backend == ClusteringBackend::VulkanCompute)
            {
                const ClusteringGpuSubmission submission = gpuState != nullptr
                    ? gpuState->Start(*snapshot)
                    : ClusteringGpuSubmission{
                          .Diagnostic =
                              "Clustering Vulkan state is unavailable.",
                      };
                if (submission.Accepted)
                {
                    stats.GpuRequestsAccepted += 1u;
                    return CommandOutcome::Ok();
                }

                stats.GpuFallbacks += 1u;
                snapshot->BackendDiagnostic = submission.Diagnostic.empty()
                    ? "K-Means Vulkan compute execution is unavailable; the CPU reference completed the request."
                    : submission.Diagnostic +
                          " The CPU reference completed the request.";
            }
            return SubmitCpuSnapshot(
                *context.Jobs,
                context.Events,
                std::move(*snapshot),
                stats);
        }
    }

    extern "C++"
    {
    ClusteringModule::ClusteringModule() = default;
    ClusteringModule::~ClusteringModule() = default;

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

        m_Device = setup.Services().Find<RHI::IDevice>();
        Graphics::IRenderer* const renderer =
            setup.Services().Find<Graphics::IRenderer>();
        if (m_Device != nullptr && renderer != nullptr)
        {
            m_GpuState = std::make_unique<ClusteringGpuState>(
                *m_Device,
                renderer->GetBufferManager(),
                m_Device->GetTransferQueue());
            m_GpuParticipant = m_Jobs->RegisterGpuQueueParticipant(
                GpuQueueParticipantDesc{
                    .DebugName = "Runtime.Clustering.KMeans",
                    .RecordFrameCommands =
                        [this](RHI::ICommandContext& commandContext)
                    {
                        if (m_GpuState != nullptr)
                            m_GpuState->RecordFrameCommands(commandContext);
                    },
                    .DrainCompletedTransfers =
                        [this]()
                    {
                        if (m_GpuState == nullptr)
                            return;
                        m_GpuState->DrainCompletedTransfers();
                        while (std::optional<ClusteringGpuResult> result =
                                   m_GpuState->ConsumeCompleted())
                        {
                            HandleGpuResult(
                                std::move(*result),
                                m_Jobs,
                                m_Worlds,
                                m_Events,
                                m_History,
                                m_Stats);
                        }
                    },
                    .HasInFlightWork = [this]() -> bool
                    {
                        return m_GpuState != nullptr &&
                               m_GpuState->HasInFlightWork();
                    },
                    .ShutdownAfterDeviceIdle = [this]()
                    {
                        m_GpuState.reset();
                        m_GpuParticipant = {};
                    },
                });
            if (!m_GpuParticipant.IsValid())
                m_GpuState.reset();
        }

        setup.RegisterCommandHandler<RunKMeans>(
            [this](CommandContext& context,
                   const RunKMeans& command) -> CommandOutcome
            {
                return HandleRunKMeansCommand(
                    context,
                    command,
                    m_GpuState.get(),
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
                        m_History,
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

    Core::Result ClusteringModule::OnResolve(EngineSetup& setup)
    {
        m_History =
            setup.Services().Find<EditorCommandHistory>();
        return Core::Ok();
    }

    void ClusteringModule::OnShutdown(RuntimeModuleShutdownContext& context)
    {
        if (m_Jobs != nullptr && m_Device != nullptr &&
            m_GpuParticipant.IsValid())
        {
            m_Jobs->UnregisterGpuQueueParticipant(
                m_GpuParticipant,
                [device = m_Device] { device->WaitIdle(); });
        }
        m_GpuParticipant = {};
        m_GpuState.reset();
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
        m_History = nullptr;
        m_Device = nullptr;
    }
    }

}
