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
            const auto visualization = PrepareEditorVisualizationEditingFrame(*context.Attachment);
            const auto status = ApplyEditorVisualizationRecipeCommand(visualization.Commands,
                {.StableEntityId = *entity, .Recipe = MakeEditorPropertyVisualizationRecipe(row->Descriptor, normalDirection)});
            const bool ok = status == EditorCommandStatus::Applied || status == EditorCommandStatus::NoChange;
            return {.IsError = !ok, .Text = Dump({{"status", DebugNameForEditorCommandStatus(status)}, {"entity", *entity},
                                                   {"domain", std::string(ToString(row->Descriptor.Domain))}, {"name", row->Name}})};
        }

        // Shows or hides one base lane like the appearance panel's checkboxes: Surface (mesh), Edges (graph) and
        // Points (point cloud) are offered per entity wherever its appearance target exists. Without `lane` it is the
        // entity's primary one: surface for meshes, edges for graphs, points for point clouds.
        AgentOperationOutcome SetVisibility(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            if (!entity || !args->contains("visible") || !(*args)["visible"].is_boolean())
                return Fail("Pass {\"entity\": <stable id>, \"visible\": true | false, \"lane\": <optional surface | edges | points>}.");
            const bool visible = (*args)["visible"].get<bool>();
            const auto laneName = String(*args, "lane");
            if (args->contains("lane") && !laneName) return Fail("lane must be \"surface\", \"edges\" or \"points\".");
            std::optional<EditorDomainWindowKind> lane;
            if (laneName == "surface") lane = EditorDomainWindowKind::Mesh;
            else if (laneName == "edges") lane = EditorDomainWindowKind::Graph;
            else if (laneName == "points") lane = EditorDomainWindowKind::PointCloud;
            else if (laneName) return Fail("lane must be \"surface\", \"edges\" or \"points\".");
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
            const std::uint32_t steps = args ? UInt(*args, "steps").value_or(1u) : 1u;
            if (steps < 1 || steps > 64) return Fail("steps must be within 1..64.");
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto scene = PrepareEditorSceneEditingFrame(*context.Attachment);
            if (!scene.DocumentCommands.Available()) return Fail("Undo/redo is unavailable in this workspace.");
            Json labels = Json::array();
            for (std::uint32_t i = 0; i < steps; ++i)
            {
                const auto result = undo ? scene.DocumentCommands.Undo() : scene.DocumentCommands.Redo();
                if (!result.Succeeded()) break;
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
        // One row per job the service retains. `editor` (the output it writes) and `cancellable`
        // only for jobs the editor queued through its job surface: exactly the ones jobs_cancel accepts.
        Json JobRow(const JobSnapshot& job, const EditorJobRecord* editor)
        {
            Json row{{"token", TokenText(job.Token)}, {"name", job.DebugName}, {"state", std::string(ToString(job.State))},
                     {"progress", job.Progress.Determinate ? Json(job.Progress.Normalized) : Json(nullptr)},
                     {"elapsed_ms", job.ElapsedMilliseconds},
                     {"editor", editor != nullptr ? Json{{"entity", editor->Identity.EntityId}, {"output", editor->Identity.OutputName}}
                                                  : Json(nullptr)},
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
            "as component colors (or normal directions).",
            Schema("{" + kEntityProperty + R"(,"name":{"type":"string"},)" + DomainProperty("Optional: restrict to one element domain.") +
                       R"(,"normal_direction":{"type":"boolean","default":false}})",
                   R"(["entity","name"])"),
            false, ShowProperty);
        add("set_visibility", "Set visibility",
            "Show or hide a lane of an entity like the appearance panel's Surface / Edges / Points checkboxes; by default the "
            "primary lane (the surface of a mesh, the edges of a graph, the points of a point cloud). One undoable step.",
            Schema("{" + kEntityProperty + R"(,"visible":{"type":"boolean"},"lane":{"type":"string","enum":["surface","edges","points"],"description":"Which lane; default the entity's primary one."}})", R"(["entity","visible"])"), false, SetVisibility);
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
                   R"("required":["position","target"]},)"
                   R"("preset":{"type":"string","enum":["front","back","left","right","top","bottom","isometric"]},)"
                   R"("focus":{"type":"boolean","description":"true: frame the entities or the selection."},)"
                   R"("entities":{"type":"array","items":{"type":"integer","minimum":0},"description":"Stable ids for preset or focus."}})"),
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
            "ranges, integer-coded enums with x-enum-names, and property-reference kinds and domains.",
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
            "write and whether jobs_cancel accepts them (cancellable).", none, true, JobsList);
        const std::string tokenProperty = R"("token":{"type":"string","pattern":"^[0-9]+:[0-9]+$","description":"Job token from jobs_list, e.g. 3:1."})";
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
    }
}
