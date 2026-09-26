// Typed property smoothing uses canonical domain capture and one guarded history transaction.
module;
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <entt/entity/registry.hpp>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Core.Config.Engine;
import Extrinsic.ECS.Scene.Registry;
import Extrinsic.ECS.Scene.Handle;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.ECS.Component.DirtyTags;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.EditorCommandHistory;
import Extrinsic.Runtime.EditorJobProjection;
import Extrinsic.Runtime.JobService;
import Extrinsic.Runtime.MeshSurfaceTopology;
import Geometry.HalfedgeMesh;
import Geometry.Smoothing;
import Geometry.DEC;
import Geometry.Properties;
#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Editor/internal/Runtime.EditorProcessingAccess.hpp"
#include "Editor/internal/Runtime.EditorGeometryHelpers.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.PointFields.hpp"
#include "Editor/Operations/Runtime.GeometryProcessingOperations.MeshSources.hpp"
#include "Editor/Operations/Runtime.MeshFieldOperations.PropertyGraph.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        namespace S = Geometry::Smoothing;
        namespace GP = GeometryProcessingDetail;
        namespace GS = ECS::Components::GeometrySources;
        using D = GeometryElementDomain;
        using K = Geometry::PropertyValueKind;
        using Json = nlohmann::json;
        constexpr std::string_view kSchema = "intrinsic.runtime.sandbox.property_smoothing";
        using PropertyGraphDetail::Snapshot;
        using PropertyGraphDetail::Channels;
        using PropertyGraphDetail::Channel;
        using PropertyGraphDetail::SetChannel;
        using PropertyGraphDetail::CountMatches;
        using PropertyGraphDetail::Capture;
        using PropertyGraphDetail::Same;
        PropertySmoothingConfig Decode(const Json& doc)
        {
            PropertySmoothingConfig c;
            ConfigDetail::DecodePointPropertyRef(doc.at("input"), c.Input);
            ConfigDetail::DecodePointPropertyRef(doc.at("output"), c.Output);
            ConfigDetail::DecodePointPropertyRef(doc.at("positions"), c.Positions);
            c.Weight = S::PropertyWeight(doc.at("weight").get<unsigned>());
            c.Filter.Method = S::PropertyFilter(doc.at("method").get<unsigned>());
            c.Filter.Laplacian = S::PropertyLaplacian(doc.at("laplacian").get<unsigned>());
            c.Filter.Iterations = doc.at("iterations").get<std::uint32_t>();
            c.Neighbors = doc.at("neighbors").get<std::uint32_t>();
            c.SpatialSigma = doc.at("spatial_sigma").get<double>();
            c.Filter.Lambda = doc.at("lambda").get<double>();
            c.Filter.Mu = doc.at("mu").get<double>();
            c.Filter.HeatTime = doc.at("heat_time").get<double>();
            c.Filter.RangeSigma = doc.at("range_sigma").get<double>();
            c.Filter.TimeStep = doc.at("time_step").get<double>();
            c.Filter.SolverTolerance = doc.at("solver_tolerance").get<double>();
            c.Filter.MaxSolverIterations = doc.at("max_solver_iterations").get<std::uint32_t>();
            c.Filter.Solver = S::PropertySolver(doc.at("solver").get<unsigned>());
            c.PreserveBoundary = doc.at("preserve_boundary").get<bool>();
            return c;
        }
        Core::Config::EngineConfigSectionValidationResult Validate(
            std::string_view payload, std::string_view, std::string_view subject)
        {
            const auto doc = Json::parse(payload, nullptr, false);
            auto merged = Json::parse(SerializePropertySmoothingConfig({}));
            if (auto error = ConfigDetail::ValidatePointConfigFields(doc, merged,
                "Smoothing config must be an object.", "Unknown smoothing field: ",
                {"method", "weight", "laplacian", "solver", "iterations", "neighbors", "max_solver_iterations"}))
                return ConfigDetail::RejectConfigSection(subject, *error);
            for (const auto key : {"input", "output", "positions"})
            {
                bool valid = false;
                for (auto kind : {K::Float, K::Double, K::Vec2, K::Vec3, K::Vec4})
                    valid |= ConfigDetail::ValidatePointPropertyRef(merged[key], kind) == ConfigDetail::PointPropertyValidation::Valid;
                if (!valid) return ConfigDetail::RejectConfigSection(subject, "Bindings require floating scalar or vector properties on a resolved domain.");
            }
            for (const auto key : {"spatial_sigma", "lambda", "mu", "heat_time", "range_sigma", "time_step", "solver_tolerance"})
                if (!merged[key].is_number() || !std::isfinite(merged[key].get<double>()))
                    return ConfigDetail::RejectConfigSection(subject, "Filter parameters must be finite numbers.");
            if (merged["method"].get<unsigned>() > unsigned(S::PropertyFilter::Implicit) ||
                merged["weight"].get<unsigned>() > unsigned(S::PropertyWeight::MeshUniform) ||
                merged["laplacian"].get<unsigned>() > unsigned(S::PropertyLaplacian::LumpedMass) ||
                merged["solver"].get<unsigned>() > unsigned(S::PropertySolver::ConjugateGradient))
                return ConfigDetail::RejectConfigSection(subject, "Unknown smoothing method, weight, Laplacian or solver.");
            if (!merged["preserve_boundary"].is_boolean())
                return ConfigDetail::RejectConfigSection(subject, "preserve_boundary must be boolean.");
            const auto c = Decode(merged);
            if (!S::ValidatePropertyFilterParams(c.Filter) || c.Neighbors < 1 || c.Neighbors > 1024 || c.SpatialSigma <= 0)
                return ConfigDetail::RejectConfigSection(subject, "Invalid smoothing parameters: iterations 1..10000, neighbors 1..1024, lambda (0,1], mu [-1,0), heat time (0,1000], positive sigmas/time step, solver tolerance (0,1), solver iterations 1..100000.");
            if (c.Input.Domain != c.Output.Domain || GeometryPropertyComponentCount(c.Input.ValueKind) != GeometryPropertyComponentCount(c.Output.ValueKind) ||
                c.Positions.ValueKind != K::Vec3 || c.Input.Name.empty() || c.Output.Name.empty() || c.Positions.Name.empty() ||
                IsTopologyProperty(c.Output.Domain, c.Output.Name) ||
                (IsStructuralVertexProperty(c.Output.Name) && c.Output.Name != "v:position") ||
                (c.Output.Name == "v:position" && (c.Output.ValueKind != K::Vec3 || c.Output.Domain != c.Positions.Domain)))
                return ConfigDetail::RejectConfigSection(subject, "Output must have matching channels on the input domain and cannot replace structural storage.");
            if ((c.Weight == S::PropertyWeight::Cotangent || c.Weight == S::PropertyWeight::MeshUniform ||
                 c.PreserveBoundary || c.Filter.Laplacian == S::PropertyLaplacian::LumpedMass) &&
                (c.Input.Domain != D::MeshVertex || c.Positions.Domain != D::MeshVertex))
                return ConfigDetail::RejectConfigSection(subject, "Mesh topology, boundary pinning and lumped mass require mesh vertices and vertex positions.");
            return {.State = Core::Config::EngineConfigState::Valid,
                    .CanonicalPayloadJson = SerializePropertySmoothingConfig(c),
                    .ParsedFieldCount = static_cast<std::uint32_t>(doc.size())};
        }
        Core::Config::EngineConfigSection Section(const PropertySmoothingConfig& c)
        { return {.Name = std::string{kPropertySmoothingConfigSectionName}, .SchemaId = std::string{kSchema},
                  .SchemaVersion = 1u, .PayloadJson = SerializePropertySmoothingConfig(c)}; }

        std::optional<ECS::EntityHandle> Target(const EditorProcessingContext& context, std::uint32_t id,
            const PropertySmoothingConfig& c, std::string& diagnostic)
        {
            const auto validation = Validate(SerializePropertySmoothingConfig(c), {}, kPropertySmoothingConfigSectionName);
            if (!validation.Usable()) { diagnostic = validation.Diagnostics.front().Message; return {}; }
            if (!context.Scene || (context.AttachmentActive && !context.AttachmentActive()) || !GP::EditorProcessingContextWorldCurrent(context)) { diagnostic = "Workspace is unavailable."; return {}; }
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), id);
            if (!entity) { diagnostic = "Choose an existing geometry entity."; return {}; }
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            const auto* props = ResolveGeometryPropertySet(a, c.Input.Domain);
            if (!props || (!ResolveGeometryProperty(a, c.Input, props->Size(), false).Resolved() || !CountMatches(*props, c.Input)))
            { diagnostic = "Choose an existing floating scalar or vector property."; return {}; }
            if (props->Exists(c.Output.Name) && (!ResolveGeometryProperty(a, c.Output, props->Size(), false).Resolved() || !CountMatches(*props, c.Output)))
            { diagnostic = "Output exists with a different storage type or cardinality."; return {}; }
            if (c.Positions.Domain != c.Input.Domain && c.Positions.Domain != GP::PrimaryPointDomain(a))
            { diagnostic = "Positions must belong to the input domain or the entity's vertices/nodes."; return {}; }
            const auto* positions = ResolveGeometryPropertySet(a, c.Positions.Domain);
            if (!positions || !ResolveGeometryProperty(a, c.Positions, positions->Size(), false).Resolved())
            { diagnostic = "Choose an existing vec3 position property."; return {}; }
            return entity;
        }
    }

    std::string SerializePropertySmoothingConfig(const PropertySmoothingConfig& c)
    {
        return Json{{"input", ConfigDetail::EncodePointPropertyRef(c.Input)}, {"output", ConfigDetail::EncodePointPropertyRef(c.Output)},
            {"positions", ConfigDetail::EncodePointPropertyRef(c.Positions)}, {"weight", unsigned(c.Weight)},
            {"method", unsigned(c.Filter.Method)}, {"laplacian", unsigned(c.Filter.Laplacian)},
            {"iterations", c.Filter.Iterations}, {"neighbors", c.Neighbors}, {"spatial_sigma", c.SpatialSigma},
            {"lambda", c.Filter.Lambda}, {"mu", c.Filter.Mu}, {"heat_time", c.Filter.HeatTime}, {"range_sigma", c.Filter.RangeSigma},
            {"time_step", c.Filter.TimeStep}, {"solver_tolerance", c.Filter.SolverTolerance},
            {"max_solver_iterations", c.Filter.MaxSolverIterations}, {"solver", unsigned(c.Filter.Solver)}, {"preserve_boundary", c.PreserveBoundary}}.dump();
    }
    Core::Config::EngineConfigSectionRegistration MakePropertySmoothingConfigSectionRegistration()
    { return {.DefaultSection = Section({}), .Validate = Validate}; }
    RuntimeEngineConfigApplyResult ApplyEditorPropertySmoothingConfig(const EditorProcessingCommands& commands, const PropertySmoothingConfig& c)
    {
        return ApplyEditorProcessingConfig(commands, Validate(SerializePropertySmoothingConfig(c), {}, kPropertySmoothingConfigSectionName),
            std::string{kPropertySmoothingConfigSectionName}, [&](Core::Config::EngineConfig& config) {
                Core::Config::UpsertEngineConfigSection(config.AppSections, Section(c)); });
    }
    std::optional<PropertySmoothingConfig> GetEditorPropertySmoothingConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return {};
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(context.EngineConfigControlState->ActiveConfig,
            kPropertySmoothingConfigSectionName, kSchema, 1u, nullptr, Validate);
        return payload ? std::optional{Decode(Json::parse(*payload))} : std::nullopt;
    }
    ActionReadiness PreviewEditorPropertySmoothingCommand(const EditorProcessingCommands& commands, std::uint32_t id, const PropertySmoothingConfig& c)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        const auto entity = Target(context, id, c, diagnostic);
        return {entity.has_value(), std::move(diagnostic)};
    }
    EditorPropertySmoothingResult ApplyEditorPropertySmoothingCommand(const EditorProcessingCommands& commands, std::uint32_t id, const PropertySmoothingConfig& c)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        EditorPropertySmoothingResult result;
        const auto fail = [&](std::string message) { result.Status = EditorCommandStatus::InvalidProcessingParameters; result.Message = std::move(message); return result; };
        std::string diagnostic;
        const auto entity = Target(context, id, c, diagnostic);
        if (!entity) return fail(diagnostic);
        const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
        GP::PointInputCapture samples;
        if (!PropertyGraphDetail::CaptureSamples(a, c.Input.Domain, c.Positions, samples, diagnostic)) return fail(diagnostic);
        const auto* props = ResolveGeometryPropertySet(a, c.Input.Domain);
        const auto input = Capture(*props, c.Input), before = Capture(*props, c.Output);
        const auto channels = GeometryPropertyComponentCount(c.Input.ValueKind);
        std::vector<double> values;
        std::visit([&](const auto& rows) {
            for (const auto slot : samples.Slots)
                for (std::size_t ch = 0; ch < channels; ++ch) values.push_back(Channel(rows[slot], ch));
        }, input.Values);
        PropertyGraphDetail::Graph graph;
        if (!PropertyGraphDetail::BuildGraph(a, {.Positions = c.Positions, .Weight = c.Weight, .Neighbors = c.Neighbors,
                .SpatialSigma = c.SpatialSigma, .LumpedMass = c.Filter.Laplacian == S::PropertyLaplacian::LumpedMass,
                .BoundaryRows = c.PreserveBoundary, .Operation = "Property smoothing"}, samples, graph, diagnostic))
            return fail(diagnostic);
        const auto filtered = S::FilterProperty(values, channels, graph.Edges, c.Filter, graph.BoundaryRows, graph.Mass);
        if (!filtered.Success) return fail(filtered.Diagnostic);
        Snapshot after = before;
        after.Exists = true;
        bool representable = true;
        std::visit([&](auto& rows) {
            using T = typename std::decay_t<decltype(rows)>::value_type;
            rows.resize(samples.SlotCount, T{0});
            for (std::size_t i = 0; i < samples.Slots.size(); ++i)
                for (std::size_t ch = 0; ch < Channels<T>(); ++ch)
                {
                    const double value = filtered.Values[i * channels + ch];
                    if constexpr (!std::is_same_v<T, double>)
                        if (std::abs(value) > std::numeric_limits<float>::max()) { representable = false; continue; }
                    SetChannel(rows[samples.Slots[i]], ch, value);
                }
        }, after.Values);
        if (!representable) return fail("Smoothed values exceed output storage range.");
        result.LiveCount = samples.LiveCount;
        result.EdgeCount = graph.Edges.size();
        result.OperatorApplications = filtered.OperatorApplications;
        // The diagnostic starts with the backend identity; a CG fallback note may follow.
        result.BackendId = filtered.Diagnostic.substr(0, filtered.Diagnostic.find(' '));
        if (Same(before, after)) { result.Message = "Smoothed property is unchanged."; return result; }
        // An aliased output is guarded by its expected values; unrelated input revisions stay fixed.
        samples.Inputs.push_back(GP::ObserveGeometryProperty(a, c.Input.Domain, c.Input.Name));
        std::erase_if(samples.Inputs, [&](const auto& watch) { return watch.Domain == c.Output.Domain && watch.Name == c.Output.Name; });
        const auto mutate = [context, entity = *entity, output = c.Output, watches = std::move(samples.Inputs)]
            (const Snapshot& expected, const Snapshot& target) {
            if (!GP::GeometryPropertiesCurrent(context, entity, watches) || !GP::EditorProcessingContextWorldCurrent(context))
                return EditorCommandHistoryStatus::StaleEntity;
            auto* properties = GP::MutableGeometryProperties(context.Scene->Raw(), entity, output.Domain);
            if (!properties || !Same(Capture(*properties, output), expected)) return EditorCommandHistoryStatus::StaleEntity;
            std::visit([&](const auto& rows) {
                using T = typename std::decay_t<decltype(rows)>::value_type;
                if (target.Exists) properties->GetOrAdd<T>(output.Name).Vector() = rows;
                else if (auto property = properties->Get<T>(output.Name)) properties->Remove(property);
            }, target.Values);
            ECS::Components::DirtyTags::MarkGpuDirty(context.Scene->Raw(), entity);
            if (output.Name == "v:position") ECS::Components::DirtyTags::MarkVertexPositionsDirty(context.Scene->Raw(), entity);
            if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
            return EditorCommandHistoryStatus::Applied;
        };
        const auto status = context.CommandHistory ? context.CommandHistory->Execute({.Label = "Smooth property",
            .Redo = [mutate, before, after] { return mutate(before, after); },
            .Undo = [mutate, before, after] { return mutate(after, before); }}).Status : mutate(before, after);
        result.Status = EditorFeatureDetail::ToEditorCommandStatus(status);
        result.Message = result.Succeeded() ? "Property smoothed (" + filtered.Diagnostic + ")." : "Property publication rejected by history guards.";
        return result;
    }
}
