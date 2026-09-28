// Editor-backed agent operations: every entry calls the same runtime query, command or
// config path the Sandbox panels use. JSON stays private to this unit; accessors never
// throw (the build has no exceptions), so every field is type-checked before use.
module;

#include <algorithm>
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
import Extrinsic.Runtime.VisualizationEditingOperations;
import Extrinsic.Runtime.GeometryProperty.Types;
import Geometry.Properties.Types;

namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;

        std::string Dump(const Json& value) { return value.dump(-1, ' ', false, Json::error_handler_t::replace); }
        AgentOperationOutcome Ok(const Json& value) { return {.IsError = false, .Text = Dump(value)}; }
        AgentOperationOutcome Fail(std::string message) { return {.IsError = true, .Text = std::move(message)}; }

        std::optional<Json> ParseObject(std::string_view text)
        {
            Json value = Json::parse(text, nullptr, false);
            if (value.is_discarded() || !value.is_object()) return std::nullopt;
            return value;
        }
        std::optional<std::uint32_t> UInt(const Json& object, const char* key)
        {
            const auto it = object.find(key);
            if (it == object.end() || !it->is_number_integer()) return std::nullopt;
            const auto value = it->get<std::int64_t>();
            if (value < 0 || value > std::int64_t(UINT32_MAX)) return std::nullopt;
            return static_cast<std::uint32_t>(value);
        }
        std::optional<std::string> String(const Json& object, const char* key)
        {
            const auto it = object.find(key);
            if (it == object.end() || !it->is_string()) return std::nullopt;
            return it->get<std::string>();
        }
        // A config payload may be passed as a JSON object or as JSON text.
        std::optional<std::string> Payload(const Json& object)
        {
            const auto it = object.find("payload");
            if (it == object.end()) return std::nullopt;
            if (it->is_object()) return Dump(*it);
            if (it->is_string()) return it->get<std::string>();
            return std::nullopt;
        }
        Json ParsedOrString(const std::string& text)
        {
            Json value = Json::parse(text, nullptr, false);
            return value.is_discarded() ? Json(text) : value;
        }
        Json Diagnostics(const std::vector<Core::Config::EngineConfigDiagnostic>& diagnostics)
        {
            Json out = Json::array();
            for (const auto& d : diagnostics)
                out.push_back({{"severity", std::string(Core::Config::ToString(d.Severity))},
                               {"code", std::string(Core::Config::ToString(d.Code))},
                               {"subject", d.Subject}, {"message", d.Message}});
            return out;
        }
        const char* KindName(const Geometry::PropertyValueKind kind)
        {
            using K = Geometry::PropertyValueKind;
            switch (kind)
            {
            case K::Bool: return "bool";
            case K::Int32: return "int32";
            case K::UInt32: return "uint32";
            case K::UInt64: return "uint64";
            case K::Float: return "float";
            case K::Double: return "double";
            case K::Vec2: return "vec2";
            case K::Vec3: return "vec3";
            case K::Vec4: return "vec4";
            case K::Unknown: break;
            }
            return "unknown";
        }

        std::optional<EditorWorkspaceSnapshotPreparedFrame> PrepareSnapshot(const AgentOperationContext& context)
        {
            if (context.Attachment == nullptr || !context.Attachment->IsAttached()) return std::nullopt;
            return PrepareEditorWorkspaceSnapshotFrame(*context.Attachment);
        }
        constexpr const char* kNoWorkspace = "The editor workspace is not attached yet; retry after the next frame.";

        std::string Schema(std::string properties, std::string required = "[]")
        {
            return R"({"type":"object","properties":)" + std::move(properties) + R"(,"required":)" +
                   std::move(required) + R"(,"additionalProperties":false})";
        }
        const std::string kEntityProperty = R"("entity":{"type":"integer","minimum":1,"description":"Stable entity id from scene_entities."})";
        const std::string kSectionProperty = R"("section":{"type":"string","description":"Config section name from config_sections, e.g. sandbox.property_smoothing."})";
        const std::string kPayloadProperty = R"("payload":{"type":["object","string"],"description":"Section payload; omitted fields keep their defaults."})";

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

        AgentOperationOutcome ImportFile(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto path = args ? String(*args, "path") : std::nullopt;
            if (!path) return Fail("Pass {\"path\": \"<file inside an allowed root>\"}.");
            const auto resolved = ResolveAgentPath(context, *path);
            if (!resolved) return Fail("Path '" + *path + "' is outside the Sandbox's allowed agent roots.");
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto scene = PrepareEditorSceneEditingFrame(*context.Attachment);
            const auto result = ApplyEditorFileImportCommand(scene.Commands, EditorFileImportCommand{.Path = *resolved});
            const Json out{{"status", DebugNameForEditorCommandStatus(result.Status)}, {"path", *resolved},
                           {"message", result.Message}, {"entities_created", result.PrimitiveEntitiesCreated}};
            // Imports are asynchronous: Pending means queued; the entity appears in scene_entities.
            return {.IsError = !result.Succeeded() && result.Status != EditorCommandStatus::Pending, .Text = Dump(out)};
        }

        AgentOperationOutcome ShowProperty(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            const auto name = args ? String(*args, "name") : std::nullopt;
            if (!entity || !name) return Fail("Pass {\"entity\": <stable id>, \"name\": <property>, \"domain\": <optional domain>}.");
            const auto domain = args ? String(*args, "domain") : std::nullopt;
            const bool normalDirection = args && args->contains("normal_direction") && (*args)["normal_direction"].is_boolean() &&
                                         (*args)["normal_direction"].get<bool>();
            const auto prepared = PrepareSnapshot(context);
            if (!prepared) return Fail(kNoWorkspace);
            const auto inspector = BuildEditorInspectorModel(prepared->SnapshotQueries, nullptr, *entity);
            if (!inspector.HasEntity) return Fail("No entity with id " + std::to_string(*entity) + ".");
            const auto row = std::ranges::find_if(inspector.PropertyCatalog.Rows, [&](const EditorPropertyCatalogRow& r) {
                return r.Name == *name && !r.Internal && (!domain || ToString(r.Descriptor.Domain) == *domain);
            });
            if (row == inspector.PropertyCatalog.Rows.end())
                return Fail("Entity " + std::to_string(*entity) + " has no property '" + *name + "'" +
                            (domain ? " on " + *domain : std::string{}) + "; see entity_properties.");
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

        // ---- mesh-field operations -------------------------------------------------------
        template <class Result>
        Json ResultJson(const Result& result)
        {
            return {{"status", DebugNameForEditorCommandStatus(result.Status)}, {"succeeded", result.Succeeded()},
                    {"message", result.Message}};
        }
        struct MeshFieldOperation
        {
            const char* Name;
            std::function<std::optional<Json>(const EditorProcessingCommands&, std::uint32_t, bool preview, std::string&)> Run;
        };
        template <class Config, class Result>
        MeshFieldOperation MakeMeshFieldOperation(
            const char* name, std::optional<Config> (*get)(const EditorProcessingCommands&),
            ActionReadiness (*previewFn)(const EditorProcessingCommands&, std::uint32_t, const Config&),
            std::function<Result(const EditorProcessingCommands&, std::uint32_t, const Config&)> apply)
        {
            return {name, [=](const EditorProcessingCommands& commands, std::uint32_t entity, bool preview, std::string& error)
                          -> std::optional<Json> {
                const auto config = get(commands);
                if (!config) { error = std::string("The active config has no usable section for '") + name + "'."; return std::nullopt; }
                if (preview)
                {
                    const auto readiness = previewFn(commands, entity, *config);
                    return Json{{"operation", name}, {"enabled", readiness.Enabled}, {"reason", readiness.DisabledReason}};
                }
                Json out = ResultJson(apply(commands, entity, *config));
                out["operation"] = name;
                return out;
            }};
        }
        const std::vector<MeshFieldOperation>& MeshFieldOperations()
        {
            static const std::vector<MeshFieldOperation> operations{
                MakeMeshFieldOperation<PropertySmoothingConfig, EditorPropertySmoothingResult>(
                    "property_smoothing", &GetEditorPropertySmoothingConfig, &PreviewEditorPropertySmoothingCommand,
                    [](const EditorProcessingCommands& c, std::uint32_t id, const PropertySmoothingConfig& config) {
                        return ApplyEditorPropertySmoothingCommand(c, id, config);
                    }),
                MakeMeshFieldOperation<LaplacianEigenbasisConfig, EditorLaplacianEigenbasisResult>(
                    "spectral_modes", &GetEditorLaplacianEigenbasisConfig, &PreviewEditorLaplacianEigenbasisCommand,
                    [](const EditorProcessingCommands& c, std::uint32_t id, const LaplacianEigenbasisConfig& config) {
                        return ApplyEditorLaplacianEigenbasisCommand(c, id, config);
                    }),
                MakeMeshFieldOperation<HarmonicFieldConfig, EditorHarmonicFieldResult>(
                    "harmonic_field", &GetEditorHarmonicFieldConfig, &PreviewEditorHarmonicFieldCommand,
                    [](const EditorProcessingCommands& c, std::uint32_t id, const HarmonicFieldConfig& config) {
                        return ApplyEditorHarmonicFieldCommand(c, id, config);
                    }),
                MakeMeshFieldOperation<ScalarGradientConfig, EditorScalarGradientResult>(
                    "scalar_gradient", &GetEditorScalarGradientConfig, &PreviewEditorScalarGradientCommand,
                    [](const EditorProcessingCommands& c, std::uint32_t id, const ScalarGradientConfig& config) {
                        return ApplyEditorScalarGradientCommand(c, id, config);
                    }),
            };
            return operations;
        }
        std::string OperationEnum()
        {
            std::string names;
            for (const auto& op : MeshFieldOperations()) names += std::string(names.empty() ? "" : ",") + "\"" + op.Name + "\"";
            return "[" + names + "]";
        }

        AgentOperationOutcome RunMeshField(const AgentOperationContext& context, std::string_view arguments, bool preview)
        {
            const auto args = ParseObject(arguments);
            const auto name = args ? String(*args, "operation") : std::nullopt;
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            if (!name || !entity) return Fail("Pass {\"operation\": <name>, \"entity\": <stable id>}.");
            const auto& operations = MeshFieldOperations();
            const auto op = std::ranges::find_if(operations, [&](const MeshFieldOperation& o) { return *name == o.Name; });
            if (op == operations.end()) return Fail("Unknown operation '" + *name + "'; valid: " + OperationEnum() + ".");
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto commands = PrepareEditorMeshFieldFrame(*context.Attachment).Commands;
            std::string error;
            const auto result = op->Run(commands, *entity, preview, error);
            if (!result) return Fail(error);
            const bool failed = preview ? false : !(*result)["succeeded"].get<bool>();
            return {.IsError = failed, .Text = Dump(*result)};
        }
    }

    namespace
    {
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
            return {{"method", "cpd"}, {"status", DebugNameForEditorCommandStatus(r.Status)},
                    {"succeeded", r.Succeeded()}, {"message", r.Message}, {"backend", r.Backend},
                    {"variant", unsigned(r.Method)}, {"output", unsigned(r.Output)}, {"termination", r.Termination},
                    {"iterations", r.Iterations}, {"sigma2", r.Sigma2}, {"negative_log_likelihood", r.NegativeLogLikelihood},
                    {"matched_weight", r.MatchedWeight}, {"mean_displacement", r.MeanDisplacement},
                    {"transform", TransformJson(r.Transform)}, {"source_points", r.SourcePointCount},
                    {"target_points", r.TargetPointCount}};
        }
        // Both methods run their configured section (config_apply first). A queued job
        // answers once it has published or failed.
        AgentOperationOutcome RunRegistration(const AgentOperationContext& context, std::string_view arguments, bool preview)
        {
            const auto args = ParseObject(arguments);
            const auto method = args ? String(*args, "method") : std::nullopt;
            if (!method || (*method != "icp" && *method != "cpd")) return Fail("Pass {\"method\": \"icp\" | \"cpd\"}.");
            if (context.Attachment == nullptr || !context.Attachment->IsAttached()) return Fail(kNoWorkspace);
            const auto commands = PrepareEditorRegistrationFrame(*context.Attachment).Commands;
            if (*method == "icp")
            {
                const auto config = GetEditorRegistrationConfig(commands);
                if (!config) return Fail("The sandbox.registration section is unavailable.");
                if (preview)
                {
                    const auto ready = PreviewEditorRegistrationCommand(commands, *config);
                    return Ok({{"method", "icp"}, {"enabled", ready.Enabled}, {"reason", ready.DisabledReason}});
                }
                auto done = std::make_shared<std::optional<EditorRegistrationResult>>();
                auto result = ApplyEditorConfiguredRegistrationCommand(commands,
                    [done](EditorRegistrationResult r) { *done = std::move(r); });
                if (result.Status != EditorCommandStatus::Pending)
                    return {.IsError = !result.Succeeded(), .Text = Dump(RegistrationJson(result))};
                return {.Continuation = [done](const AgentOperationContext&, AgentOperationOutcome& out) {
                    if (!done->has_value()) return false;
                    out = {.IsError = !(*done)->Succeeded(), .Text = Dump(RegistrationJson(**done))};
                    return true;
                }};
            }
            const auto config = GetEditorCoherentPointDriftConfig(commands);
            if (!config) return Fail("The sandbox.coherent_point_drift section is unavailable.");
            if (preview)
            {
                const auto ready = PreviewEditorCoherentPointDriftCommand(commands, *config);
                return Ok({{"method", "cpd"}, {"enabled", ready.Enabled}, {"reason", ready.DisabledReason}});
            }
            auto done = std::make_shared<std::optional<EditorCoherentPointDriftResult>>();
            auto result = ApplyEditorConfiguredCoherentPointDrift(commands,
                [done](EditorCoherentPointDriftResult r) { *done = std::move(r); });
            if (result.Status != EditorCommandStatus::Pending)
                return {.IsError = !result.Succeeded(), .Text = Dump(CoherentPointDriftJson(result))};
            return {.Continuation = [done](const AgentOperationContext&, AgentOperationOutcome& out) {
                if (!done->has_value()) return false;
                out = {.IsError = !(*done)->Succeeded(), .Text = Dump(CoherentPointDriftJson(**done))};
                return true;
            }};
        }
    }

    void RegisterEditorAgentOperations(AgentOperationRegistry& registry)
    {
        const auto add = [&](const char* name, const char* title, std::string description, std::string schema,
                             bool readOnly, AgentOperationInvoker invoke) {
            (void)registry.Register({.Name = name, .Title = title, .Description = std::move(description),
                                     .InputSchemaJson = std::move(schema), .ReadOnly = readOnly, .Invoke = std::move(invoke)});
        };
        const std::string none = Schema("{}");
        add("scene_entities", "Scene entities",
            "List scene entities with stable ids, names, selection and geometry element counts.", none, true, SceneEntities);
        add("entity_properties", "Entity properties",
            "List the properties of one entity (domain, name, value kind, element count).",
            Schema("{" + kEntityProperty + "}", R"(["entity"])"), true, EntityProperties);
        add("select_entity", "Select entity", "Select an entity in the editor, as a click in the hierarchy would.",
            Schema("{" + kEntityProperty + "}", R"(["entity"])"), false, SelectEntity);
        add("import_file", "Import file",
            "Import a geometry/model file from inside an allowed root into the scene (same path as File > Import).",
            Schema(R"({"path":{"type":"string","description":"Absolute path, or relative to the first allowed root."}})", R"(["path"])"),
            false, ImportFile);
        add("show_property", "Show property",
            "Color an entity by a property in the viewport, like a panel's Show button: scalars through the colormap, vectors "
            "as component colors (or normal directions).",
            Schema("{" + kEntityProperty + R"(,"name":{"type":"string"},"domain":{"type":"string","description":"Optional, e.g. MeshVertex or MeshFace."},"normal_direction":{"type":"boolean","default":false}})",
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
            Schema("{" + kSectionProperty + "," + kPayloadProperty + "}", R"(["section","payload"])"), false, ConfigApply);
        add("jobs", "Jobs", "Background jobs with state, progress and elapsed time.", none, true, Jobs);
        add("log", "Engine log",
            "Recent engine log entries (warnings, errors, Vulkan validation messages).",
            Schema(R"({"limit":{"type":"integer","minimum":1,"maximum":1000,"default":100},"min_level":{"type":"string","enum":["debug","info","warning","error"],"default":"info"}})"),
            true, Log);
        const std::string registration = Schema(
            R"({"method":{"type":"string","enum":["icp","cpd"],"description":"icp uses sandbox.registration, cpd uses sandbox.coherent_point_drift (config_apply first)."}})",
            R"(["method"])");
        add("preview_registration", "Preview registration",
            "Whether the configured ICP or Coherent Point Drift registration can run, and why not.", registration, true,
            [](const AgentOperationContext& c, std::string_view a) { return RunRegistration(c, a, true); });
        add("run_registration", "Run registration",
            "Register the configured source entity onto the target with ICP or Coherent Point Drift and publish the "
            "result (source transform, positions or a displacement property) as one undoable step; answers when done.",
            registration, false, [](const AgentOperationContext& c, std::string_view a) { return RunRegistration(c, a, false); });
        const std::string meshField = Schema(
            R"({"operation":{"type":"string","enum":)" + OperationEnum() +
                R"(,"description":"Mesh-field operation; its settings come from the matching config section (config_apply first)."},)" +
                kEntityProperty + "}",
            R"(["operation","entity"])");
        add("preview_mesh_operation", "Preview mesh operation",
            "Whether a mesh-field operation can run on an entity with the active settings, and why not.", meshField, true,
            [](const AgentOperationContext& c, std::string_view a) { return RunMeshField(c, a, true); });
        add("run_mesh_operation", "Run mesh operation",
            "Run a mesh-field operation (property smoothing, spectral modes, harmonic field, scalar gradient) on an entity "
            "with the active config section; undoable like the panel button.",
            meshField, false, [](const AgentOperationContext& c, std::string_view a) { return RunMeshField(c, a, false); });
    }
}
