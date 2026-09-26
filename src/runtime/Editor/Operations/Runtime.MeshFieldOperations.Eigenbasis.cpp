// Spectral modes: the k smallest eigenpairs of the shared sample-graph Laplacian, or on mesh
// vertices of the modified Dirichlet energy or the discrete-shells Hessian (Geometry.ModalAnalysis),
// with unit or lumped-area mass (Geometry.Sparse eigensolver). Modes, the optional modal signature
// and the multi-scale distance publish in one guarded history transaction.
module;
#include "GeometryIntegration/Runtime.GeometryValueComparison.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
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
import Geometry.HalfedgeMesh;
import Geometry.DEC;
import Geometry.ModalAnalysis;
import Geometry.Smoothing;
import Geometry.Sparse;
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
        namespace MA = Geometry::ModalAnalysis;
        namespace Sp = Geometry::Sparse;
        namespace GP = GeometryProcessingDetail;
        namespace PG = PropertyGraphDetail;
        using D = GeometryElementDomain;
        using K = Geometry::PropertyValueKind;
        using Json = nlohmann::json;
        constexpr std::string_view kSchema = "intrinsic.runtime.sandbox.laplacian_eigenbasis";
        constexpr std::uint32_t kMaxCount = 256;

        LaplacianEigenbasisConfig Decode(const Json& doc)
        {
            LaplacianEigenbasisConfig c;
            c.Operator = ModalOperator(doc.at("operator").get<unsigned>());
            c.Domain = D(doc.at("domain").get<unsigned>());
            ConfigDetail::DecodePointPropertyRef(doc.at("positions"), c.Positions);
            c.Weight = S::PropertyWeight(doc.at("weight").get<unsigned>());
            c.Neighbors = doc.at("neighbors").get<std::uint32_t>();
            c.SpatialSigma = doc.at("spatial_sigma").get<double>();
            c.LumpedMass = doc.at("lumped_mass").get<bool>();
            c.Count = doc.at("count").get<std::uint32_t>();
            c.OutputPrefix = doc.at("output_prefix").get<std::string>();
            c.MaxIterations = doc.at("max_iterations").get<std::uint32_t>();
            c.Tolerance = doc.at("tolerance").get<double>();
            c.ShellFlexural = doc.at("shell_flexural").get<double>();
            c.ShellLength = doc.at("shell_length").get<double>();
            c.ShellArea = doc.at("shell_area").get<double>();
            c.SkipModes = doc.at("skip_modes").get<std::uint32_t>();
            c.SignatureOutput = doc.at("signature_output").get<std::string>();
            c.SignatureScale = doc.at("signature_scale").get<double>();
            c.DistanceSource = doc.at("distance_source").get<std::int64_t>();
            c.DistanceOutput = doc.at("distance_output").get<std::string>();
            c.DistanceSamples = doc.at("distance_samples").get<std::uint32_t>();
            return c;
        }

        std::string OutputName(const LaplacianEigenbasisConfig& c, std::uint32_t j) { return c.OutputPrefix + std::to_string(j); }
        K ModeKind(const LaplacianEigenbasisConfig& c) { return c.Operator == ModalOperator::ThinShell ? K::Vec3 : K::Float; }
        // Every published property with its storage kind: modes, then signature and distance.
        std::vector<GeometryPropertyRef> Outputs(const LaplacianEigenbasisConfig& c)
        {
            std::vector<GeometryPropertyRef> refs;
            for (std::uint32_t j = 0; j < c.Count; ++j) refs.push_back({c.Domain, OutputName(c, j), ModeKind(c)});
            if (!c.SignatureOutput.empty()) refs.push_back({c.Domain, c.SignatureOutput, K::Float});
            if (c.DistanceSource >= 0) refs.push_back({c.Domain, c.DistanceOutput, K::Float});
            return refs;
        }

        Core::Config::EngineConfigSectionValidationResult Validate(
            std::string_view payload, std::string_view, std::string_view subject)
        {
            const auto doc = Json::parse(payload, nullptr, false);
            auto merged = Json::parse(SerializeLaplacianEigenbasisConfig({}));
            if (auto error = ConfigDetail::ValidatePointConfigFields(doc, merged,
                "Eigenbasis config must be an object.", "Unknown eigenbasis field: ",
                {"operator", "domain", "weight", "neighbors", "count", "max_iterations", "skip_modes", "distance_samples"}))
                return ConfigDetail::RejectConfigSection(subject, *error);
            const auto reject = [&](std::string message) { return ConfigDetail::RejectConfigSection(subject, std::move(message)); };
            if (ConfigDetail::ValidatePointPropertyRef(merged["positions"], K::Vec3) != ConfigDetail::PointPropertyValidation::Valid)
                return reject("Positions need a vec3 property reference.");
            for (const auto key : {"spatial_sigma", "tolerance", "shell_flexural", "shell_length", "shell_area", "signature_scale"})
                if (!merged[key].is_number() || !std::isfinite(merged[key].get<double>()))
                    return reject("spatial_sigma, tolerance, shell weights and signature_scale must be finite numbers.");
            if (!merged["lumped_mass"].is_boolean()) return reject("lumped_mass must be boolean.");
            if (!merged["distance_source"].is_number_integer()) return reject("distance_source must be an integer.");
            if (!merged["output_prefix"].is_string() || merged["output_prefix"].get<std::string>().empty())
                return reject("output_prefix must be a nonempty string.");
            if (!merged["signature_output"].is_string() || !merged["distance_output"].is_string())
                return reject("signature_output and distance_output must be strings.");
            if (merged["operator"].get<unsigned>() > unsigned(ModalOperator::ThinShell)) return reject("Unknown modal operator.");
            const auto domain = merged["domain"].get<unsigned>();
            if (domain < unsigned(D::MeshVertex) || domain > unsigned(D::PointCloudPoint) ||
                merged["weight"].get<unsigned>() > unsigned(S::PropertyWeight::MeshUniform))
                return reject("Unknown domain or weight.");
            const auto c = Decode(merged);
            if (c.Count < 1 || c.Count > kMaxCount || c.Neighbors < 1 || c.Neighbors > 1024 || !(c.SpatialSigma > 0) ||
                c.MaxIterations < 1 || c.MaxIterations > 100000 || !(c.Tolerance > 0) || !(c.Tolerance < 1))
                return reject("Invalid parameters: count 1..256, neighbors 1..1024, positive sigma, iterations 1..100000, tolerance (0,1).");
            if (c.ShellFlexural < 0 || c.ShellLength < 0 || c.ShellArea < 0 || !(c.ShellFlexural + c.ShellLength + c.ShellArea > 0))
                return reject("Shell weights must be nonnegative and not all zero.");
            if (c.SkipModes >= c.Count || c.SignatureScale < 0 || c.SignatureScale > 1 || c.DistanceSamples < 1 || c.DistanceSamples > 4096)
                return reject("Invalid signature parameters: skipped modes below count, scale in [0,1], distance samples 1..4096.");
            if (c.DistanceSource >= 0 && c.DistanceOutput.empty()) return reject("Distance output must be named.");
            const auto outputs = Outputs(c);
            for (std::size_t i = 0; i < outputs.size(); ++i)
            {
                if (IsTopologyProperty(c.Domain, outputs[i].Name) || IsStructuralVertexProperty(outputs[i].Name))
                    return reject("Mode, signature and distance outputs cannot replace structural storage.");
                for (std::size_t j = 0; j < i; ++j)
                    if (outputs[i].Name == outputs[j].Name) return reject("Output '" + outputs[i].Name + "' is used twice.");
            }
            const bool meshOperator = c.Operator != ModalOperator::GraphLaplacian;
            const bool meshTopology = meshOperator || c.Weight == S::PropertyWeight::Cotangent || c.Weight == S::PropertyWeight::MeshUniform || c.LumpedMass;
            if (meshTopology && (c.Domain != D::MeshVertex || c.Positions.Domain != D::MeshVertex))
                return reject("Mesh operators, mesh weights and lumped mass require mesh vertices and vertex positions.");
            return {.State = Core::Config::EngineConfigState::Valid,
                    .CanonicalPayloadJson = SerializeLaplacianEigenbasisConfig(c),
                    .ParsedFieldCount = static_cast<std::uint32_t>(doc.size())};
        }
        Core::Config::EngineConfigSection Section(const LaplacianEigenbasisConfig& c)
        { return {.Name = std::string{kLaplacianEigenbasisConfigSectionName}, .SchemaId = std::string{kSchema},
                  .SchemaVersion = 1u, .PayloadJson = SerializeLaplacianEigenbasisConfig(c)}; }

        std::optional<ECS::EntityHandle> Target(const EditorProcessingContext& context, std::uint32_t id,
            const LaplacianEigenbasisConfig& c, std::string& diagnostic)
        {
            const auto validation = Validate(SerializeLaplacianEigenbasisConfig(c), {}, kLaplacianEigenbasisConfigSectionName);
            if (!validation.Usable()) { diagnostic = validation.Diagnostics.front().Message; return {}; }
            if (!context.Scene || (context.AttachmentActive && !context.AttachmentActive()) || !GP::EditorProcessingContextWorldCurrent(context))
            { diagnostic = "Workspace is unavailable."; return {}; }
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), id);
            if (!entity) { diagnostic = "Choose an existing geometry entity."; return {}; }
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            const auto* props = ResolveGeometryPropertySet(a, c.Domain);
            if (!props) { diagnostic = "The entity has no rows on the selected domain."; return {}; }
            for (const auto& ref : Outputs(c))
            {
                if (props->Exists(ref.Name) && !(ResolveGeometryProperty(a, ref, props->Size(), false).Resolved() && PG::CountMatches(*props, ref)))
                { diagnostic = "Output '" + ref.Name + "' exists with a different storage type or cardinality."; return {}; }
            }
            if (c.Positions.Domain != c.Domain && c.Positions.Domain != GP::PrimaryPointDomain(a))
            { diagnostic = "Positions must belong to the selected domain or the entity's vertices/nodes."; return {}; }
            const auto* positions = ResolveGeometryPropertySet(a, c.Positions.Domain);
            if (!positions || !ResolveGeometryProperty(a, c.Positions, positions->Size(), false).Resolved())
            { diagnostic = "Choose an existing vec3 position property."; return {}; }
            return entity;
        }

        struct Publication { GeometryPropertyRef Ref; PG::Snapshot Before, After; };
    }

    std::string SerializeLaplacianEigenbasisConfig(const LaplacianEigenbasisConfig& c)
    {
        return Json{{"operator", unsigned(c.Operator)}, {"domain", unsigned(c.Domain)}, {"positions", ConfigDetail::EncodePointPropertyRef(c.Positions)},
            {"weight", unsigned(c.Weight)}, {"neighbors", c.Neighbors}, {"spatial_sigma", c.SpatialSigma},
            {"lumped_mass", c.LumpedMass}, {"count", c.Count}, {"output_prefix", c.OutputPrefix},
            {"max_iterations", c.MaxIterations}, {"tolerance", c.Tolerance},
            {"shell_flexural", c.ShellFlexural}, {"shell_length", c.ShellLength}, {"shell_area", c.ShellArea},
            {"skip_modes", c.SkipModes}, {"signature_output", c.SignatureOutput}, {"signature_scale", c.SignatureScale},
            {"distance_source", c.DistanceSource}, {"distance_output", c.DistanceOutput},
            {"distance_samples", c.DistanceSamples}}.dump();
    }
    Core::Config::EngineConfigSectionRegistration MakeLaplacianEigenbasisConfigSectionRegistration()
    { return {.DefaultSection = Section({}), .Validate = Validate}; }
    RuntimeEngineConfigApplyResult ApplyEditorLaplacianEigenbasisConfig(const EditorProcessingCommands& commands, const LaplacianEigenbasisConfig& c)
    {
        return ApplyEditorProcessingConfig(commands, Validate(SerializeLaplacianEigenbasisConfig(c), {}, kLaplacianEigenbasisConfigSectionName),
            std::string{kLaplacianEigenbasisConfigSectionName}, [&](Core::Config::EngineConfig& config) {
                Core::Config::UpsertEngineConfigSection(config.AppSections, Section(c)); });
    }
    std::optional<LaplacianEigenbasisConfig> GetEditorLaplacianEigenbasisConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return {};
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(context.EngineConfigControlState->ActiveConfig,
            kLaplacianEigenbasisConfigSectionName, kSchema, 1u, nullptr, Validate);
        return payload ? std::optional{Decode(Json::parse(*payload))} : std::nullopt;
    }
    ActionReadiness PreviewEditorLaplacianEigenbasisCommand(const EditorProcessingCommands& commands, std::uint32_t id,
                                                            const LaplacianEigenbasisConfig& c)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        const auto entity = Target(context, id, c, diagnostic);
        return {entity.has_value(), std::move(diagnostic)};
    }

    EditorLaplacianEigenbasisResult ApplyEditorLaplacianEigenbasisCommand(const EditorProcessingCommands& commands,
                                                                          std::uint32_t id, const LaplacianEigenbasisConfig& c)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        EditorLaplacianEigenbasisResult result;
        const auto fail = [&](std::string message) {
            result.Status = EditorCommandStatus::InvalidProcessingParameters;
            result.Message = std::move(message);
            return result;
        };
        std::string diagnostic;
        const auto entity = Target(context, id, c, diagnostic);
        if (!entity) return fail(diagnostic);
        const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
        GP::PointInputCapture samples;
        if (!PG::CaptureSamples(a, c.Domain, c.Positions, samples, diagnostic)) return fail(diagnostic);
        // Compact system rows: every live sample for the graph Laplacian; non-isolated mesh
        // vertices for the mesh operators (isolated vertices publish zero).
        std::vector<std::size_t> rows;
        Sp::SparseMatrix system;
        Sp::DiagonalMatrix mass;
        std::size_t components = 1;
        if (c.Operator == ModalOperator::GraphLaplacian)
        {
            PG::Graph graph;
            if (!PG::BuildGraph(a, {.Positions = c.Positions, .Weight = c.Weight, .Neighbors = c.Neighbors,
                    .SpatialSigma = c.SpatialSigma, .LumpedMass = c.LumpedMass, .BoundaryRows = false,
                    .Operation = "Laplacian eigenbasis"}, samples, graph, diagnostic))
                return fail(diagnostic);
            const std::size_t n = samples.Slots.size();
            rows.resize(n);
            for (std::size_t i = 0; i < n; ++i) rows[i] = i;
            // A = D - W over the compact rows; M is the lumped area or unit mass.
            std::vector<double> degree(n, 0.0);
            Sp::SparseBuilder builder(n, n);
            builder.Reserve(2 * graph.Edges.size() + n);
            for (const auto& edge : graph.Edges)
            {
                builder.Add(edge.A, edge.B, -edge.Weight);
                builder.Add(edge.B, edge.A, -edge.Weight);
                degree[edge.A] += edge.Weight;
                degree[edge.B] += edge.Weight;
            }
            for (std::size_t i = 0; i < n; ++i) builder.Add(i, i, degree[i]);
            auto laplacian = builder.Build();
            if (!laplacian.Valid) return fail("Unable to assemble the Laplacian.");
            system = std::move(laplacian.Matrix);
            mass = {n, graph.Mass.empty() ? std::vector<double>(n, 1.0) : graph.Mass};
            result.EdgeCount = graph.Edges.size();
        }
        else
        {
            auto mesh = GP::MeshSupport::BuildHalfedgeMeshForProcessing(a.SourceView, "Spectral modes", c.Positions.Name);
            if (!mesh.Succeeded()) return fail(mesh.Diagnostic);
            if (mesh.Mesh.VerticesSize() != samples.SlotCount) return fail("Mesh vertex correspondence mismatch.");
            for (unsigned d = unsigned(D::MeshVertex); d <= unsigned(D::MeshFace); ++d)
                if (const auto* set = ResolveGeometryPropertySet(a, D(d)))
                    for (const auto& name : set->Properties())
                        if (IsTopologyProperty(D(d), name)) samples.Inputs.push_back(GP::ObserveGeometryProperty(a, D(d), name));
            Sp::SparseMatrix full;
            if (c.Operator == ModalOperator::ModifiedDirichlet)
            {
                auto built = MA::BuildModifiedDirichletMatrix(mesh.Mesh);
                if (!built.Succeeded()) return fail("Modified Dirichlet energy: " + MA::DebugName(built.Status) + ".");
                full = std::move(built.Matrix);
            }
            else
            {
                auto built = MA::BuildThinShellHessian(mesh.Mesh, {.Flexural = c.ShellFlexural, .Length = c.ShellLength, .Area = c.ShellArea});
                if (!built.Succeeded()) return fail("Discrete shells Hessian: " + MA::DebugName(built.Status) + ".");
                full = std::move(built.Hessian);
                components = 3;
            }
            const auto areas = Geometry::DEC::BuildHodgeStar0(mesh.Mesh, Geometry::DEC::MassMode::Barycentric);
            for (std::size_t i = 0; i < samples.Slots.size(); ++i)
                if (!mesh.Mesh.IsIsolated(Geometry::VertexHandle{static_cast<Geometry::PropertyIndex>(samples.Slots[i])}) &&
                    areas.Diagonal[samples.Slots[i]] > 0.0)
                    rows.push_back(i);
            // Restrict the slot-indexed operator to the compact active rows.
            std::vector<std::size_t> inverse(samples.SlotCount * components, std::numeric_limits<std::size_t>::max());
            for (std::size_t r = 0; r < rows.size(); ++r)
                for (std::size_t k = 0; k < components; ++k) inverse[samples.Slots[rows[r]] * components + k] = r * components + k;
            const std::size_t m = rows.size() * components;
            Sp::SparseBuilder builder(m, m);
            builder.Reserve(full.Values.size());
            for (std::size_t i = 0; i < full.Rows; ++i)
                if (inverse[i] != std::numeric_limits<std::size_t>::max())
                    for (auto k = full.RowOffsets[i]; k < full.RowOffsets[i + 1]; ++k)
                        if (const auto j = inverse[full.ColIndices[k]]; j != std::numeric_limits<std::size_t>::max())
                            builder.Add(inverse[i], j, full.Values[k]);
            auto restricted = builder.Build();
            if (!restricted.Valid) return fail("Unable to assemble the modal operator.");
            system = std::move(restricted.Matrix);
            std::vector<double> lumped;
            for (const auto r : rows) lumped.push_back(c.LumpedMass ? areas.Diagonal[samples.Slots[r]] : 1.0);
            mass = MA::ExpandMass({rows.size(), std::move(lumped)}, components);
            result.EdgeCount = mesh.Mesh.EdgeCount();
        }
        const std::size_t n = rows.size() * components;
        if (c.Count >= n) return fail("The eigenpair count must be smaller than the number of unknowns.");
        std::optional<std::size_t> sourceRow;
        if (c.DistanceSource >= 0)
        {
            for (std::size_t r = 0; r < rows.size(); ++r)
                if (samples.Slots[rows[r]] == std::size_t(c.DistanceSource)) sourceRow = r;
            if (!sourceRow) return fail("The distance source is not a live row of the operator.");
        }
        const auto solved = Sp::SolveSymmetricGeneralizedEigen(system, mass,
            {.Count = c.Count, .MaxIterations = c.MaxIterations, .Tolerance = c.Tolerance});
        result.LiveCount = samples.LiveCount;
        result.Iterations = solved.Iterations;
        result.Eigenvalues = solved.Eigenvalues;
        result.RelativeResiduals = solved.RelativeResiduals;
        if (!solved.Succeeded()) return fail("Eigensolver: " + solved.Diagnostic + " No property was changed.");

        const MA::ModalSpectrum spectrum{solved.Eigenvalues, solved.Eigenvectors, rows.size(), components, c.SkipModes};
        std::vector<double> signature, distance;
        if (!c.SignatureOutput.empty() || sourceRow)
        {
            const auto range = MA::DefaultScaleRange(spectrum);
            if (!range.Valid()) return fail("The used modes need a positive eigenvalue to define signature scales; skip fewer modes.");
            result.ScaleMin = range.Min;
            result.ScaleMax = range.Max;
            if (!c.SignatureOutput.empty())
            {
                result.SignatureTime = MA::ScaleAt(range, c.SignatureScale);
                auto s = MA::ComputeModalSignature(spectrum, result.SignatureTime);
                if (!s.Succeeded()) return fail("Modal signature: " + MA::DebugName(s.Status) + ".");
                signature = std::move(s.Values);
            }
            if (sourceRow)
            {
                auto d = MA::ComputeModalDistance(spectrum, *sourceRow, range, c.DistanceSamples);
                if (!d.Succeeded()) return fail("Modal distance needs distinct scales (at least two used modes with distinct positive eigenvalues).");
                distance = std::move(d.Values);
            }
        }

        const auto* props = ResolveGeometryPropertySet(a, c.Domain);
        std::vector<Publication> publications;
        const auto publish = [&](const GeometryPropertyRef& ref, const auto& write) {
            Publication p{ref, PG::Capture(*props, ref), {}};
            p.After.Exists = true;
            p.After.Values = PG::EmptyStorage(ref.ValueKind);
            std::visit([&](auto& values) {
                using T = typename std::decay_t<decltype(values)>::value_type;
                values.assign(samples.SlotCount, T{});
                if (p.Before.Exists)
                    if (const auto* old = std::get_if<std::decay_t<decltype(values)>>(&p.Before.Values)) values = *old;
                for (const auto slot : samples.Slots) values[slot] = T{};
                for (std::size_t r = 0; r < rows.size(); ++r)
                    for (std::size_t k = 0; k < PG::Channels<T>(); ++k) PG::SetChannel(values[samples.Slots[rows[r]]], k, write(r, k));
            }, p.After.Values);
            publications.push_back(std::move(p));
            result.Outputs.push_back(ref.Name);
        };
        const auto outputs = Outputs(c);
        for (std::uint32_t j = 0; j < c.Count; ++j)
            publish(outputs[j], [&](std::size_t r, std::size_t k) { return solved.Eigenvectors[std::size_t(j) * n + r * components + k]; });
        if (!signature.empty()) publish({c.Domain, c.SignatureOutput, K::Float}, [&](std::size_t r, std::size_t) { return signature[r]; });
        if (!distance.empty()) publish({c.Domain, c.DistanceOutput, K::Float}, [&](std::size_t r, std::size_t) { return distance[r]; });
        if (std::ranges::all_of(publications, [](const Publication& p) { return PG::Same(p.Before, p.After); }))
        { result.Message = "Eigenbasis is unchanged."; return result; }
        std::erase_if(samples.Inputs, [&](const auto& watch) {
            return std::ranges::any_of(publications, [&](const Publication& p) {
                return watch.Domain == p.Ref.Domain && watch.Name == p.Ref.Name; });
        });
        const auto mutate = [context, entity = *entity, publications, watches = std::move(samples.Inputs)](bool redo) {
            if (!GP::GeometryPropertiesCurrent(context, entity, watches) || !GP::EditorProcessingContextWorldCurrent(context))
                return EditorCommandHistoryStatus::StaleEntity;
            auto* properties = GP::MutableGeometryProperties(context.Scene->Raw(), entity, publications.front().Ref.Domain);
            if (!properties) return EditorCommandHistoryStatus::StaleEntity;
            for (const auto& p : publications)
                if (!PG::Same(PG::Capture(*properties, p.Ref), redo ? p.Before : p.After)) return EditorCommandHistoryStatus::StaleEntity;
            for (const auto& p : publications)
            {
                const auto& target = redo ? p.After : p.Before;
                std::visit([&](const auto& values) {
                    using T = typename std::decay_t<decltype(values)>::value_type;
                    if (target.Exists) properties->GetOrAdd<T>(p.Ref.Name).Vector() = values;
                    else if (auto property = properties->Get<T>(p.Ref.Name)) properties->Remove(property);
                }, target.Values);
            }
            ECS::Components::DirtyTags::MarkGpuDirty(context.Scene->Raw(), entity);
            if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
            return EditorCommandHistoryStatus::Applied;
        };
        const auto status = context.CommandHistory ? context.CommandHistory->Execute({.Label = "Spectral modes",
            .Redo = [mutate] { return mutate(true); }, .Undo = [mutate] { return mutate(false); }}).Status : mutate(true);
        result.Status = EditorFeatureDetail::ToEditorCommandStatus(status);
        result.Message = result.Succeeded()
            ? std::to_string(c.Count) + " eigenpairs in " + std::to_string(solved.Iterations) + " iterations (" + result.BackendId + ")."
            : "Eigenbasis publication rejected by history guards.";
        return result;
    }
}
