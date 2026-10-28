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
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.ConfigFieldSpec;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryProperty.Types;
import Extrinsic.Runtime.LaneVisualizationStyle;
import Extrinsic.Graphics.Component.VisualizationConfig;
import Geometry.Properties.Types;
#include "GeometryIntegration/internal/Runtime.GeometryVectorFieldStyleJson.hpp"
#include "Visualization/internal/Runtime.LaneVisualizationStyleJson.hpp"
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
