// Editor-backed agent operations: every entry calls the same runtime query, command or
// config path the Sandbox panels use. JSON stays private to this unit; accessors never
// throw (the build has no exceptions), so every field is type-checked before use.
module;

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

module Extrinsic.Runtime.AgentOperations;

import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Core.Logging;
import Extrinsic.Runtime.AssetIngestStateMachine;
import Extrinsic.Runtime.CameraControllers;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.SceneEditingOperations;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.MeshFieldOperations;
import Extrinsic.Runtime.RegistrationOperations;
import Extrinsic.Runtime.PointSamplingOperations;
import Extrinsic.Runtime.PointAnalysisOperations;
import Extrinsic.Runtime.PointCloudServiceOperations;
import Extrinsic.Runtime.PointCloudConsolidationTypes;
import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.VertexChannelBindings;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryAvailability;
import Extrinsic.Runtime.GeometryProperty.Types;
import Geometry.Properties.Types;

#include "Agent/internal/Runtime.AgentOperations.Detail.hpp"

namespace Extrinsic::Runtime
{
    using namespace AgentDetail;
    namespace
    {
        // ---- scene -----------------------------------------------------------------------
        AgentOperationOutcome SceneEntities(const AgentOperationContext& context, std::string_view)
        {
            const auto prepared = PrepareSnapshot(context);
            if (!prepared) return Fail(kNoWorkspace);
            Json entities = Json::array();
            for (const auto& row : prepared->Frame.Hierarchy)
            {
                const auto inspector = BuildEditorInspectorModel(prepared->SnapshotQueries, nullptr, row.StableEntityId);
                const auto& g = inspector.Geometry;
                entities.push_back({{"entity", row.StableEntityId}, {"name", row.Name}, {"selected", row.Selected},
                                    {"geometry", g.Valid ? Json{{"vertices", g.VertexCount}, {"edges", g.EdgeCount},
                                                                {"faces", g.FaceCount}, {"nodes", g.NodeCount}}
                                                         : Json(nullptr)}});
            }
            return Ok({{"entities", entities}, {"selected", prepared->Frame.Selection.SelectedStableIds}});
        }

        AgentOperationOutcome EntityProperties(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            if (!entity) return Fail("Pass {\"entity\": <stable id>}.");
            const auto prepared = PrepareSnapshot(context);
            if (!prepared) return Fail(kNoWorkspace);
            const auto inspector = BuildEditorInspectorModel(prepared->SnapshotQueries, nullptr, *entity);
            if (!inspector.HasEntity) return Fail("No entity with id " + std::to_string(*entity) + ".");
            Json rows = Json::array();
            for (const auto& row : inspector.PropertyCatalog.Rows)
            {
                if (row.Internal) continue;
                rows.push_back({{"domain", std::string(ToString(row.Descriptor.Domain))}, {"name", row.Name},
                                {"kind", KindName(row.ValueKind)}, {"count", row.ElementCount},
                                {"components", row.ComponentCount}, {"bindable", row.Bindable},
                                {"generated", row.Generated}});
            }
            return Ok({{"entity", *entity}, {"name", inspector.Entity.Name}, {"properties", rows}});
        }

        AgentOperationOutcome SelectEntity(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            if (!entity) return Fail("Pass {\"entity\": <stable id>}.");
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto scene = PrepareEditorSceneEditingFrame(*context.Attachment);
            if (!SelectEditorEntity(scene.Commands, *entity)) return Fail("Entity " + std::to_string(*entity) + " cannot be selected.");
            return Ok({{"selected", *entity}});
        }

        std::vector<std::uint32_t> StableIds(const EditorWorkspaceSnapshotPreparedFrame& prepared)
        {
            std::vector<std::uint32_t> ids;
            for (const auto& row : prepared.Frame.Hierarchy) ids.push_back(row.StableEntityId);
            return ids;
        }

        // `wait: true` answers once the import materialized (the queue row reached Complete) or failed.
        AgentOperationOutcome ImportFile(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto path = args ? String(*args, "path") : std::nullopt;
            if (!path) return Fail("Pass {\"path\": \"<file inside an allowed root>\"}.");
            if (args->contains("wait") && !(*args)["wait"].is_boolean()) return Fail("wait must be true or false.");
            const bool wait = args->value("wait", false);
            const auto resolved = ResolveAgentPath(context, *path);
            if (!resolved) return Fail("Path '" + *path + "' is outside the Sandbox's allowed agent roots.");
            const auto prepared = PrepareSnapshot(context);
            if (!prepared) return Fail(kNoWorkspace);
            const auto before = StableIds(*prepared);
            const auto scene = PrepareEditorSceneEditingFrame(*context.Attachment);
            const auto result = ApplyEditorFileImportCommand(scene.Commands, EditorFileImportCommand{.Path = *resolved});
            const Json out{{"status", DebugNameForEditorCommandStatus(result.Status)}, {"path", *resolved},
                           {"message", result.Message}, {"entities_created", result.PrimitiveEntitiesCreated}};
            // Imports are asynchronous: Pending means queued; the entity appears in scene_entities.
            const bool failed = !result.Succeeded() && result.Status != EditorCommandStatus::Pending;
            if (!wait || failed || result.Status != EditorCommandStatus::Pending || !result.Operation.IsValid())
                return {.IsError = failed, .Text = Dump(out)};
            return {.Continuation = [handle = result.Operation, before, out](const AgentOperationContext& current, AgentOperationOutcome& done) {
                const auto frame = PrepareSnapshot(current);
                if (!frame) { done = Fail(kNoWorkspace); return true; }
                // By handle, so "Clear completed" hiding the queue row cannot leave the call waiting.
                const auto scene = PrepareEditorSceneEditingFrame(*current.Attachment);
                if (!scene.AssetImportQueueCommands.Find) { done = Fail("Import tracking is unavailable in this workspace."); return true; }
                const auto record = scene.AssetImportQueueCommands.Find(handle);
                if (!record) { done = Fail("The import is unknown to the asset workflow."); return true; }
                if (!IsTerminal(record->Phase)) return false;
                Json finished = out;
                const bool complete = record->Phase == RuntimeAssetIngestPhase::Complete;
                finished["status"] = complete ? "Applied" : record->Phase == RuntimeAssetIngestPhase::Failed ? "Failed" : "Cancelled";
                finished["message"] = complete ? std::string("Import completed.")
                                                : std::string("Import ") + (record->Phase == RuntimeAssetIngestPhase::Failed ? "failed: " : "cancelled: ") +
                                                      DebugNameForRuntimeAssetIngestDiagnostic(record->Diagnostic);
                finished["entities_created"] = record->Result ? record->Result->PrimitiveEntitiesCreated : 0u;
                // Best effort: entities that appeared since the call (another import running at the same time is included).
                Json created = Json::array();
                for (const auto id : StableIds(*frame))
                    if (std::ranges::find(before, id) == before.end()) created.push_back(id);
                finished["new_entities"] = created;
                done = {.IsError = !complete, .Text = Dump(finished)};
                return true;
            }};
        }

        // Save and load answer Pending with the job's token; the projected last scene-file event carries it.
        AgentOperationOutcome FinishSceneFile(const EditorSceneFileResult& result, const std::string& path)
        {
            const auto describe = [path](const EditorSceneFileResult& r) { // by value: the continuation outlives this call
                return Json{{"status", DebugNameForEditorCommandStatus(r.Status)}, {"succeeded", r.Succeeded()}, {"path", path},
                            {"message", r.Message}};
            };
            if (result.Status != EditorCommandStatus::Pending || !result.Task.IsValid())
                return {.IsError = !result.Succeeded(), .Text = Dump(describe(result))};
            AgentOperationOutcome outcome{};
            outcome.Progress = RunProgressProbe(result.Task);
            outcome.Continuation = [token = result.Task, describe](const AgentOperationContext& current, AgentOperationOutcome& out) {
                if (!PrepareSnapshot(current)) { out = Fail(kNoWorkspace); return true; }
                const auto last = PrepareEditorSceneEditingFrame(*current.Attachment).LastSceneFileResult;
                if (last && last->Task == token)
                {
                    out = {.IsError = !last->Succeeded(), .Text = Dump(describe(*last))};
                    return true;
                }
                // Only the latest scene-file event is retained: a later save or load (or a stale job that publishes
                // none) can leave this one without a result once its job ended.
                // A reaped job is unknown to the service (state Invalid): it ended too.
                if (current.Jobs != nullptr && (current.Jobs->IsComplete(token) || current.Jobs->GetState(token) == JobState::Invalid))
                {
                    out = {.IsError = true, .ErrorCode = "result_unavailable",
                           .Text = "The scene file job ended without a result for this call (a later scene file operation replaced it, or "
                                   "the document changed while it ran); check the file and the scene."};
                    return true;
                }
                return false;
            };
            return outcome;
        }

        AgentOperationOutcome SaveScene(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto path = args ? String(*args, "path") : std::nullopt;
            if (!path) return Fail("Pass {\"path\": \"<scene file inside an allowed root>\", \"overwrite\": <optional boolean>}.");
            if (args->contains("overwrite") && !(*args)["overwrite"].is_boolean()) return Fail("overwrite must be true or false.");
            const auto resolved = ResolveAgentPath(context, *path);
            if (!resolved) return Fail("Path '" + *path + "' is outside the Sandbox's allowed agent roots.");
            std::error_code status;
            const auto existing = std::filesystem::symlink_status(*resolved, status); // a dangling symlink occupies the path
            if (!status && existing.type() != std::filesystem::file_type::not_found && !args->value("overwrite", false))
                return {.IsError = true, .Text = "'" + *path + "' already exists; pass overwrite: true to replace it.", .ErrorCode = "file_exists"};
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto scene = PrepareEditorSceneEditingFrame(*context.Attachment);
            return FinishSceneFile(ApplyEditorSceneSaveCommand(scene.Commands, EditorSceneFileCommand{.Path = *resolved}), *resolved);
        }

        // Loading replaces the whole scene document.
        AgentOperationOutcome LoadScene(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto path = args ? String(*args, "path") : std::nullopt;
            if (!path) return Fail("Pass {\"path\": \"<scene file inside an allowed root>\"}.");
            const auto resolved = ResolveAgentPath(context, *path);
            if (!resolved) return Fail("Path '" + *path + "' is outside the Sandbox's allowed agent roots.");
            std::error_code status;
            if (!std::filesystem::is_regular_file(*resolved, status)) return Fail("'" + *path + "' is not an existing file.");
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto scene = PrepareEditorSceneEditingFrame(*context.Attachment);
            return FinishSceneFile(ApplyEditorSceneLoadCommand(scene.Commands, EditorSceneFileCommand{.Path = *resolved}), *resolved);
        }

        // Shows or hides one base lane like the appearance panel's checkboxes: Surface (mesh), Edges (graph) and
        // Points (point cloud) are offered per entity wherever its appearance target exists. Without `lane` it is the
        // entity's primary one: surface for meshes, edges for graphs, points for point clouds.
        AgentOperationOutcome SetVisibility(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            if (!entity || !args->contains("visible") || !(*args)["visible"].is_boolean())
                return Fail(
                    "Pass {\"entity\": <stable id>, \"visible\": true | false, \"lane\": <optional surface | edges | points | bounds>}.");
            const bool visible = (*args)["visible"].get<bool>();
            const auto laneName = String(*args, "lane");
            if (args->contains("lane") && !laneName)
                return Fail("lane must be \"surface\", \"edges\", \"points\" or \"bounds\".");
            if (laneName == "bounds")
            {
                if (context.Attachment == nullptr)
                    return Fail("No editor attachment.");
                const auto visualization = PrepareEditorVisualizationEditingFrame(*context.Attachment);
                const auto status = ApplyEditorRenderHintCommand(visualization.Commands,
                    EditorRenderHintCommand{.StableEntityId = *entity, .SetBounds = true, .EnableBounds = visible});
                const bool ok = status == EditorCommandStatus::Applied || status == EditorCommandStatus::NoChange;
                return {.IsError = !ok,
                    .Text = Dump({{"status", DebugNameForEditorCommandStatus(status)},
                        {"entity", *entity},
                        {"lane", "bounds"},
                        {"visible", visible}})};
            }
            std::optional<EditorDomainWindowKind> lane;
            if (laneName == "surface") lane = EditorDomainWindowKind::Mesh;
            else if (laneName == "edges") lane = EditorDomainWindowKind::Graph;
            else if (laneName == "points") lane = EditorDomainWindowKind::PointCloud;
            else if (laneName)
                return Fail("lane must be \"surface\", \"edges\", \"points\" or \"bounds\".");
            const auto prepared = PrepareSnapshot(context);
            if (!prepared) return Fail(kNoWorkspace);
            for (const auto kind : {EditorDomainWindowKind::Mesh, EditorDomainWindowKind::Graph, EditorDomainWindowKind::PointCloud})
            {
                if (lane && *lane != kind) continue;
                const auto model = BuildEditorDomainWindowModel(prepared->SnapshotQueries, kind, nullptr, *entity);
                if (!model.HasSelectedEntity || !model.VisualizationTargetAvailable) continue;
                if (!lane && !model.DomainMatches) continue; // the primary lane is the one of the entity's own domain
                const bool mesh = kind == EditorDomainWindowKind::Mesh;
                const bool graph = kind == EditorDomainWindowKind::Graph;
                const auto visualization = PrepareEditorVisualizationEditingFrame(*context.Attachment);
                // Exactly the panel's command, with the current domain values it reads from the same model.
                const auto status = ApplyEditorRenderHintCommand(
                    visualization.Commands,
                    EditorRenderHintCommand{.StableEntityId = *entity,
                                            .SetSurface = mesh, .EnableSurface = visible,
                                            .SurfaceDomain = model.RenderHints.SurfaceDomainValue,
                                            .SetEdges = graph, .EnableEdges = visible,
                                            .EdgeDomain = model.RenderHints.EdgeDomainValue,
                                            .SetPoints = !mesh && !graph, .EnablePoints = visible,
                                            .PointType = model.RenderHints.PointRenderTypeValue});
                const bool ok = status == EditorCommandStatus::Applied || status == EditorCommandStatus::NoChange;
                return {.IsError = !ok, .Text = Dump({{"status", DebugNameForEditorCommandStatus(status)}, {"entity", *entity},
                                                       {"lane", mesh ? "surface" : graph ? "edges" : "points"}, {"visible", visible}})};
            }
            return Fail("Entity " + std::to_string(*entity) + " has no " + (laneName ? *laneName : std::string("mesh, graph or point-cloud")) +
                        " appearance to show or hide.");
        }

        // ---- render-attribute bindings (RUNTIME-316) -------------------------------------------
        // The attribute enum and its documentation come from the runtime table
        // (RenderAttributeRules), in table order, so the schema cannot drift from the command.
        const std::vector<RenderAttribute>& TableAttributes()
        {
            static const std::vector<RenderAttribute> attributes = [] {
                std::vector<RenderAttribute> out;
                for (const RenderAttributeRule& rule : RenderAttributeRules())
                    if (std::ranges::find(out, rule.Attribute) == out.end()) out.push_back(rule.Attribute);
                return out;
            }();
            return attributes;
        }
        std::string AttributeProperty()
        {
            std::string names;
            for (const RenderAttribute attribute : TableAttributes())
                names += std::string(names.empty() ? "" : ",") + "\"" + std::string(ToString(attribute)) + "\"";
            return R"("attribute":{"type":"string","enum":[)" + names + R"(],"description":"Render attribute (shader input) whose source property is chosen."})";
        }
        // Lanes by the names set_visibility uses.
        const char* LaneName(const GeometryRenderLane lane)
        {
            return lane == GeometryRenderLane::Surface ? "surface"
                : lane == GeometryRenderLane::Edges    ? "edges"
                                                       : "points";
        }
        std::optional<GeometryRenderLane> ParseLane(const std::optional<std::string>& name)
        {
            for (const GeometryRenderLane lane :
                {GeometryRenderLane::Surface, GeometryRenderLane::Edges, GeometryRenderLane::Points})
                if (name && *name == LaneName(lane))
                    return lane;
            return std::nullopt;
        }
        std::string LaneProperty(const std::string& description)
        {
            return R"("lane":{"type":"string","enum":["surface","edges","points"],"description":")" + description +
                "\"}";
        }
        // One line per lane row: attribute, accepted type, source domains and what Default draws.
        std::string AttributeTableText()
        {
            std::string text;
            for (const RenderLaneAttributeRow& row : RenderLaneAttributeRows())
            {
                const RenderAttributeRule* rule = FindRenderAttributeRule(row.Attribute, row.Domains.front());
                text += std::string(LaneName(row.Lane)) + " " + std::string(ToString(row.Attribute)) + " (" +
                    std::string(RenderAttributeExpectedTypeText(*rule)) +
                    (rule->RequireFiniteValues ? ", all finite" : "") + ") from";
                for (const GeometryElementDomain domain : row.Domains)
                    text += " " + std::string(ToString(domain));
                text += " [default: " + std::string(row.DefaultDescription) + "]. ";
            }
            return text;
        }
        std::optional<RenderAttribute> ParseAttribute(const std::optional<std::string>& name)
        {
            RenderAttribute attribute{};
            if (!name || !TryParseRenderAttribute(*name, attribute)) return std::nullopt;
            return attribute;
        }
        AgentOperationOutcome InvalidParams(std::string message)
        {
            return {.IsError = true, .Text = std::move(message), .ErrorCode = "invalid_params"};
        }

        // One row of the binding model the Appearance panel draws (BuildEditorAttributeBindingModel).
        Json AttributeRowJson(const EditorAttributeBindingRow& row)
        {
            const RenderAttributeRule* rule = FindRenderAttributeRule(row.Attribute, row.Domains.front().Domain);
            Json domains = Json::array();
            for (const EditorAttributeBindingDomain& domain : row.Domains)
                domains.push_back({{"domain", std::string(ToString(domain.Domain))}, {"count", domain.ElementCount}});
            Json compatible = Json::array();
            Json incompatible = Json::array();
            for (const EditorAttributeBindingCandidate& candidate : row.Candidates)
            {
                Json entry{{"name", candidate.Property.Name},
                    {"domain", std::string(ToString(candidate.Property.Domain))},
                    {"kind", KindName(candidate.Property.ValueKind)},
                    {"count", candidate.ElementCount}};
                if (candidate.Compatible)
                {
                    compatible.push_back(std::move(entry));
                    continue;
                }
                entry["status"] = std::string(ToString(candidate.Reason));
                entry["reason"] = candidate.DisabledReason;
                incompatible.push_back(std::move(entry));
            }
            Json out{{"lane", LaneName(row.Lane)},
                {"attribute", std::string(ToString(row.Attribute))},
                {"domains", std::move(domains)},
                {"expected_type", row.ExpectedType},
                {"default", row.DefaultSource},
                {"canonical_property",
                    rule != nullptr && !rule->CanonicalProperty.empty() ? Json(std::string(rule->CanonicalProperty))
                                                                        : Json(nullptr)},
                {"bound", row.Bound},
                {"source",
                    row.Bound ? Json{{"name", row.Source.Name}, {"domain", std::string(ToString(row.Source.Domain))}}
                              : Json(nullptr)},
                {"using_fallback", row.UsingFallback},
                {"candidates", std::move(compatible)},
                {"incompatible", std::move(incompatible)}};
            if (row.Bound) out["status"] = std::string(ToString(row.Resolution.Status));
            if (!row.Diagnostic.empty())
                out["diagnostic"] = row.Diagnostic;
            return out;
        }

        // The entity's binding table through the prepared snapshot (the inspector carries the same
        // model as the Appearance panel). `HasEntity` is false for an unknown entity; an entity without
        // geometry has no rows.
        struct AttributeBindingLookup
        {
            bool HasEntity{false};
            EditorAttributeBindingModel Model{};
        };
        AttributeBindingLookup AttributeBindingModel(const AgentOperationContext& context, std::uint32_t entity)
        {
            const auto prepared = PrepareSnapshot(context);
            if (!prepared) return {};
            auto inspector = BuildEditorInspectorModel(prepared->SnapshotQueries, nullptr, entity);
            return {.HasEntity = inspector.HasEntity, .Model = std::move(inspector.PropertyCatalog.AttributeBindings)};
        }
        AgentOperationOutcome UnknownEntity(std::uint32_t entity)
        {
            return {.IsError = true, .Text = "No entity with id " + std::to_string(entity) + ".",
                    .ErrorCode = ErrorCodeFor(EditorCommandStatus::StaleEntity)};
        }

        AgentOperationOutcome AttributeBindings(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            if (!entity)
                return InvalidParams("Pass {\"entity\": <stable id>, \"lane\": <optional>, \"attribute\": <optional>, "
                                     "\"domain\": <optional>}.");
            const auto laneName = String(*args, "lane");
            const auto lane = ParseLane(laneName);
            if (args->contains("lane") && !lane)
                return InvalidParams("Unknown lane '" + laneName.value_or("") + "'; use surface, edges or points.");
            const auto domainName = String(*args, "domain");
            const auto domain = ParseDomain(domainName);
            if (args->contains("domain") && !domain) return InvalidParams(UnknownDomainMessage(domainName.value_or("")));
            const auto attributeName = String(*args, "attribute");
            const auto attribute = ParseAttribute(attributeName);
            if (args->contains("attribute") && !attribute) return InvalidParams("Unknown attribute '" + attributeName.value_or("") + "'.");
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto lookup = AttributeBindingModel(context, *entity);
            if (!lookup.HasEntity) return UnknownEntity(*entity);
            if (lookup.Model.Rows.empty())
                return {.IsError = true,
                        .Text = "Entity " + std::to_string(*entity) + " has no geometry element domain with render attributes.",
                        .ErrorCode = ErrorCodeFor(EditorCommandStatus::UnsupportedGeometryDomain)};
            // Grouped by lane in the table's order.
            Json lanes = Json::array();
            for (const EditorAttributeBindingRow& row : lookup.Model.Rows)
            {
                if ((lane && row.Lane != *lane) || (attribute && row.Attribute != *attribute) ||
                    (domain &&
                        std::ranges::none_of(row.Domains,
                            [&](const EditorAttributeBindingDomain& d)
                            {
                                return d.Domain == *domain;
                            })))
                    continue;
                const std::string name = LaneName(row.Lane);
                auto group = std::ranges::find_if(lanes,
                    [&](const Json& g)
                    {
                        return g["lane"] == name;
                    });
                if (group == lanes.end())
                {
                    lanes.push_back({{"lane", name}, {"attributes", Json::array()}});
                    group = lanes.end() - 1;
                }
                (*group)["attributes"].push_back(AttributeRowJson(row));
            }
            return Ok({{"entity", *entity}, {"lanes", std::move(lanes)}});
        }

        // Why a refused binding was refused, in the panel's words where the model has them.
        std::string BindingRefusal(const EditorCommandStatus status, const EditorAttributeBindingCommand& command,
                                   const EditorAttributeBindingRow* row)
        {
            const std::string what =
                std::string(ToString(command.Attribute)) + " of the " + LaneName(command.Lane) + " lane";
            std::string text = std::string(DebugNameForEditorCommandStatus(status)) + ": ";
            if (status == EditorCommandStatus::StaleEntity) return text + "no entity with id " + std::to_string(command.StableEntityId) + ".";
            if (status == EditorCommandStatus::UnsupportedRenderAttribute && row != nullptr &&
                (command.Attribute == RenderAttribute::PointSize || command.Attribute == RenderAttribute::LineWidth))
                return text + "entity " + std::to_string(command.StableEntityId) + " does not draw the lane that shows " + what +
                       " (point sizes need a shown points lane, line widths an edges lane; see set_visibility).";
            if (status == EditorCommandStatus::UnsupportedRenderAttribute || row == nullptr)
                return text + "entity " + std::to_string(command.StableEntityId) + " has no " + what + " row reading " +
                    std::string(ToString(command.Domain)) +
                    " (the lane does not read that element domain, or the entity lacks it); "
                    "see attribute_bindings.";
            if (const auto candidate = std::ranges::find_if(row->Candidates,
                    [&](const EditorAttributeBindingCandidate& c)
                    {
                        return c.Property.Name == command.PropertyName && c.Property.Domain == command.Domain;
                    });
                candidate != row->Candidates.end() && !candidate->DisabledReason.empty())
                return text + "'" + command.PropertyName + "' cannot be the " + what + " source: " + candidate->DisabledReason + ".";
            return text + "'" + command.PropertyName + "' cannot be the " + what + " source (" + row->ExpectedType +
                " on " + std::string(ToString(command.Domain)) + " expected); see attribute_bindings.";
        }

        const EditorAttributeBindingRow* FindAttributeRow(
            const EditorAttributeBindingModel& model, const GeometryRenderLane lane, const RenderAttribute attribute)
        {
            const auto found = std::ranges::find_if(model.Rows,
                [&](const EditorAttributeBindingRow& r)
                {
                    return r.Lane == lane && r.Attribute == attribute;
                });
            return found != model.Rows.end() ? &*found : nullptr;
        }

        // Runs the panel's ApplyEditorAttributeBindingCommand (one undo step on the attribute's owner)
        // and answers with the resulting row of the binding table; refusals carry the status as code.
        AgentOperationOutcome ApplyAttributeBinding(const AgentOperationContext& context, const EditorAttributeBindingCommand& command,
                                                    Json reply)
        {
            const auto visualization = PrepareEditorVisualizationEditingFrame(*context.Attachment);
            const EditorCommandStatus status = ApplyEditorAttributeBindingCommand(visualization.Commands, command);
            const auto lookup = AttributeBindingModel(context, command.StableEntityId);
            const EditorAttributeBindingRow* row = FindAttributeRow(lookup.Model, command.Lane, command.Attribute);
            if (status != EditorCommandStatus::Applied && status != EditorCommandStatus::NoChange)
                return {.IsError = true, .Text = BindingRefusal(status, command, row), .ErrorCode = ErrorCodeFor(status)};
            reply["status"] = DebugNameForEditorCommandStatus(status);
            reply["row"] = row != nullptr ? AttributeRowJson(*row) : Json(nullptr);
            return Ok(reply);
        }

        AgentOperationOutcome BindAttribute(const AgentOperationContext& context, std::string_view arguments)
        {
            constexpr const char* kUsage =
                "Pass {\"entity\": <stable id>, \"lane\": surface|edges|points, \"attribute\": <attribute>} and exactly one of "
                "\"property\": <property name> (with \"domain\" when the lane reads that name on several element domains) "
                "or \"default\": true.";
            const auto args = ParseObject(arguments);
            if (!args) return InvalidParams(kUsage);
            const auto entity = UInt(*args, "entity");
            const auto laneName = String(*args, "lane");
            const auto lane = ParseLane(laneName);
            const auto attributeName = String(*args, "attribute");
            const auto attribute = ParseAttribute(attributeName);
            const auto domainName = String(*args, "domain");
            const auto domain = ParseDomain(domainName);
            const auto property = String(*args, "property");
            const bool restoreDefault = args->contains("default") && (*args)["default"].is_boolean() && (*args)["default"].get<bool>();
            if (!entity || !laneName || !attributeName)
                return InvalidParams(kUsage);
            if (!lane)
                return InvalidParams("Unknown lane '" + *laneName + "'; use surface, edges or points.");
            if (!attribute) return InvalidParams("Unknown attribute '" + *attributeName + "'; see the tool schema.");
            if (args->contains("domain") && !domain)
                return InvalidParams(UnknownDomainMessage(domainName.value_or("")));
            if (args->contains("property") == args->contains("default") || (args->contains("property") && (!property || property->empty())) ||
                (args->contains("default") && !restoreDefault))
                return InvalidParams(kUsage);
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            // Without "domain" the source is the one domain of the lane row carrying that name.
            GeometryElementDomain sourceDomain = domain.value_or(GeometryElementDomain::Unknown);
            if (property && !domain)
            {
                const auto lookup = AttributeBindingModel(context, *entity);
                if (!lookup.HasEntity)
                    return UnknownEntity(*entity);
                const EditorAttributeBindingRow* row = FindAttributeRow(lookup.Model, *lane, *attribute);
                if (row != nullptr)
                {
                    std::vector<GeometryElementDomain> carriers;
                    for (const EditorAttributeBindingCandidate& candidate : row->Candidates)
                        if (candidate.Property.Name == *property)
                            carriers.push_back(candidate.Property.Domain);
                    if (carriers.size() > 1u)
                        return InvalidParams("'" + *property + "' exists on several element domains of the " +
                            *laneName + " lane; pass \"domain\".");
                    sourceDomain = carriers.empty() ? row->Domains.front().Domain : carriers.front();
                }
            }
            return ApplyAttributeBinding(context,
                EditorAttributeBindingCommand{.StableEntityId = *entity,
                    .Lane = *lane,
                    .Attribute = *attribute,
                    .Domain = sourceDomain,
                    .PropertyName = property.value_or("")},
                Json{{"entity", *entity},
                    {"lane", *laneName},
                    {"attribute", *attributeName},
                    {"domain",
                        sourceDomain == GeometryElementDomain::Unknown ? Json(nullptr)
                                                                       : Json(std::string(ToString(sourceDomain)))},
                    {"property", property ? Json(*property) : Json(nullptr)},
                    {"default", restoreDefault}});
        }

        // ---- vector fields (UI-083) ----------------------------------------------------------
        // The Appearance panel's vector-field model and command; the colormap names are the panel's.
        constexpr std::array<const char*, 6> kVectorFieldColormaps{
            "Viridis", "Inferno", "Plasma", "Jet", "Coolwarm", "Heat"};

        struct VectorFieldLookup
        {
            bool Prepared{false};
            bool HasEntity{false};
            EditorVectorFieldModel Model{};
        };

        VectorFieldLookup LookupVectorFields(const AgentOperationContext& context, const std::uint32_t entity)
        {
            const auto prepared = PrepareSnapshot(context);
            if (!prepared)
                return {};
            // Every window kind carries the entity's vector-field model.
            auto window =
                BuildEditorDomainWindowModel(prepared->SnapshotQueries, EditorDomainWindowKind::Mesh, nullptr, entity);
            return {.Prepared = true, .HasEntity = window.HasSelectedEntity, .Model = std::move(window.VectorFields)};
        }

        Json VectorFieldLayerJson(const EditorVectorFieldLayerRow& row)
        {
            const GeometryVectorFieldLayerRecipe& layer = row.Layer;
            const auto colormap = static_cast<std::size_t>(layer.ColorMap);
            return {{"domain", std::string(ToString(layer.Vector.Domain))},
                {"property", layer.Vector.Name},
                {"visible", layer.Enabled},
                {"depth_tested", layer.DepthTested},
                {"length_mode", layer.LengthMode == GeometryVectorFieldLengthMode::Raw ? "raw" : "normalized"},
                {"length", layer.Length},
                {"line_width", layer.LineWidthPx},
                {"color", {layer.Color.x, layer.Color.y, layer.Color.z, layer.Color.w}},
                {"stride", layer.Stride},
                {"max_glyphs", layer.MaxGlyphs},
                {"length_property", layer.LengthProperty},
                {"color_property", layer.ColorProperty},
                {"colormap", colormap < kVectorFieldColormaps.size() ? kVectorFieldColormaps[colormap] : "Viridis"},
                {"color_auto_range", layer.ColorAutoRange},
                {"color_range", {layer.ColorRangeMin, layer.ColorRangeMax}},
                {"available", row.SourceAvailable},
                {"status", row.Status}};
        }

        Json VectorFieldLayersJson(const EditorVectorFieldModel& model)
        {
            Json layers = Json::array();
            for (const EditorVectorFieldLayerRow& row : model.Layers)
                layers.push_back(VectorFieldLayerJson(row));
            return layers;
        }

        AgentOperationOutcome VectorFields(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            if (!entity)
                return InvalidParams("Pass {\"entity\": <stable id>}.");
            const VectorFieldLookup lookup = LookupVectorFields(context, *entity);
            if (!lookup.Prepared)
                return Fail(kNoWorkspace);
            if (!lookup.HasEntity)
                return UnknownEntity(*entity);
            Json domains = Json::array();
            for (const EditorVectorFieldDomainOption& domain : lookup.Model.Domains)
            {
                Json vectors = Json::array();
                for (const EditorVectorFieldPropertyOption& option : domain.Properties)
                    if (option.Compatible)
                        vectors.push_back(option.Name);
                Json styles = Json::array();
                for (const EditorVectorFieldStyleOption& option : domain.StyleProperties)
                    styles.push_back(
                        {{"name", option.Name}, {"kind", KindName(option.Kind)}, {"scalar", option.Scalar}});
                domains.push_back({{"domain", std::string(ToString(domain.Domain))},
                    {"label", domain.Label},
                    {"count", domain.ElementCount},
                    {"vector_properties", vectors},
                    {"style_properties", styles}});
            }
            return Ok({{"entity", *entity}, {"domains", domains}, {"layers", VectorFieldLayersJson(lookup.Model)}});
        }

        AgentOperationOutcome VectorField(const AgentOperationContext& context, std::string_view arguments)
        {
            constexpr const char* kUsage =
                "Pass {\"entity\": <stable id>, \"action\": add|update|remove, \"domain\": <element domain>, "
                "\"property\": <vec3 property>}; update also takes the fields to change (see the schema).";
            const auto args = ParseObject(arguments);
            if (!args)
                return InvalidParams(kUsage);
            const auto entity = UInt(*args, "entity");
            const auto action = String(*args, "action");
            const auto domainName = String(*args, "domain");
            const auto domain = ParseDomain(domainName);
            const auto property = String(*args, "property");
            if (!entity || !action || !domainName || !property || property->empty())
                return InvalidParams(kUsage);
            if (!domain)
                return InvalidParams(UnknownDomainMessage(*domainName));
            EditorVectorFieldOperation operation{};
            if (*action == "add")
                operation = EditorVectorFieldOperation::Add;
            else if (*action == "update")
                operation = EditorVectorFieldOperation::Update;
            else if (*action == "remove")
                operation = EditorVectorFieldOperation::Remove;
            else
                return InvalidParams("Unknown action '" + *action + "'; use add, update or remove.");

            // Update edits the current layer field by field; add and remove take none (add uses the
            // panel's defaults, so each action is the panel's one step).
            std::vector<std::function<void(GeometryVectorFieldLayerRecipe&)>> edits;
            std::string bad;
            const auto field = [&](const char* key, const auto& parse)
            {
                if (args->contains(key) && bad.empty() && !parse((*args)[key]))
                    bad = key;
            };
            const auto number = [](const Json& v)
            {
                return v.is_number();
            };
            field("visible",
                [&](const Json& v)
                {
                    if (!v.is_boolean())
                        return false;
                    edits.push_back(
                        [on = v.get<bool>()](auto& l)
                        {
                            l.Enabled = on;
                        });
                    return true;
                });
            field("depth_tested",
                [&](const Json& v)
                {
                    if (!v.is_boolean())
                        return false;
                    edits.push_back(
                        [on = v.get<bool>()](auto& l)
                        {
                            l.DepthTested = on;
                        });
                    return true;
                });
            field("length_mode",
                [&](const Json& v)
                {
                    if (!v.is_string() || (v != "normalized" && v != "raw"))
                        return false;
                    const auto mode =
                        v == "raw" ? GeometryVectorFieldLengthMode::Raw : GeometryVectorFieldLengthMode::Normalized;
                    edits.push_back(
                        [mode](auto& l)
                        {
                            l.LengthMode = mode;
                        });
                    return true;
                });
            field("length",
                [&](const Json& v)
                {
                    if (!number(v))
                        return false;
                    edits.push_back(
                        [x = v.get<float>()](auto& l)
                        {
                            l.Length = x;
                        });
                    return true;
                });
            field("line_width",
                [&](const Json& v)
                {
                    if (!number(v))
                        return false;
                    edits.push_back(
                        [x = v.get<float>()](auto& l)
                        {
                            l.LineWidthPx = x;
                        });
                    return true;
                });
            field("color",
                [&](const Json& v)
                {
                    if (!v.is_array() || v.size() != 4u || !std::ranges::all_of(v, number))
                        return false;
                    edits.push_back(
                        [c = glm::vec4{v[0].get<float>(), v[1].get<float>(), v[2].get<float>(), v[3].get<float>()}](
                            auto& l)
                        {
                            l.Color = c;
                        });
                    return true;
                });
            for (const char* key : {"stride", "max_glyphs"})
            {
                field(key,
                    [&](const Json&)
                    {
                        const auto value = UInt(*args, key);
                        if (!value)
                            return false;
                        const bool stride = std::string_view{key} == "stride";
                        edits.push_back(
                            [x = *value, stride](auto& l)
                            {
                                (stride ? l.Stride : l.MaxGlyphs) = x;
                            });
                        return true;
                    });
            }
            for (const char* key : {"length_property", "color_property"})
            {
                field(key,
                    [&](const Json& v)
                    {
                        if (!v.is_string())
                            return false;
                        const bool length = std::string_view{key} == "length_property";
                        edits.push_back(
                            [name = v.get<std::string>(), length](auto& l)
                            {
                                (length ? l.LengthProperty : l.ColorProperty) = name;
                            });
                        return true;
                    });
            }
            field("colormap",
                [&](const Json& v)
                {
                    const auto it = v.is_string() ? std::ranges::find(kVectorFieldColormaps, v.get<std::string>())
                                                  : kVectorFieldColormaps.end();
                    if (it == kVectorFieldColormaps.end())
                        return false;
                    const auto index = std::distance(kVectorFieldColormaps.begin(), it);
                    edits.push_back(
                        [index](auto& l)
                        {
                            l.ColorMap = static_cast<decltype(l.ColorMap)>(index);
                        });
                    return true;
                });
            field("color_auto_range",
                [&](const Json& v)
                {
                    if (!v.is_boolean())
                        return false;
                    edits.push_back(
                        [on = v.get<bool>()](auto& l)
                        {
                            l.ColorAutoRange = on;
                        });
                    return true;
                });
            field("color_range",
                [&](const Json& v)
                {
                    if (!v.is_array() || v.size() != 2u || !std::ranges::all_of(v, number))
                        return false;
                    edits.push_back(
                        [lo = v[0].get<float>(), hi = v[1].get<float>()](auto& l)
                        {
                            l.ColorRangeMin = lo;
                            l.ColorRangeMax = hi;
                        });
                    return true;
                });
            if (!bad.empty())
                return InvalidParams("Invalid \"" + bad + "\"; see the schema.");
            if (!edits.empty() && operation != EditorVectorFieldOperation::Update)
                return InvalidParams("Only update takes layer fields; add uses the panel's defaults.");

            const VectorFieldLookup lookup = LookupVectorFields(context, *entity);
            if (!lookup.Prepared)
                return Fail(kNoWorkspace);
            if (!lookup.HasEntity)
                return UnknownEntity(*entity);
            GeometryVectorFieldLayerRecipe layer{
                .Vector = {.Domain = *domain, .Name = *property, .ValueKind = Geometry::PropertyValueKind::Vec3}};
            for (const EditorVectorFieldLayerRow& row : lookup.Model.Layers)
                if (row.Layer.Vector.Domain == *domain && row.Layer.Vector.Name == *property)
                    layer = row.Layer;
            for (const auto& edit : edits)
                edit(layer);

            const auto visualization = PrepareEditorVisualizationEditingFrame(*context.Attachment);
            const EditorCommandStatus status = ApplyEditorGeometryVectorFieldCommand(visualization.Commands,
                EditorGeometryVectorFieldCommand{
                    .StableEntityId = *entity, .Operation = operation, .Layer = std::move(layer)});
            if (status != EditorCommandStatus::Applied && status != EditorCommandStatus::NoChange)
                return {.IsError = true,
                    .Text = "The vector field command answered " +
                        std::string(DebugNameForEditorCommandStatus(status)) +
                        " (a missing field, a property that does not resolve, or a value out of range).",
                    .ErrorCode = ErrorCodeFor(status)};
            return Ok({{"status", DebugNameForEditorCommandStatus(status)},
                {"layers", VectorFieldLayersJson(LookupVectorFields(context, *entity).Model)}});
        }

        AgentOperationOutcome ShowProperty(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            const auto name = args ? String(*args, "name") : std::nullopt;
            if (!entity || !name) return Fail("Pass {\"entity\": <stable id>, \"name\": <property>, \"domain\": <optional domain>}.");
            const auto domainName = args ? String(*args, "domain") : std::nullopt;
            const auto domain = ParseDomain(domainName);
            if (domainName && !domain) return Fail(UnknownDomainMessage(*domainName));
            const bool normalDirection = args && args->contains("normal_direction") && (*args)["normal_direction"].is_boolean() &&
                                         (*args)["normal_direction"].get<bool>();
            const auto prepared = PrepareSnapshot(context);
            if (!prepared) return Fail(kNoWorkspace);
            const auto inspector = BuildEditorInspectorModel(prepared->SnapshotQueries, nullptr, *entity);
            if (!inspector.HasEntity) return Fail("No entity with id " + std::to_string(*entity) + ".");
            const auto row = std::ranges::find_if(inspector.PropertyCatalog.Rows, [&](const EditorPropertyCatalogRow& r) {
                return r.Name == *name && !r.Internal && (!domain || r.Descriptor.Domain == *domain);
            });
            if (row == inspector.PropertyCatalog.Rows.end())
                return Fail("Entity " + std::to_string(*entity) + " has no property '" + *name + "'" +
                            (domain ? " on " + *domainName : std::string{}) + "; see entity_properties.");
            Json reply{{"entity", *entity}, {"domain", std::string(ToString(row->Descriptor.Domain))}, {"name", row->Name}};
            // One Color mechanism: showing a property is binding it as the Color source of its domain
            // (bind_attribute color), the same command and overlay as the Appearance panel's selector.
            if (!normalDirection)
            {
                const auto lane = DefaultColorLaneFor(row->Descriptor.Domain);
                if (!lane)
                    return {.IsError = true,
                        .Text = "'" + row->Name + "' is on " + std::string(ToString(row->Descriptor.Domain)) +
                            ", which no lane's Color reads.",
                        .ErrorCode = ErrorCodeFor(EditorCommandStatus::UnsupportedRenderAttribute)};
                reply["lane"] = LaneName(*lane);
                return ApplyAttributeBinding(context,
                    EditorAttributeBindingCommand{.StableEntityId = *entity,
                        .Lane = *lane,
                        .Attribute = RenderAttribute::Color,
                        .Domain = row->Descriptor.Domain,
                        .PropertyName = row->Name},
                    std::move(reply));
            }
            // Normal directions are a display recipe of a vector, not a source binding; they write the
            // same overlay through the recipe command the Color binding itself uses.
            const auto visualization = PrepareEditorVisualizationEditingFrame(*context.Attachment);
            const auto status = ApplyEditorVisualizationRecipeCommand(visualization.Commands,
                {.StableEntityId = *entity, .Recipe = MakeEditorPropertyVisualizationRecipe(row->Descriptor, true)});
            const bool ok = status == EditorCommandStatus::Applied || status == EditorCommandStatus::NoChange;
            reply["status"] = DebugNameForEditorCommandStatus(status);
            if (!ok) return {.IsError = true, .Text = Dump(reply), .ErrorCode = ErrorCodeFor(status)};
            return Ok(reply);
        }

        Json PoseJson(const EditorCameraPose& pose)
        {
            const auto vec = [](const glm::vec3& v) { return Json::array({v.x, v.y, v.z}); };
            return {{"position", vec(pose.Position)}, {"forward", vec(pose.Forward)}, {"up", vec(pose.Up)}};
        }

        std::optional<glm::vec3> Vec3(const Json& object, const char* key)
        {
            const auto it = object.find(key);
            if (it == object.end() || !it->is_array() || it->size() != 3u) return std::nullopt;
            glm::vec3 out{};
            for (std::size_t i = 0; i != 3u; ++i)
            {
                if (!(*it)[i].is_number()) return std::nullopt;
                const double value = (*it)[i].get<double>();
                if (!std::isfinite(value) || std::abs(value) > 1.0e30) return std::nullopt;
                out[static_cast<glm::length_t>(i)] = static_cast<float>(value);
            }
            return out;
        }

        // The main camera, through the editor commands the Camera panel uses. Exactly one of:
        //   controller: switch the controller kind (the panel's Orbit / Fly / Free look / Top down buttons);
        //   pose:       {position, target, up?} through ApplyEditorCameraPoseCommand;
        //   preset:     a named view framing `entities` (default: everything with world bounds);
        //   focus:      true, frame `entities` (default: the current selection), keeping the direction.
        // Camera changes are view state: not undoable and not destructive.
        AgentOperationOutcome SetCamera(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            constexpr const char* kUsage =
                "Pass exactly one of {\"controller\": \"orbit\" | \"fly\" | \"free_look\" | \"top_down\"}, "
                "{\"pose\": {\"position\": [x,y,z], \"target\": [x,y,z], \"up\": [x,y,z]}}, "
                "{\"preset\": \"front\" | \"back\" | \"left\" | \"right\" | \"top\" | \"bottom\" | \"isometric\"} or "
                "{\"focus\": true}; preset and focus take an optional \"entities\": [stable ids].";
            if (!args) return Fail(kUsage);
            const int modes = int(args->contains("controller")) + int(args->contains("pose")) +
                              int(args->contains("preset")) + int(args->contains("focus"));
            if (modes != 1) return Fail(kUsage);
            const auto prepared = PrepareSnapshot(context);
            if (!prepared) return Fail(kNoWorkspace);
            if (!prepared->Frame.CameraRender.CameraControlsAvailable) return Fail("Camera controls are unavailable in this workspace.");
            const auto scene = PrepareEditorSceneEditingFrame(*context.Attachment);

            if (args->contains("controller"))
            {
                const auto name = String(*args, "controller");
                using Kind = EditorCameraControllerKind;
                std::optional<Kind> kind;
                if (name == "orbit") kind = Kind::Orbit;
                else if (name == "fly") kind = Kind::Fly;
                else if (name == "free_look") kind = Kind::FreeLook;
                else if (name == "top_down") kind = Kind::TopDown;
                if (!kind) return Fail(kUsage);
                const std::string previous = prepared->Frame.CameraRender.HasMainCameraController
                    ? std::string(DebugNameForEditorCameraControllerKind(prepared->Frame.CameraRender.MainCameraControllerKind))
                    : std::string("none");
                const auto status = ApplyEditorCameraControllerCommand(scene.Commands, EditorCameraControllerCommand{.Kind = *kind});
                const bool ok = status == EditorCommandStatus::Applied || status == EditorCommandStatus::NoChange;
                return {.IsError = !ok, .Text = Dump({{"status", DebugNameForEditorCommandStatus(status)}, {"controller", *name},
                                                       {"previous", previous}})};
            }

            EditorCameraPoseCommand command{};
            if (args->contains("pose"))
            {
                const auto& pose = (*args)["pose"];
                const auto position = pose.is_object() ? Vec3(pose, "position") : std::nullopt;
                const auto target = pose.is_object() ? Vec3(pose, "target") : std::nullopt;
                if (!position || !target) return Fail("pose needs finite numeric position and target, each [x,y,z].");
                command.Mode = EditorCameraPoseMode::Pose;
                command.Position = *position;
                command.Target = *target;
                if (pose.contains("up"))
                {
                    const auto up = Vec3(pose, "up");
                    if (!up) return Fail("pose.up must be finite numbers [x,y,z].");
                    command.Up = *up;
                }
            }
            else
            {
                if (args->contains("preset"))
                {
                    const auto name = String(*args, "preset");
                    const auto preset = name ? ParseCameraViewPreset(*name) : std::nullopt;
                    if (!preset) return Fail(kUsage);
                    command.Mode = EditorCameraPoseMode::Preset;
                    command.Preset = *preset;
                }
                else
                {
                    if (!(*args)["focus"].is_boolean() || !(*args)["focus"].get<bool>()) return Fail(kUsage);
                    command.Mode = EditorCameraPoseMode::Focus;
                }
                if (args->contains("entities"))
                {
                    const auto& list = (*args)["entities"];
                    if (!list.is_array() || list.size() > 4096u) return Fail("entities must be an array of stable ids.");
                    for (const auto& id : list)
                    {
                        if (!id.is_number_integer() || id.get<std::int64_t>() < 0 || id.get<std::int64_t>() > std::int64_t(UINT32_MAX))
                            return Fail("entities must be an array of stable ids.");
                        command.StableEntityIds.push_back(id.get<std::uint32_t>());
                    }
                }
            }

            const auto result = ApplyEditorCameraPoseCommand(scene.Commands, command);
            Json out{{"status", DebugNameForEditorCommandStatus(result.Status)}};
            if (result.HasPose)
            {
                out["previous"] = PoseJson(result.Previous);
                out["current"] = PoseJson(result.Current);
            }
            if (result.Status == EditorCommandStatus::Applied)
            {
                out["up_ignored"] = result.UpIgnored;
                out["position_clamped"] = result.PositionClamped;
                return Ok(out);
            }
            out["error"] = result.Status == EditorCommandStatus::NoChange
                ? "Nothing with world bounds to frame (imported geometry has bounds); the camera did not move."
                : result.Status == EditorCommandStatus::UnsupportedCameraPose
                    ? "The main camera controller cannot look that way; switch controller or use another pose."
                    : "The camera was not changed.";
            return {.IsError = true, .Text = Dump(out)};
        }

        // ---- history ---------------------------------------------------------------------
        Json HistoryJson(const AgentOperationContext& context)
        {
            if (context.History == nullptr) return {{"available", false}};
            const auto snapshot = context.History->Snapshot();
            return {{"available", true}, {"can_undo", snapshot.CanUndo}, {"can_redo", snapshot.CanRedo},
                    {"undo_label", snapshot.UndoLabel}, {"redo_label", snapshot.RedoLabel},
                    {"undo_count", snapshot.UndoCount}, {"redo_count", snapshot.RedoCount},
                    {"dirty", snapshot.Dirty}, {"revision", snapshot.Revision}};
        }
        AgentOperationOutcome History(const AgentOperationContext& context, std::string_view) { return Ok(HistoryJson(context)); }
        AgentOperationOutcome UndoRedo(const AgentOperationContext& context, std::string_view arguments, bool undo)
        {
            const auto args = ParseObject(arguments);
            if (!args)
                return {.IsError = true, .Text = "Arguments must be a JSON object.", .ErrorCode = "invalid_params"};
            const auto requested = args->contains("steps") ? UInt(*args, "steps") : std::optional<std::uint32_t>{1u};
            if (!requested || *requested < 1 || *requested > 64)
                return {.IsError = true, .Text = "steps must be an integer within 1..64.", .ErrorCode = "invalid_params"};
            const std::uint32_t steps = *requested;
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto scene = PrepareEditorSceneEditingFrame(*context.Attachment);
            if (!scene.DocumentCommands.Available()) return Fail("Undo/redo is unavailable in this workspace.");
            Json labels = Json::array();
            for (std::uint32_t i = 0; i < steps; ++i)
            {
                const auto result = undo ? scene.DocumentCommands.Undo() : scene.DocumentCommands.Redo();
                if (result.Status == EditorCommandHistoryStatus::EmptyUndoStack ||
                    result.Status == EditorCommandHistoryStatus::EmptyRedoStack)
                    break;
                if (!result.Succeeded())
                {
                    return {.IsError = true,
                        .Text = Dump({{undo ? "undone" : "redone", labels},
                            {"status", DebugNameForEditorCommandHistoryStatus(result.Status)},
                            {"label", result.Label},
                            {"history", HistoryJson(context)}})};
                }
                labels.push_back(result.Label);
            }
            return Ok({{undo ? "undone" : "redone", labels}, {"history", HistoryJson(context)}});
        }

        // ---- config ----------------------------------------------------------------------
        AgentOperationOutcome ConfigSections(const AgentOperationContext& context, std::string_view)
        {
            if (context.ConfigControl == nullptr) return Fail("Engine config control is unavailable.");
            Json sections = Json::array();
            for (const auto& entry : context.ConfigControl->SectionRegistry().Entries())
                sections.push_back({{"section", entry.DefaultSection.Name}, {"schema_id", entry.DefaultSection.SchemaId},
                                    {"schema_version", entry.DefaultSection.SchemaVersion},
                                    {"has_schema", !entry.SchemaJson.empty()}});
            return Ok({{"sections", sections}});
        }

        // The ExportEngineConfigSchema document, or one section's entry of its $defs.
        AgentOperationOutcome ConfigSchema(const AgentOperationContext& context, std::string_view arguments)
        {
            if (context.ConfigControl == nullptr) return Fail("Engine config control is unavailable.");
            const auto args = ParseObject(arguments);
            if (!args) return Fail("Arguments must be a JSON object.");
            const auto& registry = context.ConfigControl->SectionRegistry();
            const std::string document = Core::Config::ExportEngineConfigSchema(registry);
            const auto section = String(*args, "section");
            if (!section) return {.IsError = false, .Text = document};
            if (registry.Find(*section) == nullptr) return Fail("Unknown config section '" + *section + "'.");
            const Json parsed = Json::parse(document, nullptr, false);
            return Ok(parsed["$defs"][*section]);
        }

        AgentOperationOutcome ConfigGet(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto section = args ? String(*args, "section") : std::nullopt;
            if (!section) return Fail("Pass {\"section\": \"<name>\"}.");
            if (context.ConfigControl == nullptr) return Fail("Engine config control is unavailable.");
            const auto* registration = context.ConfigControl->SectionRegistry().Find(*section);
            if (registration == nullptr) return Fail("Unknown config section '" + *section + "'.");
            const auto& active = context.ConfigControl->GetEngineConfigControlState().ActiveConfig;
            const auto* current = Core::Config::FindEngineConfigSection(active.AppSections, *section);
            const auto& payload = current != nullptr ? current->PayloadJson : registration->DefaultSection.PayloadJson;
            Json result{{"section", *section}, {"active", current != nullptr}, {"payload", ParsedOrString(payload)},
                        {"defaults", ParsedOrString(registration->DefaultSection.PayloadJson)}};
            if (!registration->SchemaJson.empty()) result["schema"] = ParsedOrString(registration->SchemaJson);
            return Ok(result);
        }

        struct SectionUpdate
        {
            std::string Section, Canonical;
            const Core::Config::EngineConfigSectionRegistration* Registration{nullptr};
            Core::Config::EngineConfigSectionValidationResult Validation{};
        };
        std::optional<SectionUpdate> ValidateSection(const AgentOperationContext& context, std::string_view arguments,
                                                     std::string& error)
        {
            const auto args = ParseObject(arguments);
            const auto section = args ? String(*args, "section") : std::nullopt;
            const auto payload = args ? Payload(*args) : std::nullopt;
            if (!section || !payload) { error = "Pass {\"section\": \"<name>\", \"payload\": {...}}."; return std::nullopt; }
            if (context.ConfigControl == nullptr) { error = "Engine config control is unavailable."; return std::nullopt; }
            const auto* registration = context.ConfigControl->SectionRegistry().Find(*section);
            if (registration == nullptr || !registration->Validate) { error = "Unknown config section '" + *section + "'."; return std::nullopt; }
            SectionUpdate update{.Section = *section, .Registration = registration};
            update.Validation = registration->Validate(*payload, registration->DefaultSection.PayloadJson, *section);
            update.Canonical = update.Validation.CanonicalPayloadJson;
            return update;
        }

        AgentOperationOutcome ConfigPreview(const AgentOperationContext& context, std::string_view arguments)
        {
            std::string error;
            const auto update = ValidateSection(context, arguments, error);
            if (!update) return Fail(error);
            return Ok({{"section", update->Section}, {"valid", update->Validation.Usable()},
                       {"state", std::string(Core::Config::ToString(update->Validation.State))},
                       {"canonical_payload", update->Validation.Usable() ? ParsedOrString(update->Canonical) : Json(nullptr)},
                       {"diagnostics", Diagnostics(update->Validation.Diagnostics)}});
        }

        AgentOperationOutcome ConfigApply(const AgentOperationContext& context, std::string_view arguments)
        {
            std::string error;
            const auto update = ValidateSection(context, arguments, error);
            if (!update) return Fail(error);
            if (!update->Validation.Usable())
                return {.IsError = true, .Text = Dump({{"applied", false}, {"diagnostics", Diagnostics(update->Validation.Diagnostics)}})};
            // Same path as the panels: upsert into the active config, preview the whole
            // document, then apply the hot subset with the agent as the recorded source.
            auto candidate = context.ConfigControl->GetEngineConfigControlState().ActiveConfig;
            auto section = update->Registration->DefaultSection;
            section.PayloadJson = update->Canonical;
            Core::Config::UpsertEngineConfigSection(candidate.AppSections, std::move(section));
            const auto load = context.ConfigControl->PreviewEngineConfigControlDocument(
                Core::Config::SerializeEngineConfig(candidate), "agent:" + update->Section);
            const auto applied = context.ConfigControl->ApplyEngineConfigHotSubset(load, RuntimeConfigControlSource::AgentCli);
            const Json out{{"applied", applied.Succeeded()}, {"changed", applied.SectionChanged(update->Section)},
                           {"payload", ParsedOrString(update->Canonical)}, {"diagnostics", Diagnostics(applied.LoadResult.Diagnostics)}};
            return {.IsError = !applied.Succeeded(), .Text = Dump(out)};
        }

        // ---- jobs (RUNTIME-279) and log -----------------------------------------------------
        // A job token as the job tools print and accept it: "<index>:<generation>".
        std::string TokenText(const JobToken token)
        {
            return std::to_string(token.Index) + ":" + std::to_string(token.Generation);
        }
        std::optional<JobToken> ParseToken(const std::string& text)
        {
            const auto colon = text.find(':');
            if (colon == std::string::npos) return std::nullopt;
            const auto number = [](std::string_view digits) -> std::optional<std::uint32_t> {
                std::uint32_t value = 0;
                const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
                if (digits.empty() || error != std::errc{} || end != digits.data() + digits.size()) return std::nullopt;
                return value;
            };
            const auto index = number(std::string_view(text).substr(0, colon));
            const auto generation = number(std::string_view(text).substr(colon + 1));
            if (!index || !generation) return std::nullopt;
            const JobToken token{*index, *generation};
            return token.IsValid() ? std::optional{token} : std::nullopt;
        }
        // The editor part of a jobs_list row: the output it writes, the backend domain its config
        // requested and the one it resolved to (null while unknown) and the run's diagnostic.
        Json EditorJobJson(const EditorJobRecord& editor)
        {
            const auto domain = [](const std::optional<EditorJobDomain> value) {
                return value ? Json(std::string(ToString(*value))) : Json(nullptr);
            };
            return Json{{"entity", editor.Identity.EntityId}, {"output", editor.Identity.OutputName},
                        {"requested_backend", domain(editor.RequestedJobDomain)},
                        {"resolved_backend", domain(editor.ResolvedJobDomain)},
                        {"diagnostic", editor.Diagnostic.empty() ? Json(nullptr) : Json(editor.Diagnostic)}};
        }
        // One row per job the service retains. `editor` (the output it writes) and `cancellable`
        // only for jobs the editor queued through its job surface: exactly the ones jobs_cancel accepts.
        Json JobRow(const JobSnapshot& job, const EditorJobRecord* editor)
        {
            Json row{{"token", TokenText(job.Token)}, {"name", job.DebugName}, {"state", std::string(ToString(job.State))},
                     {"progress", job.Progress.Determinate ? Json(job.Progress.Normalized) : Json(nullptr)},
                     {"elapsed_ms", job.ElapsedMilliseconds},
                     {"editor", editor != nullptr ? EditorJobJson(*editor) : Json(nullptr)},
                     {"cancellable", editor != nullptr && IsActiveEditorJobState(job.State)}};
            if (job.CorrelationId != 0u) row["correlation_id"] = job.CorrelationId;
            return row;
        }
        const EditorJobRecord* FindRecord(const std::vector<EditorJobRecord>& records, const JobToken token)
        {
            const auto it = std::find_if(records.begin(), records.end(), [token](const EditorJobRecord& r) { return r.Token == token; });
            return it != records.end() ? &*it : nullptr;
        }
        std::optional<JobSnapshot> FindJob(const JobService& jobs, const JobToken token)
        {
            for (auto& job : jobs.SnapshotAll())
                if (job.Token == token) return std::move(job);
            return std::nullopt;
        }

        AgentOperationOutcome JobsList(const AgentOperationContext& context, std::string_view)
        {
            if (context.Jobs == nullptr) return Fail("The job service is unavailable.");
            // Without an attached workspace every row is listed, none as an editor job.
            const auto editor = PrepareSnapshot(context) ? GetEditorJobs(PrepareEditorProcessingCommands(*context.Attachment))
                                                         : std::vector<EditorJobRecord>{};
            Json jobs = Json::array();
            for (const auto& job : context.Jobs->SnapshotAll()) jobs.push_back(JobRow(job, FindRecord(editor, job.Token)));
            return Ok({{"jobs", jobs}});
        }

        // Waits on frames, never on the main thread: an immediate answer, or a continuation that the
        // server polls once per frame until the job completed (its terminal result delivered), the
        // deadline passed, the scene was replaced or the workspace detached. The tool needs a
        // presented frame, so a minimized window ends it too (`viewport_not_presentable`).
        AgentOperationOutcome JobsWait(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            if (!args) return Fail("Pass {\"token\": \"<from jobs_list>\"} or {\"entity\": <id>, \"output\": \"<name>\"}.");
            const auto invalid = [](std::string message) {
                return AgentOperationOutcome{.IsError = true, .Text = std::move(message), .ErrorCode = "invalid_params"};
            };
            const bool byToken = args->contains("token");
            const bool byOutput = args->contains("entity") || args->contains("output");
            if (byToken == byOutput) return invalid("Pass either token, or entity and output.");
            std::uint32_t timeoutMs = 30000u;
            if (args->contains("timeout_ms"))
            {
                const auto value = UInt(*args, "timeout_ms");
                if (!value || *value > 60000u) return invalid("timeout_ms must be an integer from 0 to 60000.");
                timeoutMs = *value;
            }
            if (context.Jobs == nullptr) return Fail("The job service is unavailable.");
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto commands = PrepareEditorProcessingCommands(*context.Attachment);
            JobToken token{};
            if (byToken)
            {
                const auto text = String(*args, "token");
                const auto parsed = text ? ParseToken(*text) : std::nullopt;
                if (!parsed) return invalid("token must look like \"3:1\" (from jobs_list).");
                token = *parsed;
            }
            else
            {
                const auto entity = UInt(*args, "entity");
                const auto output = String(*args, "output");
                if (!entity || !output) return invalid("Pass entity (an integer) together with output (a property name).");
                // The newest run writing that output when the call is made; a later run is not followed.
                const auto run = FindEditorOperationRun(GetEditorJobs(commands), EditorOutputRef{*entity, *output});
                if (!run)
                    return {.IsError = true, .Text = "No editor job writes '" + *output + "' of entity " + std::to_string(*entity) + ".",
                            .ErrorCode = "unknown_job"};
                token = run->Token;
            }
            // Every answer of the session carries its scene epoch, which a scene new/load/close advances.
            const auto sceneEpoch = [](const EditorProcessingCommands& c) { return GetEditorOperationProgress(c, EditorRunCorrelation{}).Epoch; };
            const std::uint64_t epoch = sceneEpoch(commands);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
            // The last row this wait saw and its state: a job reaped between two polls (it ended without
            // a result to deliver, so it completed and was reaped in one frame) still ended for this wait.
            auto seen = std::make_shared<std::optional<std::pair<Json, JobState>>>();
            // Set by `notifications/cancelled`: a cancelled wait frees its slot at the next poll.
            auto abandoned = std::make_shared<bool>(false);
            const auto step = [token, epoch, deadline, sceneEpoch, seen, abandoned](const AgentOperationContext& current)
                -> std::optional<AgentOperationOutcome> {
                if (*abandoned) return Fail("The wait was cancelled.");
                if (current.Attachment == nullptr || !current.Attachment->IsAttached() || current.Jobs == nullptr)
                    return Fail(kNoWorkspace);
                const auto c = PrepareEditorProcessingCommands(*current.Attachment);
                const auto sceneReplaced = [&] {
                    return AgentOperationOutcome{.IsError = true, .Text = "The scene was replaced while waiting for job " + TokenText(token) +
                                                 "; its result will not apply to the new scene.", .ErrorCode = "scene_replaced"};
                };
                const auto job = FindJob(*current.Jobs, token);
                if (!job && seen->has_value())
                {
                    // Reaped since the last poll: it ended, but in a replaced scene that end is not this scene's.
                    if (sceneEpoch(c) != epoch) return sceneReplaced();
                    auto [last, state] = **seen;
                    // The terminal state was not observed when the last poll saw it still running.
                    if (IsActiveEditorJobState(state) || state == JobState::Invalid) last["state"] = "ended";
                    return Ok({{"job", std::move(last)}, {"finished", true}, {"reaped", true}, {"timed_out", false}});
                }
                if (!job)
                    return AgentOperationOutcome{.IsError = true, .Text = "Job " + TokenText(token) + " is not retained by the job service: "
                                                 "unknown, or it ended and was reaped. Check jobs_list and the scene.", .ErrorCode = "unknown_job"};
                const auto editor = GetEditorJobs(c);
                const Json row = JobRow(*job, FindRecord(editor, token));
                *seen = std::pair{row, job->State};
                // Complete: terminal and its unpublished finalizer (the terminal result) delivered.
                if (current.Jobs->IsComplete(token)) return Ok({{"job", row}, {"finished", true}, {"reaped", false}, {"timed_out", false}});
                if (sceneEpoch(c) != epoch) return sceneReplaced();
                if (std::chrono::steady_clock::now() >= deadline) return Ok({{"job", row}, {"finished", false}, {"reaped", false}, {"timed_out", true}});
                return std::nullopt;
            };
            if (auto immediate = step(context)) return std::move(*immediate);
            AgentOperationOutcome outcome{};
            outcome.Progress = RunProgressProbe(token);
            outcome.Cancel = [abandoned](const AgentOperationContext&) -> std::string {
                *abandoned = true;
                return "the wait ends; the job is not affected";
            };
            outcome.Continuation = [step](const AgentOperationContext& current, AgentOperationOutcome& out) {
                auto answer = step(current);
                if (!answer) return false;
                out = std::move(*answer);
                return true;
            };
            return outcome;
        }

        AgentOperationOutcome JobsCancel(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto text = args ? String(*args, "token") : std::nullopt;
            const auto token = text ? ParseToken(*text) : std::nullopt;
            if (!token)
                return {.IsError = true, .Text = "Pass {\"token\": \"<from jobs_list>\"}, e.g. \"3:1\".", .ErrorCode = "invalid_params"};
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto status = CancelEditorJob(PrepareEditorProcessingCommands(*context.Attachment), *token);
            switch (status)
            {
            case EditorJobCancelStatus::Requested:
                return Ok({{"token", *text}, {"status", std::string(ToString(status))},
                           {"message", "Cancel requested: the job ends cancelled on a later frame and publishes nothing; "
                                       "jobs_wait reports when it ended."}});
            case EditorJobCancelStatus::NotActive:
                return {.IsError = true, .Text = "Job " + *text + " already ended or is already being cancelled.", .ErrorCode = "job_not_active"};
            case EditorJobCancelStatus::NotEditorJob:
                return {.IsError = true,
                        .Text = "Job " + *text + " is not an editor job (or no longer retained). Only rows of jobs_list with "
                                "\"cancellable\": true can be cancelled; asset imports, scene files, k-means and consolidation runs cannot.",
                        .ErrorCode = "not_editor_job"};
            case EditorJobCancelStatus::Unavailable: break;
            }
            return Fail(kNoWorkspace);
        }

        AgentOperationOutcome Log(const AgentOperationContext&, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const std::uint32_t limit = std::min<std::uint32_t>(args ? UInt(*args, "limit").value_or(100u) : 100u, 1000u);
            const std::string minimum = args ? String(*args, "min_level").value_or("info") : "info";
            const auto rank = [](Core::Log::Level level) {
                switch (level)
                {
                case Core::Log::Level::Debug: return 0;
                case Core::Log::Level::Info: return 1;
                case Core::Log::Level::Warning: return 2;
                case Core::Log::Level::Error: return 3;
                }
                return 1;
            };
            const int threshold = minimum == "debug" ? 0 : minimum == "warning" ? 2 : minimum == "error" ? 3 : 1;
            const auto snapshot = Core::Log::TakeSnapshot();
            Json entries = Json::array();
            for (const auto& entry : snapshot.Entries)
                if (rank(entry.Lvl) >= threshold)
                    entries.push_back({{"level", rank(entry.Lvl) == 3 ? "error" : rank(entry.Lvl) == 2 ? "warning"
                                                  : rank(entry.Lvl) == 0 ? "debug" : "info"},
                                       {"message", entry.Message}});
            if (entries.size() > limit) entries.erase(entries.begin(), entries.end() - std::ptrdiff_t(limit));
            return Ok({{"sequence", snapshot.Sequence}, {"entries", entries}});
        }
    }

    void RegisterEditorAgentOperations(AgentOperationRegistry& registry)
    {
        const auto add = [&](const char* name, const char* title, std::string description, std::string schema,
                             bool readOnly, AgentOperationInvoker invoke, bool destructive = false, bool gpu = false) {
            AddOperation(registry, name, title, std::move(description), std::move(schema), readOnly, std::move(invoke), destructive, gpu);
        };
        const std::string& none = kNone;
        add("scene_entities", "Scene entities",
            "List scene entities with stable ids, names, selection and geometry element counts.", none, true, SceneEntities);
        add("entity_properties", "Entity properties",
            "List the properties of one entity (domain, name, value kind, element count).",
            Schema("{" + kEntityProperty + "}", R"(["entity"])"), true, EntityProperties);
        add("select_entity", "Select entity", "Select an entity in the editor, as a click in the hierarchy would.",
            Schema("{" + kEntityProperty + "}", R"(["entity"])"), false, SelectEntity);
        add("import_file", "Import file",
            "Import a geometry/model file from inside an allowed root into the scene (same path as File > Import).",
            Schema(R"({"path":{"type":"string","description":"Absolute path, or relative to the first allowed root."},"wait":{"type":"boolean","default":false,"description":"Answer once the import materialized or failed (reports the new entities) instead of when it is queued."}})", R"(["path"])"),
            false, ImportFile);
        add("save_scene", "Save scene",
            "Save the scene document to a file inside an allowed root, like File > Save As. Refuses an existing file unless "
            "overwrite is true; the write is not part of the undo history. Answers when the save finished.",
            Schema(R"({"path":{"type":"string","description":"Absolute path, or relative to the first allowed root."},"overwrite":{"type":"boolean","default":false}})", R"(["path"])"),
            false, SaveScene, true);
        add("load_scene", "Load scene",
            "Replace the scene document with a scene file from inside an allowed root, like File > Open: every entity of the "
            "current scene is dropped (save it first) and the undo history no longer covers it. Answers when loaded.",
            Schema(R"({"path":{"type":"string","description":"Absolute path, or relative to the first allowed root."}})", R"(["path"])"),
            false, LoadScene, true);
        add("show_property", "Show property",
            "Color an entity by a property in the viewport, like a panel's Show button: scalars through the colormap, vectors "
            "as component colors (or normal directions). This is bind_attribute color on the property's domain (the one Color "
            "mechanism, the lane's visualization overlay): the last call wins and attribute_bindings shows it as the Color "
            "source; bind_attribute color with default: true clears it. normal_direction draws a vec3 as normal directions "
            "on the same overlay. Refusals carry the snake-case command status as error code; the reply carries the row.",
            Schema("{" + kEntityProperty + R"(,"name":{"type":"string"},)" + DomainProperty("Optional: restrict to one element domain.") +
                       R"(,"normal_direction":{"type":"boolean","default":false}})",
                   R"(["entity","name"])"),
            false, ShowProperty);
        add("attribute_bindings",
            "Render attribute sources",
            "List, per render lane of an entity (surface, edges, points: the Appearance panel's Faces, Edges and Vertices "
            "sections), every render attribute row of the panel's source selectors: the element domains the row reads "
            "(domains), the current source (bound, source with its domain, status; using_fallback with a diagnostic when an "
            "authored source no longer resolves and the default is drawn), what Default draws (default, canonical_property), "
            "the compatible candidate properties and the incompatible ones with their domain, typed status and reason. "
            "Optional lane, attribute and domain narrow the listing. Rows: " +
                AttributeTableText(),
            Schema("{" + kEntityProperty + "," + LaneProperty("Optional: only this lane.") + "," +
                    DomainProperty("Optional: only rows reading this element domain.") + "," + AttributeProperty() +
                    "}",
                R"(["entity"])"),
            true,
            AttributeBindings);
        add(
            "bind_attribute",
            "Bind render attribute source",
            "Choose the property that feeds one render attribute of one render lane of an entity, like the Appearance "
            "panel's source selector (ApplyEditorAttributeBindingCommand): pass property (a compatible candidate from "
            "attribute_bindings; add domain when the lane reads that name on several element domains) or default: true to "
            "restore the default source. One undoable step; the reply carries the resulting row. A binding is a "
            "rendering-time choice: canonical data such as v:position is never written. Color writes that lane's "
            "visualization overlay, the one Color mechanism shared with show_property; position, normal and texcoord are "
            "still one source per entity shared by all lanes. Refusals end with the snake-case command status as error "
            "code: unsupported_render_attribute (the lane does not read that domain, or the entity lacks it), "
            "attribute_source_missing, attribute_source_type_mismatch, attribute_source_count_mismatch, "
            "attribute_source_non_finite, stale_entity; bad arguments invalid_params. Rows: " +
                AttributeTableText(),
            []
            {
                Json schema = Json::parse(Schema("{" + kEntityProperty + "," + LaneProperty("Render lane of the row.") +
                        "," + AttributeProperty() + "," +
                        DomainProperty(
                            "Element domain of the source property; needed only when the lane reads that name on several.") +
                        R"(,"property":{"type":"string","minLength":1,"description":"Source property."},)"
                        R"("default":{"type":"boolean","const":true,"description":"Restore the default source."}})",
                    R"(["entity","lane","attribute"])"));
                schema["oneOf"] = Json::array({Json{{"required", {"property"}}}, Json{{"required", {"default"}}}});
                return Dump(schema);
            }(),
            false,
            BindAttribute);
        add("vector_fields",
            "Vector fields",
            "List an entity's Appearance vector fields like the panel's Vector fields section: per element domain the vec3 "
            "properties a field can draw and the style properties a field can take (scalar ones for length and color, "
            "vec3/vec4 for color), and every layer with its fields and status (a missing or GPU-preview style property).",
            Schema("{" + kEntityProperty + "}", R"(["entity"])"),
            true,
            VectorFields);
        add(
            "vector_field",
            "Edit vector field",
            "Add, update or remove one Appearance vector field (identified by domain and vec3 property) through the panel's "
            "command; one undoable step each. add uses the panel's defaults (bounding-box length, palette color); update "
            "changes only the fields passed. Per-arrow inputs: length_property (a scalar; arrow length = length x value, a "
            "value that is not > 0 draws no arrow) and color_property (a scalar through colormap and color_range, or "
            "vec3/vec4 as RGB(A); an invalid value draws the uniform color); \"\" restores Default. Refusals end with the "
            "snake-case command status as error code (invalid_visualization_property, stale_entity); bad arguments "
            "invalid_params. The reply lists the entity's layers.",
            []
            {
                std::string colormaps;
                for (const char* name : kVectorFieldColormaps)
                    colormaps += std::string(colormaps.empty() ? "" : ",") + "\"" + name + "\"";
                return Schema("{" + kEntityProperty +
                        R"(,"action":{"type":"string","enum":["add","update","remove"]},)" +
                        DomainProperty("Element domain of the vec3 property.") +
                        R"(,"property":{"type":"string","minLength":1,"description":"The vec3 property the arrows draw."},)"
                        R"("visible":{"type":"boolean"},"depth_tested":{"type":"boolean"},)"
                        R"("length_mode":{"type":"string","enum":["normalized","raw"]},)"
                        R"("length":{"type":"number","exclusiveMinimum":0,"description":"Arrow length (normalized) or scale (raw); multiplies a length property."},)"
                        R"("line_width":{"type":"number","minimum":0.5,"maximum":32},)"
                        R"("color":{"type":"array","items":{"type":"number","minimum":0,"maximum":1},"minItems":4,"maxItems":4,"description":"Uniform RGBA, drawn while color_property is Default."},)"
                        R"("stride":{"type":"integer","minimum":1,"maximum":4294967295},"max_glyphs":{"type":"integer","minimum":0,"maximum":4294967295},)"
                        R"("length_property":{"type":"string","description":"Scalar property multiplying each arrow's length; empty is Default."},)"
                        R"("color_property":{"type":"string","description":"Scalar or vec3/vec4 property coloring each arrow; empty is Default."},)"
                        R"("colormap":{"type":"string","enum":[)" +
                        colormaps +
                        R"(]},)"
                        R"("color_auto_range":{"type":"boolean"},)"
                        R"("color_range":{"type":"array","items":{"type":"number"},"minItems":2,"maxItems":2,"description":"Manual colormap range, min < max."}})",
                    R"(["entity","action","domain","property"])");
            }(),
            false,
            VectorField);
        add("set_visibility",
            "Set visibility",
            "Show or hide a lane of an entity like the appearance panel's Surface / Edges / Points / Bounding box checkboxes; by default the "
            "primary lane (the surface of a mesh, the edges of a graph, the points of a point cloud). One undoable step.",
            Schema("{" + kEntityProperty +
                    R"(,"visible":{"type":"boolean"},"lane":{"type":"string","enum":["surface","edges","points","bounds"],"description":"Which lane; default the entity's primary one."}})",
                R"(["entity","visible"])"),
            false,
            SetVisibility);
        add("set_camera", "Set the main camera",
            "Control the main camera like the Camera panel. Pass exactly one of: controller (orbit, fly, free look or top down; "
            "keeps the current view), pose (position, target and optional up: the camera looks from position at target; the "
            "result reports up_ignored for controllers without roll and position_clamped when the orbit radius was limited), "
            "preset (front, back, left, right, top, bottom or isometric, framing 'entities' or everything with world bounds) "
            "or focus (frame 'entities' or the selection, keeping the direction). Returns the previous and current pose. A view "
            "the controller cannot look along is refused (UnsupportedCameraPose) and nothing changes. Not undoable.",
            Schema(R"({"controller":{"type":"string","enum":["orbit","fly","free_look","top_down"]},)"
                   R"("pose":{"type":"object","properties":{"position":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},)"
                   R"("target":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3},)"
                   R"("up":{"type":"array","items":{"type":"number"},"minItems":3,"maxItems":3,"description":"Default [0,1,0]."}},)"
                   R"("required":["position","target"],"additionalProperties":false},)"
                   R"("preset":{"type":"string","enum":["front","back","left","right","top","bottom","isometric"]},)"
                   R"("focus":{"type":"boolean","description":"true: frame the entities or the selection."},)"
                   R"("entities":{"type":"array","items":{"type":"integer","minimum":0,"maximum":4294967295},"description":"Stable ids for preset or focus."}})"),
            false, SetCamera);
        add("history", "Undo history", "Undo/redo availability, top labels, counts and dirty state.", none, true, History);
        const std::string steps = Schema(R"({"steps":{"type":"integer","minimum":1,"maximum":64,"default":1}})");
        add("undo", "Undo", "Undo editor commands, like Edit > Undo.", steps, false,
            [](const AgentOperationContext& c, std::string_view a) { return UndoRedo(c, a, true); });
        add("redo", "Redo", "Redo editor commands, like Edit > Redo.", steps, false,
            [](const AgentOperationContext& c, std::string_view a) { return UndoRedo(c, a, false); });
        add("config_sections", "Config sections", "List the engine config sections operations read their settings from.",
            none, true, ConfigSections);
        add("config_schema", "Config schema",
            "JSON Schema of the config section payloads (all sections, or one with 'section'): field descriptions, "
            "ranges, integer or string-token enums with optional x-enum-names, and property-reference kinds and domains.",
            Schema("{" + kSectionProperty + "}"), true, ConfigSchema);
        add("config_get", "Get config section", "Active payload, defaults and payload schema of one config section.",
            Schema("{" + kSectionProperty + "}", R"(["section"])"), true, ConfigGet);
        add("config_preview", "Validate config section",
            "Validate a section payload without applying it; returns the canonical payload or diagnostics.",
            Schema("{" + kSectionProperty + "," + kPayloadProperty + "}", R"(["section","payload"])"), true, ConfigPreview);
        add("config_apply", "Apply config section",
            "Validate and apply a section payload to the running engine (same path as the panels; recorded as an agent change). "
            "The payload follows the section's schema from config_schema or config_get.",
            Schema("{" + kSectionProperty + "," + kPayloadProperty + "}", R"(["section","payload"])"), false, ConfigApply,
            true); // engine config is not part of the undo history
        add("jobs_list", "Jobs",
            "Background jobs with token, state, progress and elapsed time; editor jobs also name the entity and output they "
            "write, the requested and resolved backend (cpu, gpu_compute, gpu_graphics, auto; null while unknown), the run's "
            "diagnostic and whether jobs_cancel accepts them (cancellable).", none, true, JobsList);
        const std::string tokenProperty = R"("token":{"type":"string","description":"Job token from jobs_list: two decimal numbers joined by ':', e.g. 3:1."})";
        add("jobs_wait", "Wait for a job",
            "Wait until a job ended (finished: true) or timeout_ms passed (timed_out: true), while frames keep running. Name the "
            "job by token, or by entity and output (the newest editor run writing that output when called). A job that ended "
            "and was reaped between two polls answers finished with reaped: true (its last seen terminal state, or \"ended\" when it was last seen still running; scene_replaced if the scene changed meanwhile). Ends with an error "
            "when the job is unknown or already reaped when called (unknown_job), the scene is replaced (scene_replaced), the workspace detaches or "
            "the window is minimized (viewport_not_presentable).",
            R"({"type":"object","properties":{)" + tokenProperty + "," + kEntityProperty +
                R"(,"output":{"type":"string","description":"Output property name the editor job writes."},"timeout_ms":{"type":"integer","minimum":0,"maximum":60000,"default":30000}},"oneOf":[{"required":["token"]},{"required":["entity","output"]}],"additionalProperties":false})",
            true, JobsWait, false, true);
        add("jobs_cancel", "Cancel a job",
            "Cancel an editor job (a jobs_list row with cancellable: true). It ends cancelled on a later frame and publishes "
            "nothing, so the scene and the undo history stay as they are; asset imports, scene files, k-means and consolidation "
            "runs cannot be cancelled.",
            Schema("{" + tokenProperty + "}", R"(["token"])"), false, JobsCancel);
        add("log", "Engine log",
            "Recent engine log entries (warnings, errors, Vulkan validation messages).",
            Schema(R"({"limit":{"type":"integer","minimum":1,"maximum":1000,"default":100},"min_level":{"type":"string","enum":["debug","info","warning","error"],"default":"info"}})"),
            true, Log);
        RegisterProcessingAgentOperations(registry);
        RegisterPropertyInspectionAgentOperations(registry);
        RegisterDiagnosticsAgentOperations(registry);
    }
}
