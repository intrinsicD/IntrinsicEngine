// Laplacian eigenbasis: the k smallest eigenpairs of the shared sample-graph Laplacian with unit
// or lumped-area mass (Geometry.Sparse eigensolver), published as one float property per
// eigenvector in one guarded history transaction.
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
            return c;
        }

        std::string OutputName(const LaplacianEigenbasisConfig& c, std::uint32_t j) { return c.OutputPrefix + std::to_string(j); }

        Core::Config::EngineConfigSectionValidationResult Validate(
            std::string_view payload, std::string_view, std::string_view subject)
        {
            const auto doc = Json::parse(payload, nullptr, false);
            auto merged = Json::parse(SerializeLaplacianEigenbasisConfig({}));
            if (auto error = ConfigDetail::ValidatePointConfigFields(doc, merged,
                "Eigenbasis config must be an object.", "Unknown eigenbasis field: ",
                {"domain", "weight", "neighbors", "count", "max_iterations"}))
                return ConfigDetail::RejectConfigSection(subject, *error);
            const auto reject = [&](std::string message) { return ConfigDetail::RejectConfigSection(subject, std::move(message)); };
            if (ConfigDetail::ValidatePointPropertyRef(merged["positions"], K::Vec3) != ConfigDetail::PointPropertyValidation::Valid)
                return reject("Positions need a vec3 property reference.");
            for (const auto key : {"spatial_sigma", "tolerance"})
                if (!merged[key].is_number() || !std::isfinite(merged[key].get<double>()))
                    return reject("spatial_sigma and tolerance must be finite numbers.");
            if (!merged["lumped_mass"].is_boolean()) return reject("lumped_mass must be boolean.");
            if (!merged["output_prefix"].is_string() || merged["output_prefix"].get<std::string>().empty())
                return reject("output_prefix must be a nonempty string.");
            const auto domain = merged["domain"].get<unsigned>();
            if (domain < unsigned(D::MeshVertex) || domain > unsigned(D::PointCloudPoint) ||
                merged["weight"].get<unsigned>() > unsigned(S::PropertyWeight::MeshUniform))
                return reject("Unknown domain or weight.");
            const auto c = Decode(merged);
            if (c.Count < 1 || c.Count > kMaxCount || c.Neighbors < 1 || c.Neighbors > 1024 || !(c.SpatialSigma > 0) ||
                c.MaxIterations < 1 || c.MaxIterations > 100000 || !(c.Tolerance > 0) || !(c.Tolerance < 1))
                return reject("Invalid parameters: count 1..256, neighbors 1..1024, positive sigma, iterations 1..100000, tolerance (0,1).");
            for (std::uint32_t j = 0; j < c.Count; ++j)
                if (IsTopologyProperty(c.Domain, OutputName(c, j)) || IsStructuralVertexProperty(OutputName(c, j)))
                    return reject("Eigenvector outputs cannot replace structural storage.");
            const bool meshTopology = c.Weight == S::PropertyWeight::Cotangent || c.Weight == S::PropertyWeight::MeshUniform || c.LumpedMass;
            if (meshTopology && (c.Domain != D::MeshVertex || c.Positions.Domain != D::MeshVertex))
                return reject("Mesh weights and lumped mass require mesh vertices and vertex positions.");
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
            for (std::uint32_t j = 0; j < c.Count; ++j)
            {
                const GeometryPropertyRef ref{c.Domain, OutputName(c, j), K::Float};
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
        return Json{{"domain", unsigned(c.Domain)}, {"positions", ConfigDetail::EncodePointPropertyRef(c.Positions)},
            {"weight", unsigned(c.Weight)}, {"neighbors", c.Neighbors}, {"spatial_sigma", c.SpatialSigma},
            {"lumped_mass", c.LumpedMass}, {"count", c.Count}, {"output_prefix", c.OutputPrefix},
            {"max_iterations", c.MaxIterations}, {"tolerance", c.Tolerance}}.dump();
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
        PG::Graph graph;
        if (!PG::BuildGraph(a, {.Positions = c.Positions, .Weight = c.Weight, .Neighbors = c.Neighbors,
                .SpatialSigma = c.SpatialSigma, .LumpedMass = c.LumpedMass, .BoundaryRows = false,
                .Operation = "Laplacian eigenbasis"}, samples, graph, diagnostic))
            return fail(diagnostic);
        const std::size_t n = samples.Slots.size();
        if (c.Count >= n) return fail("The eigenpair count must be smaller than the number of live rows.");
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
        const auto laplacian = builder.Build();
        if (!laplacian.Valid) return fail("Unable to assemble the Laplacian.");
        const Sp::DiagonalMatrix mass{n, graph.Mass.empty() ? std::vector<double>(n, 1.0) : graph.Mass};
        const auto solved = Sp::SolveSymmetricGeneralizedEigen(laplacian.Matrix, mass,
            {.Count = c.Count, .MaxIterations = c.MaxIterations, .Tolerance = c.Tolerance});
        result.LiveCount = samples.LiveCount;
        result.EdgeCount = graph.Edges.size();
        result.Iterations = solved.Iterations;
        result.Eigenvalues = solved.Eigenvalues;
        result.RelativeResiduals = solved.RelativeResiduals;
        if (!solved.Succeeded()) return fail("Eigensolver: " + solved.Diagnostic + " No property was changed.");

        const auto* props = ResolveGeometryPropertySet(a, c.Domain);
        std::vector<Publication> publications;
        for (std::uint32_t j = 0; j < c.Count; ++j)
        {
            Publication p{{c.Domain, OutputName(c, j), K::Float}, PG::Capture(*props, {c.Domain, OutputName(c, j), K::Float}), {}};
            p.After = p.Before;
            p.After.Exists = true;
            auto& rows = std::get<std::vector<float>>(p.After.Values);
            rows.resize(samples.SlotCount, 0.0f);
            for (std::size_t i = 0; i < n; ++i)
                rows[samples.Slots[i]] = static_cast<float>(solved.Eigenvectors[std::size_t(j) * n + i]);
            publications.push_back(std::move(p));
            result.Outputs.push_back(OutputName(c, j));
        }
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
        const auto status = context.CommandHistory ? context.CommandHistory->Execute({.Label = "Laplacian eigenbasis",
            .Redo = [mutate] { return mutate(true); }, .Undo = [mutate] { return mutate(false); }}).Status : mutate(true);
        result.Status = EditorFeatureDetail::ToEditorCommandStatus(status);
        result.Message = result.Succeeded()
            ? std::to_string(c.Count) + " eigenpairs in " + std::to_string(solved.Iterations) + " iterations (" + result.BackendId + ")."
            : "Eigenbasis publication rejected by history guards.";
        return result;
    }
}
