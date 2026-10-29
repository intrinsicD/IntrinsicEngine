// RUNTIME-330: contract tests generated from the declared style tables (moonshot M2). Every row
// gets the cases its spec implies and each case is checked on the table's reader (all or nothing),
// the stored value, the scene encoding and, where an agent offers the table, the agent schema. The
// commands' per-field checks are the same declared check over the flat writer's output, so a stored
// value equal to its accepted request passes them; their cross-field rules keep their own tests. A new row is tested without new test code. Generated cases cannot
// show that a declaration is right; the hand-written fixtures in the owners' tests stay for that.
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <gtest/gtest.h>
#include <gtest/gtest-spi.h>
#include <nlohmann/json.hpp>
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.ClusteringConfig;
import Extrinsic.Runtime.ParameterizationConfig;
import Extrinsic.Runtime.KernelDensityConfig;
import Extrinsic.Runtime.PointSpacingConfig;
import Extrinsic.Runtime.MeshTopologyOperations;
import Extrinsic.Runtime.ScalarRidgeOperations;
import Extrinsic.Runtime.ConfigFieldSpec;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.LaneVisualizationStyle;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Geometry.Properties.Types;
#include "GeometryIntegration/internal/Runtime.GeometryVectorFieldStyleJson.hpp"
#include "Visualization/internal/Runtime.LaneVisualizationStyleJson.hpp"
#include "Config/internal/Runtime.PointConfigJson.hpp"
#include "Config/internal/Runtime.ConfigFieldJson.hpp"
namespace R = Extrinsic::Runtime;
using Json = nlohmann::json;
using FT = R::ConfigFieldType;

namespace
{
    struct FieldCase
    {
        Json Value;
        bool Accepted{false};
        // Checked on the reader only: NaN and infinity (no JSON text) and doubles a float cannot hold,
        // which the agent schema's "number" still offers.
        bool ReaderOnly{false};
    };

    constexpr float kInf = std::numeric_limits<float>::infinity();

    // The test's own reading of a Float range, independent of the checker under test.
    bool Inside(const R::ConfigFieldSpec& field, const double x)
    {
        return (!field.Min || (field.ExclusiveMin ? x > *field.Min : x >= *field.Min)) &&
            (!field.Max || (field.ExclusiveMax ? x < *field.Max : x <= *field.Max));
    }

    bool IsToken(const R::ConfigFieldSpec& field, const std::string& value)
    {
        return std::ranges::find(field.EnumValues, value) != field.EnumValues.end();
    }

    // The values `field` implies and the verdict its spec alone gives them. Wire cases come back
    // parsed from text, as an agent or a scene sends them (`1` is then an unsigned integer). A spec
    // variant the tables do not use yet fails the test instead of going untested.
    std::vector<FieldCase> CasesFor(const R::ConfigFieldSpec& field)
    {
        std::vector<FieldCase> cases;
        const auto add = [&](const Json& value, const bool accepted)
        {
            cases.push_back({Json::parse(value.dump()), accepted});
        };
        if (field.Nullable || field.NonEmpty)
        {
            ADD_FAILURE() << field.Name << ": Nullable and NonEmpty are not generated";
            return cases;
        }
        switch (field.Type)
        {
        case FT::Bool:
            add(true, true);
            add(false, true);
            add(0, false);
            add("true", false);
            add(nullptr, false);
            break;
        case FT::UInt:
        {
            // The declared range within the 32-bit unsigned domain the checker also enforces.
            constexpr double kMax = std::numeric_limits<std::uint32_t>::max();
            const double lo = std::max(
                field.Min ? (field.ExclusiveMin ? std::floor(*field.Min) + 1.0 : std::ceil(*field.Min)) : 0.0, 0.0);
            const double hi = std::min(
                field.Max ? (field.ExclusiveMax ? std::ceil(*field.Max) - 1.0 : std::floor(*field.Max)) : kMax, kMax);
            if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi)
            {
                ADD_FAILURE() << field.Name << ": an empty or non-finite UInt range is not generated";
                return cases;
            }
            const auto u = [](const double v)
            {
                return Json(static_cast<std::uint64_t>(v));
            };
            add(u(lo), true);
            add(u(hi), true);
            add(lo > 0.0 ? u(lo - 1.0) : Json(-1), false);
            add(u(hi + 1.0), false); // past the bound, or past 32 bits on the wire
            add(-1, false);
            add(lo + 0.5, false);
            add(lo, false); // an integer-valued float ("1.0")
            add("1", false);
            add(true, false);
            break;
        }
        case FT::Float:
        {
            // Float neighbours, so every accepted value is stored exactly.
            for (const auto& bound : {field.Min, field.Max})
                if (bound)
                    for (const float x :
                        {float(*bound), std::nextafter(float(*bound), -kInf), std::nextafter(float(*bound), kInf)})
                        add(double(x), Inside(field, x));
            if (!field.Min)
                add(double(-1e30f), true);
            if (!field.Max)
                add(double(1e30f), true);
            // The tables tested here store Float rows as float: a double past float range, or so close
            // to an exclusive bound that the stored float is the bound, is refused as stored.
            std::vector<double> narrow;
            if (!field.Min)
                narrow.push_back(-1e39);
            if (!field.Max)
                narrow.push_back(1e39);
            if (field.Min && field.ExclusiveMin)
                narrow.push_back(std::nextafter(*field.Min, std::numeric_limits<double>::infinity()));
            if (field.Max && field.ExclusiveMax)
                narrow.push_back(std::nextafter(*field.Max, -std::numeric_limits<double>::infinity()));
            for (const double x : narrow)
                if (const float stored = float(x); !std::isfinite(stored) || !Inside(field, stored))
                    cases.push_back({Json::parse(Json(x).dump()), false, true});
            const double integer = field.Min ? std::floor(*field.Min) + 1.0 : 1.0;
            add(Json(static_cast<std::int64_t>(integer)), Inside(field, integer));
            add("1", false);
            add(true, false);
            add(nullptr, false);
            for (const double x : {double(std::numeric_limits<float>::quiet_NaN()), double(kInf)})
                cases.push_back({Json(x), false, true});
            break;
        }
        case FT::String:
            add("", true);
            add("generated-name", true);
            add(1, false);
            add(nullptr, false);
            add(Json::array(), false);
            break;
        case FT::StringEnum:
            for (const std::string_view token : field.EnumValues)
            {
                std::string flipped{token};
                for (char& c : flipped)
                    c = std::isupper(static_cast<unsigned char>(c)) ? char(std::tolower(static_cast<unsigned char>(c)))
                                                                    : char(std::toupper(static_cast<unsigned char>(c)));
                add(std::string(token), true);
                add(flipped, IsToken(field, flipped));
            }
            add("", IsToken(field, ""));
            add(0, false);
            add(nullptr, false);
            break;
        case FT::Array:
        {
            if (field.Item == nullptr)
            {
                ADD_FAILURE() << field.Name << ": an array without an item spec is not generated";
                return cases;
            }
            const std::vector<FieldCase> items = CasesFor(*field.Item);
            const auto fill = std::ranges::find_if(items,
                [](const FieldCase& c)
                {
                    return c.Accepted;
                });
            const auto refused = std::ranges::find_if(items,
                [](const FieldCase& c)
                {
                    return !c.Accepted && !c.ReaderOnly;
                });
            if (fill == items.end() || refused == items.end())
            {
                ADD_FAILURE() << field.Name << ": needs an accepted and a refused item";
                return cases;
            }
            const std::size_t minItems = field.MinItems.value_or(0u);
            const auto filled = [&](const std::size_t n)
            {
                return Json(std::vector<Json>(n, fill->Value));
            };
            add(Json::array(), minItems == 0u);
            if (minItems > 0u)
            {
                add(filled(minItems), true);
                add(filled(minItems - 1u), false);
            }
            if (field.MaxItems)
            {
                add(filled(*field.MaxItems), true);
                add(filled(*field.MaxItems + 1u), false);
            }
            add(Json::object(), false);
            add(1.5, false);
            // Each item case at the last of at least two positions where the length allows (a check
            // that stops early misses it), one refused item at the first.
            const std::size_t n =
                std::max<std::size_t>(minItems, std::min<std::size_t>(2u, field.MaxItems.value_or(2u)));
            if (n == 0u)
                break;
            for (const FieldCase& item : items)
            {
                Json array = filled(n);
                array[n - 1u] = item.Value;
                if (item.ReaderOnly)
                    cases.push_back({std::move(array), item.Accepted, true});
                else
                    add(array, item.Accepted);
            }
            Json first = filled(n);
            first[0] = refused->Value;
            add(first, false);
            break;
        }
        default: ADD_FAILURE() << field.Name << ": type " << int(field.Type) << " is not generated";
        }
        return cases;
    }

    template <class T> struct Table
    {
        std::span<const R::ConfigFieldSpec> Fields;
        T Defaults;
        std::function<bool(const Json&, T&)> Read; // flat object by spec name; false leaves T unchanged
        std::function<Json(const T&)> Flat; // every field by spec name
        std::function<bool(const T&, T&)> SceneRoundTrip;
        std::string Schema{}; // the agent's schema, empty without an agent surface
        Json Envelope{}; // a valid call the field is added to
    };

    template <class T> void CheckEveryRow(const Table<T>& table)
    {
        if (!table.Schema.empty())
            ASSERT_TRUE(R::IsSupportedAgentSchemaJson(table.Schema));
        const Json defaults = table.Flat(table.Defaults);
        // Per row, its cases and one accepted value that changes it; a refused case also carries the
        // changes of the rows before and after it, so a reader that applies anything before it has
        // checked everything is caught, whatever order it walks the rows in.
        std::vector<std::vector<FieldCase>> rows;
        std::vector<std::pair<std::string, Json>> changes;
        for (const R::ConfigFieldSpec& field : table.Fields)
        {
            const std::string name{field.Name};
            ASSERT_TRUE(defaults.contains(name)) << name;
            rows.push_back(CasesFor(field));
            const auto& cases = rows.back();
            const auto change = std::ranges::find_if(cases,
                [&](const FieldCase& c)
                {
                    return c.Accepted && c.Value != defaults[name];
                });
            ASSERT_NE(change, cases.end()) << name << ": no accepted case changes the field";
            ASSERT_TRUE(std::ranges::any_of(cases,
                [](const FieldCase& c)
                {
                    return !c.Accepted;
                }))
                << name;
            changes.emplace_back(name, change->Value);
        }
        ASSERT_GT(rows.size(), 1u);
        for (std::size_t row = 0u; row < rows.size(); ++row)
        {
            const std::string name{table.Fields[row].Name};
            for (const FieldCase& c : rows[row])
            {
                const std::string label = name + "=" + c.Value.dump();
                if (!table.Schema.empty() && !c.ReaderOnly)
                {
                    Json arguments = table.Envelope;
                    arguments[name] = c.Value;
                    const auto violation = R::ValidateAgentArgumentsJson(table.Schema, arguments.dump());
                    EXPECT_EQ(!violation.has_value(), c.Accepted)
                        << "agent schema " << label << ": " << violation.value_or("");
                }
                Json input{{name, c.Value}};
                if (!c.Accepted)
                    for (const std::size_t other : {row + changes.size() - 1u, row + 1u})
                        input[changes[other % changes.size()].first] = changes[other % changes.size()].second;
                T target = table.Defaults;
                EXPECT_EQ(table.Read(input, target), c.Accepted) << "reader " << input.dump();
                if (!c.Accepted)
                {
                    EXPECT_EQ(table.Flat(target), defaults) << "partly applied " << input.dump();
                    continue;
                }
                EXPECT_EQ(table.Flat(target)[name], c.Value) << "stored " << label;
                T read = table.Defaults;
                EXPECT_TRUE(table.SceneRoundTrip(target, read)) << "scene " << label;
                EXPECT_EQ(table.Flat(read), table.Flat(target)) << "scene " << label;
            }
        }
    }

    std::optional<bool> VerdictOf(const std::vector<FieldCase>& cases, const std::string& text)
    {
        for (const FieldCase& c : cases)
            if (c.Value.dump() == text)
                return c.Accepted;
        return std::nullopt;
    }
}

// The generator itself, on one hand-picked spec per type (values by their JSON text, so `1` and
// `1.0` stay apart).
TEST(DeclaredFieldContracts, GeneratorCases)
{
    const auto flags = CasesFor({.Name = "b", .Type = FT::Bool});
    for (const auto& [text, accepted] : std::vector<std::pair<std::string, bool>>{
             {"true", true}, {"false", true}, {"0", false}, {"\"true\"", false}, {"null", false}})
        EXPECT_EQ(VerdictOf(flags, text), accepted) << text;
    const auto names = CasesFor({.Name = "s", .Type = FT::String});
    for (const auto& [text, accepted] :
        std::vector<std::pair<std::string, bool>>{{"\"\"", true}, {"1", false}, {"null", false}, {"[]", false}})
        EXPECT_EQ(VerdictOf(names, text), accepted) << text;

    const R::ConfigFieldSpec count{.Name = "n", .Type = FT::UInt, .Min = 1.0, .Max = 10.0, .ExclusiveMax = true};
    const auto counts = CasesFor(count);
    for (const auto& [text, accepted] : std::vector<std::pair<std::string, bool>>{{"1", true},
             {"9", true},
             {"0", false},
             {"10", false},
             {"1.0", false},
             {"1.5", false},
             {"-1", false},
             {"\"1\"", false},
             {"true", false}})
        EXPECT_EQ(VerdictOf(counts, text), accepted) << text;
    const auto anyCount = CasesFor({.Name = "m", .Type = FT::UInt});
    EXPECT_EQ(VerdictOf(anyCount, "4294967295"), true);
    EXPECT_EQ(VerdictOf(anyCount, "4294967296"), false);
    // A declared range past the 32-bit domain is cut to it.
    const auto wide = CasesFor({.Name = "w", .Type = FT::UInt, .Min = -2.0, .Max = 1e10});
    EXPECT_EQ(VerdictOf(wide, "0"), true);
    EXPECT_EQ(VerdictOf(wide, "4294967296"), false);
    static const R::ConfigFieldSpec kEmpty{.Name = "z", .Type = FT::UInt, .Max = -2.0};
    EXPECT_NONFATAL_FAILURE((void)CasesFor(kEmpty), "empty or non-finite UInt range");

    const R::ConfigFieldSpec unit{.Name = "t", .Type = FT::Float, .Min = 0.0, .Max = 1.0, .ExclusiveMin = true};
    const auto units = CasesFor(unit);
    const float tiny = std::numeric_limits<float>::denorm_min();
    for (const auto& [value, accepted] : std::vector<std::pair<double, bool>>{{0.0, false},
             {double(tiny), true},
             {-double(tiny), false},
             {1.0, true},
             {double(std::nextafter(1.0f, 2.0f)), false}})
        EXPECT_EQ(VerdictOf(units, Json(value).dump()), accepted) << value;
    EXPECT_EQ(VerdictOf(units, "1"), true);
    EXPECT_TRUE(std::ranges::any_of(units,
        [](const FieldCase& c)
        {
            return c.ReaderOnly && !c.Accepted;
        }));
    const auto open = CasesFor({.Name = "x", .Type = FT::Float});
    EXPECT_EQ(VerdictOf(open, Json(double(1e30f)).dump()), true);
    EXPECT_EQ(VerdictOf(open, Json(double(-1e30f)).dump()), true);
    for (const auto& [cases, x] :
        {std::pair{&open, 1e39}, std::pair{&open, -1e39}, std::pair{&units, std::nextafter(0.0, 1.0)}})
        EXPECT_TRUE(std::ranges::any_of(*cases,
            [x](const FieldCase& c)
            {
                return c.ReaderOnly && !c.Accepted && c.Value.dump() == Json(x).dump();
            }))
            << x << " is refused as the float it would be stored as";

    static constexpr std::array<std::string_view, 2> kTokens{"a", "B"};
    const auto tokens = CasesFor({.Name = "e", .Type = FT::StringEnum, .EnumValues = kTokens});
    EXPECT_EQ(VerdictOf(tokens, "\"a\""), true);
    EXPECT_EQ(VerdictOf(tokens, "\"A\""), false);
    EXPECT_EQ(VerdictOf(tokens, "\"b\""), false);
    EXPECT_EQ(VerdictOf(tokens, "\"\""), false);

    const R::ConfigFieldSpec pair{.Name = "p", .Type = FT::Array, .Item = &unit, .MinItems = 2u, .MaxItems = 2u};
    const auto pairs = CasesFor(pair);
    const std::string t = Json(double(tiny)).dump();
    EXPECT_EQ(VerdictOf(pairs, "[" + t + "," + t + "]"), true);
    EXPECT_EQ(VerdictOf(pairs, "[" + t + "]"), false);
    EXPECT_EQ(VerdictOf(pairs, "[" + t + "," + t + "," + t + "]"), false);
    EXPECT_EQ(VerdictOf(pairs, "[" + t + ",0.0]"), false);
    EXPECT_EQ(VerdictOf(pairs, "[0.0," + t + "]"), false);
    EXPECT_EQ(VerdictOf(pairs, "{}"), false);
    EXPECT_EQ(VerdictOf(pairs, "[]"), false);
    const auto list = CasesFor({.Name = "q", .Type = FT::Array, .Item = &unit, .MaxItems = 8u});
    EXPECT_EQ(VerdictOf(list, "[]"), true);
    EXPECT_EQ(VerdictOf(list, "[" + t + ",0.0]"), false) << "a bad second item, not only a bad first";

    static const R::ConfigFieldSpec kInt{.Name = "i", .Type = FT::Int};
    EXPECT_NONFATAL_FAILURE((void)CasesFor(kInt), "is not generated");
}

TEST(DeclaredFieldContracts, VectorFieldStyle)
{
    using Layer = R::GeometryVectorFieldLayerRecipe;
    using Keys = R::GeometryVectorFieldStyleKeys;
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    const auto* tool = registry.Find("vector_field");
    ASSERT_NE(tool, nullptr);
    CheckEveryRow<Layer>({
        .Fields = R::GeometryVectorFieldStyleFields(),
        .Defaults = {.Vector = {.Domain = R::GeometryElementDomain::MeshVertex,
                         .Name = "v",
                         .ValueKind = Geometry::PropertyValueKind::Vec3}},
        .Read =
            [](const Json& flat, Layer& layer)
        {
            return !R::ReadGeometryVectorFieldStyleJson(flat, layer, Keys::Agent);
        },
        .Flat =
            [](const Layer& layer)
        {
            return R::GeometryVectorFieldStyleToJson(layer, Keys::Agent);
        },
        .SceneRoundTrip =
            [](const Layer& from, Layer& to)
        {
            return !R::ReadGeometryVectorFieldStyleJson(
                R::GeometryVectorFieldStyleToJson(from, Keys::Scene), to, Keys::Scene);
        },
        .Schema = tool->InputSchemaJson,
        .Envelope = {{"entity", 1}, {"action", "update"}, {"domain", "MeshVertex"}, {"property", "v"}},
    });
}

TEST(DeclaredFieldContracts, LaneVisualizationStyle)
{
    using Config = Extrinsic::Graphics::Components::VisualizationConfig;
    // RUNTIME-336: the lane_style agent tool offers the table, so its schema is checked too.
    R::AgentOperationRegistry registry;
    R::RegisterEditorAgentOperations(registry);
    const auto* tool = registry.Find("lane_style");
    ASSERT_NE(tool, nullptr);
    CheckEveryRow<Config>({
        .Fields = R::LaneVisualizationStyleFields(),
        .Defaults = {},
        .Read =
            [](const Json& flat, Config& config)
        {
            return R::ReadLaneVisualizationStyleFields(flat, config);
        },
        .Flat =
            [](const Config& config)
        {
            return R::LaneVisualizationStyleFieldsJson(config);
        },
        .SceneRoundTrip =
            [](const Config& from, Config& to)
        {
            return R::ReadLaneVisualizationStyleJson(R::LaneVisualizationStyleToJson(from), to);
        },
        .Schema = tool->InputSchemaJson,
        .Envelope = {{"entity", 1}, {"lane", "surface"}},
    });
}

// RUNTIME-339: a nested object's schema lists required members only where they are declared. The
// config sections' Objects (clustering's property bindings, parameterization's five strategy and
// view groups) declare none, so their schemas keep having no "required" key; set_camera and
// property_compare are pinned with theirs by AgentOperations.DeclaredSchemasEqualTheHandWrittenBaseline.
TEST(DeclaredFieldContracts, NestedObjectsRequireOnlyDeclaredMembers)
{
    const auto check = [](const std::string& sectionSchema, std::initializer_list<const char*> objects)
    {
        const Json schema = Json::parse(sectionSchema, nullptr, false);
        ASSERT_TRUE(schema.is_object());
        for (const char* name : objects)
        {
            SCOPED_TRACE(name);
            const Json& object = schema["properties"][name];
            ASSERT_TRUE(object.is_object() && object.contains("properties")) << object.dump();
            EXPECT_FALSE(object["properties"].empty());
            EXPECT_FALSE(object.contains("required")) << object.dump();
        }
    };
    check(R::MakeClusteringConfigSectionRegistration().SchemaJson, {"properties"});
    check(R::MakeParameterizationConfigSectionRegistration().SchemaJson, {"lscm", "harmonic", "bff", "view", "atlas"});
}

namespace
{
    // One field's generated schema property, after checking that the config check and the agent
    // validator on the section schema (without its document keys, as ArgumentsSchema strips them)
    // each give every case its expected verdict.
    Json ExpectCheckAndSchemaVerdicts(const R::ConfigFieldSpec& field, const std::vector<std::pair<Json, bool>>& cases)
    {
        const std::array<R::ConfigFieldSpec, 1> fields{field};
        Json document = Json::parse(R::ConfigDetail::BuildSectionSchemaJson("", "", "", fields, Json::object()));
        document.erase("$schema");
        document.erase("$id");
        const std::string schema = document.dump();
        EXPECT_TRUE(R::IsSupportedAgentSchemaJson(schema)) << schema;
        for (const auto& [value, accepted] : cases)
        {
            const Json payload{{std::string(field.Name), value}};
            EXPECT_EQ(R::ConfigDetail::CollectDeclaredFieldErrors(payload, fields).empty(), accepted) << value.dump();
            EXPECT_EQ(!R::ValidateAgentArgumentsJson(schema, payload.dump()).has_value(), accepted) << value.dump();
        }
        return document["properties"][std::string(field.Name)];
    }
}

// RUNTIME-340: an object-or-string field (a payload passed as an object or as its JSON text) is
// accepted and refused as expected by the config check and by the agent validator on its generated
// schema, also when nullable (Codex B1: null then passes both).
TEST(DeclaredFieldContracts, ObjectOrStringCheckAgreesWithItsSchema)
{
    for (const bool nullable : {false, true})
    {
        SCOPED_TRACE(nullable ? "nullable" : "not nullable");
        const Json property =
            ExpectCheckAndSchemaVerdicts({.Name = "payload", .Type = FT::ObjectOrString, .Nullable = nullable},
                {{Json::object(), true},
                    {Json{{"a", 1}}, true},
                    {"{}", true},
                    {"", true},
                    {nullptr, nullable},
                    {1, false},
                    {1.5, false},
                    {true, false},
                    {Json::array(), false}});
        EXPECT_EQ(
            property["type"], nullable ? Json::array({"object", "string", "null"}) : Json::array({"object", "string"}));
    }
}

// RUNTIME-341: a true-only flag (bind_attribute's default) accepts only true, in the config check
// and in the agent validator on its generated schema; a plain Bool still takes false in both.
TEST(DeclaredFieldContracts, OnlyTrueCheckAgreesWithItsSchema)
{
    for (const bool onlyTrue : {false, true})
    {
        SCOPED_TRACE(onlyTrue ? "only true" : "plain");
        const Json property = ExpectCheckAndSchemaVerdicts({.Name = "flag", .Type = FT::Bool, .OnlyTrue = onlyTrue},
            {{true, true}, {false, !onlyTrue}, {1, false}, {"true", false}, {nullptr, false}});
        EXPECT_EQ(property.contains("const"), onlyTrue) << property.dump();
    }
}

// RUNTIME-342: an Object without member fields is an open object (run_operation's params, whose
// members each operation's case declares), in the config check and the agent validator alike.
TEST(DeclaredFieldContracts, ObjectWithoutFieldsIsOpen)
{
    const Json property = ExpectCheckAndSchemaVerdicts({.Name = "params", .Type = FT::Object},
        {{Json::object(), true}, {Json{{"a", 1}}, true}, {Json::array(), false}, {"x", false}, {1, false}});
    EXPECT_EQ(property, (Json{{"type", "object"}}));
}

namespace
{
    // A command with one bound row set by its field name, recording the names checked so a row no
    // expectation names fails the test.
    template <class Cmd> struct BoundRows
    {
        std::span<const R::BoundField<Cmd>> Rows;
        std::vector<std::string> Checked{};

        Cmd With(const std::string_view name, const double wire)
        {
            Cmd command{};
            const auto row = std::ranges::find(Rows,
                name,
                [](const R::BoundField<Cmd>& r)
                {
                    return r.Spec.Name;
                });
            EXPECT_NE(row, Rows.end()) << name;
            if (row != Rows.end())
            {
                R::SetFieldNumber(*row, command, wire);
                EXPECT_EQ(R::FieldNumber(*row, command), wire) << name;
                for (const R::BoundField<Cmd>& other : Rows)
                    if (&other != &*row && other.Get != nullptr)
                        EXPECT_EQ(R::FieldNumber(other, command), R::FieldNumber(other, Cmd{}))
                            << name << " also changed " << other.Spec.Name;
                Checked.emplace_back(name);
            }
            return command;
        }
        ~BoundRows()
        {
            for (const R::BoundField<Cmd>& row : Rows)
                EXPECT_TRUE(row.Get == nullptr || std::ranges::contains(Checked, std::string(row.Spec.Name)))
                    << row.Spec.Name << " has no expectation";
        }
    };
}

// RUNTIME-343: each field of the explicit-parameter commands is declared once with the member it
// sets. Setting a field by name reaches exactly the member of that name (an independent
// expectation per field, so two rows bound to each other's members fail), and the generic range
// check refuses a value just outside each numeric field's declared range.
TEST(DeclaredFieldContracts, BoundFieldsReachTheirNamedMembers)
{
    using R::EditorMeshDenoiseCommand, R::EditorMeshRemeshCommand, R::EditorMeshSubdivideCommand,
        R::EditorMeshSimplifyCommand, R::EditorScalarRidgeCommand;
    {
        BoundRows<EditorMeshDenoiseCommand> d{R::EditorMeshDenoiseFields()};
        EXPECT_EQ(d.With("normal_iterations", 7).NormalIterations, 7u);
        EXPECT_EQ(d.With("vertex_iterations", 8).VertexIterations, 8u);
        EXPECT_EQ(d.With("sigma_spatial", 0.25).SigmaSpatial, 0.25);
        EXPECT_EQ(d.With("sigma_range", 0.5).SigmaRange, 0.5);
        EXPECT_FALSE(d.With("preserve_boundary", 0).PreserveBoundary);
        EXPECT_EQ(d.With("degenerate_normal_length_epsilon", 1e-6).DegenerateNormalLengthEpsilon, 1e-6);
    }
    {
        BoundRows<EditorMeshRemeshCommand> r{R::EditorMeshRemeshFields()};
        EXPECT_EQ(r.With("mode", 1).Mode, R::EditorMeshRemeshMode::Adaptive);
        EXPECT_EQ(r.With("sizing_law", 1).SizingLaw, R::EditorMeshRemeshSizingLaw::ErrorBoundedTaubin);
        EXPECT_EQ(r.With("iterations", 3).Iterations, 3u);
        EXPECT_EQ(r.With("target_edge_length", 0.1).TargetEdgeLength, 0.1);
        EXPECT_EQ(r.With("lambda", 0.25).Lambda, 0.25);
        EXPECT_EQ(r.With("curvature_adaptation", 2.0).CurvatureAdaptation, 2.0);
        EXPECT_EQ(r.With("approximation_error", 0.02).ApproximationError, 0.02);
        EXPECT_FALSE(r.With("preserve_boundary", 0).PreserveBoundary);
        EXPECT_TRUE(r.With("project_to_surface", 1).ProjectToSurface);
        EXPECT_EQ(r.With("reference_projection_k", 8).ReferenceProjectionK, 8u);
        EXPECT_EQ(r.With("max_reference_projection_distance", 0.3).MaxReferenceProjectionDistance, 0.3);
    }
    {
        BoundRows<EditorMeshSubdivideCommand> s{R::EditorMeshSubdivideFields()};
        EXPECT_EQ(s.With("operator", 2).Operator, R::EditorMeshSubdivideOperator::Sqrt3);
        EXPECT_EQ(s.With("iterations", 2).Iterations, 2u);
        EXPECT_TRUE(s.With("preserve_loop_feature_edges", 1).PreserveLoopFeatureEdges);
        EXPECT_EQ(s.With("max_output_faces", 1000).MaxOutputFaces, 1000u);
    }
    {
        BoundRows<EditorMeshSimplifyCommand> s{R::EditorMeshSimplifyFields()};
        EXPECT_EQ(s.With("metric", 0).Metric, R::EditorMeshSimplifyMetric::ClassicalQEM);
        EXPECT_EQ(s.With("target_faces", 100).TargetFaces, 100u);
        EXPECT_EQ(s.With("max_error", 0.1).MaxError, 0.1);
        EXPECT_FALSE(s.With("preserve_boundary", 0).PreserveBoundary);
        EXPECT_EQ(s.With("feature_angle_threshold_degrees", 30.0).FeatureAngleThresholdDegrees, 30.0);
        EXPECT_EQ(s.With("normal_weight", 2.0).NormalWeight, 2.0);
        EXPECT_EQ(s.With("boundary_weight", 3.0).BoundaryWeight, 3.0);
        EXPECT_EQ(s.With("curvature_weight", 4.0).CurvatureWeight, 4.0);
        EXPECT_FALSE(s.With("preserve_sharp_features", 0).PreserveSharpFeatures);
        EXPECT_FALSE(s.With("preserve_uv_seams", 0).PreserveUvSeams);
    }
    {
        BoundRows<EditorScalarRidgeCommand> g{R::EditorScalarRidgeFields()};
        EXPECT_EQ(g.With("method", 1).Method, R::EditorScalarExtremaMethod::Watershed);
        EXPECT_EQ(g.With("radius_ratio", 0.05).RadiusRatio, 0.05);
        EXPECT_EQ(g.With("scale", 2).Scale, 2u);
        EXPECT_EQ(g.With("minimum_sharpness", 0.1).MinimumSharpness, 0.1);
        EXPECT_EQ(g.With("minimum_strength", 0.2).MinimumStrength, 0.2);
        EXPECT_FALSE(g.With("ridges", 0).Ridges);
        EXPECT_FALSE(g.With("valleys", 0).Valleys);
        EXPECT_TRUE(g.With("require_persistence", 1).RequirePersistence);
        EXPECT_EQ(g.With("minimum_persistence", 0.1).MinimumPersistence, 0.1);
        EXPECT_FALSE(g.With("publish_graph", 0).PublishGraph);
        EXPECT_TRUE(g.With("publish_mesh_features", 1).PublishMeshFeatures);
    }
    // The only row without accessors is the ridge's property reference, which its owner reads.
    std::vector<std::string> unbound;
    const auto collect = [&](const auto rows)
    {
        for (const auto& row : rows)
            if (row.Get == nullptr)
                unbound.emplace_back(row.Spec.Name);
    };
    collect(R::EditorMeshDenoiseFields());
    collect(R::EditorMeshRemeshFields());
    collect(R::EditorMeshSubdivideFields());
    collect(R::EditorMeshSimplifyFields());
    collect(R::EditorScalarRidgeFields());
    EXPECT_EQ(unbound, std::vector<std::string>{"property"});
}

// RUNTIME-343: the generic range check refuses, for each command, a value just outside every
// numeric field's declared range and an enum value past its names (members set directly, each a
// value the member can hold; subdivide's max_output_faces has none, its range is its whole
// uint32_t); the defaults pass.
TEST(DeclaredFieldContracts, BoundFieldsRefuseValuesOutsideTheirRanges)
{
    const auto refused = [](const auto rows, auto command, const auto& change)
    {
        EXPECT_TRUE(R::FieldsInDeclaredRange(rows, command));
        change(command);
        return !R::FieldsInDeclaredRange(rows, command);
    };
    const auto denoise = [&](const auto& change)
    {
        return refused(R::EditorMeshDenoiseFields(), R::EditorMeshDenoiseCommand{}, change);
    };
    EXPECT_TRUE(denoise(
        [](auto& c)
        {
            c.NormalIterations = 0u;
        }));
    EXPECT_TRUE(denoise(
        [](auto& c)
        {
            c.VertexIterations = R::kMeshDenoiseMaxIterations + 1u;
        }));
    EXPECT_TRUE(denoise(
        [](auto& c)
        {
            c.SigmaSpatial = -1.0;
        }));
    EXPECT_TRUE(denoise(
        [](auto& c)
        {
            c.SigmaRange = 1.0e7;
        }));
    EXPECT_TRUE(denoise(
        [](auto& c)
        {
            c.DegenerateNormalLengthEpsilon = 0.0;
        }));
    const auto remesh = [&](const auto& change)
    {
        return refused(R::EditorMeshRemeshFields(), R::EditorMeshRemeshCommand{}, change);
    };
    EXPECT_TRUE(remesh(
        [](auto& c)
        {
            c.Mode = static_cast<R::EditorMeshRemeshMode>(2);
        }));
    EXPECT_TRUE(remesh(
        [](auto& c)
        {
            c.SizingLaw = static_cast<R::EditorMeshRemeshSizingLaw>(2);
        }));
    EXPECT_TRUE(remesh(
        [](auto& c)
        {
            c.Iterations = 0u;
        }));
    EXPECT_TRUE(remesh(
        [](auto& c)
        {
            c.TargetEdgeLength = -1.0;
        }));
    EXPECT_TRUE(remesh(
        [](auto& c)
        {
            c.Lambda = 0.0;
        }));
    EXPECT_TRUE(remesh(
        [](auto& c)
        {
            c.CurvatureAdaptation = -1.0;
        }));
    EXPECT_TRUE(remesh(
        [](auto& c)
        {
            c.ApproximationError = -1.0;
        }));
    EXPECT_TRUE(remesh(
        [](auto& c)
        {
            c.ReferenceProjectionK = R::kMeshRemeshMaxProjectionNeighbors + 1u;
        }));
    EXPECT_TRUE(remesh(
        [](auto& c)
        {
            c.MaxReferenceProjectionDistance = -1.0;
        }));
    const auto subdivide = [&](const auto& change)
    {
        return refused(R::EditorMeshSubdivideFields(), R::EditorMeshSubdivideCommand{}, change);
    };
    EXPECT_TRUE(subdivide(
        [](auto& c)
        {
            c.Operator = static_cast<R::EditorMeshSubdivideOperator>(3);
        }));
    EXPECT_TRUE(subdivide(
        [](auto& c)
        {
            c.Iterations = R::kMeshSubdivideMaxIterations + 1u;
        }));
    const auto simplify = [&](const auto& change)
    {
        return refused(R::EditorMeshSimplifyFields(), R::EditorMeshSimplifyCommand{}, change);
    };
    EXPECT_TRUE(simplify(
        [](auto& c)
        {
            c.Metric = static_cast<R::EditorMeshSimplifyMetric>(2);
        }));
    EXPECT_TRUE(simplify(
        [](auto& c)
        {
            c.TargetFaces = 2'000'000'000u;
        }));
    EXPECT_TRUE(simplify(
        [](auto& c)
        {
            c.MaxError = -1.0;
        }));
    EXPECT_TRUE(simplify(
        [](auto& c)
        {
            c.FeatureAngleThresholdDegrees = 181.0;
        }));
    EXPECT_TRUE(simplify(
        [](auto& c)
        {
            c.NormalWeight = 1001.0;
        }));
    EXPECT_TRUE(simplify(
        [](auto& c)
        {
            c.BoundaryWeight = -1.0;
        }));
    EXPECT_TRUE(simplify(
        [](auto& c)
        {
            c.CurvatureWeight = 1001.0;
        }));
    const auto ridge = [&](const auto& change)
    {
        return refused(R::EditorScalarRidgeFields(), R::EditorScalarRidgeCommand{}, change);
    };
    EXPECT_TRUE(ridge(
        [](auto& c)
        {
            c.Method = static_cast<R::EditorScalarExtremaMethod>(2);
        }));
    EXPECT_TRUE(ridge(
        [](auto& c)
        {
            c.RadiusRatio = R::kScalarRidgeMaxRadiusRatio * 2.0;
        }));
    EXPECT_TRUE(ridge(
        [](auto& c)
        {
            c.Scale = 3u;
        }));
    EXPECT_TRUE(ridge(
        [](auto& c)
        {
            c.MinimumSharpness = -1.0;
        }));
    EXPECT_TRUE(ridge(
        [](auto& c)
        {
            c.MinimumStrength = -1.0;
        }));
    EXPECT_TRUE(ridge(
        [](auto& c)
        {
            c.MinimumPersistence = -0.1;
        }));
}

// RUNTIME-343: an Enum's wire number adds its declared Min (no current command uses a Min, so a
// local one shows the offset).
TEST(DeclaredFieldContracts, BoundEnumFieldsOffsetTheirWireCode)
{
    enum class Kind : std::uint8_t
    {
        A,
        B,
        C
    };
    struct Command
    {
        Kind Value{Kind::A};
    };
    static constexpr std::array<std::string_view, 3> kNames{"a", "b", "c"};
    static constexpr auto kRow =
        R::Bind<&Command::Value>({.Name = "kind", .Type = FT::Enum, .Min = 1.0, .EnumNames = kNames});
    Command command{};
    EXPECT_EQ(R::FieldNumber(kRow, command), 1.0);
    R::SetFieldNumber(kRow, command, 3.0);
    EXPECT_EQ(command.Value, Kind::C);
    EXPECT_TRUE(R::FieldsInDeclaredRange(std::span<const R::BoundField<Command>>(&kRow, 1), command));
    command.Value = static_cast<Kind>(3);
    EXPECT_FALSE(R::FieldsInDeclaredRange(std::span<const R::BoundField<Command>>(&kRow, 1), command));
}

namespace
{
    // RUNTIME-346: a config section's observable codec behaviour, so a derived codec is held to the
    // hand-written one it replaces: canonical output of the defaults and of a fully non-default record, the
    // outcome of one payload per refusal path and of valid payloads, and the registration's default payload
    // and schema. The golden file was captured from the hand-written codecs before they were replaced.
    using SectionValidation = Extrinsic::Core::Config::EngineConfigSectionValidationResult;
    struct SectionCodec
    {
        std::string Name, Output, FloatField, Defaults, NonDefault, InvalidEnum;
        std::function<SectionValidation(std::string_view)> Validate;
        Extrinsic::Core::Config::EngineConfigSectionRegistration Registration;
    };
    std::vector<SectionCodec> GoldenSections()
    {
        using D = R::GeometryElementDomain;
        using K = Geometry::PropertyValueKind;
        const R::KernelDensityConfig density{.StableEntityId = 7,
            .Backend = R::KernelDensityBackend::VulkanLBVH,
            .Positions = {.Domain = D::PointCloudPoint, .Name = "p", .ValueKind = K::Vec3},
            .Density = {.Domain = D::PointCloudPoint, .Name = "d", .ValueKind = K::Double},
            .KNeighbors = 9,
            .GpuQueryBatchSize = 128,
            .Bandwidth = 0.25f};
        const R::PointSpacingConfig spacing{.StableEntityId = 7,
            .Backend = R::PointSpacingBackend::CpuLBVH,
            .Positions = {.Domain = D::MeshVertex, .Name = "p", .ValueKind = K::Vec3},
            .Radii = {.Domain = D::MeshVertex, .Name = "r", .ValueKind = K::Float},
            .KNeighbors = 3,
            .GpuQueryBatchSize = 64,
            .ScaleFactor = 1.5f};
        return {
            {"kernel_density",
                "density",
                "bandwidth",
                R::SerializeKernelDensityConfig({}),
                R::SerializeKernelDensityConfig(density),
                R::SerializeKernelDensityConfig({.Backend = static_cast<R::KernelDensityBackend>(7)}),
                [](const std::string_view payload)
                {
                    return R::ValidateKernelDensityConfigSection(payload, {}, "golden");
                },
                R::MakeKernelDensityConfigSectionRegistration()},
            {"point_spacing",
                "radii",
                "scale_factor",
                R::SerializePointSpacingConfig({}),
                R::SerializePointSpacingConfig(spacing),
                R::SerializePointSpacingConfig({.Backend = static_cast<R::PointSpacingBackend>(7)}),
                [](const std::string_view payload)
                {
                    return R::ValidatePointSpacingConfigSection(payload, {}, "golden");
                },
                R::MakePointSpacingConfigSectionRegistration()},
        };
    }
    // One payload per refusal path, then valid ones; $O is the section's output field, $F its float field.
    std::vector<std::string> GoldenPayloads(const SectionCodec& section)
    {
        const std::string p = R"("positions":{"domain":"MeshVertex","name":"p","kind":"vec3"})";
        std::vector<std::string> payloads{R"([])",
            R"({"bogus":1})",
            R"({"k_neighbors":-1})",
            R"({"k_neighbors":1.5})",
            R"({"entity":4294967296})",
            R"({"gpu_query_batch_size":0})",
            R"({"gpu_query_batch_size":16385})",
            R"({"backend":"gpu"})",
            R"({"backend":1})",
            R"({"$F":-1})",
            R"({"$F":"x"})",
            R"({"$F":1e-50})",
            R"({"$F":1e39})",
            R"({"positions":{"domain":"MeshVertex","name":"","kind":"vec3"}})",
            R"({"positions":{"domain":"MeshVertex","name":"p","kind":"float"}})",
            R"({"positions":{"domain":"Nowhere","name":"p","kind":"vec3"}})",
            "{" + p + R"(,"$O":{"domain":"MeshVertex","name":"p","kind":"float"}})",
            "{" + p + R"(,"$O":{"domain":"MeshFace","name":"q","kind":"float"}})",
            "{" + p + R"(,"$O":{"domain":"MeshVertex","name":"q","kind":"vec3"}})",
            "{" + p +
                R"(,"$O":{"domain":"MeshVertex","name":"q","kind":"double"},"k_neighbors":4,"$F":0.5,"backend":"cpu_lbvh","entity":3})",
            R"({})",
            section.NonDefault,
            // Normalization: the canonical output is the converted record (a float rounds; -0 stays a float zero).
            R"({"$F":0.1})",
            R"({"$F":-0.0})",
            R"({"$F":3})",
            R"({"k_neighbors":4294967295})",
            // Precedence: with two refusals the earlier stage reports.
            R"({"gpu_query_batch_size":0,"backend":"gpu"})",
            R"({"backend":"gpu","$F":-1})",
            R"({"$F":1e-50,"positions":{"domain":"MeshVertex","name":"","kind":"vec3"}})",
            R"({"k_neighbors":-1,"bogus":1})",
            R"({"positions":{"domain":"MeshVertex","name":"","kind":"vec3"},"gpu_query_batch_size":0})"};
        for (std::string& payload : payloads)
            for (const auto& [key, value] :
                {std::pair{std::string("$O"), section.Output}, {std::string("$F"), section.FloatField}})
                for (auto at = payload.find(key); at != std::string::npos; at = payload.find(key))
                    payload.replace(at, key.size(), value);
        return payloads;
    }
    Json GoldenOutcome(const SectionCodec& section)
    {
        Json cases = Json::array();
        for (const std::string& payload : GoldenPayloads(section))
        {
            const SectionValidation result = section.Validate(payload);
            Json messages = Json::array();
            for (const auto& diagnostic : result.Diagnostics)
                messages.push_back(diagnostic.Message);
            cases.push_back({{"payload", payload},
                {"usable", result.Usable()},
                {"canonical", result.CanonicalPayloadJson},
                {"fields", result.ParsedFieldCount},
                {"messages", messages}});
        }
        const auto& registration = section.Registration;
        return {{"defaults", section.Defaults},
            {"non_default", section.NonDefault},
            {"invalid_enum", section.InvalidEnum},
            {"cases", cases},
            {"registration",
                {{"name", registration.DefaultSection.Name},
                    {"schema_id", registration.DefaultSection.SchemaId},
                    {"version", registration.DefaultSection.SchemaVersion},
                    {"payload", registration.DefaultSection.PayloadJson},
                    {"schema", registration.SchemaJson}}}};
    }
    std::filesystem::path GoldenPath()
    {
        return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
            "data/config_section_codec_golden.json";
    }
}

TEST(DeclaredFieldContracts, ConfigSectionCodecsMatchTheirGolden)
{
    std::ifstream file(GoldenPath());
    const Json golden = Json::parse(file, nullptr, false);
    ASSERT_TRUE(golden.is_object());
    for (const SectionCodec& section : GoldenSections())
    {
        SCOPED_TRACE(section.Name);
        ASSERT_TRUE(golden.contains(section.Name));
        const Json outcome = GoldenOutcome(section);
        const Json& expected = golden[section.Name];
        EXPECT_EQ(outcome["defaults"], expected["defaults"]);
        EXPECT_EQ(outcome["non_default"], expected["non_default"]);
        EXPECT_EQ(outcome["invalid_enum"], expected["invalid_enum"]);
        EXPECT_EQ(outcome["registration"], expected["registration"]);
        ASSERT_EQ(outcome["cases"].size(), expected["cases"].size());
        for (std::size_t i = 0; i < outcome["cases"].size(); ++i)
            EXPECT_EQ(outcome["cases"][i], expected["cases"][i]) << outcome["cases"][i]["payload"];
    }
}
