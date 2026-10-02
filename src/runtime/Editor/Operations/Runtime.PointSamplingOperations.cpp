module;
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
module Extrinsic.Runtime.PointSamplingOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Error;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.ECS.Component.Hierarchy;
import Extrinsic.ECS.Component.MetaData;
import Extrinsic.ECS.Component.Transform;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Core.Dag.Scheduler;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.PointSamplingGpu;
import Extrinsic.Runtime.GpuPropertyBinding;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.WorldHandle;
import Geometry.Graph;
import Geometry.HalfedgeMesh;
import Geometry.PointCloud;
import Geometry.PointSampling;
import Geometry.Properties;
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorFramedGpuJob.hpp"
#include "Editor/internal/Runtime.EditorGeneratedEntity.hpp"
#include "Editor/internal/Runtime.EditorTransformHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace ECSC = ECS::Components;
        namespace GPD = GeometryProcessingDetail;
        namespace PS = Geometry::PointSampling;
        using EditorFeatureDetail::ResolveStableEntity;
        using EditorFeatureDetail::ToEditorCommandStatus;
        constexpr std::uint64_t kSamplingIdentityHigh = 0x53414d504c494e47ull; // "SAMPLING"

        glm::dmat4 ModelMatrix(const ECSC::Transform::Component* transform)
        {
            if (transform == nullptr) return glm::dmat4(1.0);
            glm::dmat4 model = glm::dmat4(glm::mat4_cast(glm::dquat(transform->Rotation)));
            model[0] *= double(transform->Scale.x);
            model[1] *= double(transform->Scale.y);
            model[2] *= double(transform->Scale.z);
            model[3] = glm::dvec4(glm::dvec3(transform->Position), 1.0);
            return model;
        }

        struct Captured
        {
            entt::entity Entity{entt::null};
            GPD::PointInputCapture Input{};
            std::vector<glm::vec3> World{};
            std::vector<double> Weights{};
            std::vector<float> Scores{};
            // World frame the samples were taken in (checked before a deferred publication).
            std::optional<ECSC::Transform::Component> Transform{};
        };

        EditorPointSamplingResult Failure(EditorCommandStatus status, std::string message)
        {
            return {.Status = status, .Message = std::move(message)};
        }

        // Shared by readiness (no value copy) and apply (world-space copies).
        std::optional<EditorPointSamplingResult> Capture(const EditorProcessingContext& context,
                                                         PointSamplingOperationConfig& config, bool copyValues,
                                                         Captured* out)
        {
            const auto validation = ValidatePointSamplingOperationConfigSection(
                SerializePointSamplingOperationConfig(config), {}, kPointSamplingConfigSectionName);
            if (!validation.Usable())
                return Failure(EditorCommandStatus::InvalidProcessingParameters, validation.Diagnostics.front().Message);
            if (context.Scene == nullptr) return Failure(EditorCommandStatus::MissingScene, "Scene is unavailable.");
            entt::registry& raw = context.Scene->Raw();
            const auto entity = ResolveStableEntity(raw, config.SourceStableEntityId);
            if (!entity) return Failure(EditorCommandStatus::StaleEntity, "Choose an existing entity.");
            if (const auto* hierarchy = raw.try_get<ECSC::Hierarchy::Component>(*entity);
                hierarchy != nullptr && raw.valid(hierarchy->Parent))
                return Failure(EditorCommandStatus::InvalidProcessingParameters,
                               "Point sampling needs an unparented entity (points are sampled in its world frame).");
            const auto available = BuildGeometryAvailability(raw, *entity);
            if (config.Positions.Domain == GeometryElementDomain::Unknown)
                config.Positions.Domain = GPD::PrimaryPointDomain(available);
            GPD::PointInputCapture input;
            std::string diagnostic;
            if (!GPD::CapturePointInput(available, config.Positions, copyValues, input, diagnostic))
                return Failure(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
            if (input.LiveCount == 0u)
                return Failure(EditorCommandStatus::InvalidProcessingParameters, "The point domain has no live points.");
            for (const auto& name : {config.RankName, config.SelectedName})
                if (IsTopologyProperty(config.Positions.Domain, name))
                    return Failure(EditorCommandStatus::InvalidProcessingParameters,
                                   "The output properties cannot replace topology or deletion data.");
            const auto* props = ResolveGeometryPropertySet(available, config.Positions.Domain);
            std::optional<std::span<const float>> weights;
            if (!config.WeightsName.empty())
            {
                const auto property = props ? props->Get<float>(config.WeightsName) : decltype(props->Get<float>("")){};
                if (!property || property.Vector().size() != props->Size())
                    return Failure(EditorCommandStatus::InvalidProcessingParameters,
                                   "The weights must be a float property of the points' domain.");
                weights = std::span<const float>(property.Vector());
            }
            if (out == nullptr) return std::nullopt;
            out->Entity = *entity;
            out->Input = std::move(input);
            // Outputs are also watched from admission, so an edit during compute is never overwritten.
            for (const auto& name : {config.RankName, config.SelectedName})
                out->Input.Inputs.push_back(GPD::ObserveGeometryProperty(available, config.Positions.Domain, name));
            // A deferred run publishes only while the weights it read are unchanged, too.
            if (weights) out->Input.Inputs.push_back(GPD::ObserveGeometryProperty(available, config.Positions.Domain,
                                                                                  config.WeightsName));
            if (const auto* transform = raw.try_get<ECSC::Transform::Component>(*entity)) out->Transform = *transform;
            const glm::dmat4 model = ModelMatrix(raw.try_get<ECSC::Transform::Component>(*entity));
            for (const auto& p : out->Input.Points)
                out->World.push_back(glm::vec3(model * glm::dvec4(glm::dvec3(p), 1.0)));
            if (weights)
                for (const std::uint32_t slot : out->Input.Slots)
                {
                    out->Weights.push_back(double((*weights)[slot]));
                    out->Scores.push_back((*weights)[slot]);
                }
            return std::nullopt;
        }

        // One history entry writing (or restoring) the rank and selection properties together.
        EditorCommandHistoryStatus PublishProperties(const EditorProcessingContext& context, const Captured& captured,
                                                     const PointSamplingOperationConfig& config,
                                                     const std::vector<std::uint32_t>& order, std::size_t count)
        {
            entt::registry& raw = context.Scene->Raw();
            const GeometryElementDomain domain = config.Positions.Domain;
            const auto* props = GPD::MutableGeometryProperties(raw, captured.Entity, domain);
            if (props == nullptr) return EditorCommandHistoryStatus::StaleEntity;
            struct State
            {
                bool RankExists{}, SelectedExists{};
                std::vector<float> Rank{};
                std::vector<bool> Selected{};
            };
            const auto& read = std::as_const(*props);
            const auto rank = read.Get<float>(config.RankName);
            const auto selected = read.Get<bool>(config.SelectedName);
            auto before = std::make_shared<State>(State{bool(rank), bool(selected), rank ? rank.Vector() : std::vector<float>{},
                                                        selected ? selected.Vector() : std::vector<bool>{}});
            auto after = std::make_shared<State>(State{true, true, std::vector<float>(props->Size(), -1.0f),
                                                       std::vector<bool>(props->Size(), false)});
            for (std::size_t k = 0; k < order.size(); ++k)
            {
                const std::uint32_t slot = captured.Input.Slots[order[k]];
                after->Rank[slot] = float(k);
                if (k < count) after->Selected[slot] = true;
            }
            const auto available = BuildGeometryAvailability(raw, captured.Entity);
            auto revisions = std::make_shared<std::vector<GPD::PointPropertyWatch>>(
                std::vector{GPD::ObserveGeometryProperty(available, domain, config.RankName),
                            GPD::ObserveGeometryProperty(available, domain, config.SelectedName)});
            auto inputs = captured.Input.Inputs;
            std::erase_if(inputs, [&](const auto& input) {
                return input.Domain == domain && (input.Name == config.RankName || input.Name == config.SelectedName);
            });
            const auto mutate = [context, entity = captured.Entity, inputs = std::move(inputs), revisions, domain,
                                 rankName = config.RankName, selectedName = config.SelectedName](const State& target) {
                if (!GPD::GeometryPropertiesCurrent(context, entity, inputs) ||
                    !GPD::GeometryPropertiesCurrent(context, entity, *revisions))
                    return EditorCommandHistoryStatus::StaleEntity;
                auto& registry = context.Scene->Raw();
                auto* set = GPD::MutableGeometryProperties(registry, entity, domain);
                if (target.RankExists) set->GetOrAdd<float>(rankName).Vector() = target.Rank;
                else if (auto property = set->Get<float>(rankName)) set->Remove(property);
                if (target.SelectedExists) set->GetOrAdd<bool>(selectedName).Vector() = target.Selected;
                else if (auto property = set->Get<bool>(selectedName)) set->Remove(property);
                const auto now = BuildGeometryAvailability(registry, entity);
                for (auto& watch : *revisions) watch = GPD::ObserveGeometryProperty(now, domain, watch.Name);
                ECS::Components::DirtyTags::MarkGpuDirty(registry, entity);
                ECS::Components::DirtyTags::MarkVertexAttributesDirty(registry, entity);
                if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
                return EditorCommandHistoryStatus::Applied;
            };
            return context.CommandHistory
                ? context.CommandHistory->Execute({.Label = "Point sampling",
                                                   .Redo = [mutate, after] { return mutate(*after); },
                                                   .Undo = [mutate, before] { return mutate(*before); }}).Status
                : mutate(*after);
        }

        // Publishes an order computed by either backend (the source was captured in `captured`).
        EditorPointSamplingResult Publish(const EditorProcessingContext& context, const Captured& captured,
                                          const PointSamplingOperationConfig& config, std::size_t count,
                                          const PS::Result& order, EditorPointSamplingResult result)
        {
            if (!order.Succeeded())
            {
                result.Status = EditorCommandStatus::InvalidProcessingParameters;
                result.Message = "Sampling failed: " + std::string(PS::ToString(order.State)) + ".";
                return result;
            }
            result.DistancePairs = order.DistancePairs;
            result.SampleCount = std::uint32_t(std::min(count, order.Order.size()));
            if (config.Output == PointSamplingOutput::PointCloud)
            {
                Geometry::PointCloud::Cloud cloud;
                cloud.Reserve(result.SampleCount);
                for (std::size_t k = 0; k < result.SampleCount; ++k)
                    static_cast<void>(cloud.AddPoint(captured.World[order.Order[k]]));
                const auto* meta = context.Scene->Raw().try_get<ECSC::MetaData>(captured.Entity);
                const std::string name = (meta ? meta->EntityName : std::string("Points")) + " samples";
                const auto published = GPD::PublishEditorGeneratedEntity(
                    context, {.Source = captured.Entity, .Name = name, .Cloud = std::move(cloud),
                              .IdentityHigh = kSamplingIdentityHigh, .Label = "Point sampling"});
                result.Status = published.Status;
                result.OutputEntityId = published.OutputEntityId;
                result.Message = published.Succeeded()
                    ? "Created " + name + " with " + std::to_string(result.SampleCount) + " points."
                    : published.Message;
                return result;
            }
            result.Status = ToEditorCommandStatus(PublishProperties(context, captured, config, order.Order, count));
            result.Message = result.Succeeded()
                ? "Ranked " + std::to_string(order.Order.size()) + " points; selected " +
                      std::to_string(result.SampleCount) + "."
                : "The source changed before publication.";
            return result;
        }

        EditorPointSamplingResult Apply(const EditorProcessingContext& context, PointSamplingOperationConfig config,
                                        std::function<void(EditorPointSamplingResult)> onComplete)
        {
            const auto finish = [&onComplete](EditorPointSamplingResult result) {
                if (onComplete) onComplete(result);
                return result;
            };
            auto captured = std::make_shared<Captured>();
            if (auto failure = Capture(context, config, true, captured.get())) return finish(*failure);
            EditorPointSamplingResult result;
            result.Method = std::string(DisplayName(config.Sampling.Method));
            result.InputCount = std::uint32_t(captured->World.size());
            const std::size_t count = config.Count == 0u ? captured->World.size()
                                                         : std::min<std::size_t>(config.Count, captured->World.size());
            const auto params = ToPointSamplingParams(config.Sampling, captured->Weights, captured->Scores);
            if (config.Backend == PointSamplingBackend::Vulkan)
            {
                result.RequestedBackend = std::string(kPointSamplingGpuBackendId);
                result.BackendDiagnostic = !context.JobCommands.Available() || context.SpatialIndices == nullptr
                    ? "No job lane or spatial compute service; sampling ran on the CPU."
                    : PointSamplingGpuUnsupportedReason(params, captured->World.size(), count, context.Device);
            }
            // The Vulkan run's job is named by the rank output it writes on the source.
            const EditorJobIdentity identity{.EntityId = config.SourceStableEntityId,
                                             .Scope = ToEditorJobScope(config.Positions.Domain),
                                             .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                                             .OutputName = config.RankName};
            // Property output: any run (CPU or Vulkan) would overwrite the rank and selection an active
            // Vulkan run publishes, so a duplicate is refused like every queued editor job (Pending,
            // the active run keeps its callback). A point-cloud output creates a new entity per run
            // and conflicts with nothing.
            if (config.Output != PointSamplingOutput::PointCloud && context.JobCommands.Available())
                if (auto busy = GPD::MeshSupport::ActiveOutputJobRefusal(context, identity, "Point sampling"))
                {
                    result.Status = EditorCommandStatus::Pending;
                    result.Message = std::move(busy->Message);
                    return result;
                }
            if (config.Backend == PointSamplingBackend::Cpu || !result.BackendDiagnostic.empty())
            {
                const auto start = std::chrono::steady_clock::now();
                const PS::Result order = PS::Order(std::span<const glm::vec3>(captured->World), params, count);
                result.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                return finish(Publish(context, *captured, config, count, order, std::move(result)));
            }

            // Vulkan: bounded framed chunks, the prefix checked against the CPU before publishing.
            auto* residency = context.SpatialIndices->PropertyResidency();
            if (!residency) return finish(Failure(EditorCommandStatus::GeometryProcessingFailed, "GPU property residency unavailable."));
            const auto before = residency->Stats();
            const auto positions = ResolveGpuPropertyInput(*residency, *context.Scene, context.World, captured->Entity, config.Positions);
            std::optional<Graphics::GpuPropertyView> weights;
            if (!config.WeightsName.empty())
                weights = ResolveGpuPropertyInput(*residency, *context.Scene, context.World, captured->Entity,
                    {.Domain = config.Positions.Domain, .Name = config.WeightsName, .ValueKind = Geometry::PropertyValueKind::Float});
            result.GpuInputUploadBytes = residency->Stats().UploadBytes - before.UploadBytes;
            result.GpuInputCacheHits = residency->Stats().Hits - before.Hits;
            if (!positions || (!config.WeightsName.empty() && !weights))
            {
                result.Status = EditorCommandStatus::GeometryProcessingFailed;
                result.Message = "Resident sampling input acquisition refused; nothing was changed.";
                return finish(result);
            }
            Graphics::FarthestPointGpuInput input{.Positions = *positions, .Weights = weights.value_or(Graphics::GpuPropertyView{}),
                .Rows = captured->Input.Slots, .FirstIndex = params.FirstIndex, .Count = std::uint32_t(count)};
            // Every row live: the shader's identity path needs no row map upload.
            if (captured->Input.Slots.size() == positions->Layout.Count) input.Rows = {};
            const auto model = ModelMatrix(captured->Transform ? &*captured->Transform : nullptr);
            for (std::size_t column = 0; column < 4; ++column)
                for (std::size_t row = 0; row < 4; ++row) input.Model[4 * column + row] = model[column][row];
            auto run = std::make_shared<PointSamplingGpuRun>(*context.SpatialIndices, input, captured->World, params);
            const auto started = std::chrono::steady_clock::now();
            auto queued = result;
            queued.Message = "Vulkan point sampling queued.";
            // `onComplete` stays with `finish`: this operation reports immediate answers through it too.
            const GPD::MeshSupport::QueuedJobDelivery<EditorPointSamplingResult> delivery{
                context, onComplete, std::move(queued), "Vulkan point sampling"};
            // Everything the samples depend on: positions, deletions, weights and the world frame.
            const auto current = [context, captured] {
                if (!GPD::EditorProcessingContextWorldCurrent(context) ||
                    !GPD::GeometryPropertiesCurrent(context, captured->Entity, captured->Input.Inputs))
                    return false;
                auto& raw = context.Scene->Raw();
                if (const auto* hierarchy = raw.try_get<ECSC::Hierarchy::Component>(captured->Entity);
                    hierarchy != nullptr && raw.valid(hierarchy->Parent))
                    return false;
                const auto* transform = raw.try_get<ECSC::Transform::Component>(captured->Entity);
                return transform == nullptr ? !captured->Transform
                                            : captured->Transform &&
                                                  EditorFeatureDetail::SameTransformComponent(*transform, *captured->Transform);
            };
            JobDesc job = EditorFeatureDetail::MakeFramedGpuJobDesc({
                .DebugName = "Vulkan point sampling", .Scope = context.World, .Current = current,
                .Queue = [context, run] { return run->QueueNext(*context.SpatialIndices); },
                .Observe = [run](const SpatialGpuResult& chunk) { return run->Observe(chunk); },
                .Publish = [context, captured, config, count, params, run, result, delivery, started](
                               const SpatialGpuResult* gpu) {
                    auto final = result;
                    std::string why;
                    PS::Result order;
                    if (gpu == nullptr || gpu->State != SpatialQueryState::Ready)
                    {
                        final.Status = EditorCommandStatus::GeometryProcessingFailed;
                        final.Message = gpu && !gpu->Diagnostic.empty() ? gpu->Diagnostic : "GPU sampling submission refused.";
                        return delivery.Publish(std::move(final));
                    }
                    if (run->Current().Order.size() != count)
                        why = "The Vulkan sampler returned an incomplete order; sampling ran on the CPU.";
                    else if (!run->VerifyPrefix(why)) {}
                    if (why.empty())
                    {
                        order = run->Current();
                        final.Backend = std::string(kPointSamplingGpuBackendId);
                    }
                    else
                    {
                        final.BackendDiagnostic = std::move(why);
                        order = PS::Order(std::span<const glm::vec3>(captured->World), params, count);
                    }
                    final.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
                    return delivery.Publish(Publish(context, *captured, config, count, order, std::move(final)));
                },
                .Abandon = [delivery] { delivery.Finalize(); }});
            if (!context.JobCommands.Submit(std::move(job), identity).IsValid())
                return finish(delivery.Rejected());
            return delivery.Pending();
        }
    }

    ActionReadiness PreviewEditorPointSamplingCommand(const EditorProcessingCommands& commands,
                                                      const PointSamplingOperationConfig& config)
    {
        auto resolved = config;
        const auto failure = Capture(EditorProcessingCommandsAccess::Resolve(commands), resolved, false, nullptr);
        return {!failure.has_value(), failure ? failure->Message : std::string{}};
    }

    EditorPointSamplingResult ApplyEditorPointSamplingCommand(const EditorProcessingCommands& commands,
                                                              const PointSamplingOperationConfig& config,
                                                              std::function<void(EditorPointSamplingResult)> onComplete)
    {
        return Apply(EditorProcessingCommandsAccess::Resolve(commands), config, std::move(onComplete));
    }

    EditorPointSamplingResult ApplyEditorConfiguredPointSampling(const EditorProcessingCommands& commands,
                                                                 std::function<void(EditorPointSamplingResult)> onComplete)
    {
        const auto config = GetEditorPointSamplingConfig(commands);
        if (!config)
        {
            auto failure = Failure(EditorCommandStatus::InvalidProcessingParameters, "The sandbox.point_sampling section is unavailable.");
            if (onComplete) onComplete(failure);
            return failure;
        }
        return ApplyEditorPointSamplingCommand(commands, *config, std::move(onComplete));
    }

    RuntimeEngineConfigApplyResult ApplyEditorPointSamplingConfig(const EditorProcessingCommands& commands,
                                                                 const PointSamplingOperationConfig& config,
                                                                 std::string sourceId)
    {
        return ApplyEditorProcessingConfig(commands,
            ValidatePointSamplingOperationConfigSection(SerializePointSamplingOperationConfig(config), {},
                                                        kPointSamplingConfigSectionName),
            sourceId.empty() ? std::string{kPointSamplingConfigSectionName} : sourceId,
            [&](Core::Config::EngineConfig& candidate) { SetPointSamplingOperationConfig(candidate, config); });
    }

    std::optional<PointSamplingOperationConfig> GetEditorPointSamplingConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return std::nullopt;
        return GetPointSamplingOperationConfig(context.EngineConfigControlState->ActiveConfig);
    }
}
