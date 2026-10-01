// Editor-backed agent operations: every entry calls the same runtime query, command or
// config path the Sandbox panels use. JSON stays private to this unit; accessors never
// throw (the build has no exceptions), so every field is type-checked before use.
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

module Extrinsic.Runtime.AgentOperations;

import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Logging;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.PointSamplingOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Runtime.PointCloudConsolidationTypes;
import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.GeometryProperty.Types;
import Geometry.Properties.Types;

#include "Agent/internal/Runtime.AgentOperations.Detail.hpp"

namespace Extrinsic::Runtime
{
    using namespace AgentDetail;
    namespace
    {
        // ---- configured operations -------------------------------------------------------
        // One table over the editor commands that run on their config section: `run_operation` and
        // `preview_operation` select a row by name. A section that carries an entity supplies it
        // (Config); otherwise the call names it (Argument). Each row calls the same Preview*/Apply*
        // path as its panel.
        enum class OperationEntity : std::uint8_t { Config, Argument };
        struct OperationCall
        {
            const AgentOperationContext& Context;
            const Json& Args;
            std::optional<std::uint32_t> Entity; // set for Argument rows
            bool Preview;
        };
        struct ConfiguredOperation
        {
            const char* Name;
            const char* Family; // "mesh_field" (also served by run_mesh_operation), "mesh_analysis", "point"
            OperationEntity Entity;
            const char* Section; // the config section the settings (and a Config row's entity) come from
            std::function<AgentOperationOutcome(const OperationCall&)> Run;
        };

        Json OperationResultJson(const char* name, Json result)
        {
            result["operation"] = name;
            return result;
        }
        AgentOperationOutcome NoReadinessCheck(const char* name)
        {
            return Ok({{"operation", name}, {"enabled", nullptr},
                       {"reason", "This operation has no readiness check; run it to find out."}});
        }
        template <class Result>
        Result MissingSection(const char* section)
        {
            return {.Status = EditorCommandStatus::InvalidProcessingParameters,
                    .Message = std::string("The ") + section + " section is unavailable or invalid."};
        }

        // The entity comes from the section: get(commands) -> optional<Config>, preview(commands, config),
        // apply(commands, onComplete) -> Result.
        template <class Result, class Frame, class Get, class Preview, class Apply>
        ConfiguredOperation ConfigSourced(const char* name, const char* family, const char* section, Frame frame, Get get,
                                          Preview preview, Apply apply)
        {
            return {name, family, OperationEntity::Config, section,
                    [=](const OperationCall& call) -> AgentOperationOutcome {
                        const auto commands = frame(*call.Context.Attachment).Commands;
                        if (call.Preview)
                        {
                            const auto config = get(commands);
                            if (!config)
                                return Ok(ReadinessJson(commands, {false, std::string("The ") + section + " section is unavailable."},
                                                        {{"operation", name}}));
                            return Ok(ReadinessJson(commands, preview(commands, *config), {{"operation", name}}));
                        }
                        return FinishApply<Result>([&](auto onComplete) { return apply(commands, std::move(onComplete)); },
                                                   [name](const Result& r) { return OperationResultJson(name, ResultJson(r)); });
                    }};
        }
        // The call names the entity; the settings come from the section. preview(commands, entity) answers
        // nullopt for a command without a readiness check; apply(commands, entity, onComplete) -> Result
        // (synchronous commands never return Pending, so their callback is never used).
        template <class Result, class Frame, class Preview, class Apply>
        ConfiguredOperation EntityOperation(const char* name, const char* family, const char* section, Frame frame,
                                            Preview preview, Apply apply)
        {
            return {name, family, OperationEntity::Argument, section,
                    [=](const OperationCall& call) -> AgentOperationOutcome {
                        const auto commands = frame(*call.Context.Attachment).Commands;
                        const std::uint32_t entity = *call.Entity;
                        if (call.Preview)
                        {
                            const std::optional<ActionReadiness> readiness = preview(commands, entity);
                            if (!readiness) return NoReadinessCheck(name);
                            return Ok(ReadinessJson(commands, *readiness, {{"operation", name}}));
                        }
                        return FinishApply<Result>([&](auto onComplete) { return apply(commands, entity, std::move(onComplete)); },
                                                   [name](const Result& r) { return OperationResultJson(name, ResultJson(r)); });
                    }};
        }
        // EntityOperation for a command that takes the section's parsed config next to the entity.
        template <class Result, class Config, class Frame, class Get, class Preview, class Apply>
        ConfiguredOperation EntityOverConfig(const char* name, const char* family, const char* section, Frame frame, Get get,
                                             Preview preview, Apply apply)
        {
            return EntityOperation<Result>(
                name, family, section, frame,
                [=](const EditorProcessingCommands& commands, std::uint32_t entity) -> std::optional<ActionReadiness> {
                    const std::optional<Config> config = get(commands);
                    if (!config) return ActionReadiness{false, std::string("The ") + section + " section is unavailable."};
                    return preview(commands, entity, *config);
                },
                [=](const EditorProcessingCommands& commands, std::uint32_t entity, auto onComplete) {
                    const std::optional<Config> config = get(commands);
                    if (!config) return MissingSection<Result>(section);
                    return apply(commands, entity, *config, std::move(onComplete));
                });
        }

        const std::vector<ConfiguredOperation>& ConfiguredOperations()
        {
            static const std::vector<ConfiguredOperation> operations = [] {
                std::vector<ConfiguredOperation> ops;
                // -- mesh fields (entity argument, settings from the section)
                ops.push_back(EntityOverConfig<EditorPropertySmoothingResult, PropertySmoothingConfig>(
                    "property_smoothing", "mesh_field", "sandbox.property_smoothing", &PrepareEditorMeshFieldFrame,
                    &GetEditorPropertySmoothingConfig, &PreviewEditorPropertySmoothingCommand,
                    [](const auto& c, std::uint32_t id, const PropertySmoothingConfig& config, auto onComplete) {
                        return ApplyEditorPropertySmoothingCommand(c, id, config, std::move(onComplete));
                    }));
                ops.push_back(EntityOverConfig<EditorLaplacianEigenbasisResult, LaplacianEigenbasisConfig>(
                    "spectral_modes", "mesh_field", "sandbox.laplacian_eigenbasis", &PrepareEditorMeshFieldFrame,
                    &GetEditorLaplacianEigenbasisConfig, &PreviewEditorLaplacianEigenbasisCommand,
                    [](const auto& c, std::uint32_t id, const LaplacianEigenbasisConfig& config, auto) {
                        return ApplyEditorLaplacianEigenbasisCommand(c, id, config);
                    }));
                ops.push_back(EntityOverConfig<EditorHarmonicFieldResult, HarmonicFieldConfig>(
                    "harmonic_field", "mesh_field", "sandbox.harmonic_field", &PrepareEditorMeshFieldFrame,
                    &GetEditorHarmonicFieldConfig, &PreviewEditorHarmonicFieldCommand,
                    [](const auto& c, std::uint32_t id, const HarmonicFieldConfig& config, auto) {
                        return ApplyEditorHarmonicFieldCommand(c, id, config);
                    }));
                ops.push_back(EntityOverConfig<EditorScalarGradientResult, ScalarGradientConfig>(
                    "scalar_gradient", "mesh_field", "sandbox.scalar_gradient", &PrepareEditorMeshFieldFrame,
                    &GetEditorScalarGradientConfig, &PreviewEditorScalarGradientCommand,
                    [](const auto& c, std::uint32_t id, const ScalarGradientConfig& config, auto) {
                        return ApplyEditorScalarGradientCommand(c, id, config);
                    }));
                // -- mesh analysis
                ops.push_back(ConfigSourced<EditorMeshCurvatureResult>(
                    "mesh_curvature", "mesh_analysis", "sandbox.mesh_curvature", &PrepareEditorMeshFieldFrame,
                    &GetEditorMeshCurvatureConfig, &PreviewEditorMeshCurvatureCommand,
                    [](const auto& c, auto onComplete) {
                        const auto config = GetEditorMeshCurvatureConfig(c);
                        if (!config) return MissingSection<EditorMeshCurvatureResult>("sandbox.mesh_curvature");
                        return ApplyEditorMeshCurvatureCommand(c, *config, std::move(onComplete));
                    }));
                ops.push_back(EntityOperation<EditorGeodesicsResult>(
                    "geodesics", "mesh_analysis", "sandbox.geodesics", &PrepareEditorMeshFieldFrame,
                    [](const auto&, std::uint32_t) { return std::optional<ActionReadiness>{}; },
                    [](const auto& c, std::uint32_t id, auto) { return ApplyEditorConfiguredGeodesicsCommand(c, id); }));
                ops.push_back(EntityOperation<EditorCurvatureSegmentationResult>(
                    "curvature_segmentation", "mesh_analysis", "sandbox.curvature_segmentation", &PrepareEditorMeshFieldFrame,
                    [](const auto& c, std::uint32_t id) -> std::optional<ActionReadiness> {
                        const auto config = GetEditorCurvatureSegmentationConfig(c);
                        if (!config) return ActionReadiness{false, "The sandbox.curvature_segmentation section is unavailable."};
                        return PreviewEditorCurvatureSegmentationCommand(c, {id, *config});
                    },
                    [](const auto& c, std::uint32_t id, auto) { return ApplyEditorConfiguredCurvatureSegmentationCommand(c, id); }));
                // -- point families (entity from the section)
                ops.push_back(ConfigSourced<EditorNormalEstimationResult>(
                    "normal_estimation", "point", "sandbox.normal_estimation", &PrepareEditorNormalFrame,
                    &GetEditorNormalEstimationConfig, &PreviewEditorNormalEstimationCommand,
                    [](const auto& c, auto onComplete) { return ApplyEditorConfiguredNormalEstimation(c, std::move(onComplete)); }));
                ops.push_back(ConfigSourced<EditorKernelDensityResult>(
                    "kernel_density", "point", "sandbox.kernel_density", &PrepareEditorPointFieldFrame,
                    &GetEditorKernelDensityConfig, &PreviewEditorKernelDensityCommand,
                    [](const auto& c, auto onComplete) { return ApplyEditorConfiguredKernelDensity(c, std::move(onComplete)); }));
                ops.push_back(ConfigSourced<EditorPointSpacingResult>(
                    "point_spacing", "point", "sandbox.point_spacing", &PrepareEditorPointFieldFrame,
                    &GetEditorPointSpacingConfig, &PreviewEditorPointSpacingCommand,
                    [](const auto& c, auto onComplete) { return ApplyEditorConfiguredPointSpacing(c, std::move(onComplete)); }));
                ops.push_back(ConfigSourced<EditorOutlierAnalysisResult>(
                    "outlier_analysis", "point", "sandbox.outlier_analysis", &PrepareEditorPointAnalysisFrame,
                    &GetEditorOutlierAnalysisConfig, &PreviewEditorOutlierAnalysisCommand,
                    [](const auto& c, auto onComplete) { return ApplyEditorConfiguredOutlierAnalysis(c, std::move(onComplete)); }));
                ops.push_back(ConfigSourced<EditorDensityWeightResult>(
                    "density_weight", "point", "sandbox.density_weights", &PrepareEditorPointAnalysisFrame,
                    &GetEditorDensityWeightConfig, &PreviewEditorDensityWeightCommand,
                    [](const auto& c, auto onComplete) { return ApplyEditorConfiguredDensityWeight(c, std::move(onComplete)); }));
                ops.push_back(ConfigSourced<EditorDescriptorAnalysisResult>(
                    "descriptor_analysis", "point", "sandbox.descriptor_analysis", &PrepareEditorPointAnalysisFrame,
                    &GetEditorDescriptorAnalysisConfig, &PreviewEditorDescriptorAnalysisCommand,
                    [](const auto& c, auto onComplete) { return ApplyEditorConfiguredDescriptorAnalysis(c, std::move(onComplete)); }));
                ops.push_back(ConfigSourced<EditorBilateralFilterResult>(
                    "bilateral_filter", "point", "sandbox.bilateral_filter", &PrepareEditorPointSetFrame,
                    &GetEditorBilateralFilterConfig, &PreviewEditorBilateralFilterCommand,
                    [](const auto& c, auto onComplete) { return ApplyEditorConfiguredBilateralFilter(c, std::move(onComplete)); }));
                ops.push_back(ConfigSourced<EditorPointConstructionResult>(
                    "point_construction", "point", "sandbox.point_construction", &PrepareEditorPointConstructionFrame,
                    &GetEditorPointConstructionConfig,
                    [](const auto& c, const PointConstructionConfig& config) {
                        const auto readiness = PreviewEditorPointConstructionCommand(c, config);
                        return ActionReadiness{readiness.Ready, readiness.Diagnostic};
                    },
                    [](const auto& c, auto onComplete) { return ApplyEditorConfiguredPointConstruction(c, std::move(onComplete)); }));
                return ops;
            }();
            return operations;
        }

        bool InFamily(const ConfiguredOperation& op, const char* family) { return family == nullptr || std::string_view(op.Family) == family; }
        std::string OperationEnum(const char* family)
        {
            std::string names;
            for (const auto& op : ConfiguredOperations())
                if (InFamily(op, family)) names += std::string(names.empty() ? "" : ",") + "\"" + op.Name + "\"";
            return "[" + names + "]";
        }
        // "name (entity from <section> | entity argument, settings from <section>)" per row.
        std::string OperationSummary(const char* family)
        {
            std::string text;
            for (const auto& op : ConfiguredOperations())
                if (InFamily(op, family))
                    text += std::string(text.empty() ? "" : "; ") + op.Name + " (" + op.Section +
                            (op.Entity == OperationEntity::Config ? "; entity from the section)" : "; entity argument)");
            return text;
        }

        // `family` narrows the table (run_mesh_operation serves only the mesh_field rows).
        AgentOperationOutcome RunConfiguredOperation(const AgentOperationContext& context, std::string_view arguments, bool preview,
                                                     const char* family)
        {
            const auto args = ParseObject(arguments);
            const auto name = args ? String(*args, "operation") : std::nullopt;
            if (!name) return Fail("Pass {\"operation\": <name>, \"entity\": <stable id, where the operation needs one>}.");
            const auto& operations = ConfiguredOperations();
            const auto op = std::ranges::find_if(operations, [&](const ConfiguredOperation& o) { return *name == o.Name && InFamily(o, family); });
            if (op == operations.end()) return Fail("Unknown operation '" + *name + "'; valid: " + OperationEnum(family) + ".");
            const auto entity = UInt(*args, "entity");
            if (op->Entity == OperationEntity::Argument && !entity)
                return Fail("Operation '" + *name + "' needs {\"entity\": <stable id>}; its settings come from " + op->Section + ".");
            if (op->Entity == OperationEntity::Config && args->contains("entity"))
                return Fail("Operation '" + *name + "' takes its entity from config section '" + op->Section +
                            "' (config_apply first); do not pass entity.");
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            return op->Run({context, *args, entity, preview});
        }

        // ---- registration (ICP, Coherent Point Drift) --------------------------------------
        Json TransformJson(const glm::dmat4& m)
        {
            Json rows = Json::array();
            for (int r = 0; r < 4; ++r) rows.push_back({m[0][r], m[1][r], m[2][r], m[3][r]});
            return rows;
        }
        Json RegistrationJson(const EditorRegistrationResult& r)
        {
            return {{"method", "icp"}, {"status", DebugNameForEditorCommandStatus(r.Status)},
                    {"succeeded", r.Succeeded()}, {"message", r.Message},
                    {"backend", ToString(r.ActualBackend)}, {"requested_backend", ToString(r.RequestedBackend)},
                    {"iterations", r.IterationsPerformed}, {"converged", r.Converged}, {"rmse", r.FinalRMSE},
                    {"inliers", r.FinalInlierCount}, {"source_points", r.SourcePointCount}, {"target_points", r.TargetPointCount}};
        }
        Json CoherentPointDriftJson(const EditorCoherentPointDriftResult& r)
        {
            constexpr std::array<std::string_view, 4> kVariants{"rigid", "affine", "nonrigid", "bayesian"};
            constexpr std::array<std::string_view, 3> kOutputs{"source_transform", "positions", "displacement_property"};
            const auto named = [](const auto& names, auto value) {
                return std::size_t(value) < names.size() ? std::string(names[std::size_t(value)]) : std::string("unknown");
            };
            return {{"method", "cpd"}, {"status", DebugNameForEditorCommandStatus(r.Status)},
                    {"succeeded", r.Succeeded()}, {"message", r.Message}, {"backend", r.Backend},
                    {"variant", named(kVariants, r.Method)}, {"output", named(kOutputs, r.Output)}, {"termination", r.Termination},
                    {"iterations", r.Iterations}, {"sigma2", r.Sigma2}, {"negative_log_likelihood", r.NegativeLogLikelihood},
                    {"matched_weight", r.MatchedWeight}, {"mean_displacement", r.MeanDisplacement},
                    {"transform", TransformJson(r.Transform)}, {"source_points", r.SourcePointCount},
                    {"target_points", r.TargetPointCount}, {"e_step_error_bound", r.EStepErrorBound},
                    {"e_step_sampled_error", r.EStepSampledError},
                    {"e_step_fallbacks", r.EStepFallbacks}, {"e_step_device_iterations", r.EStepDeviceIterations},
                    {"e_step_device_cpu_rows", r.EStepDeviceCpuRows}, {"gpu_diagnostic", r.GpuDiagnostic},
                    {"kernel_rank", r.KernelRank}, {"kernel_approximation_error", r.KernelApproximationError}};
        }
        // RUNTIME-274: the standalone sampling operation on the sandbox.point_sampling section.
        AgentOperationOutcome RunPointSampling(const AgentOperationContext& context, bool preview)
        {
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace); // prepares the session frame the feature frames read
            const auto commands = PrepareEditorRegistrationFrame(*context.Attachment).Commands;
            const auto config = GetEditorPointSamplingConfig(commands);
            if (!config) return Fail("The sandbox.point_sampling section is unavailable.");
            if (preview) return Ok(ReadinessJson(commands, PreviewEditorPointSamplingCommand(commands, *config)));
            // A Vulkan run is queued and answers once it has published or failed.
            return FinishApply<EditorPointSamplingResult>(
                [&](auto onComplete) { return ApplyEditorPointSamplingCommand(commands, *config, std::move(onComplete)); },
                [](const EditorPointSamplingResult& r) {
                    return Json{{"status", DebugNameForEditorCommandStatus(r.Status)}, {"succeeded", r.Succeeded()},
                                {"message", r.Message}, {"method", r.Method}, {"input_points", r.InputCount},
                                {"samples", r.SampleCount}, {"output_entity", r.OutputEntityId},
                                {"milliseconds", r.Milliseconds}, {"distance_pairs", r.DistancePairs},
                                {"gpu_input_upload_bytes", r.GpuInputUploadBytes}, {"gpu_input_cache_hits", r.GpuInputCacheHits},
                                {"cpu_stage_readback_bytes", r.CpuStageReadbackBytes},
                                {"requested_backend", r.RequestedBackend}, {"backend", r.Backend},
                                {"backend_diagnostic", r.BackendDiagnostic}};
                });
        }

        AgentOperationOutcome RunKeypoints(const AgentOperationContext& context)
        {
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace); // prepares the session frame the feature frames read
            const auto commands = PrepareEditorPointAnalysisFrame(*context.Attachment).Commands;
            return FinishApply<EditorKeypointAnalysisResult>(
                [&](auto onComplete) { return ApplyEditorConfiguredKeypointAnalysis(commands, std::move(onComplete)); },
                [](const EditorKeypointAnalysisResult& r) {
                    return Json{{"status", DebugNameForEditorCommandStatus(r.Status)}, {"succeeded", r.Succeeded()},
                                {"message", r.Message}, {"requested_backend", ToString(r.RequestedBackend)},
                                {"actual_backend", r.ActualBackend}, {"implementation_id", r.ImplementationId},
                                {"gpu_input_upload_bytes", r.GpuInputUploadBytes}, {"gpu_input_cache_hits", r.GpuInputCacheHits},
                                {"cpu_stage_upload_bytes", r.CpuStageUploadBytes},
                                {"cpu_stage_readback_bytes", r.CpuStageReadbackBytes},
                                {"gpu_submissions", r.GpuQueryBatches}, {"keypoints", r.KeypointCount}};
                });
        }

        AgentOperationOutcome PreviewKeypoints(const AgentOperationContext& context)
        {
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto commands = PrepareEditorPointAnalysisFrame(*context.Attachment).Commands;
            const auto config = GetEditorKeypointAnalysisConfig(commands);
            if (!config) return Ok(ReadinessJson(commands, {false, "The sandbox.keypoint_analysis section is unavailable."}));
            return Ok(ReadinessJson(commands, PreviewEditorKeypointAnalysisCommand(commands, *config)));
        }

        // K-means and consolidation take their entity and domain as arguments (section
        // convention in agent-control-lane.md): neither section names an entity.
        struct KMeansCall
        {
            EditorPointCloudServicePreparedFrame Frame{};
            RunKMeans Request{};
            std::string Unavailable{}; // preview only: why the service or section cannot be used at all
        };
        std::optional<KMeansCall> PrepareKMeansCall(const AgentOperationContext& context, std::string_view arguments,
                                                    AgentOperationOutcome& failure, bool preview)
        {
            const auto args = ParseObject(arguments);
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            if (!entity) { failure = Fail("Pass {\"entity\": <stable id>, \"domain\": <domain>}."); return std::nullopt; }
            const auto domainName = String(*args, "domain");
            const auto domain = ParseDomain(domainName);
            if (domainName && !domain) { failure = Fail(UnknownDomainMessage(*domainName)); return std::nullopt; }
            if (!PrepareSnapshot(context)) { failure = Fail(kNoWorkspace); return std::nullopt; } // prepares the session frame the feature frames read
            KMeansCall call{.Frame = PrepareEditorPointCloudServiceFrame(*context.Attachment)};
            const auto config = GetEditorClusteringConfig(call.Frame.Commands);
            if (!config || !call.Frame.ClusteringAvailable)
            {
                if (preview) { call.Unavailable = "Clustering is unavailable."; return call; }
                failure = Fail("Clustering is unavailable.");
                return std::nullopt;
            }
            if (!config->Properties && !domain)
            {
                failure = Fail("Pass a domain: the sandbox.clustering section binds no properties.");
                return std::nullopt;
            }
            auto refs = config->Properties.value_or(MakeKMeansPropertyRefs(domain.value_or(GeometryElementDomain::Unknown)));
            if (const auto positions = String(*args, "positions")) refs.InputPositions.Name = *positions;
            call.Request = MakeConfiguredKMeansRequest(*entity, std::move(refs), *config);
            call.Request.AutoAccept = true;
            return call;
        }
        AgentOperationOutcome PreviewKMeansOperation(const AgentOperationContext& context, std::string_view arguments)
        {
            AgentOperationOutcome failure;
            const auto call = PrepareKMeansCall(context, arguments, failure, true);
            if (!call) return failure;
            if (!call->Unavailable.empty()) return Ok(ReadinessJson(call->Frame.Commands, {false, call->Unavailable}));
            return Ok(ReadinessJson(call->Frame.Commands, PreviewEditorKMeansRun(call->Frame.Commands, call->Frame.Clustering, call->Request)));
        }
        AgentOperationOutcome RunKMeansOperation(const AgentOperationContext& context, std::string_view arguments)
        {
            AgentOperationOutcome failure;
            const auto call = PrepareKMeansCall(context, arguments, failure, false);
            if (!call) return failure;
            const auto& frame = call->Frame;
            const auto& request = call->Request;
            return AwaitServiceRun<KMeansRunCompleted>(
                frame.Clustering,
                [](ClusteringService& service, auto onCompleted) { return service.SubscribeRunCompleted(std::move(onCompleted)); },
                [&] {
                    const auto submitted = SubmitKMeansRun(frame.Commands, frame.Clustering, request);
                    return ServiceSubmission{submitted.Correlation, submitted.Status == KMeansRunStatus::Queued, submitted.Message};
                },
                [](const KMeansRunCompleted& r) {
                    return Json{{"status", ToString(r.Status)}, {"message", r.Message}, {"succeeded", r.Succeeded()},
                                {"requested_backend", ToString(r.RequestedBackend)}, {"actual_backend", ToString(r.ActualBackend)},
                                {"implementation_id", r.ImplementationId}, {"backend_diagnostic", r.BackendDiagnostic},
                                {"fell_back_to_cpu", r.FellBackToCpu},
                                {"gpu_input_upload_bytes", r.GpuInputUploadBytes}, {"gpu_input_cache_hits", r.GpuInputCacheHits},
                                {"cpu_stage_upload_bytes", r.CpuStageUploadBytes},
                                {"cpu_stage_readback_bytes", r.CpuStageReadbackBytes},
                                {"gpu_submissions", r.GpuSubmissions}, {"gpu_previews", r.GpuPreviews},
                                {"iterations", r.Iterations}, {"converged", r.Converged}, {"inertia", r.Inertia}};
                });
        }

        struct ConsolidationCall
        {
            EditorPointCloudServicePreparedFrame Frame{};
            PointCloudConsolidationRequest Request{};
            std::string Unavailable{}; // preview only: why the service or section cannot be used at all
        };
        std::optional<ConsolidationCall> PrepareConsolidationCall(const AgentOperationContext& context, std::string_view arguments,
                                                                  AgentOperationOutcome& failure, bool preview)
        {
            const auto args = ParseObject(arguments);
            if (!args) { failure = Fail("Expected an object with entity and domain."); return std::nullopt; }
            const auto entity = UInt(*args, "entity");
            const auto domainName = String(*args, "domain");
            const auto domain = ParseDomain(domainName);
            if (domainName && !domain) { failure = Fail(UnknownDomainMessage(*domainName)); return std::nullopt; }
            if (!entity || !domain) { failure = Fail("Pass {\"entity\": <stable id>, \"domain\": <domain>}."); return std::nullopt; }
            if (!PrepareSnapshot(context)) { failure = Fail(kNoWorkspace); return std::nullopt; } // prepares the session frame the feature frames read
            ConsolidationCall call{.Frame = PrepareEditorPointCloudServiceFrame(*context.Attachment)};
            const auto config = GetEditorPointCloudConsolidationConfig(call.Frame.Commands);
            if (!config || !call.Frame.PointCloudConsolidationAvailable)
            {
                if (preview) { call.Unavailable = "Point-cloud consolidation is unavailable."; return call; }
                failure = Fail("Point-cloud consolidation is unavailable.");
                return std::nullopt;
            }
            call.Request = PointCloudConsolidationRequest{
                .StableEntityId = *entity,
                .Properties = MakePointCloudConsolidationPropertyRefs(*domain, String(*args, "positions").value_or("v:position")),
                .Config = *config, .AutoAccept = true};
            if (!IsValidPointCloudConsolidationPropertyRefs(call.Request.Properties))
            {
                failure = Fail("Invalid point property domain or name.");
                return std::nullopt;
            }
            return call;
        }
        // The panel's readiness: the service's availability for exactly this request.
        AgentOperationOutcome PreviewConsolidation(const AgentOperationContext& context, std::string_view arguments)
        {
            AgentOperationOutcome failure;
            const auto call = PrepareConsolidationCall(context, arguments, failure, true);
            if (!call) return failure;
            if (!call->Unavailable.empty()) return Ok(ReadinessJson(call->Frame.Commands, {false, call->Unavailable}));
            const auto availability = PrepareEditorPointCloudConsolidationAvailability(
                call->Frame.Commands, call->Frame.PointCloudConsolidation, call->Request);
            return Ok(ReadinessJson(call->Frame.Commands, {availability.Available, availability.Message},
                                    {{"pending", availability.Pending}, {"input_points", availability.InputPointCount},
                                     {"cardinality_changing", availability.CardinalityChanging}}));
        }
        AgentOperationOutcome RunConsolidation(const AgentOperationContext& context, std::string_view arguments)
        {
            AgentOperationOutcome failure;
            auto call = PrepareConsolidationCall(context, arguments, failure, false);
            if (!call) return failure;
            auto& frame = call->Frame;
            return AwaitServiceRun<PointCloudConsolidationResult>(
                frame.PointCloudConsolidation,
                [](PointCloudConsolidationService& service, auto onCompleted) { return service.SubscribeCompleted(std::move(onCompleted)); },
                [&] {
                    const auto submitted = SubmitEditorPointCloudConsolidation(frame.Commands, frame.PointCloudConsolidation, std::move(call->Request));
                    return ServiceSubmission{submitted.Correlation, submitted.Status == PointCloudConsolidationRunStatus::Queued,
                                             submitted.Message};
                },
                [](const PointCloudConsolidationResult& r) {
                    return Json{{"status", ToString(r.Status)}, {"message", r.Message}, {"succeeded", r.Succeeded()},
                                {"requested_backend", StableToken(r.RequestedBackend)}, {"actual_backend", StableToken(r.ActualBackend)},
                                {"backend_diagnostic", r.BackendDiagnostic}, {"fell_back_to_cpu", r.FellBackToCpu},
                                {"gpu_input_upload_bytes", r.GpuInputUploadBytes}, {"gpu_input_cache_hits", r.GpuInputCacheHits},
                                {"cpu_stage_upload_bytes", r.CpuStageUploadBytes},
                                {"cpu_stage_readback_bytes", r.CpuStageReadbackBytes},
                                {"gpu_submissions", r.GpuSubmissions}, {"gpu_previews", r.GpuPreviews},
                                {"iterations", r.Iterations}};
                });
        }

        // Both methods run their configured section (config_apply first). A queued job
        // answers once it has published or failed.
        AgentOperationOutcome RunRegistration(const AgentOperationContext& context, std::string_view arguments, bool preview)
        {
            const auto args = ParseObject(arguments);
            const auto method = args ? String(*args, "method") : std::nullopt;
            if (!method || (*method != "icp" && *method != "cpd")) return Fail("Pass {\"method\": \"icp\" | \"cpd\"}.");
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace); // prepares the session frame the feature frames read
            const auto commands = PrepareEditorRegistrationFrame(*context.Attachment).Commands;
            if (*method == "icp")
            {
                const auto config = GetEditorRegistrationConfig(commands);
                if (!config) return Fail("The sandbox.registration section is unavailable.");
                if (preview)
                {
                    return Ok(ReadinessJson(commands, PreviewEditorRegistrationCommand(commands, *config), {{"method", "icp"}}));
                }
                return FinishApply<EditorRegistrationResult>(
                    [&](auto onComplete) { return ApplyEditorConfiguredRegistrationCommand(commands, std::move(onComplete)); },
                    &RegistrationJson);
            }
            const auto config = GetEditorCoherentPointDriftConfig(commands);
            if (!config) return Fail("The sandbox.coherent_point_drift section is unavailable.");
            if (preview)
            {
                return Ok(ReadinessJson(commands, PreviewEditorCoherentPointDriftCommand(commands, *config), {{"method", "cpd"}}));
            }
            return FinishApply<EditorCoherentPointDriftResult>(
                [&](auto onComplete) { return ApplyEditorConfiguredCoherentPointDrift(commands, std::move(onComplete)); },
                &CoherentPointDriftJson);
        }
    }

    void RegisterProcessingAgentOperations(AgentOperationRegistry& registry)
    {
        const auto add = [&](const char* name, const char* title, std::string description, std::string schema,
                             bool readOnly, AgentOperationInvoker invoke, bool destructive = false, bool gpu = false) {
            AddOperation(registry, name, title, std::move(description), std::move(schema), readOnly, std::move(invoke), destructive, gpu);
        };
        const std::string& none = kNone;
        const std::string registration = Schema(
            R"({"method":{"type":"string","enum":["icp","cpd"],"description":"icp uses sandbox.registration, cpd uses sandbox.coherent_point_drift (config_apply first)."}})",
            R"(["method"])");
        add("preview_registration", "Preview registration",
            "Whether the configured ICP or Coherent Point Drift registration can run, and why not.", registration, true,
            [](const AgentOperationContext& c, std::string_view a) { return RunRegistration(c, a, true); });
        add("run_registration", "Run registration",
            "Register the configured source entity onto the target with ICP or Coherent Point Drift and publish the "
            "result (source transform, positions or a displacement property) as one undoable step; answers when done.",
            registration, false, [](const AgentOperationContext& c, std::string_view a) { return RunRegistration(c, a, false); },
            false, true);
        add("preview_point_sampling", "Preview point sampling",
            "Whether the configured point sampling (sandbox.point_sampling) can run, and why not.", none, true,
            [](const AgentOperationContext& c, std::string_view) { return RunPointSampling(c, true); });
        add("run_point_sampling", "Run point sampling",
            "Order the configured entity's points with the chosen sampling method (sandbox.point_sampling; config_apply "
            "first) and publish rank/selection properties or a new point cloud as one undoable step.",
            none, false, [](const AgentOperationContext& c, std::string_view) { return RunPointSampling(c, false); },
            false, true);
        add("preview_keypoint_analysis", "Preview keypoint analysis",
            "Whether the configured keypoint analysis (sandbox.keypoint_analysis) can run, and why not.", none, true,
            [](const AgentOperationContext& c, std::string_view) { return PreviewKeypoints(c); });
        add("run_keypoint_analysis", "Run keypoint analysis",
            "Run sandbox.keypoint_analysis; GPU score and mask auto-accept in one undoable entry. Reports backend and IO.",
            none, false, [](const AgentOperationContext& c, std::string_view) { return RunKeypoints(c); }, false, true);
        const std::string kmeansSchema = Schema("{" + kEntityProperty + "," + DomainProperty("Element domain of the positions; needed only while sandbox.clustering binds no properties.") +
                       "," + kPositionsProperty + "}",
                   R"(["entity"])");
        const std::string consolidationSchema = Schema("{" + kEntityProperty + "," + DomainProperty("Element domain of the positions.") + "," + kPositionsDefaultProperty + "}",
                   R"(["entity","domain"])");
        add("preview_kmeans", "Preview K-Means",
            "Whether K-Means can run on an entity with sandbox.clustering, and why not.", kmeansSchema, true, PreviewKMeansOperation);
        add("run_kmeans", "Run K-Means",
            "Cluster a point property of an entity with sandbox.clustering (config_apply first); GPU results auto-accept "
            "atomically. Reports backend and IO. The section's bound properties win over 'domain'.",
            kmeansSchema, false, RunKMeansOperation, false, true);
        add("preview_point_cloud_consolidation", "Preview point-cloud consolidation",
            "Whether consolidation can run on an entity's point property with sandbox.point_cloud_consolidation, and why not.",
            consolidationSchema, true, PreviewConsolidation);
        add("run_point_cloud_consolidation", "Run point-cloud consolidation",
            "Consolidate the named vec3 point property of an entity using sandbox.point_cloud_consolidation; GPU runs "
            "auto-accept. Reports backend and IO.",
            consolidationSchema, false, RunConsolidation, false, true);
        const std::string meshField = Schema(
            R"({"operation":{"type":"string","enum":)" + OperationEnum("mesh_field") +
                R"(,"description":"Mesh-field operation; its settings come from the matching config section (config_apply first)."},)" +
                kEntityProperty + "}",
            R"(["operation","entity"])");
        add("preview_mesh_operation", "Preview mesh operation",
            "Whether a mesh-field operation can run on an entity with the active settings, and why not. "
            "Alias of preview_operation limited to the mesh-field operations.", meshField, true,
            [](const AgentOperationContext& c, std::string_view a) { return RunConfiguredOperation(c, a, true, "mesh_field"); });
        add("run_mesh_operation", "Run mesh operation",
            "Run a mesh-field operation (property smoothing, spectral modes, harmonic field, scalar gradient) on an entity "
            "with the active config section; undoable like the panel button. Alias of run_operation limited to these.",
            meshField, false, [](const AgentOperationContext& c, std::string_view a) { return RunConfiguredOperation(c, a, false, "mesh_field"); },
            false, true); // property smoothing may select a Vulkan backend that waits on readback
        const std::string configured = Schema(
            R"({"operation":{"type":"string","enum":)" + OperationEnum(nullptr) +
                R"(,"description":"Operation; its settings come from its config section (config_apply first)."},)" +
                R"("entity":{"type":"integer","minimum":1,"description":"Stable entity id from scene_entities; required for operations marked 'entity argument', refused for those whose section names the entity."}})",
            R"(["operation"])");
        add("preview_operation", "Preview operation",
            "Whether a configured editor operation can run with the active settings, and why not. Operations: " +
                OperationSummary(nullptr) + ".",
            configured, true,
            [](const AgentOperationContext& c, std::string_view a) { return RunConfiguredOperation(c, a, true, nullptr); });
        add("run_operation", "Run operation",
            "Run a configured editor operation (mesh curvature, geodesics, curvature segmentation, normal estimation, kernel "
            "density, point spacing, outlier analysis, density weight, descriptor analysis, bilateral filter, point "
            "construction and the mesh-field operations) with its config section; one undoable step like the panel button, "
            "answers when done. Operations: " + OperationSummary(nullptr) + ".",
            configured, false, [](const AgentOperationContext& c, std::string_view a) { return RunConfiguredOperation(c, a, false, nullptr); },
            false, true); // several operations can select a Vulkan backend
    }
}
