module;
#include <string_view>
#include <functional>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <utility>
#include <glm/vec3.hpp>
#include <entt/entity/registry.hpp>
module Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Core.Error;
import Geometry.PointCloud.Utils;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.WorldHandle;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.Runtime.SpatialIndexCache;
import Extrinsic.Runtime.EngineConfigControl;
import Extrinsic.Runtime.KernelEvents;
import Geometry.Properties;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"

// Kernel density and point spacing each publish one kNN-derived scalar per live
// sample. They share capture, index admission, job lifecycle and publication;
// the two method records below keep config, statistics and self-neighbor floors.
namespace Extrinsic::Runtime
{
    namespace
    {
        namespace PC = Geometry::PointCloud;
        using namespace GeometryProcessingDetail;

        std::string Join(std::initializer_list<std::string_view> parts)
        {
            std::string text;
            for (const auto part : parts) text += part;
            return text;
        }

        // Evaluate receives compact kNN rows, or null for the octree reference path.
        struct KernelDensityMethod
        {
            using Config = KernelDensityConfig;
            using Result = EditorKernelDensityResult;
            using Backend = KernelDensityBackend;
            // Noun/Lower compose diagnostics, job names and completion text.
            static constexpr std::string_view Noun{"Density"}, Lower{"density"}, Method{"Kernel density"},
                History{"Estimate kernel density"}, Evaluation{"CPU bandwidth/Gaussian evaluation"},
                Failure{"Kernel density failed: invalid neighborhoods or unrepresentable float kernel values/bandwidth."};
            static constexpr std::size_t MinimumK = 2; // Query min(n,max(k,2)+1) including self.
            static auto& Output(auto& c) { return c.Density; }
            static Result Initial(const Config& c) { return {.RequestedBackend = c.Backend, .Density = c.Density}; }
            static auto Validate(const Config& c)
            {
                return ValidateKernelDensityConfigSection(
                    SerializeKernelDensityConfig(c), {}, kKernelDensityConfigSectionName);
            }
            static std::optional<std::vector<float>> Evaluate(const Config& c, std::span<const glm::vec3> points,
                const std::vector<std::uint32_t>* neighbors, Result& r)
            {
                const PC::KDEParams params{.KNeighbors = c.KNeighbors, .Bandwidth = c.Bandwidth};
                auto analysis = neighbors ? PC::EstimateKernelDensityFromNeighbors(points, *neighbors, params)
                                          : PC::EstimateKernelDensity(points, params);
                if (!analysis || analysis->Densities.size() != points.size()) return std::nullopt;
                r.UsedBandwidth = analysis->UsedBandwidth; r.MeanDensity = analysis->MeanDensity;
                r.MinDensity = analysis->MinDensity; r.MaxDensity = analysis->MaxDensity;
                return std::move(analysis->Densities);
            }
        };
        struct PointSpacingMethod
        {
            using Config = PointSpacingConfig;
            using Result = EditorPointSpacingResult;
            using Backend = PointSpacingBackend;
            static constexpr std::string_view Noun{"Radii"}, Lower{"radii"}, Method{"Point spacing"},
                History{"Estimate point spacing and radii"}, Evaluation{"CPU spacing/radius evaluation"},
                Failure{"Point spacing failed: invalid neighborhoods or unrepresentable float distances/radii."};
            static constexpr std::size_t MinimumK = 1; // Query min(n,max(k,1)+1) including self.
            static auto& Output(auto& c) { return c.Radii; }
            static Result Initial(const Config& c) { return {.RequestedBackend = c.Backend, .Radii = c.Radii}; }
            static auto Validate(const Config& c)
            {
                return ValidatePointSpacingConfigSection(
                    SerializePointSpacingConfig(c), {}, kPointSpacingConfigSectionName);
            }
            static std::optional<std::vector<float>> Evaluate(const Config& c, std::span<const glm::vec3> points,
                const std::vector<std::uint32_t>* neighbors, Result& r)
            {
                const PC::RadiusEstimationParams params{.KNeighbors = c.KNeighbors, .ScaleFactor = c.ScaleFactor};
                auto analysis = neighbors ? PC::EstimateRadiiFromNeighbors(points, *neighbors, params)
                                          : PC::EstimateRadii(points, params);
                if (!analysis || analysis->Radii.size() != points.size()) return std::nullopt;
                r.MeanRadius = analysis->AverageRadius; r.MinRadius = analysis->MinRadius;
                r.MaxRadius = analysis->MaxRadius; r.Centroid = analysis->Statistics.Centroid;
                r.AverageSpacing = analysis->Statistics.AverageSpacing;
                r.MinSpacing = analysis->Statistics.MinSpacing;
                r.MaxSpacing = analysis->Statistics.MaxSpacing;
                r.BoundingBoxDiagonal = analysis->Statistics.BoundingBoxDiagonal;
                return std::move(analysis->Radii);
            }
        };

        template <class M>
        struct PointFieldWork : PointScalarCapture
        {
            typename M::Config Config{};
            entt::entity Entity{};
            std::shared_ptr<const SpatialIndexSnapshot> Index{};
            SpatialIndexHandle GpuIndex{};
            PointKnnRows Neighbors{};
            bool Abandoned{};
            typename M::Result Result{};
        };
        template <class M>
        std::uint32_t NeighborWidth(const PointFieldWork<M>& w)
        {
            return std::uint32_t(std::min<std::size_t>(
                w.Points.size(), std::max<std::size_t>(w.Config.KNeighbors, M::MinimumK) + 1));
        }
        enum class CapturePurpose { Execute, Readiness };
        template <class M>
        std::shared_ptr<PointFieldWork<M>> Capture(const EditorProcessingContext& context,
            typename M::Config c, std::string& diagnostic, CapturePurpose purpose = CapturePurpose::Execute)
        {
            using Backend = typename M::Backend;
            auto fail = [&](std::string why) -> std::shared_ptr<PointFieldWork<M>> { diagnostic = std::move(why); return {}; };
            const auto validation = M::Validate(c);
            if (!validation.Usable()) return fail(validation.Diagnostics.front().Message);
            if (!context.Scene) return fail("Scene is unavailable.");
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), c.StableEntityId);
            if (!entity) return fail(Join({M::Noun, " target entity is stale or missing."}));
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            auto w = std::make_shared<PointFieldWork<M>>();
            if (!CapturePointScalarField(context, *entity, a, c.Positions, M::Output(c), M::Noun,
                                        purpose == CapturePurpose::Execute, *w, diagnostic)) return {};
            w->Config = c; w->Entity = *entity;
            w->Result = M::Initial(c);
            w->Result.SlotCount = w->SlotCount; w->Result.LiveCount = w->LiveCount;
            if (w->Result.LiveCount < 2) return fail(Join({M::Method, " requires at least two live samples."}));
            if (c.Backend != Backend::CpuOctree)
            {
                if (!context.SpatialIndices || !w->ValidLbvh || w->Result.LiveCount > (1u << 24))
                    return fail("LBVH requires the spatial cache, at most 2^24 samples and coordinates within 1e18.");
                if (c.Backend == Backend::VulkanLBVH)
                {
                    if (!context.JobCommands.Available() || !context.SpatialIndices->GpuQueriesAvailable())
                        return fail(Join({"Vulkan ", M::Lower,
                            " neighborhoods require the framed spatial cache and job service."}));
                    if (w->Result.LiveCount > (1u << 20) ||
                        c.KNeighbors > 63)
                        return fail(Join({"Vulkan ", M::Lower,
                            " queries support at most 2^20 live samples and k<=63 (64 candidates including self)."}));
                }
            }
            return w;
        }
        template <class M>
        void Compute(PointFieldWork<M>& w)
        {
            using Backend = typename M::Backend;
            const auto started = std::chrono::steady_clock::now();
            const auto& c = w.Config;
            auto& r = w.Result;
            r.Status = EditorCommandStatus::GeometryProcessingFailed;
            r.ActualBackend = ToString(c.Backend);
            const std::vector<std::uint32_t>* neighbors = nullptr;
            if (c.Backend != Backend::CpuOctree)
            {
                if (c.Backend == Backend::CpuLBVH &&
                    !AppendPointKnnRows(*w.Index, w.Points, NeighborWidth(w), w.Neighbors.Indices, r.Message))
                    return;
                neighbors = &w.Neighbors.Indices;
            }
            const auto values = M::Evaluate(c, w.Points, neighbors, r);
            if (!values) { r.Message = M::Failure; return; }
            for (std::size_t i = 0; i < w.Slots.size(); ++i) w.AfterValues[w.Slots[i]] = (*values)[i];
            r.WrittenCount = w.Slots.size(); r.Status = EditorCommandStatus::Applied;
            r.CpuComputeMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            r.Message = Join({M::Noun, " computed using ", r.ActualBackend, " neighborhoods and ", M::Evaluation, "."});
        }
        template <class M>
        bool AdvanceGpu(const EditorProcessingContext& context, PointFieldWork<M>& w)
        {
            if (w.Abandoned || !PointScalarFieldCurrent(context, w.Entity, w))
            {
                w.Result.Status = EditorCommandStatus::StaleEntity;
                w.Result.Message = Join({M::Noun, " inputs changed or the job was cancelled."});
                w.Neighbors.Batch.reset();
                return true;
            }
            const auto state = AdvancePointKnnRows(*context.SpatialIndices, w.GpuIndex,
                w.Points, w.Slots, NeighborWidth(w), w.Config.GpuQueryBatchSize, w.Neighbors, w.Result.Message);
            w.Result.GpuQueryBatches = w.Neighbors.QueryBatches;
            w.Result.GpuNeighborhoodMilliseconds = w.Neighbors.Milliseconds;
            if (state == KnnRowsState::Failed) w.Result.Status = EditorCommandStatus::GeometryProcessingFailed;
            return state != KnnRowsState::Pending;
        }
        template <class M>
        typename M::Result Publish(const EditorProcessingContext& context, const std::shared_ptr<PointFieldWork<M>>& w)
        {
            auto& r = w->Result;
            if (!PointScalarFieldCurrent(context, w->Entity, *w))
            {
                r.Status = EditorCommandStatus::StaleEntity;
                r.Message = Join({M::Noun, " input or output changed before publication."});
                return r;
            }
            if (r.Status != EditorCommandStatus::Applied) return r;
            const auto status = PublishPointScalarField(context, w->Entity, *w, std::string(M::History));
            r.Status = status == EditorCommandHistoryStatus::InvalidCommand
                ? EditorCommandStatus::InvalidProcessingParameters
                : EditorFeatureDetail::ToEditorCommandStatus(status);
            if (!r.Succeeded()) r.Message = status == EditorCommandHistoryStatus::InvalidCommand
                ? "Output values are not exactly representable in the selected scalar storage."
                : Join({M::Noun, " publication rejected by history checks."});
            return r;
        }
        template <class M>
        ActionReadiness Preview(const EditorProcessingCommands& commands, const typename M::Config& config)
        {
            const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
            std::string diagnostic;
            const auto work = Capture<M>(context, config, diagnostic, CapturePurpose::Readiness);
            return {bool(work), std::move(diagnostic)};
        }
        template <class M>
        typename M::Result Apply(const EditorProcessingCommands& commands, const typename M::Config& config,
                                 std::function<void(typename M::Result)> onComplete)
        {
            using Backend = typename M::Backend;
            const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
            std::string diagnostic;
            auto w = Capture<M>(context, config, diagnostic);
            const auto report = [&config, &w](EditorCommandStatus status, std::string message) {
                auto result = w ? w->Result : M::Initial(config);
                result.Status = status;
                result.Message = std::move(message);
                return result;
            };
            if (!w)
                return report(EditorCommandStatus::InvalidProcessingParameters, std::move(diagnostic));
            if (w->Config.Backend != Backend::CpuOctree)
            {
                const auto indexState = AcquirePointIndex(
                    *context.SpatialIndices, context.World, w->Entity, w->Config.Positions,
                    w->Slots, w->Points, w->GpuIndex, w->Index, w->Result.IndexReused, diagnostic);
                if (indexState == PointIndexState::Unavailable)
                    return report(EditorCommandStatus::InvalidProcessingParameters, diagnostic);
                if (indexState == PointIndexState::Mismatched)
                    return report(EditorCommandStatus::StaleEntity,
                                  Join({M::Noun, " index snapshot does not match the selected samples."}));
            }
            if (!context.JobCommands.Available())
            {
                Compute(*w);
                return Publish(context, w);
            }
            const auto& output = M::Output(w->Config);
            const EditorJobIdentity identity{.EntityId = config.StableEntityId,
                                             .Scope = ToEditorJobScope(output.Domain),
                                             .OutputSemantic = GeometryPresentationSlotSemantic::ScalarField,
                                             .OutputName = output.Name};
            if (auto active = MeshSupport::FindActiveEditorJob(context, identity);
                active && IsActiveEditorJobState(active->State))
                return report(EditorCommandStatus::Pending,
                              Join({"A ", M::Lower, " job for this output is already active."}));
            auto sink = GuardEditorProcessingResult(context, std::move(onComplete));
            auto delivered = std::make_shared<bool>(false);
            auto pending = w->Result;
            pending.Status = EditorCommandStatus::Pending;
            pending.Message = Join({M::Noun, " estimation queued."});
            JobDesc desc{
                .DebugName = Join({M::Noun, " estimation"}),
                .Scope = context.World,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .Work = [w](const JobCancellation &) -> JobResultEnvelope {
                    Compute(*w);
                    return JobResultEnvelope::Make(true);
                },
                .ValidateBeforeApply =
                    [context, w] {
                        return PointScalarFieldCurrent(context, w->Entity, *w) ? JobApplyValidation::Current
                                                                               : JobApplyValidation::StaleGeneration;
                    },
                .PublishCompletion =
                    [context, w, sink, delivered](KernelEventBus &, const JobResultEnvelope &) {
                        auto result = Publish(context, w);
                        *delivered = true;
                        if (sink)
                            sink(result);
                        return result.Succeeded();
                    },
                .FinalizeUnpublishedOnMainThread =
                    [sink, delivered, w, pending]() mutable {
                        w->Abandoned = true;
                        if (sink && !*delivered)
                        {
                            if (w->Result.Status == EditorCommandStatus::GeometryProcessingFailed)
                                pending = w->Result;
                            else
                            {
                                pending.Status = EditorCommandStatus::StaleEntity;
                                pending.Message = Join({M::Noun,
                                    " job was cancelled or its source became stale; previous output retained."});
                            }
                            sink(std::move(pending));
                        }
                    }};
            if (w->Config.Backend == Backend::VulkanLBVH)
            {
                JobDesc gpu{
                    .DebugName = Join({M::Noun, " neighborhoods (Vulkan)"}), .Scope = context.World,
                    .Kind = RuntimeTaskKinds::GeometryProcess,
                    .Work = [](const JobCancellation&) { return JobResultEnvelope::Make(true); },
                    .IsReadyToApply = [context, w] { return AdvanceGpu(context, *w); },
                    .PublishCompletion = [w](KernelEventBus&, const JobResultEnvelope&) { return w->Neighbors.Finished; },
                    .FinalizeUnpublishedOnMainThread = [w] { w->Abandoned = true; }};
                const auto prerequisite = context.JobCommands.Submit(std::move(gpu), identity);
                if (!prerequisite.IsValid())
                    return report(EditorCommandStatus::GeometryProcessingFailed,
                                  Join({"GPU ", M::Lower, " job submission was rejected."}));
                desc.DependsOn.push_back({prerequisite,
                    Join({"Complete Vulkan ", M::Lower, " neighborhoods before ", M::Evaluation})});
            }
            const auto token = context.JobCommands.Submit(std::move(desc), identity);
            if (!token.IsValid())
            {
                w->Abandoned = true;
                pending.Status = EditorCommandStatus::GeometryProcessingFailed;
                pending.Message = Join({M::Noun, " job submission was rejected."});
            }
            return pending;
        }
        template <class M>
        typename M::Result ApplyConfigured(const EditorProcessingCommands& commands,
            const std::optional<typename M::Config>& config, std::function<void(typename M::Result)> onComplete)
        {
            if (!config)
                return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                        .Message = Join({M::Noun, " estimation config is unavailable."})};
            return Apply<M>(commands, *config, std::move(onComplete));
        }
    }
    ActionReadiness PreviewEditorKernelDensityCommand(
        const EditorProcessingCommands& commands, const KernelDensityConfig& config)
    {
        return Preview<KernelDensityMethod>(commands, config);
    }
    GeometryPropertyCatalogSnapshot GetEditorKernelDensityInputCatalog(
        const EditorProcessingCommands& commands, std::uint32_t id)
    {
        return GeometryProcessingDetail::BuildPointInputCatalog(EditorProcessingCommandsAccess::Resolve(commands), id, 2);
    }
    EditorKernelDensityResult ApplyEditorKernelDensityCommand(const EditorProcessingCommands& commands,
        const KernelDensityConfig& config, std::function<void(EditorKernelDensityResult)> onComplete)
    {
        return Apply<KernelDensityMethod>(commands, config, std::move(onComplete));
    }
    EditorKernelDensityResult ApplyEditorConfiguredKernelDensity(
        const EditorProcessingCommands& commands, std::function<void(EditorKernelDensityResult)> onComplete)
    {
        return ApplyConfigured<KernelDensityMethod>(
            commands, GetEditorKernelDensityConfig(commands), std::move(onComplete));
    }
    ActionReadiness PreviewEditorPointSpacingCommand(
        const EditorProcessingCommands& commands, const PointSpacingConfig& config)
    {
        return Preview<PointSpacingMethod>(commands, config);
    }
    GeometryPropertyCatalogSnapshot GetEditorPointSpacingInputCatalog(
        const EditorProcessingCommands& commands, std::uint32_t id)
    {
        return GeometryProcessingDetail::BuildPointInputCatalog(EditorProcessingCommandsAccess::Resolve(commands), id, 2);
    }
    EditorPointSpacingResult ApplyEditorPointSpacingCommand(const EditorProcessingCommands& commands,
        const PointSpacingConfig& config, std::function<void(EditorPointSpacingResult)> onComplete)
    {
        return Apply<PointSpacingMethod>(commands, config, std::move(onComplete));
    }
    EditorPointSpacingResult ApplyEditorConfiguredPointSpacing(
        const EditorProcessingCommands& commands, std::function<void(EditorPointSpacingResult)> onComplete)
    {
        return ApplyConfigured<PointSpacingMethod>(
            commands, GetEditorPointSpacingConfig(commands), std::move(onComplete));
    }
} // namespace Extrinsic::Runtime
