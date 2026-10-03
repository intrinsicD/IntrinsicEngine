// JSON adapters for the same read-only property queries used by the Property Inspector.
module;
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.AgentOperations;
import Extrinsic.ECS.Components.GeometrySources;
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.EditorCommon;
import Extrinsic.Runtime.EditorProcessing;
import Extrinsic.Runtime.GeometryProcessingOperations;
import Extrinsic.Runtime.EditorWorkspaceSnapshots;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.PropertyInspectionOperations;
import Extrinsic.Runtime.CommandBus;
import Extrinsic.Runtime.KernelEvents;
import Geometry.Properties.Types;
#include "Agent/internal/Runtime.AgentOperations.Detail.hpp"

namespace Extrinsic::Runtime
{
    namespace
    {
        using namespace AgentDetail;
        Json InspectionDiagnostics(const std::vector<EditorDiagnostic>& diagnostics)
        {
            Json rows = Json::array();
            for (const auto& d : diagnostics)
                rows.push_back({{"code", DebugNameForEditorDiagnosticCode(d.Code)}, {"message", d.Message}});
            return rows;
        }
        Json PropertyRefJson(const GeometryPropertyRef& ref)
        { return {{"domain", ToString(ref.Domain)}, {"name", ref.Name}, {"kind", KindName(ref.ValueKind)}}; }
        Json InspectionNumber(double value)
        {
            if (std::isnan(value)) return "NaN";
            if (std::isinf(value)) return value > 0 ? "+Infinity" : "-Infinity";
            return value;
        }
        Json NumericStatisticsJson(const EditorPropertyNumericStatistics& s)
        {
            const auto number = [&](double v) { return s.FiniteCount ? InspectionNumber(v) : Json(nullptr); };
            Json edges = Json::array();
            for (const auto v : s.Histogram.Edges) edges.push_back(InspectionNumber(v));
            return {{"count", s.Count}, {"finite_count", s.FiniteCount}, {"nan_count", s.NaNCount},
                    {"inf_count", s.InfCount}, {"zero_count", s.ZeroCount}, {"min", number(s.Min)},
                    {"max", number(s.Max)}, {"mean", number(s.Mean)}, {"rms", number(s.RMS)},
                    {"stddev", number(s.StdDev)}, {"histogram", {{"edges", edges}, {"counts", s.Histogram.Counts}}}};
        }
        std::optional<GeometryPropertyRef> InspectionRef(const Json& args)
        {
            const auto domain = ParseDomain(String(args, "domain"));
            const auto name = String(args, "name");
            if (!domain || !name || name->empty()) return std::nullopt;
            return GeometryPropertyRef{.Domain = *domain, .Name = *name};
        }
        std::optional<std::size_t> InspectionIndex(const Json& args, const char* key, std::size_t fallback)
        {
            if (!args.contains(key)) return fallback;
            const auto& value = args[key];
            if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0)) return std::nullopt;
            const auto number = value.get<std::uint64_t>();
            if (number > std::numeric_limits<std::size_t>::max()) return std::nullopt;
            return static_cast<std::size_t>(number);
        }
        AgentOperationOutcome PropertyList(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            if (!entity || *entity == 0) return Fail("Pass {entity: <stable id>}.");
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto result = GetEditorPropertyCatalog(PrepareEditorProcessingCommands(*context.Attachment), *entity);
            Json rows = Json::array();
            for (const auto& entry : result.Catalog.Entries)
            {
                Json row = PropertyRefJson(entry.Ref);
                row["count"] = entry.ElementCount;
                row["generation"] = entry.PropertyGeneration;
                rows.push_back(std::move(row));
            }
            return {.IsError = !result.Success, .Text = Dump({{"entity", *entity}, {"properties", rows},
                {"source_generation", result.Catalog.SourceGeneration}, {"diagnostics", InspectionDiagnostics(result.Diagnostics)}})};
        }
        AgentOperationOutcome PropertyStats(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            const auto ref = args ? InspectionRef(*args) : std::nullopt;
            const auto bins = args ? InspectionIndex(*args, "bins", 32) : std::nullopt;
            if (!entity || *entity == 0 || !ref || !bins || *bins > MaxEditorPropertyHistogramBins)
                return Fail("Pass {entity, domain, name, bins: 0..256}; bins=0 disables histograms.");
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto result = GetEditorPropertyStatistics(PrepareEditorProcessingCommands(*context.Attachment), *entity, *ref, *bins);
            const auto& s = result.Statistics;
            Json components = Json::array();
            for (const auto& c : s.Components) components.push_back(NumericStatisticsJson(c));
            return {.IsError = !result.Success, .Text = Dump({{"entity", *entity}, {"property", PropertyRefJson(result.Property)},
                {"status", result.Success || s.Status != EditorPropertyStatisticsStatus::Ok ? Json(static_cast<unsigned>(s.Status)) : Json(nullptr)}, {"row_count", s.RowCount}, {"count", s.Count},
                {"deleted_count", s.DeletedCount}, {"finite_count", s.FiniteCount}, {"nan_count", s.NaNCount},
                {"inf_count", s.InfCount}, {"zero_count", s.ZeroCount}, {"components", components},
                {"magnitude", s.Magnitude ? NumericStatisticsJson(*s.Magnitude) : Json(nullptr)},
                {"diagnostics", InspectionDiagnostics(result.Diagnostics)}})};
        }
        AgentOperationOutcome PropertyCompare(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            const auto a = args && args->contains("a") && (*args)["a"].is_object() ? InspectionRef((*args)["a"]) : std::nullopt;
            const auto b = args && args->contains("b") && (*args)["b"].is_object() ? InspectionRef((*args)["b"]) : std::nullopt;
            if (!entity || *entity == 0 || !a || !b) return Fail("Pass {entity, a: {domain, name}, b: {domain, name}}.");
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto result = CompareEditorProperties(PrepareEditorProcessingCommands(*context.Attachment), *entity, *a, *b);
            const auto& c = result.Comparison;
            return {.IsError = !result.Success, .Text = Dump({{"entity", *entity}, {"a", PropertyRefJson(result.A)},
                {"b", PropertyRefJson(result.B)}, {"status", result.Success || c.Status != EditorPropertyStatisticsStatus::Ok ? Json(static_cast<unsigned>(c.Status)) : Json(nullptr)}, {"row_count", c.RowCount},
                {"deleted_count", c.DeletedCount}, {"comparable_rows", c.ComparableRows}, {"nonfinite_rows", c.NonFiniteRows},
                {"identical_rows", c.IdenticalRows}, {"max_abs_error", InspectionNumber(c.MaxAbsError)},
                {"mean_abs_error", InspectionNumber(c.MeanAbsError)}, {"rms_error", InspectionNumber(c.RMSError)},
                {"max_error_row", c.MaxErrorRow ? Json(*c.MaxErrorRow) : Json(nullptr)},
                {"diagnostics", InspectionDiagnostics(result.Diagnostics)}})};
        }
        AgentOperationOutcome PropertyValues(const AgentOperationContext& context, std::string_view arguments)
        {
            const auto args = ParseObject(arguments);
            const auto entity = args ? UInt(*args, "entity") : std::nullopt;
            const auto ref = args ? InspectionRef(*args) : std::nullopt;
            const auto offset = args ? InspectionIndex(*args, "offset", 0) : std::nullopt;
            const auto limit = args ? InspectionIndex(*args, "limit", 256) : std::nullopt;
            if (!entity || *entity == 0 || !ref || !offset || !limit || *limit == 0 || *limit > MaxEditorPropertyValues)
                return Fail("Pass {entity, domain, name, offset: >=0, limit: 1..65536}.");
            if (!PrepareSnapshot(context)) return Fail(kNoWorkspace);
            const auto result = ReadEditorPropertyValues(PrepareEditorProcessingCommands(*context.Attachment), *entity, *ref, *offset, *limit);
            Json rows = Json::array();
            for (const auto& row : result.Rows)
            {
                Json values = Json::array();
                for (const auto& value : row.Components) std::visit([&](const auto& v) { values.push_back(v); }, value);
                rows.push_back({{"index", row.Index}, {"deleted", row.Deleted}, {"values", std::move(values)}});
            }
            return {.IsError = !result.Success, .Text = Dump({{"entity", *entity}, {"property", PropertyRefJson(result.Property)},
                {"total_count", result.TotalCount}, {"offset", result.Offset}, {"has_more", result.HasMore},
                {"rows", rows}, {"diagnostics", InspectionDiagnostics(result.Diagnostics)}})};
        }
    }
    extern "C++" void RegisterPropertyInspectionAgentOperations(AgentOperationRegistry& registry)
    {
        using namespace AgentDetail;
        const auto refFields = DomainProperty("Element domain from property_list.") + R"(,"name":{"type":"string","minLength":1})";
        const auto refSchema = Schema("{" + refFields + "}", R"(["domain","name"])" );
        AddOperation(registry, "property_list", "Property catalog", "List all property domains, kinds, row counts and generations for an entity.",
            Schema("{" + kEntityProperty + "}", R"(["entity"])"), true, PropertyList);
        AddOperation(registry, "property_stats", "Property statistics", "Read finite/deleted/nonfinite counts, scalar or component/magnitude statistics and histograms. Deleted rows are excluded; no scene changes.",
            Schema("{" + kEntityProperty + "," + refFields + R"(,"bins":{"type":"integer","minimum":0,"maximum":256,"default":32}})", R"(["entity","domain","name"])"), true, PropertyStats);
        AddOperation(registry, "property_compare", "Compare property values", "Compare two properties on the same entity and element domain. Scalars may mix numeric kinds; vectors require matching dimensions. Deleted/nonfinite rows are excluded.",
            Schema("{" + kEntityProperty + ",\"a\":" + refSchema + ",\"b\":" + refSchema + "}", R"(["entity","a","b"])"), true, PropertyCompare);
        AddOperation(registry, "property_values", "Property values", "Read a bounded page by storage index, including deleted flags. UInt64 values are exact decimal strings; NaN/+Infinity/-Infinity are explicit strings. Vector rows contain one cell per component.",
            Schema("{" + kEntityProperty + "," + refFields + R"(,"offset":{"type":"integer","minimum":0,"default":0},"limit":{"type":"integer","minimum":1,"maximum":65536,"default":256}})", R"(["entity","domain","name"])"), true, PropertyValues);
    }
}
