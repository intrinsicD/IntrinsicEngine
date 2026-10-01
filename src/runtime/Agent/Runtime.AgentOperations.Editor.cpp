// Editor-backed agent operations: every entry calls the same runtime query, command or
// config path the Sandbox panels use. JSON stays private to this unit; accessors never
// throw (the build has no exceptions), so every field is type-checked before use.
module;

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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
import Extrinsic.Runtime.AssetIngestStateMachine;
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
                const auto row = std::ranges::find_if(frame->Frame.AssetImportQueue.Rows,
                                                      [&](const EditorAssetImportQueueRow& r) { return r.Operation == handle; });
                if (row == frame->Frame.AssetImportQueue.Rows.end()) return false; // not listed yet
                using Stage = RuntimeAssetImportQueueStage;
                if (row->Stage != Stage::Complete && row->Stage != Stage::Failed && row->Stage != Stage::Cancelled) return false;
                Json finished = out;
                finished["status"] = row->Stage == Stage::Complete ? "Applied" : row->Stage == Stage::Failed ? "Failed" : "Cancelled";
                finished["message"] = row->DiagnosticText.empty() ? row->StageText : row->DiagnosticText;
                Json created = Json::array();
                for (const auto id : StableIds(*frame))
                    if (std::ranges::find(before, id) == before.end()) created.push_back(id);
                finished["entities_created"] = created.size();
                finished["new_entities"] = created;
                done = {.IsError = row->Stage != Stage::Complete, .Text = Dump(finished)};
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
            return {.Continuation = [token = result.Task, describe](const AgentOperationContext& current, AgentOperationOutcome& out) {
                if (!PrepareSnapshot(current)) { out = Fail(kNoWorkspace); return true; }
                const auto last = PrepareEditorSceneEditingFrame(*current.Attachment).LastSceneFileResult;
                if (!last || last->Task != token) return false;
                out = {.IsError = !last->Succeeded(), .Text = Dump(describe(*last))};
                return true;
            }};
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

        // ---- jobs and log ----------------------------------------------------------------
        AgentOperationOutcome Jobs(const AgentOperationContext& context, std::string_view)
        {
            if (context.Jobs == nullptr) return Fail("The job service is unavailable.");
            Json jobs = Json::array();
            for (const auto& job : context.Jobs->SnapshotAll())
                jobs.push_back({{"name", job.DebugName}, {"state", std::string(ToString(job.State))},
                                {"progress", job.Progress.Determinate ? Json(job.Progress.Normalized) : Json(nullptr)},
                                {"elapsed_ms", job.ElapsedMilliseconds}});
            return Ok({{"jobs", jobs}});
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
        add("jobs", "Jobs", "Background jobs with state, progress and elapsed time.", none, true, Jobs);
        add("log", "Engine log",
            "Recent engine log entries (warnings, errors, Vulkan validation messages).",
            Schema(R"({"limit":{"type":"integer","minimum":1,"maximum":1000,"default":100},"min_level":{"type":"string","enum":["debug","info","warning","error"],"default":"info"}})"),
            true, Log);
        RegisterProcessingAgentOperations(registry);
    }
}
