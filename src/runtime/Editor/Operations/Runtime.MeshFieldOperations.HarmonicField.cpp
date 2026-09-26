// Harmonic fields bind typed input, constraint and output properties on any element domain,
// solve on the shared sample graph and publish every output in one guarded history transaction.
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
import Geometry.HarmonicField;
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
        namespace H = Geometry::HarmonicField;
        namespace GP = GeometryProcessingDetail;
        namespace PG = PropertyGraphDetail;
        using D = GeometryElementDomain;
        using K = Geometry::PropertyValueKind;
        using Json = nlohmann::json;
        constexpr std::string_view kSchema = "intrinsic.runtime.sandbox.harmonic_field";

        bool Floating(K k) { return k == K::Float || k == K::Double || k == K::Vec2 || k == K::Vec3 || k == K::Vec4; }
        bool Scalar(K k) { return k == K::Float || k == K::Double; }

        Json EncodeOptional(const GeometryPropertyRef& ref)
        { return ref.Name.empty() ? Json(nullptr) : ConfigDetail::EncodePointPropertyRef(ref); }
        void DecodeOptional(const Json& value, GeometryPropertyRef& ref)
        {
            if (value.is_null()) ref.Name.clear();
            else ConfigDetail::DecodePointPropertyRef(value, ref);
        }
        // A reference is valid for one of the listed kinds.
        bool ValidRef(const Json& value, std::initializer_list<K> kinds)
        {
            for (const auto kind : kinds)
                if (ConfigDetail::ValidatePointPropertyRef(value, kind) == ConfigDetail::PointPropertyValidation::Valid) return true;
            return false;
        }

        HarmonicFieldConfig Decode(const Json& doc)
        {
            HarmonicFieldConfig c;
            c.Mode = HarmonicFieldMode(doc.at("mode").get<unsigned>());
            ConfigDetail::DecodePointPropertyRef(doc.at("input"), c.Input);
            ConfigDetail::DecodePointPropertyRef(doc.at("output"), c.Output);
            ConfigDetail::DecodePointPropertyRef(doc.at("positions"), c.Positions);
            DecodeOptional(doc.at("hard_mask"), c.HardMask);
            DecodeOptional(doc.at("soft_weights"), c.SoftWeights);
            DecodeOptional(doc.at("confidence"), c.Confidence);
            DecodeOptional(doc.at("source"), c.Source);
            c.WeightsPrefix = doc.at("weights_prefix").is_null() ? std::string{} : doc.at("weights_prefix").get<std::string>();
            c.PinBoundary = doc.at("pin_boundary").get<bool>();
            c.Unlabeled = doc.at("unlabeled").get<std::int32_t>();
            c.Weight = S::PropertyWeight(doc.at("weight").get<unsigned>());
            c.Neighbors = doc.at("neighbors").get<std::uint32_t>();
            c.SpatialSigma = doc.at("spatial_sigma").get<double>();
            c.Field.Order = H::FieldOrder(doc.at("order").get<unsigned>());
            c.Field.Unconstrained = H::UnconstrainedPolicy(doc.at("unconstrained").get<unsigned>());
            c.LumpedMass = doc.at("lumped_mass").get<bool>();
            return c;
        }

        bool Structural(const GeometryPropertyRef& ref)
        {
            return IsTopologyProperty(ref.Domain, ref.Name) || (IsStructuralVertexProperty(ref.Name) && ref.Name != "v:position");
        }

        Core::Config::EngineConfigSectionValidationResult Validate(
            std::string_view payload, std::string_view, std::string_view subject)
        {
            const auto doc = Json::parse(payload, nullptr, false);
            auto merged = Json::parse(SerializeHarmonicFieldConfig({}));
            if (auto error = ConfigDetail::ValidatePointConfigFields(doc, merged,
                "Harmonic field config must be an object.", "Unknown harmonic field: ",
                {"mode", "weight", "neighbors", "order", "unconstrained"}))
                return ConfigDetail::RejectConfigSection(subject, *error);
            const auto reject = [&](std::string message) { return ConfigDetail::RejectConfigSection(subject, std::move(message)); };
            if (merged["mode"].get<unsigned>() > unsigned(HarmonicFieldMode::Labels))
                return reject("Unknown harmonic field mode.");
            const bool labels = merged["mode"].get<unsigned>() == unsigned(HarmonicFieldMode::Labels);
            for (const auto key : {"input", "output"})
                if (labels ? !ValidRef(merged[key], {K::Int32}) : !ValidRef(merged[key], {K::Float, K::Double, K::Vec2, K::Vec3, K::Vec4}))
                    return reject(labels ? "Label mode binds Int32 input and output properties."
                                         : "Field mode binds floating scalar or vector input and output properties.");
            if (!ValidRef(merged["positions"], {K::Vec3}))
                return reject("Positions need a vec3 property reference.");
            if (!merged["hard_mask"].is_null() && !ValidRef(merged["hard_mask"], {K::Bool}))
                return reject("hard_mask must be null or a bool property reference.");
            for (const auto key : {"soft_weights", "confidence"})
                if (!merged[key].is_null() && !ValidRef(merged[key], {K::Float, K::Double}))
                    return reject(std::string(key) + " must be null or a float/double property reference.");
            if (!merged["source"].is_null() && !ValidRef(merged["source"], {K::Float, K::Double, K::Vec2, K::Vec3, K::Vec4}))
                return reject("source must be null or a floating scalar or vector property reference.");
            if (!merged["weights_prefix"].is_null() &&
                (!merged["weights_prefix"].is_string() || merged["weights_prefix"].get<std::string>().empty()))
                return reject("weights_prefix must be null or a nonempty string.");
            for (const auto key : {"pin_boundary", "lumped_mass"})
                if (!merged[key].is_boolean()) return reject(std::string(key) + " must be boolean.");
            if (!merged["unlabeled"].is_number_integer() ||
                merged["unlabeled"].get<std::int64_t>() < std::numeric_limits<std::int32_t>::min() ||
                merged["unlabeled"].get<std::int64_t>() > std::numeric_limits<std::int32_t>::max())
                return reject("unlabeled must be a 32-bit signed integer.");
            if (!merged["spatial_sigma"].is_number() || !std::isfinite(merged["spatial_sigma"].get<double>()) ||
                merged["spatial_sigma"].get<double>() <= 0)
                return reject("spatial_sigma must be a positive finite number.");
            if (merged["weight"].get<unsigned>() > unsigned(S::PropertyWeight::MeshUniform) ||
                merged["order"].get<unsigned>() < 1 || merged["order"].get<unsigned>() > unsigned(H::FieldOrder::Triharmonic) ||
                merged["unconstrained"].get<unsigned>() > unsigned(H::UnconstrainedPolicy::ZeroMean))
                return reject("Unknown weight, order or unconstrained-component policy.");
            const auto c = Decode(merged);
            if (c.Neighbors < 1 || c.Neighbors > 1024) return reject("Neighbors must be in 1..1024.");
            const auto domain = c.Input.Domain;
            if (c.Output.Domain != domain || (!c.HardMask.Name.empty() && c.HardMask.Domain != domain) ||
                (!c.SoftWeights.Name.empty() && c.SoftWeights.Domain != domain) ||
                (!c.Confidence.Name.empty() && c.Confidence.Domain != domain) ||
                (!c.Source.Name.empty() && c.Source.Domain != domain))
                return reject("Output, constraint, source and confidence properties must share the input domain.");
            if (!labels && GeometryPropertyComponentCount(c.Input.ValueKind) != GeometryPropertyComponentCount(c.Output.ValueKind))
                return reject("Field output needs the input's channel count.");
            if (!c.Source.Name.empty() && GeometryPropertyComponentCount(c.Input.ValueKind) != GeometryPropertyComponentCount(c.Source.ValueKind))
                return reject("The source needs the input's channel count.");
            if (Structural(c.Output) || (!c.Confidence.Name.empty() && Structural(c.Confidence)) ||
                (c.Output.Name == "v:position" && (c.Output.ValueKind != K::Vec3 || c.Output.Domain != c.Positions.Domain)))
                return reject("Outputs cannot replace structural storage.");
            const bool grounded = c.Field.Unconstrained == H::UnconstrainedPolicy::ZeroMean;
            if (labels && (!c.HardMask.Name.empty() || !c.SoftWeights.Name.empty() || c.PinBoundary || !c.Source.Name.empty()))
                return reject("Label mode takes its constraints from the seed labels.");
            if (labels && grounded)
                return reject("Label mode cannot ground unseeded components; fail or keep their labels.");
            if (!labels && c.HardMask.Name.empty() && c.SoftWeights.Name.empty() && !c.PinBoundary && !grounded)
                return reject("Field mode needs a hard mask, soft weights, pinned mesh boundary or zero-mean grounding.");
            if (!labels && (!c.Confidence.Name.empty() || !c.WeightsPrefix.empty()))
                return reject("Confidence and weight outputs apply to label mode only.");
            if (!c.Confidence.Name.empty() && (c.Confidence.Name == c.Output.Name || c.Confidence.Name == c.Input.Name))
                return reject("Confidence needs its own property name.");
            if (c.LumpedMass && c.Field.Order == H::FieldOrder::Harmonic && c.Source.Name.empty())
                return reject("Lumped mass only affects orders above harmonic and the source.");
            const bool meshTopology = c.Weight == S::PropertyWeight::Cotangent || c.Weight == S::PropertyWeight::MeshUniform ||
                                      c.PinBoundary || c.LumpedMass;
            if (meshTopology && (domain != D::MeshVertex || c.Positions.Domain != D::MeshVertex))
                return reject("Mesh weights, boundary pinning and lumped mass require mesh vertices and vertex positions.");
            return {.State = Core::Config::EngineConfigState::Valid,
                    .CanonicalPayloadJson = SerializeHarmonicFieldConfig(c),
                    .ParsedFieldCount = static_cast<std::uint32_t>(doc.size())};
        }
        Core::Config::EngineConfigSection Section(const HarmonicFieldConfig& c)
        { return {.Name = std::string{kHarmonicFieldConfigSectionName}, .SchemaId = std::string{kSchema},
                  .SchemaVersion = 1u, .PayloadJson = SerializeHarmonicFieldConfig(c)}; }

        std::optional<ECS::EntityHandle> Target(const EditorProcessingContext& context, std::uint32_t id,
            const HarmonicFieldConfig& c, std::string& diagnostic)
        {
            const auto validation = Validate(SerializeHarmonicFieldConfig(c), {}, kHarmonicFieldConfigSectionName);
            if (!validation.Usable()) { diagnostic = validation.Diagnostics.front().Message; return {}; }
            if (!context.Scene || (context.AttachmentActive && !context.AttachmentActive()) || !GP::EditorProcessingContextWorldCurrent(context))
            { diagnostic = "Workspace is unavailable."; return {}; }
            const auto entity = EditorFeatureDetail::ResolveStableEntity(context.Scene->Raw(), id);
            if (!entity) { diagnostic = "Choose an existing geometry entity."; return {}; }
            const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
            const auto* props = ResolveGeometryPropertySet(a, c.Input.Domain);
            const auto existing = [&](const GeometryPropertyRef& ref) {
                return ResolveGeometryProperty(a, ref, props->Size(), false).Resolved() && PG::CountMatches(*props, ref);
            };
            if (!props || !existing(c.Input))
            { diagnostic = c.Mode == HarmonicFieldMode::Labels ? "Choose an existing Int32 label property."
                                                               : "Choose an existing floating scalar or vector property."; return {}; }
            for (const auto* ref : {&c.HardMask, &c.SoftWeights, &c.Source})
                if (!ref->Name.empty() && !existing(*ref))
                { diagnostic = "Constraint or source property '" + ref->Name + "' is missing or has another type."; return {}; }
            for (const auto* ref : {&c.Output, &c.Confidence})
                if (!ref->Name.empty() && props->Exists(ref->Name) && !existing(*ref))
                { diagnostic = "Output '" + ref->Name + "' exists with a different storage type or cardinality."; return {}; }
            if (c.Positions.Domain != c.Input.Domain && c.Positions.Domain != GP::PrimaryPointDomain(a))
            { diagnostic = "Positions must belong to the input domain or the entity's vertices/nodes."; return {}; }
            const auto* positions = ResolveGeometryPropertySet(a, c.Positions.Domain);
            if (!positions || !ResolveGeometryProperty(a, c.Positions, positions->Size(), false).Resolved())
            { diagnostic = "Choose an existing vec3 position property."; return {}; }
            return entity;
        }

        struct Publication { GeometryPropertyRef Ref; PG::Snapshot Before, After; };

        // Writes compact solved rows into a copy of the current output; deleted rows keep their
        // stored value (new storage starts at zero).
        bool Fill(Publication& p, std::span<const std::uint32_t> slots, std::size_t slotCount,
                  std::span<const double> values, std::size_t channels)
        {
            p.After = p.Before;
            p.After.Exists = true;
            bool representable = true;
            std::visit([&](auto& rows) {
                using T = typename std::decay_t<decltype(rows)>::value_type;
                rows.resize(slotCount, T{});
                for (std::size_t i = 0; i < slots.size(); ++i)
                    for (std::size_t ch = 0; ch < PG::Channels<T>(); ++ch)
                    {
                        const double value = values[i * channels + ch];
                        if constexpr (!std::is_same_v<T, double> && !std::is_same_v<T, std::int32_t>)
                            if (std::abs(value) > std::numeric_limits<float>::max()) { representable = false; continue; }
                        PG::SetChannel(rows[slots[i]], ch, value);
                    }
            }, p.After.Values);
            return representable;
        }
    }

    std::string SerializeHarmonicFieldConfig(const HarmonicFieldConfig& c)
    {
        return Json{{"mode", unsigned(c.Mode)}, {"input", ConfigDetail::EncodePointPropertyRef(c.Input)},
            {"output", ConfigDetail::EncodePointPropertyRef(c.Output)}, {"positions", ConfigDetail::EncodePointPropertyRef(c.Positions)},
            {"hard_mask", EncodeOptional(c.HardMask)}, {"soft_weights", EncodeOptional(c.SoftWeights)},
            {"pin_boundary", c.PinBoundary}, {"source", EncodeOptional(c.Source)}, {"unlabeled", c.Unlabeled},
            {"confidence", EncodeOptional(c.Confidence)},
            {"weights_prefix", c.WeightsPrefix.empty() ? Json(nullptr) : Json(c.WeightsPrefix)},
            {"weight", unsigned(c.Weight)}, {"neighbors", c.Neighbors}, {"spatial_sigma", c.SpatialSigma},
            {"order", unsigned(c.Field.Order)}, {"unconstrained", unsigned(c.Field.Unconstrained)},
            {"lumped_mass", c.LumpedMass}}.dump();
    }
    Core::Config::EngineConfigSectionRegistration MakeHarmonicFieldConfigSectionRegistration()
    {
        HarmonicFieldConfig defaults;
        defaults.PinBoundary = true; // the default section must validate: field mode needs a constraint source
        return {.DefaultSection = Section(defaults), .Validate = Validate};
    }
    RuntimeEngineConfigApplyResult ApplyEditorHarmonicFieldConfig(const EditorProcessingCommands& commands, const HarmonicFieldConfig& c)
    {
        return ApplyEditorProcessingConfig(commands, Validate(SerializeHarmonicFieldConfig(c), {}, kHarmonicFieldConfigSectionName),
            std::string{kHarmonicFieldConfigSectionName}, [&](Core::Config::EngineConfig& config) {
                Core::Config::UpsertEngineConfigSection(config.AppSections, Section(c)); });
    }
    std::optional<HarmonicFieldConfig> GetEditorHarmonicFieldConfig(const EditorProcessingCommands& commands)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        if (!context.EngineConfigControlState) return {};
        const auto payload = ConfigDetail::FindValidatedCanonicalPayload(context.EngineConfigControlState->ActiveConfig,
            kHarmonicFieldConfigSectionName, kSchema, 1u, nullptr, Validate);
        return payload ? std::optional{Decode(Json::parse(*payload))} : std::nullopt;
    }
    ActionReadiness PreviewEditorHarmonicFieldCommand(const EditorProcessingCommands& commands, std::uint32_t id, const HarmonicFieldConfig& c)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        std::string diagnostic;
        const auto entity = Target(context, id, c, diagnostic);
        return {entity.has_value(), std::move(diagnostic)};
    }
    EditorHarmonicFieldResult ApplyEditorHarmonicFieldCommand(const EditorProcessingCommands& commands, std::uint32_t id, const HarmonicFieldConfig& c)
    {
        const auto& context = EditorProcessingCommandsAccess::Resolve(commands);
        EditorHarmonicFieldResult result;
        const auto fail = [&](std::string message) { result.Status = EditorCommandStatus::InvalidProcessingParameters; result.Message = std::move(message); return result; };
        std::string diagnostic;
        const auto entity = Target(context, id, c, diagnostic);
        if (!entity) return fail(diagnostic);
        const auto a = BuildGeometryAvailability(context.Scene->Raw(), *entity);
        GP::PointInputCapture samples;
        if (!PG::CaptureSamples(a, c.Input.Domain, c.Positions, samples, diagnostic)) return fail(diagnostic);
        PG::Graph graph;
        if (!PG::BuildGraph(a, {.Positions = c.Positions, .Weight = c.Weight, .Neighbors = c.Neighbors,
                .SpatialSigma = c.SpatialSigma, .LumpedMass = c.LumpedMass, .BoundaryRows = c.PinBoundary,
                .Operation = "Harmonic field"}, samples, graph, diagnostic))
            return fail(diagnostic);
        const auto* props = ResolveGeometryPropertySet(a, c.Input.Domain);
        const auto input = PG::Capture(*props, c.Input);
        const auto rows = samples.Slots.size();
        std::vector<Publication> publications{{c.Output, PG::Capture(*props, c.Output), {}}};
        H::Diagnostics stats;
        std::string backend;
        if (c.Mode == HarmonicFieldMode::Labels)
        {
            const auto& stored = std::get<std::vector<std::int32_t>>(input.Values);
            std::vector<std::int32_t> labels;
            for (const auto slot : samples.Slots) labels.push_back(stored[slot]);
            const auto solved = H::PropagateLabels(labels, c.Unlabeled, graph.Edges, c.Field, graph.Mass);
            if (!solved.Success) return fail(solved.Diagnostic);
            stats = solved.Stats;
            backend = solved.Diagnostic;
            const std::vector<double> asDouble(solved.Labels.begin(), solved.Labels.end());
            if (!Fill(publications[0], samples.Slots, samples.SlotCount, asDouble, 1)) return fail("Labels exceed output storage range.");
            if (!c.Confidence.Name.empty())
            {
                publications.push_back({c.Confidence, PG::Capture(*props, c.Confidence), {}});
                if (!Fill(publications.back(), samples.Slots, samples.SlotCount, solved.Confidence, 1))
                    return fail("Confidence exceeds output storage range.");
            }
            if (!c.WeightsPrefix.empty())
            {
                const auto labelCount = solved.SeedLabels.size();
                std::vector<double> column(rows);
                for (std::size_t l = 0; l < labelCount; ++l)
                {
                    const GeometryPropertyRef ref{c.Input.Domain, c.WeightsPrefix + std::to_string(solved.SeedLabels[l]), K::Float};
                    if (Structural(ref) || std::ranges::any_of(publications, [&](const Publication& p) { return p.Ref.Name == ref.Name; }) ||
                        ref.Name == c.Input.Name || ref.Name == c.Positions.Name)
                        return fail("Weight output '" + ref.Name + "' collides with another bound or structural property.");
                    if (props->Exists(ref.Name) && !(ResolveGeometryProperty(a, ref, props->Size(), false).Resolved() && PG::CountMatches(*props, ref)))
                        return fail("Weight output '" + ref.Name + "' exists with a different storage type or cardinality.");
                    for (std::size_t i = 0; i < rows; ++i) column[i] = solved.Weights[i * labelCount + l];
                    publications.push_back({ref, PG::Capture(*props, ref), {}});
                    if (!Fill(publications.back(), samples.Slots, samples.SlotCount, column, 1))
                        return fail("Label weights exceed output storage range.");
                }
                result.WeightOutputs = labelCount;
            }
        }
        else
        {
            const auto channels = GeometryPropertyComponentCount(c.Input.ValueKind);
            std::vector<double> values;
            values.reserve(rows * channels);
            std::visit([&](const auto& stored) {
                for (const auto slot : samples.Slots)
                    for (std::size_t ch = 0; ch < channels; ++ch) values.push_back(PG::Channel(stored[slot], ch));
            }, input.Values);
            std::vector<bool> hard(rows, false);
            if (!c.HardMask.Name.empty())
            {
                const auto mask = props->Get<bool>(c.HardMask.Name);
                for (std::size_t i = 0; i < rows; ++i) hard[i] = mask[samples.Slots[i]];
            }
            for (const auto row : graph.BoundaryRows) hard[row] = true;
            std::vector<std::size_t> hardRows;
            for (std::size_t i = 0; i < rows; ++i) if (hard[i]) hardRows.push_back(i);
            std::vector<H::SoftConstraint> softRows;
            if (!c.SoftWeights.Name.empty())
            {
                const auto weights = PG::Capture(*props, c.SoftWeights);
                bool valid = true;
                std::visit([&](const auto& stored) {
                    using T = typename std::decay_t<decltype(stored)>::value_type;
                    if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>)
                        for (std::size_t i = 0; i < rows; ++i)
                        {
                            const double w = stored[samples.Slots[i]];
                            if (!std::isfinite(w) || w < 0) { valid = false; return; }
                            if (w > 0 && !hard[i]) softRows.push_back({i, w}); // hard rows win
                        }
                }, weights.Values);
                if (!valid) return fail("Soft constraint weights must be finite and nonnegative.");
            }
            std::vector<double> source;
            if (!c.Source.Name.empty())
            {
                const auto density = PG::Capture(*props, c.Source);
                source.reserve(rows * channels);
                std::visit([&](const auto& stored) {
                    using T = typename std::decay_t<decltype(stored)>::value_type;
                    if constexpr (!std::is_same_v<T, std::int32_t>)
                        for (std::size_t i = 0; i < rows; ++i)
                            for (std::size_t ch = 0; ch < channels; ++ch)
                                source.push_back(PG::Channel(stored[samples.Slots[i]], ch) * (graph.Mass.empty() ? 1.0 : graph.Mass[i]));
                }, density.Values);
                if (source.size() != rows * channels) return fail("The source needs the input's channel count.");
            }
            const auto solved = H::Solve(values, channels, graph.Edges, c.Field, hardRows, softRows, graph.Mass, source);
            if (!solved.Success) return fail(solved.Diagnostic);
            stats = solved.Stats;
            backend = solved.Diagnostic;
            if (!Fill(publications[0], samples.Slots, samples.SlotCount, solved.Values, channels))
                return fail("Harmonic values exceed output storage range.");
        }
        result.LiveCount = samples.LiveCount;
        result.EdgeCount = graph.Edges.size();
        result.FreeRows = stats.FreeRows;
        result.HardRows = stats.HardRows;
        result.SoftRows = stats.SoftRows;
        result.Components = stats.Components;
        result.UnconstrainedComponents = stats.UnconstrainedComponents;
        result.MaxRelativeResidual = stats.MaxRelativeResidual;
        result.GroundedComponents = stats.GroundedComponents;
        result.MaxCompatibilityDefect = stats.MaxCompatibilityDefect;
        result.BackendId = backend;
        if (std::ranges::all_of(publications, [](const Publication& p) { return PG::Same(p.Before, p.After); }))
        { result.Message = "Harmonic field is unchanged."; return result; }
        // Aliased outputs are guarded by their expected values; other inputs by their revisions.
        samples.Inputs.push_back(GP::ObserveGeometryProperty(a, c.Input.Domain, c.Input.Name));
        for (const auto* ref : {&c.HardMask, &c.SoftWeights, &c.Source})
            if (!ref->Name.empty()) samples.Inputs.push_back(GP::ObserveGeometryProperty(a, ref->Domain, ref->Name));
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
            if (publications.front().Ref.Name == "v:position") ECS::Components::DirtyTags::MarkVertexPositionsDirty(context.Scene->Raw(), entity);
            if (context.InvalidateWorkspaceSnapshotCache) context.InvalidateWorkspaceSnapshotCache();
            return EditorCommandHistoryStatus::Applied;
        };
        const auto status = context.CommandHistory ? context.CommandHistory->Execute({.Label = "Harmonic field",
            .Redo = [mutate] { return mutate(true); }, .Undo = [mutate] { return mutate(false); }}).Status : mutate(true);
        result.Status = EditorFeatureDetail::ToEditorCommandStatus(status);
        result.Message = result.Succeeded() ? "Harmonic field solved (" + backend + ")." : "Harmonic field publication rejected by history guards.";
        return result;
    }
}
