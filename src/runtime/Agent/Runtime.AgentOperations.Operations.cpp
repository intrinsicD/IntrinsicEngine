// Editor-backed agent operations: every entry calls the same runtime query, command or
// config path the Sandbox panels use. JSON stays private to this unit; accessors never
// throw (the build has no exceptions), so every field is type-checked before use.
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>
#include <span>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

module Extrinsic.Runtime.AgentOperations;

import Extrinsic.ECS.Components.GeometrySources;
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
import Extrinsic.Runtime.ConfigFieldSpec;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.ScalarRidgeOperations;
import Extrinsic.Runtime.ParameterizationOperations;
import Extrinsic.Runtime.NormalOperations;
import Extrinsic.Runtime.PointFieldOperations;
import Extrinsic.Runtime.PointSetOperations;
import Extrinsic.Runtime.PointConstructionOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Runtime.PointCloudConsolidationTypes;
import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.StepRunExecution;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.GeometryProperty.Types;
import Geometry.Properties.Types;

#include "Agent/internal/Runtime.AgentOperations.Detail.hpp"
#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"

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
            const EditorWorkspaceSnapshotPreparedFrame& Prepared;
        };
        struct ConfiguredOperation
        {
            const char* Name;
            const char* Family; // "mesh_field" (also served by run_mesh_operation), "mesh_analysis", "point"
            OperationEntity Entity;
            const char* Section; // the config section the settings (and a Config row's entity) come from; empty with Params
            std::function<AgentOperationOutcome(const OperationCall&)> Run;
            std::string Params{}; // rows without a section: "key=default [range], ..." of their `params` object
            std::string ParamsSchemaJson{}; // JSON Schema of that object
            // ARCH-023: run_operation's wait false, for a row whose run is a one-shot step run: starts it with the
            // section's auto_accept and start and answers at once with its run id (run). Empty: the row only waits.
            std::function<AgentOperationOutcome(const OperationCall&)> Start{};
        };

        Json OperationResultJson(const char* name, Json result)
        {
            result["operation"] = name;
            return result;
        }
        // A started step run's answer: its result so far (Pending while it runs) with the run's id; an error once it
        // was refused or ended unpublished.
        template <class Result> AgentOperationOutcome StartedRun(const Result& r, const Json& text)
        {
            const bool going = r.Status == EditorCommandStatus::Pending && r.StepRun != 0u;
            return {.IsError = !going && !r.Succeeded(),
                .Text = Dump(text),
                .ErrorCode =
                    going || r.Succeeded() ? std::string{} : std::string{DebugNameForEditorCommandStatus(r.Status)}};
        }
        template <class Result> AgentOperationOutcome StartedRun(const char* name, const Result& r)
        {
            return StartedRun(r, OperationResultJson(name, ResultJson(r))); // ResultJson names the run
        }
        AgentOperationOutcome NoReadinessCheck(const char* name)
        {
            return Ok({{"operation", name}, {"enabled", nullptr},
                       {"reason", "This operation has no readiness check; run it to find out."}, {"reasons", Json::array()}});
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
                        return FinishApply<Result>(call.Context, [&](auto onComplete) { return apply(commands, std::move(onComplete)); },
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
                        return FinishApply<Result>(call.Context, [&](auto onComplete) { return apply(commands, entity, std::move(onComplete)); },
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

        // ---- explicit parameters ----------------------------------------------------------
        // Rows without a config section take a `params` object. The command's owner declares each
        // field once (a ConfigFieldSpec table: name, type, range, enum names, description) and
        // validates against it; here a field only maps its JSON value onto the command member.
        // Defaults come from the command struct's own member initializers (`Cmd{}`); absent params
        // keep them. Enums are accepted as their name or integer code, like config sections.
        template <class Cmd>
        struct ParamSpec
        {
            const ConfigFieldSpec* Field{nullptr};
            Json Default{};
            std::function<std::string(Cmd&, const Json&)> Set{}; // empty on success, else why the value is wrong
        };
        template <class Cmd>
        using ParamSpecs = std::vector<ParamSpec<Cmd>>;

        // RUNTIME-343: a field's mapping from its bound row, the owner's one declaration of the field
        // and its member; a row without accessors (a property reference) has its own builder.
        template <class Cmd> ParamSpec<Cmd> Param(const BoundField<Cmd>& row)
        {
            const Cmd defaults{};
            ParamSpec<Cmd> spec;
            if (row.Get == nullptr)
                return spec;
            spec.Field = &row.Spec;
            const ConfigFieldSpec& field = row.Spec;
            const std::string range = DescribeConfigFieldRange(field);
            switch (field.Type)
            {
            case ConfigFieldType::Bool:
                spec.Default = Json(row.Get(defaults) != 0.0);
                spec.Set = [&row](Cmd& command, const Json& value) -> std::string
                {
                    if (!value.is_boolean())
                        return "must be true or false";
                    row.Set(command, value.get<bool>() ? 1.0 : 0.0);
                    return {};
                };
                break;
            case ConfigFieldType::Enum:
            {
                const auto first = static_cast<std::int64_t>(field.Min.value_or(0.0));
                spec.Default = Json(static_cast<std::int64_t>(FieldNumber(row, defaults)));
                spec.Set = [&row, first, range](Cmd& command, const Json& value) -> std::string
                {
                    const auto names = row.Spec.EnumNames;
                    if (value.is_number_integer())
                    {
                        const auto code = value.get<std::int64_t>();
                        if (code < first || code >= first + std::int64_t(names.size()))
                            return "must be " + range;
                        SetFieldNumber(row, command, double(code));
                        return {};
                    }
                    if (value.is_string())
                        for (std::size_t i = 0; i < names.size(); ++i)
                            if (names[i] == value.get<std::string>())
                            {
                                SetFieldNumber(row, command, double(first + std::int64_t(i)));
                                return {};
                            }
                    return "must be " + range;
                };
                break;
            }
            case ConfigFieldType::UInt:
            case ConfigFieldType::Int:
                spec.Default = Json(static_cast<std::int64_t>(row.Get(defaults)));
                spec.Set = [&row, range](Cmd& command, const Json& value) -> std::string
                {
                    if (!value.is_number_integer())
                        return "must be an integer";
                    const double number = value.is_number_unsigned() ? double(value.get<std::uint64_t>()) : double(value.get<std::int64_t>());
                    if (!AcceptsConfigFieldNumber(row.Spec, number))
                        return "must be " + (range.empty() ? std::string("in range") : range);
                    row.Set(command, number);
                    return {};
                };
                break;
            case ConfigFieldType::Float:
                spec.Default = Json(row.Get(defaults));
                spec.Set = [&row, range](Cmd& command, const Json& value) -> std::string
                {
                    if (!value.is_number())
                        return "must be a number";
                    if (!AcceptsConfigFieldNumber(row.Spec, value.get<double>()))
                        return "must be " + (range.empty() ? std::string("finite") : range);
                    row.Set(command, value.get<double>());
                    return {};
                };
                break;
            default:
                spec.Field = nullptr; // property refs, strings and arrays have their own builders
                break;
            }
            return spec;
        }
        // Every bound row's mapping, in table order (rows without accessors are left to their builder).
        template <class Cmd> ParamSpecs<Cmd> ParamsOf(const std::span<const BoundField<Cmd>> rows)
        {
            ParamSpecs<Cmd> specs;
            for (const BoundField<Cmd>& row : rows)
                if (row.Get != nullptr)
                    specs.push_back(Param(row));
            return specs;
        }
        // A property name; the command's GeometryPropertyRef keeps its other fields until resolved.
        template <class Cmd>
        ParamSpec<Cmd> NameParam(const std::span<const ConfigFieldSpec> fields, const std::string_view name,
                                 GeometryPropertyRef Cmd::* member)
        {
            const Cmd defaults{};
            ParamSpec<Cmd> spec;
            spec.Field = FindConfigFieldSpec(fields, name);
            spec.Default = (defaults.*member).Name;
            spec.Set = [member](Cmd& command, const Json& value) -> std::string {
                if (!value.is_string() || value.get<std::string>().empty()) return "must be a property name";
                (command.*member).Name = value.get<std::string>();
                return {};
            };
            return spec;
        }
        template <class Cmd>
        std::string ApplyParams(const ParamSpecs<Cmd>& specs, const Json& args, Cmd& command)
        {
            const auto it = args.find("params");
            if (it == args.end()) return {};
            if (!it->is_object()) return "params must be an object.";
            for (const auto& [key, value] : it->items())
            {
                const auto spec = std::ranges::find_if(specs, [&](const ParamSpec<Cmd>& s) { return s.Field && s.Field->Name == key; });
                if (spec == specs.end())
                {
                    std::string valid;
                    for (const auto& s : specs)
                        if (s.Field != nullptr) valid += (valid.empty() ? "" : ", ") + std::string(s.Field->Name);
                    return "Unknown parameter '" + key + "'; valid: " + valid + ".";
                }
                if (const auto problem = spec->Set(command, value); !problem.empty()) return "Parameter '" + key + "' " + problem + ".";
            }
            return {};
        }
        // "name=default (accepted values), ..." for the tool description.
        template <class Cmd>
        std::string ParamsText(const ParamSpecs<Cmd>& specs)
        {
            std::string text;
            for (const auto& spec : specs)
            {
                if (spec.Field == nullptr) continue;
                text += (text.empty() ? "" : ", ") + std::string(spec.Field->Name) + "=";
                if (spec.Field->Type == ConfigFieldType::Enum)
                {
                    const auto first = static_cast<std::int64_t>(spec.Field->Min.value_or(0.0));
                    const auto index = static_cast<std::size_t>(spec.Default.template get<std::int64_t>() - first);
                    text += index < spec.Field->EnumNames.size() ? std::string(spec.Field->EnumNames[index]) : std::string("?");
                }
                else text += spec.Default.is_string() ? spec.Default.template get<std::string>() : spec.Default.dump();
                if (const auto range = DescribeConfigFieldRange(*spec.Field); !range.empty()) text += " [" + range + "]";
            }
            return text;
        }
        // JSON Schema of the `params` object: the owner's field table with the command's defaults.
        template <class Cmd>
        std::string ParamsSchema(const char* name, std::span<const ConfigFieldSpec> fields, const ParamSpecs<Cmd>& specs)
        {
            Json defaults = Json::object();
            for (const auto& spec : specs)
                if (spec.Field != nullptr) defaults[std::string(spec.Field->Name)] = spec.Default;
            Json schema = Json::parse(ConfigDetail::BuildSectionSchemaJson("", name, "", fields, defaults));
            for (const char* key : {"$schema", "$id", "title", "description"}) schema.erase(key);
            // Integer-coded operation enums also accept their human-readable names.
            // String-token section enums retain their owner's wire representation.
            for (auto& [key, property] : schema["properties"].items())
            {
                if (property.value("type", "") != "integer" || !property.contains("x-enum-names")) continue;
                Json codes{{"type", "integer"}, {"enum", property["enum"]}};
                Json names{{"type", "string"}, {"enum", property["x-enum-names"]}};
                property.erase("type");
                property.erase("enum");
                property["anyOf"] = Json::array({std::move(codes), std::move(names)});
            }
            return Dump(schema);
        }
        AgentOperationOutcome InvalidParams(std::string message)
        {
            return {.IsError = true, .Text = std::move(message), .ErrorCode = "invalid_params"};
        }

        // The call names the entity; the settings are `params` over the command's own defaults.
        // `resolve(call, command)` may refine the command after the params applied (empty = fine).
        template <class Result, class Cmd, class Frame, class Preview, class Apply, class Describe, class Resolve>
        ConfiguredOperation ExplicitOperation(const char* name, const char* family, std::span<const ConfigFieldSpec> fields,
                                              ParamSpecs<Cmd> specs, Frame frame, Preview preview, Apply apply,
                                              Describe describe, Resolve resolve)
        {
            std::erase_if(specs, [](const ParamSpec<Cmd>& spec) { return spec.Field == nullptr; }); // a name the owner's table lacks
            ConfiguredOperation op{name, family, OperationEntity::Argument, "",
                [=](const OperationCall& call) -> AgentOperationOutcome {
                    Cmd command{};
                    command.StableEntityId = *call.Entity;
                    if (const auto problem = ApplyParams(specs, call.Args, command); !problem.empty()) return InvalidParams(problem);
                    if (const auto problem = resolve(call, command); !problem.empty()) return InvalidParams(problem);
                    const auto commands = frame(*call.Context.Attachment).Commands;
                    if (call.Preview)
                    {
                        const std::optional<ActionReadiness> readiness = preview(commands, command);
                        if (!readiness) return NoReadinessCheck(name);
                        return Ok(ReadinessJson(commands, *readiness, {{"operation", name}}));
                    }
                    return FinishApply<Result>(call.Context, [&](auto onComplete) { return apply(commands, command, std::move(onComplete)); },
                                               [name, describe](const Result& r) { return OperationResultJson(name, describe(r)); });
                }};
            op.Params = ParamsText(specs);
            op.ParamsSchemaJson = ParamsSchema(name, fields, specs);
            return op;
        }
        template <class Result, class Cmd, class Frame, class Preview, class Apply, class Describe>
        ConfiguredOperation ExplicitOperation(const char* name, const char* family, std::span<const ConfigFieldSpec> fields,
                                              ParamSpecs<Cmd> specs, Frame frame, Preview preview, Apply apply, Describe describe)
        {
            return ExplicitOperation<Result>(name, family, fields, std::move(specs), frame, preview, apply, describe,
                                             [](const OperationCall&, Cmd&) { return std::string{}; });
        }
        template <class Result>
        Json TopologyJson(const Result& r)
        {
            Json out = ResultJson(r);
            out["input_vertices"] = r.InputVertexCount;
            out["input_faces"] = r.InputFaceCount;
            out["output_vertices"] = r.OutputVertexCount;
            out["output_faces"] = r.OutputFaceCount;
            out["texcoords"] = DebugNameForEditorMeshTexcoordOutcome(r.TexcoordOutcome);
            out["dropped_properties"] = r.DroppedProperties;
            return out;
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
                // The panel's interactive start; a Vulkan run's step run holds its transaction, so the handle may go.
                ops.back().Start = [](const OperationCall& call)
                {
                    const auto commands = PrepareEditorMeshFieldFrame(*call.Context.Attachment).Commands;
                    const auto config = GetEditorPropertySmoothingConfig(commands);
                    auto result = MissingSection<EditorPropertySmoothingResult>("sandbox.property_smoothing");
                    if (config)
                        (void)StartEditorPropertySmoothing(commands, *call.Entity, *config, result);
                    auto text = OperationResultJson("property_smoothing", ResultJson(result));
                    text["requested_backend"] =
                        result.RequestedBackend == PropertySmoothingBackend::Vulkan ? "vulkan" : "cpu";
                    // No actual backend: the solver is known only at publication.
                    return StartedRun(result, text);
                };
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
                    [](const auto& c, std::uint32_t) -> std::optional<ActionReadiness> {
                        const auto config = GetEditorGeodesicsConfig(c);
                        if (!config) return ActionReadiness{false, "The sandbox.geodesics section is unavailable."};
                        return PreviewEditorGeodesicsCommand(c, *config);
                    },
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
                ops.back().Start = [](const OperationCall& call)
                {
                    return StartedRun("normal_estimation",
                        StartEditorConfiguredNormalEstimation(
                            PrepareEditorNormalFrame(*call.Context.Attachment).Commands));
                };
                ops.push_back(ConfigSourced<EditorOutlierAnalysisResult>(
                    "outlier_analysis", "point", "sandbox.outlier_analysis", &PrepareEditorPointAnalysisFrame,
                    &GetEditorOutlierAnalysisConfig, &PreviewEditorOutlierAnalysisCommand,
                    [](const auto& c, auto onComplete) { return ApplyEditorConfiguredOutlierAnalysis(c, std::move(onComplete)); }));
                ops.back().Start = [](const OperationCall& call)
                {
                    return StartedRun("outlier_analysis",
                        StartEditorConfiguredOutlierAnalysis(
                            PrepareEditorPointAnalysisFrame(*call.Context.Attachment).Commands));
                };
                ops.push_back(ConfigSourced<EditorPointConstructionResult>(
                    "point_construction", "point", "sandbox.point_construction", &PrepareEditorPointConstructionFrame,
                    &GetEditorPointConstructionConfig,
                    [](const auto& c, const PointConstructionConfig& config) {
                        const auto readiness = PreviewEditorPointConstructionCommand(c, config);
                        return ActionReadiness{readiness.Ready, readiness.Diagnostic};
                    },
                    [](const auto& c, auto onComplete) { return ApplyEditorConfiguredPointConstruction(c, std::move(onComplete)); }));
                ops.back().Start = [](const OperationCall& call)
                {
                    return StartedRun("point_construction",
                        StartEditorConfiguredPointConstruction(
                            PrepareEditorPointConstructionFrame(*call.Context.Attachment).Commands));
                };
                // -- topology editing and ridges: explicit params over the command defaults
                ops.push_back(ExplicitOperation<EditorMeshDenoiseResult, EditorMeshDenoiseCommand>(
                    "mesh_denoise",
                    "mesh_topology",
                    EditorMeshDenoiseFieldSpecs(),
                    ParamsOf(EditorMeshDenoiseFields()),
                    &PrepareEditorMeshTopologyFrame,
                    [](const auto& c, const EditorMeshDenoiseCommand& cmd) -> std::optional<ActionReadiness>
                    {
                        return PreviewEditorMeshDenoiseCommand(c, cmd);
                    },
                    [](const auto& c, const EditorMeshDenoiseCommand& cmd, auto onComplete)
                    {
                        return ApplyEditorMeshDenoiseCommand(c, cmd, std::move(onComplete));
                    },
                    [](const EditorMeshDenoiseResult& r)
                    {
                        Json out = ResultJson(r);
                        out["moved_vertices"] = r.MovedVertexCount;
                        out["pinned_boundary_vertices"] = r.PinnedBoundaryVertexCount;
                        return out;
                    }));
                ops.push_back(ExplicitOperation<EditorMeshRemeshResult, EditorMeshRemeshCommand>(
                    "mesh_remesh",
                    "mesh_topology",
                    EditorMeshRemeshFieldSpecs(),
                    ParamsOf(EditorMeshRemeshFields()),
                    &PrepareEditorMeshTopologyFrame,
                    [](const auto& c, const EditorMeshRemeshCommand& cmd) -> std::optional<ActionReadiness>
                    {
                        return PreviewEditorMeshRemeshCommand(c, cmd);
                    },
                    [](const auto& c, const EditorMeshRemeshCommand& cmd, auto onComplete)
                    {
                        return ApplyEditorMeshRemeshCommand(c, cmd, std::move(onComplete));
                    },
                    [](const EditorMeshRemeshResult& r)
                    {
                        Json out = TopologyJson(r);
                        out["splits"] = r.SplitCount;
                        out["collapses"] = r.CollapseCount;
                        out["flips"] = r.FlipCount;
                        return out;
                    }));
                ops.push_back(ExplicitOperation<EditorMeshSubdivideResult, EditorMeshSubdivideCommand>(
                    "mesh_subdivide",
                    "mesh_topology",
                    EditorMeshSubdivideFieldSpecs(),
                    ParamsOf(EditorMeshSubdivideFields()),
                    &PrepareEditorMeshTopologyFrame,
                    [](const auto& c, const EditorMeshSubdivideCommand& cmd) -> std::optional<ActionReadiness>
                    {
                        return PreviewEditorMeshSubdivideCommand(c, cmd);
                    },
                    [](const auto& c, const EditorMeshSubdivideCommand& cmd, auto onComplete)
                    {
                        return ApplyEditorMeshSubdivideCommand(c, cmd, std::move(onComplete));
                    },
                    [](const EditorMeshSubdivideResult& r)
                    {
                        return TopologyJson(r);
                    }));
                ops.push_back(ExplicitOperation<EditorMeshSimplifyResult, EditorMeshSimplifyCommand>(
                    "mesh_simplify",
                    "mesh_topology",
                    EditorMeshSimplifyFieldSpecs(),
                    ParamsOf(EditorMeshSimplifyFields()),
                    &PrepareEditorMeshTopologyFrame,
                    [](const auto& c, const EditorMeshSimplifyCommand& cmd) -> std::optional<ActionReadiness>
                    {
                        return PreviewEditorMeshSimplifyCommand(c, cmd);
                    },
                    [](const auto& c, const EditorMeshSimplifyCommand& cmd, auto onComplete)
                    {
                        return ApplyEditorMeshSimplifyCommand(c, cmd, std::move(onComplete));
                    },
                    [](const EditorMeshSimplifyResult& r)
                    {
                        Json out = TopologyJson(r);
                        out["collapses"] = r.CollapseCount;
                        out["max_collapse_error"] = r.MaxCollapseError;
                        return out;
                    }));
                // The ridge property is picked from the catalog like the panel's combo (vertex scalars).
                ops.push_back(ExplicitOperation<EditorScalarRidgeResult, EditorScalarRidgeCommand>(
                    "scalar_ridge",
                    "mesh_analysis",
                    EditorScalarRidgeFieldSpecs(),
                    []
                    {
                        // The property row (first in the table) has its own builder.
                        auto specs = ParamsOf(EditorScalarRidgeFields());
                        specs.insert(specs.begin(),
                            NameParam(EditorScalarRidgeFieldSpecs(), "property", &EditorScalarRidgeCommand::Property));
                        return specs;
                    }(),
                    &PrepareEditorMeshFieldFrame,
                    [](const auto& c, const EditorScalarRidgeCommand& cmd) -> std::optional<ActionReadiness>
                    {
                        return PreviewEditorScalarRidgeCommand(c, cmd);
                    },
                    [](const auto& c, const EditorScalarRidgeCommand& cmd, auto)
                    {
                        return ApplyEditorScalarRidgeCommand(c, cmd);
                    },
                    [](const EditorScalarRidgeResult& r)
                    {
                        Json out = ResultJson(r);
                        out["output_entity"] = r.OutputEntityId;
                        out["ridge_segments"] = r.RidgeSegmentCount;
                        out["valley_segments"] = r.ValleySegmentCount;
                        out["feature_vertices"] = r.FeatureVertexCount;
                        out["feature_edges"] = r.FeatureEdgeCount;
                        out["basins"] = r.BasinCount;
                        out["milliseconds"] = r.ComputeMilliseconds;
                        return out;
                    },
                    [](const OperationCall& call, EditorScalarRidgeCommand& command) -> std::string
                    {
                        const auto params = call.Args.find("params");
                        if (params == call.Args.end() || !params->contains("property"))
                            return {};
                        const auto inspector = BuildEditorInspectorModel(call.Prepared.SnapshotQueries, nullptr, command.StableEntityId);
                        if (!inspector.HasEntity)
                            return "No entity with id " + std::to_string(command.StableEntityId) + ".";
                        const auto row = std::ranges::find_if(inspector.PropertyCatalog.Rows, [&](const EditorPropertyCatalogRow& r) {
                            return r.Name == command.Property.Name && !r.Internal && r.Bindable &&
                                   r.Descriptor.Domain == GeometryElementDomain::MeshVertex &&
                                   GeometryPropertyComponentCount(r.ValueKind) <= 1u;
                        });
                        if (row == inspector.PropertyCatalog.Rows.end())
                            return "Entity " + std::to_string(command.StableEntityId) + " has no vertex scalar '" + command.Property.Name +
                                   "'; see entity_properties.";
                        command.Property = row->Descriptor;
                        return {};
                    }));
                // -- config-backed rows whose command also takes an entity
                ops.push_back(EntityOverConfig<EditorProgressivePoissonResult, ProgressivePoissonPlaygroundConfig>(
                    "progressive_poisson", "point", "sandbox.progressive_poisson", &PrepareEditorPointSetFrame,
                    &GetEditorProgressivePoissonConfig,
                    [](const auto& c, std::uint32_t id, const ProgressivePoissonPlaygroundConfig& config) {
                        return PreviewEditorProgressivePoissonCommand(c, {id, config});
                    },
                    [](const auto& c, std::uint32_t id, const ProgressivePoissonPlaygroundConfig& config, auto onComplete) {
                        return ApplyEditorProgressivePoissonCommand(c, {id, config}, std::move(onComplete));
                    }));
                ops.push_back(EntityOperation<EditorParameterizationResult>(
                    "parameterization",
                    "mesh_analysis",
                    "sandbox.parameterization",
                    &PrepareEditorParameterizationFrame,
                    [](const auto& c, std::uint32_t id)
                    {
                        const auto config = GetEditorParameterizationConfig(c);
                        return std::optional<ActionReadiness>{config
                                ? PreviewEditorParameterizationCommand(c, {id, *config})
                                : ActionReadiness{
                                      false, "Configured parameterization is missing its registered config section."}};
                    },
                    [](const auto& c, std::uint32_t id, auto onComplete)
                    {
                        return ApplyEditorConfiguredParameterizationCommand(c, EditorConfiguredParameterizationCommand{id}, std::move(onComplete));
                    }));
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
                {
                    text += std::string(text.empty() ? "" : "; ") + op.Name + " (";
                    if (!op.Params.empty()) text += "entity argument; params " + op.Params + ")";
                    else text += std::string(op.Section) + (op.Entity == OperationEntity::Config ? "; entity from the section)" : "; entity argument)");
                }
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
            const std::string settings = op->Params.empty() ? std::string("its settings come from ") + op->Section
                                                            : std::string("its settings are the optional params");
            if (op->Entity == OperationEntity::Argument && !entity)
                return Fail("Operation '" + *name + "' needs {\"entity\": <stable id>}; " + settings + ".");
            if (op->Entity == OperationEntity::Config && args->contains("entity"))
                return Fail("Operation '" + *name + "' takes its entity from config section '" + op->Section +
                            "' (config_apply first); do not pass entity.");
            if (op->Params.empty() && args->contains("params"))
                return Fail("Operation '" + *name + "' takes no params; its settings come from " + op->Section + ".");
            const bool wait = preview || args->value("wait", true);
            if (!wait && !op->Start)
                return InvalidParams("wait false starts the step-run operations only (property_smoothing, "
                                     "normal_estimation, outlier_analysis, point_construction); '" +
                    *name + "' answers when done.");
            const auto prepared = PrepareSnapshot(context);
            if (!prepared) return Fail(kNoWorkspace);
            const OperationCall call{context, *args, entity, preview, *prepared};
            return wait ? op->Run(call) : op->Start(call);
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
            return {{"method", "icp"},
                {"status", DebugNameForEditorCommandStatus(r.Status)},
                {"succeeded", r.Succeeded()},
                {"message", r.Message},
                {"backend", ToString(r.ActualBackend)},
                {"requested_backend", ToString(r.RequestedBackend)},
                {"iterations", r.IterationsPerformed},
                {"converged", r.Converged},
                {"rmse", r.FinalRMSE},
                {"inliers", r.FinalInlierCount},
                {"source_points", r.SourcePointCount},
                {"target_points", r.TargetPointCount},
                {"run", r.StepRun}};
        }
        Json CoherentPointDriftJson(const EditorCoherentPointDriftResult& r)
        {
            constexpr std::array<std::string_view, 4> kVariants{"rigid", "affine", "nonrigid", "bayesian"};
            constexpr std::array<std::string_view, 3> kOutputs{"source_transform", "positions", "displacement_property"};
            const auto named = [](const auto& names, auto value) {
                return std::size_t(value) < names.size() ? std::string(names[std::size_t(value)]) : std::string("unknown");
            };
            return {{"method", "cpd"},
                {"status", DebugNameForEditorCommandStatus(r.Status)},
                {"succeeded", r.Succeeded()},
                {"message", r.Message},
                {"backend", r.Backend},
                {"variant", named(kVariants, r.Method)},
                {"output", named(kOutputs, r.Output)},
                {"termination", r.Termination},
                {"iterations", r.Iterations},
                {"sigma2", r.Sigma2},
                {"negative_log_likelihood", r.NegativeLogLikelihood},
                {"matched_weight", r.MatchedWeight},
                {"mean_displacement", r.MeanDisplacement},
                {"transform", TransformJson(r.Transform)},
                {"source_points", r.SourcePointCount},
                {"target_points", r.TargetPointCount},
                {"e_step_error_bound", r.EStepErrorBound},
                {"e_step_sampled_error", r.EStepSampledError},
                {"e_step_fallbacks", r.EStepFallbacks},
                {"e_step_device_iterations", r.EStepDeviceIterations},
                {"e_step_device_cpu_rows", r.EStepDeviceCpuRows},
                {"gpu_diagnostic", r.GpuDiagnostic},
                {"subsample_requested_backend", r.SubsampleRequestedBackend},
                {"subsample_target_backend", r.SubsampleTargetBackend},
                {"subsample_source_backend", r.SubsampleSourceBackend},
                {"subsample_backend_diagnostic", r.SubsampleBackendDiagnostic},
                {"kernel_rank", r.KernelRank},
                {"kernel_approximation_error", r.KernelApproximationError},
                {"run", r.StepRun}};
        }
        Json PointSamplingJson(const EditorPointSamplingResult& r)
        {
            return {{"status", DebugNameForEditorCommandStatus(r.Status)},
                {"succeeded", r.Succeeded()},
                {"message", r.Message},
                {"method", r.Method},
                {"input_points", r.InputCount},
                {"samples", r.SampleCount},
                {"output_entity", r.OutputEntityId},
                {"milliseconds", r.Milliseconds},
                {"distance_pairs", r.DistancePairs},
                {"gpu_input_upload_bytes", r.GpuInputUploadBytes},
                {"gpu_input_cache_hits", r.GpuInputCacheHits},
                {"cpu_stage_readback_bytes", r.CpuStageReadbackBytes},
                {"requested_backend", r.RequestedBackend},
                {"backend", r.Backend},
                {"backend_diagnostic", r.BackendDiagnostic},
                {"run", r.StepRun},
                {"steps", r.Steps}};
        }
        Json KeypointsJson(const EditorKeypointAnalysisResult& r)
        {
            return {{"status", DebugNameForEditorCommandStatus(r.Status)},
                {"succeeded", r.Succeeded()},
                {"message", r.Message},
                {"requested_backend", ToString(r.RequestedBackend)},
                {"actual_backend", r.ActualBackend},
                {"implementation_id", r.ImplementationId},
                {"gpu_input_upload_bytes", r.GpuInputUploadBytes},
                {"gpu_input_cache_hits", r.GpuInputCacheHits},
                {"cpu_stage_upload_bytes", r.CpuStageUploadBytes},
                {"cpu_stage_readback_bytes", r.CpuStageReadbackBytes},
                {"gpu_submissions", r.GpuQueryBatches},
                {"keypoints", r.KeypointCount},
                {"run", r.StepRun}};
        }
        // RUNTIME-274: the standalone sampling operation on the sandbox.point_sampling section. wait false starts the
        // panel's step run with the section's start and auto_accept and answers at once with its run.
        AgentOperationOutcome RunPointSampling(
            const AgentOperationContext& context, std::string_view arguments, bool preview)
        {
            const auto args = ParseObject(arguments);
            const bool wait = !args || args->value("wait", true);
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace); // prepares the session frame the feature frames read
            const auto commands = PrepareEditorRegistrationFrame(*context.Attachment).Commands;
            const auto config = GetEditorPointSamplingConfig(commands);
            if (!config)
                return preview ? Ok(ReadinessJson(commands, {false, "The sandbox.point_sampling section is unavailable."}))
                               : Fail("The sandbox.point_sampling section is unavailable.");
            if (preview) return Ok(ReadinessJson(commands, PreviewEditorPointSamplingCommand(commands, *config)));
            if (!wait)
            {
                const auto started = StartEditorPointSampling(commands, *config);
                auto text = PointSamplingJson(started);
                text["backend"] = nullptr; // decided at Accept (device order or CPU fallback), not at the start
                return StartedRun(started, text);
            }
            // A Vulkan run is queued and answers once it has published or failed.
            return FinishApply<EditorPointSamplingResult>(
                context,
                [&](auto onComplete)
                {
                    return ApplyEditorPointSamplingCommand(commands, *config, std::move(onComplete));
                },
                &PointSamplingJson);
        }

        AgentOperationOutcome RunKeypoints(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const bool wait = !args || args->value("wait", true);
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace); // prepares the session frame the feature frames read
            const auto commands = PrepareEditorPointAnalysisFrame(*context.Attachment).Commands;
            if (!wait)
            {
                const auto started = StartEditorConfiguredKeypointAnalysis(commands);
                return StartedRun(started, KeypointsJson(started));
            }
            return FinishApply<EditorKeypointAnalysisResult>(
                context,
                [&](auto onComplete)
                {
                    return ApplyEditorConfiguredKeypointAnalysis(commands, std::move(onComplete));
                },
                &KeypointsJson);
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
        std::optional<KMeansCall> PrepareKMeansCall(const AgentOperationContext& context,
            std::string_view arguments,
            AgentOperationOutcome& failure,
            bool preview,
            bool batch = true)
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
            if (batch)
            {
                // The call answers with the accepted result: it runs at once and accepts at its end, whatever the
                // section's start and auto_accept (start_kmeans starts a run as they say; run_control drives it).
                call.Request.Parameters.Execution.Start = StepRunStart::Run;
                call.Request.Parameters.Execution.AutoAccept = StepRunAutoAccept::End;
                return call;
            }
            // start_kmeans: the section's execution options, each one the call names replacing it (the shared reader
            // of the rows the dispatcher already checked).
            std::string refusal;
            if (!ReadStepRunExecutionArguments(
                    arguments, kMaxKMeansIterations, call.Request.Parameters.Execution, refusal))
            {
                failure = Fail(std::move(refusal));
                return std::nullopt;
            }
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
                [](ClusteringService& service, auto onCompleted)
                {
                    return service.SubscribeRunCompleted(std::move(onCompleted));
                },
                [&]
                {
                    const auto submitted = SubmitKMeansRun(frame.Commands, frame.Clustering, request);
                    return ServiceSubmission{submitted.Correlation, submitted.Status == KMeansRunStatus::Queued, submitted.Message};
                },
                [](const KMeansRunCompleted& r)
                {
                    return Json{{"status", ToString(r.Status)},
                        {"message", r.Message},
                        {"succeeded", r.Succeeded()},
                        {"requested_backend", ToString(r.RequestedBackend)},
                        {"actual_backend", ToString(r.ActualBackend)},
                        {"implementation_id", r.ImplementationId},
                        {"backend_diagnostic", r.BackendDiagnostic},
                        {"fell_back_to_cpu", r.FellBackToCpu},
                        {"gpu_input_upload_bytes", r.GpuInputUploadBytes},
                        {"gpu_input_cache_hits", r.GpuInputCacheHits},
                        {"cpu_stage_upload_bytes", r.CpuStageUploadBytes},
                        {"cpu_stage_readback_bytes", r.CpuStageReadbackBytes},
                        {"gpu_submissions", r.GpuSubmissions},
                        {"gpu_previews", r.GpuPreviews},
                        {"run", r.StepRun},
                        {"iterations", r.Iterations},
                        {"converged", r.Converged},
                        {"inertia", r.Inertia}};
                });
        }

        // A K-Means step run started without waiting for its end: answers its run id once the request started one
        // (the next command drain), or the refusal's completion.
        AgentOperationOutcome StartKMeansOperation(const AgentOperationContext& context, std::string_view arguments)
        {
            AgentOperationOutcome failure;
            const auto call = PrepareKMeansCall(context, arguments, failure, false, false);
            if (!call)
                return failure;
            const auto& frame = call->Frame;
            auto run = std::make_shared<ServiceRun<KMeansRunCompleted, ClusteringService>>();
            run->Owner = frame.Clustering;
            run->Subscription = frame.Clustering->SubscribeRunCompleted(
                [weak = std::weak_ptr<ServiceRun<KMeansRunCompleted, ClusteringService>>(run)](
                    const KMeansRunCompleted& r)
                {
                    if (auto state = weak.lock(); state && state->Correlation == r.Correlation)
                        state->Completed = r;
                });
            const auto submitted = SubmitKMeansRun(frame.Commands, frame.Clustering, call->Request);
            run->Correlation = submitted.Correlation;
            if (submitted.Status != KMeansRunStatus::Queued)
            {
                run->Release();
                return Fail(submitted.Message);
            }
            AgentOperationOutcome outcome{};
            outcome.Continuation = [run](const AgentOperationContext& current, AgentOperationOutcome& out)
            {
                if (!current.Attachment || !current.Attachment->IsAttached())
                {
                    run->Release();
                    out = Fail(kNoWorkspace);
                    return true;
                }
                // A run that already ended (auto_accept) still names its run; a refusal names none.
                const auto id = run->Completed ? run->Completed->StepRun : run->Owner->StepRun(run->Correlation);
                if (id == 0u && !run->Completed)
                    return false;
                run->Release();
                if (id == 0u)
                {
                    out = Fail(run->Completed->Message);
                    return true;
                }
                out = Ok(Json{{"run", id},
                    {"correlation", run->Correlation.Value},
                    {"status", run->Completed ? ToString(run->Completed->Status) : std::string_view{"started"}}});
                return true;
            };
            return outcome;
        }

        struct ConsolidationCall
        {
            EditorPointCloudServicePreparedFrame Frame{};
            PointCloudConsolidationRequest Request{};
            std::string Unavailable{}; // preview only: why the service or section cannot be used at all
        };
        std::optional<ConsolidationCall> PrepareConsolidationCall(const AgentOperationContext& context,
            std::string_view arguments,
            AgentOperationOutcome& failure,
            bool preview,
            const bool batch = true)
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
            call.Request = PointCloudConsolidationRequest{.StableEntityId = *entity,
                .Properties = MakePointCloudConsolidationPropertyRefs(*domain,
                    String(*args, "positions")
                        .value_or(std::string{ECS::Components::GeometrySources::PropertyNames::kPosition})),
                .Config = *config};
            if (batch)
            {
                // As run_kmeans: it answers with the accepted result, whatever the section's start and auto_accept.
                call.Request.Config.Execution.Start = StepRunStart::Run;
                call.Request.Config.Execution.AutoAccept = StepRunAutoAccept::End;
            }
            else
            {
                // start_point_cloud_consolidation: the section's execution options, each one the call names replacing
                // it (the shared reader of the rows the dispatcher already checked).
                std::string refusal;
                if (!ReadStepRunExecutionArguments(
                        arguments, kMaxPointCloudConsolidationIterations, call.Request.Config.Execution, refusal))
                {
                    failure = Fail(std::move(refusal));
                    return std::nullopt;
                }
            }
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
                [](PointCloudConsolidationService& service, auto onCompleted)
                {
                    return service.SubscribeCompleted(std::move(onCompleted));
                },
                [&]
                {
                    const auto submitted = SubmitEditorPointCloudConsolidation(frame.Commands, frame.PointCloudConsolidation, std::move(call->Request));
                    return ServiceSubmission{submitted.Correlation, submitted.Status == PointCloudConsolidationRunStatus::Queued,
                                             submitted.Message};
                },
                [](const PointCloudConsolidationResult& r)
                {
                    return Json{{"status", ToString(r.Status)},
                        {"message", r.Message},
                        {"succeeded", r.Succeeded()},
                        {"requested_backend", StableToken(r.RequestedBackend)},
                        {"actual_backend", StableToken(r.ActualBackend)},
                        {"backend_diagnostic", r.BackendDiagnostic},
                        {"fell_back_to_cpu", r.FellBackToCpu},
                        {"initial_sampling_requested_backend", r.InitialSamplingRequestedBackend},
                        {"initial_sampling_backend", r.InitialSamplingBackend},
                        {"initial_sampling_diagnostic", r.InitialSamplingDiagnostic},
                        {"gpu_input_upload_bytes", r.GpuInputUploadBytes},
                        {"gpu_input_cache_hits", r.GpuInputCacheHits},
                        {"cpu_stage_upload_bytes", r.CpuStageUploadBytes},
                        {"cpu_stage_readback_bytes", r.CpuStageReadbackBytes},
                        {"gpu_submissions", r.GpuSubmissions},
                        {"gpu_previews", r.GpuPreviews},
                        {"run", r.StepRun},
                        {"iterations", r.Iterations}};
                });
        }

        // A consolidation step run started without waiting for its end: answers its run id once the request started
        // one (the next command drain), or the refusal's completion (as start_kmeans).
        AgentOperationOutcome StartConsolidation(const AgentOperationContext& context, std::string_view arguments)
        {
            AgentOperationOutcome failure;
            auto call = PrepareConsolidationCall(context, arguments, failure, false, false);
            if (!call)
                return failure;
            auto& frame = call->Frame;
            using Run = ServiceRun<PointCloudConsolidationResult, PointCloudConsolidationService>;
            auto run = std::make_shared<Run>();
            run->Owner = frame.PointCloudConsolidation;
            run->Subscription = frame.PointCloudConsolidation->SubscribeCompleted(
                [weak = std::weak_ptr<Run>(run)](const PointCloudConsolidationResult& r)
                {
                    if (auto state = weak.lock(); state && state->Correlation == r.Correlation)
                        state->Completed = r;
                });
            const auto submitted = SubmitEditorPointCloudConsolidation(
                frame.Commands, frame.PointCloudConsolidation, std::move(call->Request));
            run->Correlation = submitted.Correlation;
            if (submitted.Status != PointCloudConsolidationRunStatus::Queued)
            {
                run->Release();
                return Fail(submitted.Message);
            }
            AgentOperationOutcome outcome{};
            outcome.Continuation = [run](const AgentOperationContext& current, AgentOperationOutcome& out)
            {
                if (!current.Attachment || !current.Attachment->IsAttached())
                {
                    run->Release();
                    out = Fail(kNoWorkspace);
                    return true;
                }
                // A run that already ended (auto_accept) still names its run; a refusal names none.
                const auto id = run->Completed ? run->Completed->StepRun : run->Owner->GpuRun(run->Correlation).StepRun;
                if (id == 0u && !run->Completed)
                    return false;
                run->Release();
                if (id == 0u)
                {
                    out = Fail(run->Completed->Message);
                    return true;
                }
                out = Ok(Json{{"run", id},
                    {"correlation", run->Correlation.Value},
                    {"status", run->Completed ? ToString(run->Completed->Status) : std::string_view{"started"}}});
                return true;
            };
            return outcome;
        }

        // Both methods run their configured section (config_apply first) as a step run that computes at once and
        // accepts at its end, whatever the section's start and auto_accept; a queued run answers once it has
        // published or failed. With wait false each method runs the section's own start and auto_accept instead and
        // answers at once with its run id (run), which run_control and run_observe drive.
        AgentOperationOutcome RunRegistration(const AgentOperationContext& context, std::string_view arguments, bool preview)
        {
            const auto args = ParseObject(arguments);
            const auto method = args ? String(*args, "method") : std::nullopt;
            if (!method || (*method != "icp" && *method != "cpd")) return Fail("Pass {\"method\": \"icp\" | \"cpd\"}.");
            const bool wait = args->value("wait", true);
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace); // prepares the session frame the feature frames read
            const auto commands = PrepareEditorRegistrationFrame(*context.Attachment).Commands;
            if (*method == "icp")
            {
                const auto config = GetEditorRegistrationConfig(commands);
                if (!config)
                    return preview ? Ok(ReadinessJson(commands, {false, "The sandbox.registration section is unavailable."}, {{"method", "icp"}}))
                                   : Fail("The sandbox.registration section is unavailable.");
                if (preview)
                {
                    return Ok(ReadinessJson(commands, PreviewEditorRegistrationCommand(commands, *config), {{"method", "icp"}}));
                }
                if (!wait)
                {
                    const auto started = ApplyEditorRegistrationCommand(commands, *config);
                    return {.IsError = started.Status != EditorCommandStatus::Pending && !started.Succeeded(),
                        .Text = Dump(RegistrationJson(started))};
                }
                auto batch = *config;
                batch.Execution.Start = StepRunStart::Run;
                batch.Execution.AutoAccept = StepRunAutoAccept::End;
                return FinishApply<EditorRegistrationResult>(
                    context,
                    [&](auto onComplete)
                    {
                        return ApplyEditorRegistrationCommand(commands, batch, std::move(onComplete));
                    },
                    &RegistrationJson);
            }
            const auto config = GetEditorCoherentPointDriftConfig(commands);
            if (!config)
                return preview ? Ok(ReadinessJson(commands, {false, "The sandbox.coherent_point_drift section is unavailable."}, {{"method", "cpd"}}))
                               : Fail("The sandbox.coherent_point_drift section is unavailable.");
            if (preview)
            {
                return Ok(ReadinessJson(commands, PreviewEditorCoherentPointDriftCommand(commands, *config), {{"method", "cpd"}}));
            }
            if (!wait)
            {
                EditorCoherentPointDriftResult result;
                const auto run = StartEditorCoherentPointDrift(commands, *config, result);
                if (run)
                    result = SnapshotEditorCoherentPointDrift(run).Result;
                return {.IsError = !run, .Text = Dump(CoherentPointDriftJson(result))};
            }
            return FinishApply<EditorCoherentPointDriftResult>(
                context, [&](auto onComplete) { return ApplyEditorConfiguredCoherentPointDrift(commands, std::move(onComplete)); },
                &CoherentPointDriftJson);
        }
    }

    extern "C++" void RegisterProcessingAgentOperations(AgentOperationRegistry& registry)
    {
        const auto add = [&](const char* name,
                             const char* title,
                             std::string description,
                             ToolSchema schema,
                             bool readOnly,
                             AgentOperationInvoker invoke,
                             bool destructive = false,
                             bool gpu = false)
        {
            AddOperation(registry, name, title, std::move(description), std::move(schema), readOnly, std::move(invoke), destructive, gpu);
        };
        // RUNTIME-337: declared arguments, except the configured families (allOf of if/then per operation).
        static constexpr std::array<std::string_view, 2> kRegistrationMethods{"icp", "cpd"};
        const ToolSchema none = ArgumentsSchema({});
        const AgentArgument registrationMethod{
            .Spec = {.Name = "method",
                .Type = ConfigFieldType::StringEnum,
                .Description =
                    "icp uses sandbox.registration, cpd uses sandbox.coherent_point_drift (config_apply first).",
                .EnumValues = kRegistrationMethods},
            .Required = true};
        const ToolSchema registration = ArgumentsSchema({registrationMethod,
            {.Spec = {.Name = "wait",
                 .Type = ConfigFieldType::Bool,
                 .Description =
                     "true (default): run at once, accept at the end and answer when done. false: run the section's start and auto_accept and answer at once with its run id (run)."},
                .Default = true}});
        add("preview_registration",
            "Preview registration",
            "Whether the configured ICP or Coherent Point Drift registration can run, and why not.",
            ArgumentsSchema({registrationMethod}),
            true,
            [](const AgentOperationContext& c, std::string_view a)
            {
                return RunRegistration(c, a, true);
            });
        add(
            "run_registration",
            "Run registration",
            "Register the configured source entity onto the target with ICP or Coherent Point Drift and publish the "
            "result (source transform, positions or a displacement property) as one undoable step. By default the step "
            "run computes at once and accepts at its end whatever the section's start and auto_accept, and the call "
            "answers when done, with its run id (run). wait false runs the section's start and auto_accept (ready: "
            "nothing computed yet) and answers at once with its run id (run) for run_control and run_observe.",
            registration,
            false,
            [](const AgentOperationContext& c, std::string_view a)
            {
                return RunRegistration(c, a, false);
            },
            false,
            true);
        add("preview_point_sampling",
            "Preview point sampling",
            "Whether the configured point sampling (sandbox.point_sampling) can run, and why not.",
            none,
            true,
            [](const AgentOperationContext& c, std::string_view a)
            {
                return RunPointSampling(c, a, true);
            });
        const ToolSchema startable = ArgumentsSchema({{.Spec = {.Name = "wait",
                                                           .Type = ConfigFieldType::Bool,
                                                           .Description = "true (default): run at once, accept at "
                                                                          "the end and answer when done. false: run "
                                                                          "the section's start and auto_accept and "
                                                                          "answer at once with its run id (run)."},
            .Default = true}});
        add(
            "run_point_sampling",
            "Run point sampling",
            "Order the configured entity's points with the chosen sampling method (sandbox.point_sampling; config_apply "
            "first) and publish rank/selection properties or a new point cloud as one undoable step. By default runs "
            "at once and accepts at its end whatever the section's start and auto_accept; reports its run id (run, for "
            "run_observe) and steps (selected samples of exact farthest point, else 1). wait false runs the section's "
            "start and auto_accept (ready: nothing computed yet) and answers at once with its run for run_control.",
            startable,
            false,
            [](const AgentOperationContext& c, std::string_view a)
            {
                return RunPointSampling(c, a, false);
            },
            false,
            true);
        add("preview_keypoint_analysis", "Preview keypoint analysis",
            "Whether the configured keypoint analysis (sandbox.keypoint_analysis) can run, and why not.", none, true,
            [](const AgentOperationContext& c, std::string_view) { return PreviewKeypoints(c); });
        add(
            "run_keypoint_analysis",
            "Run keypoint analysis",
            "Run sandbox.keypoint_analysis at once by default (one step, accepted at its end whatever the section's "
            "start and auto_accept); GPU score and mask publish in one undoable entry. Reports backend, IO and the run "
            "id (run). wait false runs the section's start and auto_accept (ready: nothing computed yet) and answers "
            "at once with its run for run_control.",
            startable,
            false,
            [](const AgentOperationContext& c, std::string_view a)
            {
                return RunKeypoints(c, a);
            },
            false,
            true);
        const AgentArgument positions{.Spec = {.Name = "positions",
                                          .Type = ConfigFieldType::String,
                                          .Description = "Name of the vec3 position property."}};
        const ToolSchema kmeansSchema = ArgumentsSchema({EntityArgument(),
            DomainArgument(
                "Element domain of the positions; needed only while sandbox.clustering binds no properties."),
            positions});
        AgentArgument positionsDefault = positions;
        positionsDefault.Default = std::string{ECS::Components::GeometrySources::PropertyNames::kPosition};
        const ToolSchema consolidationSchema = ArgumentsSchema(
            {EntityArgument(), DomainArgument("Element domain of the positions.", true), positionsDefault});
        add("preview_kmeans", "Preview K-Means",
            "Whether K-Means can run on an entity with sandbox.clustering, and why not.", kmeansSchema, true, PreviewKMeansOperation);
        add("run_kmeans",
            "Run K-Means",
            "Cluster a point property of an entity with sandbox.clustering (config_apply first); the call runs at once "
            "and accepts at its end (GPU results atomically) whatever the section's start and auto_accept. Reports "
            "backend, IO and the GPU run's id (run). The section's bound properties win over 'domain'.",
            kmeansSchema,
            false,
            RunKMeansOperation,
            false,
            true);
        static constexpr auto kKMeansExecutionFields = StepRunExecutionFields(kMaxKMeansIterations);
        add("start_kmeans",
            "Start a K-Means step run",
            "Start K-Means on an entity with sandbox.clustering without waiting for its end: answers the step run's id "
            "once started (CPU reference or Vulkan, one step one Lloyd iteration). The section's execution options "
            "apply unless the call names max_iterations, preview_every, preview_interval_ms, auto_accept or start "
            "(ready: initialized, nothing computed); drive it with run_control and watch it with run_observe.",
            ArgumentsSchema(
                {EntityArgument(),
                    DomainArgument("Element domain of the positions; needed only while sandbox.clustering binds no "
                                   "properties."),
                    positions},
                kKMeansExecutionFields),
            false,
            StartKMeansOperation,
            false,
            true);
        add("preview_point_cloud_consolidation", "Preview point-cloud consolidation",
            "Whether consolidation can run on an entity's point property with sandbox.point_cloud_consolidation, and why not.",
            consolidationSchema, true, PreviewConsolidation);
        add("run_point_cloud_consolidation",
            "Run point-cloud consolidation",
            "Consolidate the named vec3 point property of an entity using sandbox.point_cloud_consolidation; the call "
            "runs at once and accepts at its end whatever the section's start and auto_accept. Reports backend, IO and "
            "an in-place resident LOP run's id (run).",
            consolidationSchema,
            false,
            RunConsolidation,
            false,
            true);
        static constexpr auto kConsolidationExecutionFields =
            StepRunExecutionFields(kMaxPointCloudConsolidationIterations);
        add("start_point_cloud_consolidation",
            "Start a point-cloud consolidation step run",
            "Start consolidating the named vec3 point property of an entity with sandbox.point_cloud_consolidation "
            "without waiting for its end: answers the step run's id once started (every backend; one step one "
            "projection iteration). The section's execution options apply unless the call names max_iterations, "
            "preview_every, preview_interval_ms, auto_accept or start (ready: initialized, nothing computed); drive it "
            "with run_control and watch it with run_observe.",
            ArgumentsSchema(
                {EntityArgument(), DomainArgument("Element domain of the positions.", true), positionsDefault},
                kConsolidationExecutionFields),
            false,
            StartConsolidation,
            false,
            true);
        // Operation names in table order (`family` nullptr: every operation).
        const auto operationNames = [](const char* family)
        {
            std::vector<std::string_view> names;
            for (const auto& op : ConfiguredOperations())
                if (InFamily(op, family))
                    names.emplace_back(op.Name);
            return names;
        };
        static const std::vector<std::string_view> kMeshFieldOperations = operationNames("mesh_field");
        static const std::vector<std::string_view> kOperations = operationNames(nullptr);
        const ToolSchema meshField = ArgumentsSchema(
            {{.Spec = {.Name = "operation",
                  .Type = ConfigFieldType::StringEnum,
                  .Description =
                      "Mesh-field operation; its settings come from the matching config section (config_apply first).",
                  .EnumValues = kMeshFieldOperations},
                 .Required = true},
                EntityArgument()});
        add("preview_mesh_operation", "Preview mesh operation",
            "Whether a mesh-field operation can run on an entity with the active settings, and why not. "
            "Alias of preview_operation limited to the mesh-field operations.", meshField, true,
            [](const AgentOperationContext& c, std::string_view a) { return RunConfiguredOperation(c, a, true, "mesh_field"); });
        add("run_mesh_operation", "Run mesh operation",
            "Run a mesh-field operation (property smoothing, spectral modes, harmonic field, scalar gradient) on an entity "
            "with the active config section; undoable like the panel button. Alias of run_operation limited to these.",
            meshField, false, [](const AgentOperationContext& c, std::string_view a) { return RunConfiguredOperation(c, a, false, "mesh_field"); },
            false, true); // property smoothing may select a Vulkan backend that waits on readback
        // RUNTIME-342: per-operation `params` rules, each generated from the operation's field table.
        std::vector<ArgumentCase> paramsCases;
        for (const auto& op : ConfiguredOperations())
            if (!op.ParamsSchemaJson.empty())
                paramsCases.push_back({.Argument = "operation",
                    .Value = op.Name,
                    .Property = "params",
                    .Schema = Json::parse(op.ParamsSchemaJson)});
        const AgentArgument operationArgument{.Spec = {.Name = "operation",
                                                  .Type = ConfigFieldType::StringEnum,
                                                  .Description = "Operation; its settings come from its config "
                                                                 "section (config_apply first).",
                                                  .EnumValues = kOperations},
            .Required = true};
        const AgentArgument operationEntity = EntityArgument(false,
            "Stable entity id from scene_entities; required for operations marked 'entity argument', refused "
            "for those whose section names the entity.");
        const AgentArgument operationParams{
            .Spec = {.Name = "params",
                .Type = ConfigFieldType::Object,
                .Description = "Explicit settings of the operations listed with 'params' (the schema per operation is "
                               "in allOf, keys, defaults and ranges are in the description); omitted keys keep their "
                               "defaults; enums take their name or integer code. Refused for operations that read a "
                               "config section."}};
        const AgentArgument operationWait{
            .Spec = {.Name = "wait",
                .Type = ConfigFieldType::Bool,
                .Description = "true (default): run at once, accept at the end and "
                               "answer when done. false (property_smoothing, "
                               "normal_estimation, outlier_analysis, point_construction): run the section's start and "
                               "auto_accept and answer at once with run, the step run "
                               "run_control and run_observe drive."},
            .Default = true};
        const ToolSchema configuredSchema =
            ArgumentsSchema({operationArgument, operationEntity, operationParams}, {}, {}, paramsCases);
        const ToolSchema runOperationSchema =
            ArgumentsSchema({operationArgument, operationEntity, operationParams, operationWait}, {}, {}, paramsCases);
        add("preview_operation", "Preview operation",
            "Whether a configured editor operation can run with the active settings, and why not. Operations: " +
                OperationSummary(nullptr) + ".",
            configuredSchema, true,
            [](const AgentOperationContext& c, std::string_view a) { return RunConfiguredOperation(c, a, true, nullptr); });
        add(
            "run_operation",
            "Run operation",
            "Run a configured editor operation (mesh-field operations, mesh curvature, geodesics, curvature segmentation, "
            "parameterization, scalar ridges, mesh denoise, remesh, subdivide and simplify, normal estimation, "
            "outlier analysis, point construction, "
            "progressive Poisson) with its config section or `params`; one undoable step like the panel button, answers when "
            "done; wait false starts property_smoothing, normal_estimation, outlier_analysis or point_construction as a step run with the section's start and "
            "auto_accept and answers at once with its run. Operations: " +
                OperationSummary(nullptr) + ".",
            runOperationSchema,
            false,
            [](const AgentOperationContext& c, std::string_view a)
            {
                return RunConfiguredOperation(c, a, false, nullptr);
            },
            false,
            true); // several operations can select a Vulkan backend
    }
}

// RUNTIME-337: a tool's input schema from its declared arguments, with the section schema generator
// ParamsSchema uses (declared here, where the config codecs are included).
extern "C++"
{
    namespace Extrinsic::Runtime::AgentDetail
    {
        ToolSchema ArgumentsSchema(const std::initializer_list<AgentArgument> arguments,
            const std::span<const ConfigFieldSpec> table,
            const std::initializer_list<std::initializer_list<std::string_view>> alternatives,
            const std::span<const ArgumentCase> cases)
        {
            std::vector<ConfigFieldSpec> fields;
            Json defaults = Json::object();
            Json required = Json::array();
            for (const AgentArgument& argument : arguments)
            {
                fields.push_back(argument.Spec);
                if (!argument.Default.is_null())
                    defaults[std::string(argument.Spec.Name)] = argument.Default;
                if (argument.Required)
                    required.push_back(std::string(argument.Spec.Name));
            }
            fields.insert(fields.end(), table.begin(), table.end());
            Json schema = Json::parse(ConfigDetail::BuildSectionSchemaJson("", "", "", fields, defaults));
            for (const char* key : {"$schema", "$id", "title", "description"})
                schema.erase(key);
            schema["required"] = std::move(required);
            for (const auto& names : alternatives)
            {
                Json set = Json::array();
                for (const std::string_view name : names)
                    set.push_back(std::string(name));
                schema["oneOf"].push_back({{"required", std::move(set)}});
            }
            for (const ArgumentCase& c : cases)
            {
                const std::string argument{c.Argument};
                Json condition = Json::object();
                condition["properties"][argument]["const"] = std::string(c.Value);
                condition["required"] = Json::array({argument});
                Json then = Json::object();
                then["properties"][std::string(c.Property)] = c.Schema;
                schema["allOf"].push_back({{"if", std::move(condition)}, {"then", std::move(then)}});
            }
            return {Dump(schema), true};
        }
    }
}
