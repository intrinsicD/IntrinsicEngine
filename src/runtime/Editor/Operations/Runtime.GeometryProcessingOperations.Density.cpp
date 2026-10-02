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
import Extrinsic.Graphics.PointScalarAnalysis;
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
#include "Editor/Operations/Runtime.GeometryProcessingOperations.JobFailure.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.RadiusRows.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.GpuScalar.hpp"

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
            std::size_t size = 0;
            for (const auto part : parts) size += part.size();
            std::string text;
            text.reserve(size);
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
            static Graphics::PointScalarGpuParams Params(const Config& c) { return {.Method=0,.K=c.KNeighbors,.Bandwidth=c.Bandwidth}; }
            static void Stats(Result& r,const Graphics::PointScalarGpuStats& s) { r.UsedBandwidth=s.Bandwidth;r.MeanDensity=s.Mean;r.MinDensity=s.Minimum;r.MaxDensity=s.Maximum; }
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
            static Graphics::PointScalarGpuParams Params(const Config& c) { return {.Method=1,.K=c.KNeighbors,.Scale=c.ScaleFactor}; }
            static void Stats(Result& r,const Graphics::PointScalarGpuStats& s) { r.MeanRadius=s.Mean;r.MinRadius=s.Minimum;r.MaxRadius=s.Maximum;
                r.AverageSpacing=s.AverageSpacing;r.MinSpacing=s.MinSpacing;r.MaxSpacing=s.MaxSpacing;r.BoundingBoxDiagonal=s.Diagonal;r.Centroid={s.CentroidX,s.CentroidY,s.CentroidZ}; }
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
                    if(!AdmitPointScalarGpu(context,*w,M::Params(c),diagnostic))return {};
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
        template<class M>
        void FoldGpu(typename M::Result& r,const EditorPointScalarTransactionSnapshot& s)
        {
            r.Status=s.Status;r.Message=s.Message;r.LiveCount=s.LiveCount;r.ActualBackend=s.GpuQueryBatches?"vulkan_lbvh":"";
            r.GpuInputUploadBytes=s.GpuInputUploadBytes;r.GpuInputCacheHits=s.GpuInputCacheHits;r.CpuStageReadbackBytes=s.CpuStageReadbackBytes;
            r.GpuQueryBatches=s.GpuQueryBatches;r.IndexReused=s.IndexReused;M::Stats(r,s.Statistics);if(s.Phase==EditorGpuTransactionPhase::Applied)r.WrittenCount=r.LiveCount;
        }
        template<class M>
        EditorPointScalarTransactionHandle StartGpu(const EditorProcessingContext& ctx,const std::shared_ptr<PointFieldWork<M>>& w,
            typename M::Result& result,std::function<void(typename M::Result)> sink,bool automatic,Graphics::GpuPropertyResidency* test=nullptr, const EditorPointScalarTransactionSnapshot& diagnostics = {})
        {
            EditorPointScalarTransactionSnapshot state;
            auto run=StartPointScalarGpu(ctx,w,w->Entity,w->Config.StableEntityId,w->Config.Positions,M::Params(w->Config),std::string(M::History),state,
                [w,sink=std::move(sink)](EditorPointScalarTransactionSnapshot s){FoldGpu<M>(w->Result,s);if(sink)sink(w->Result);},automatic,test,diagnostics);
            FoldGpu<M>(w->Result,state);result=w->Result;return run;
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
            if(w->Config.Backend==Backend::VulkanLBVH){typename M::Result result; (void)StartGpu<M>(context,w,result,std::move(onComplete),true);return result;}
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
            const std::string label = Join({M::Noun, " estimation"});
            if (auto busy = MeshSupport::ActiveOutputJobRefusal(context, identity, label))
                return report(EditorCommandStatus::Pending, std::move(busy->Message));
            auto queued = w->Result;
            queued.Message = label + " queued.";
            const MeshSupport::QueuedJobDelivery<typename M::Result> delivery{
                context, std::move(onComplete), std::move(queued), label};
            JobDesc desc{
                .DebugName = label,
                .Scope = context.World,
                .Kind = RuntimeTaskKinds::GeometryProcess,
                .Work = [w](const JobCancellation &) -> JobResultEnvelope {
                    Compute(*w);
                    return JobResultEnvelope::Make(true);
                },
                .ValidateBeforeApply = [context, w] {
                    return MeshSupport::ValidateQueuedJob(w->Abandoned, PointScalarFieldCurrent(context, w->Entity, *w));
                },
                .PublishCompletion = [context, w, delivery](KernelEventBus &, const JobResultEnvelope &) {
                    return delivery.Publish(Publish(context, w));
                },
                .FinalizeUnpublishedOnMainThread = [w, delivery] {
                    w->Abandoned = true;
                    delivery.FinalizeAfterWorker(w->Result);
                }};
            if (!context.JobCommands.Submit(std::move(desc), identity).IsValid())
            {
                w->Abandoned = true;
                return delivery.Rejected();
            }
            return delivery.Pending();
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
    void UpdateEditorPointScalarResult(EditorKernelDensityResult& result, const EditorPointScalarTransactionSnapshot& snapshot)
    { FoldGpu<KernelDensityMethod>(result,snapshot); }
    EditorPointScalarTransactionHandle StartEditorKernelDensityTransaction(const EditorProcessingCommands& commands,const KernelDensityConfig& config,
        EditorKernelDensityResult& result,std::function<void(EditorKernelDensityResult)> sink)
    {
        const auto& ctx=EditorProcessingCommandsAccess::Resolve(commands);std::string why;
        auto w=Capture<KernelDensityMethod>(ctx,config,why);
        if(!w||config.Backend!=KernelDensityBackend::VulkanLBVH){result=KernelDensityMethod::Initial(config);result.Status=EditorCommandStatus::InvalidProcessingParameters;result.Message=w?"GPU transaction requires vulkan_lbvh.":why;return {};}
        return StartGpu<KernelDensityMethod>(ctx,w,result,std::move(sink),false);
    }
    EditorPointScalarTransactionHandle MakeEditorKernelDensityTransactionForTest(const EditorProcessingCommands& commands,const KernelDensityConfig& config,
        std::vector<float> values,Graphics::GpuPropertyResidency& residency, const EditorPointScalarTransactionSnapshot& diagnostics)
    {
        const auto& ctx=EditorProcessingCommandsAccess::Resolve(commands);auto cpu=config;cpu.Backend=KernelDensityBackend::CpuOctree;std::string why;
        auto w=Capture<KernelDensityMethod>(ctx,cpu,why);if(!w||values.size()!=w->SlotCount)return {};
        w->Config.Backend=config.Backend;w->Result.RequestedBackend=config.Backend;w->AfterValues=std::move(values);EditorKernelDensityResult result;
        return StartGpu<KernelDensityMethod>(ctx,w,result,{},false,&residency,diagnostics);
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
    void UpdateEditorPointScalarResult(EditorPointSpacingResult& result, const EditorPointScalarTransactionSnapshot& snapshot)
    { FoldGpu<PointSpacingMethod>(result,snapshot); }
    EditorPointScalarTransactionHandle StartEditorPointSpacingTransaction(const EditorProcessingCommands& commands,const PointSpacingConfig& config,
        EditorPointSpacingResult& result,std::function<void(EditorPointSpacingResult)> sink)
    {
        const auto& ctx=EditorProcessingCommandsAccess::Resolve(commands);std::string why;
        auto w=Capture<PointSpacingMethod>(ctx,config,why);
        if(!w||config.Backend!=PointSpacingBackend::VulkanLBVH){result=PointSpacingMethod::Initial(config);result.Status=EditorCommandStatus::InvalidProcessingParameters;result.Message=w?"GPU transaction requires vulkan_lbvh.":why;return {};}
        return StartGpu<PointSpacingMethod>(ctx,w,result,std::move(sink),false);
    }
    EditorPointScalarTransactionHandle MakeEditorPointSpacingTransactionForTest(const EditorProcessingCommands& commands,const PointSpacingConfig& config,
        std::vector<float> values,Graphics::GpuPropertyResidency& residency, const EditorPointScalarTransactionSnapshot& diagnostics)
    {
        const auto& ctx=EditorProcessingCommandsAccess::Resolve(commands);auto cpu=config;cpu.Backend=PointSpacingBackend::CpuOctree;std::string why;
        auto w=Capture<PointSpacingMethod>(ctx,cpu,why);if(!w||values.size()!=w->SlotCount)return {};
        w->Config.Backend=config.Backend;w->Result.RequestedBackend=config.Backend;w->AfterValues=std::move(values);EditorPointSpacingResult result;
        return StartGpu<PointSpacingMethod>(ctx,w,result,{},false,&residency,diagnostics);
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
