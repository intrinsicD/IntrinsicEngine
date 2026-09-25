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
        using Storage = std::variant<std::vector<float>, std::vector<double>, std::vector<glm::vec2>,
                                     std::vector<glm::vec3>, std::vector<glm::vec4>>;
        struct Snapshot { bool Exists{}; Storage Values{}; };

        template<class T> constexpr std::size_t Channels()
        { if constexpr (std::is_arithmetic_v<T>) return 1; else return T::length(); }
        template<class T> double Channel(const T& v, std::size_t c)
        { if constexpr (std::is_arithmetic_v<T>) return v; else return v[c]; }
        template<class T> void SetChannel(T& v, std::size_t c, double x)
        { if constexpr (std::is_arithmetic_v<T>) v = static_cast<T>(x); else v[c] = static_cast<float>(x); }
        Storage EmptyStorage(K k)
        {
            switch (k) {
            case K::Double: return std::vector<double>{};
            case K::Vec2: return std::vector<glm::vec2>{};
            case K::Vec3: return std::vector<glm::vec3>{};
            case K::Vec4: return std::vector<glm::vec4>{};
            default: return std::vector<float>{}; }
        }
        bool CountMatches(const Geometry::PropertySet& props, const GeometryPropertyRef& ref)
        {
            return std::visit([&](const auto& values) {
                using T = typename std::decay_t<decltype(values)>::value_type;
                const auto property = props.Get<T>(ref.Name);
                return property && property.Size() == props.Size();
            }, EmptyStorage(ref.ValueKind));
        }
        Snapshot Capture(const Geometry::PropertySet& props, const GeometryPropertyRef& ref)
        {
            Snapshot s{props.Exists(ref.Name), EmptyStorage(ref.ValueKind)};
            std::visit([&](auto& values) {
                using T = typename std::decay_t<decltype(values)>::value_type;
                if (const auto property = props.Get<T>(ref.Name)) values = property.Vector();
            }, s.Values);
            return s;
        }
        bool Same(const Snapshot& a, const Snapshot& b)
        {
            if (a.Exists != b.Exists || a.Values.index() != b.Values.index()) return false;
            return std::visit([&](const auto& values) {
                return GeometryValueComparison::BitEqual(values, std::get<std::decay_t<decltype(values)>>(b.Values));
            }, a.Values);
        }
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

        bool CaptureSamples(const GeometryEntityAvailability& a, const PropertySmoothingConfig& c,
            GP::PointInputCapture& capture, std::string& diagnostic)
        {
            auto positions = c.Positions;
            if (positions.Domain == c.Input.Domain)
                return GP::CapturePointInput(a, positions, true, capture, diagnostic);
            const auto* props = ResolveGeometryPropertySet(a, c.Input.Domain);
            const auto* vertices = ResolveGeometryPropertySet(a, positions.Domain);
            GP::PointInputCapture vertexCapture;
            if (!GP::CapturePointInput(a, positions, true, vertexCapture, diagnostic)) return false;
            capture.Inputs = std::move(vertexCapture.Inputs);
            capture.SlotCount = props->Size();
            const auto source = vertices->Get<glm::vec3>(positions.Name);
            std::vector<glm::vec3> anchors;
            if (c.Input.Domain == D::MeshFace)
            {
                if (BuildMeshFaceCenters(a.SourceView, source.Span(), anchors, capture.Slots) != MeshSurfaceTopologyStatus::Success)
                { diagnostic = "Unable to derive face centers from mesh topology."; return false; }
                const auto deleted = props->Get<bool>("f:deleted");
                if (anchors.size() != props->Size() || (props->Exists("f:deleted") && (!deleted || deleted.Size() != props->Size())))
                { diagnostic = "Invalid face or deletion cardinality."; return false; }
                std::size_t live = 0;
                for (std::size_t row = 0; row < props->Size(); ++row)
                    if (!deleted || !deleted[row]) ++live;
                if (capture.Slots.size() != live)
                { diagnostic = "A live face has no valid finite center."; return false; }
            }
            else
            {
                const bool halfedge = c.Input.Domain == D::MeshHalfedge || c.Input.Domain == D::GraphHalfedge;
                const auto edgeDomain = c.Input.Domain == D::MeshHalfedge ? D::MeshEdge : c.Input.Domain == D::GraphHalfedge ? D::GraphEdge : c.Input.Domain;
                const auto* edges = ResolveGeometryPropertySet(a, edgeDomain);
                if (!edges) { diagnostic = "Missing edge topology."; return false; }
                const auto v0 = edges->Get<std::uint32_t>(GS::PropertyNames::kEdgeV0);
                const auto v1 = edges->Get<std::uint32_t>(GS::PropertyNames::kEdgeV1);
                const auto deleted = edges->Get<bool>("e:deleted");
                if (!v0 || !v1 || v0.Size() != edges->Size() || v1.Size() != edges->Size() ||
                    (edges->Exists("e:deleted") && (!deleted || deleted.Size() != edges->Size())) ||
                    capture.SlotCount != edges->Size() * (halfedge ? 2 : 1))
                { diagnostic = "Invalid edge endpoint or deletion storage."; return false; }
                std::vector<bool> liveVertex(vertices->Size(), false);
                for (auto slot : vertexCapture.Slots) liveVertex[slot] = true;
                anchors.resize(capture.SlotCount);
                for (std::size_t row = 0; row < capture.SlotCount; ++row)
                {
                    const auto e = halfedge ? row / 2 : row;
                    if (deleted && deleted[e]) continue;
                    if (v0[e] >= source.Size() || v1[e] >= source.Size() || !liveVertex[v0[e]] || !liveVertex[v1[e]])
                    { diagnostic = "Live edge references an invalid or deleted vertex."; return false; }
                    anchors[row] = glm::vec3(0.5 * (glm::dvec3(source[v0[e]]) + glm::dvec3(source[v1[e]])));
                    capture.Slots.push_back(static_cast<std::uint32_t>(row));
                }
            }
            // Topology and deletion revisions guard derived anchors through undo/redo.
            for (unsigned domain = unsigned(D::MeshVertex); domain <= unsigned(D::PointCloudPoint); ++domain)
                if (const auto* set = ResolveGeometryPropertySet(a, D(domain)))
                    for (const auto& name : set->Properties())
                        if (IsTopologyProperty(D(domain), name)) capture.Inputs.push_back(GP::ObserveGeometryProperty(a, D(domain), name));
            for (auto slot : capture.Slots) capture.Points.push_back(anchors[slot]);
            capture.LiveCount = capture.Slots.size();
            if (!capture.LiveCount) { diagnostic = "No live property rows."; return false; }
            return true;
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
        if (!CaptureSamples(a, c, samples, diagnostic)) return fail(diagnostic);
        const auto* props = ResolveGeometryPropertySet(a, c.Input.Domain);
        const auto input = Capture(*props, c.Input), before = Capture(*props, c.Output);
        const auto channels = GeometryPropertyComponentCount(c.Input.ValueKind);
        std::vector<double> values;
        std::visit([&](const auto& rows) {
            for (const auto slot : samples.Slots)
                for (std::size_t ch = 0; ch < channels; ++ch) values.push_back(Channel(rows[slot], ch));
        }, input.Values);
        std::optional<std::vector<S::PropertyEdge>> edges;
        std::vector<std::size_t> fixedRows;
        std::vector<double> mass;
        const bool meshWeights = c.Weight == S::PropertyWeight::Cotangent || c.Weight == S::PropertyWeight::MeshUniform;
        if (meshWeights || c.PreserveBoundary || c.Filter.Laplacian == S::PropertyLaplacian::LumpedMass)
        {
            auto mesh = GP::MeshSupport::BuildHalfedgeMeshForProcessing(a.SourceView, "Property smoothing", c.Positions.Name);
            if (!mesh.Succeeded()) return fail(mesh.Diagnostic);
            if (mesh.Mesh.VerticesSize() != samples.SlotCount) return fail("Mesh vertex correspondence mismatch.");
            if (c.Filter.Laplacian == S::PropertyLaplacian::LumpedMass)
            {
                const auto areas = Geometry::DEC::BuildHodgeStar0(mesh.Mesh);
                for (auto slot : samples.Slots)
                {
                    const Geometry::VertexHandle v{static_cast<Geometry::PropertyIndex>(slot)};
                    mass.push_back(mesh.Mesh.IsIsolated(v) ? 1.0 : areas.Diagonal[slot]);
                }
            }
            if (c.PreserveBoundary)
                for (std::size_t i = 0; i < samples.Slots.size(); ++i)
                    if (mesh.Mesh.IsBoundary(Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(samples.Slots[i])}))
                        fixedRows.push_back(i);
            std::vector<std::size_t> inverse(samples.SlotCount, samples.SlotCount);
            for (std::size_t i = 0; i < samples.Slots.size(); ++i) inverse[samples.Slots[i]] = i;
            if (c.Weight == S::PropertyWeight::MeshUniform)
            {
                edges.emplace();
                for (std::size_t e = 0; e < mesh.Mesh.EdgesSize(); ++e)
                {
                    if (mesh.Mesh.IsDeleted(Geometry::EdgeHandle{static_cast<Geometry::PropertyIndex>(e)})) continue;
                    const Geometry::HalfedgeHandle h{static_cast<Geometry::PropertyIndex>(2 * e)};
                    const auto i = mesh.Mesh.FromVertex(h).Index, j = mesh.Mesh.ToVertex(h).Index;
                    if (inverse[i] != samples.SlotCount && inverse[j] != samples.SlotCount)
                        edges->push_back({inverse[i], inverse[j], 1.0});
                }
            }
            else if (c.Weight == S::PropertyWeight::Cotangent)
            {
                // Cotangent weights are only assembled when selected; kNN weights ignore them.
                const auto laplacian = Geometry::DEC::BuildLaplacian(mesh.Mesh);
                edges.emplace();
                for (std::size_t i = 0; i < laplacian.Rows; ++i)
                    for (auto entry = laplacian.RowOffsets[i]; entry < laplacian.RowOffsets[i+1]; ++entry)
                    {
                        const auto j = laplacian.ColIndices[entry];
                        if (j > i && inverse[i] != samples.SlotCount && inverse[j] != samples.SlotCount)
                            edges->push_back({inverse[i], inverse[j], std::max(0.0, -laplacian.Values[entry])});
                    }
            }
            for (unsigned d = unsigned(D::MeshVertex); d <= unsigned(D::MeshFace); ++d)
                if (const auto* set = ResolveGeometryPropertySet(a, D(d)))
                    for (const auto& name : set->Properties())
                        if (IsTopologyProperty(D(d), name)) samples.Inputs.push_back(GP::ObserveGeometryProperty(a, D(d), name));
        }
        if (!meshWeights)
            edges = S::BuildPropertyNeighborhood(samples.Points, c.Neighbors, c.Weight, c.SpatialSigma);
        if (!edges) return fail("Invalid spatial neighborhood; check finite positions, coordinate bounds and sigma.");
        const auto filtered = S::FilterProperty(values, channels, *edges, c.Filter, fixedRows, mass);
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
        result.EdgeCount = edges->size();
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
