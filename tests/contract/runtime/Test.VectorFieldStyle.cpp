// RUNTIME-327: the vector-field layer style is declared once; the agent schema and the command
// validator must agree on every bound.
#include <cmath>
#include <cstdint>
#include <limits>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
import Extrinsic.Runtime.AgentOperations;
import Extrinsic.Runtime.GeometryPresentation;
import Extrinsic.Runtime.GeometryProperty.Types;
import Geometry.Properties.Types;
import Extrinsic.Graphics.Colormap;
import Extrinsic.Runtime.ConfigFieldSpec;
#include "GeometryIntegration/internal/Runtime.GeometryVectorFieldStyleJson.hpp"
namespace R = Extrinsic::Runtime;
using Json = nlohmann::json;

namespace
{
    std::string VectorFieldSchema()
    {
        R::AgentOperationRegistry registry;
        R::RegisterEditorAgentOperations(registry);
        const auto* spec = registry.Find("vector_field");
        return spec ? spec->InputSchemaJson : std::string{};
    }

    R::GeometryVectorFieldLayerRecipe ValidLayer()
    {
        return {.Vector = {.Domain = R::GeometryElementDomain::MeshVertex,
                    .Name = "v",
                    .ValueKind = Geometry::PropertyValueKind::Vec3}};
    }

    struct BoundCase
    {
        const char* Field;
        Json Value;
        bool Accepted;
        // Empty for a value the recipe cannot hold (the agent refuses it alone).
        std::function<void(R::GeometryVectorFieldLayerRecipe&)> Set;
    };
}

TEST(VectorFieldStyle, AgentSchemaBoundsMatchTheCommand)
{
    const std::string schema = VectorFieldSchema();
    ASSERT_FALSE(schema.empty());
    const auto f = [](float R::GeometryVectorFieldLayerRecipe::* member, const float value)
    { return [member, value](R::GeometryVectorFieldLayerRecipe& l) { l.*member = value; }; };
    const auto u = [](std::uint32_t R::GeometryVectorFieldLayerRecipe::* member, const std::uint32_t value)
    { return [member, value](R::GeometryVectorFieldLayerRecipe& l) { l.*member = value; }; };
    const auto red = [](const float value)
    { return [value](R::GeometryVectorFieldLayerRecipe& l) { l.Color.x = value; }; };
    using L = R::GeometryVectorFieldLayerRecipe;
    // Inclusive bounds: accepted at the bound, refused at the next representable float outside.
    const auto below = [](const float x) { return std::nextafter(x, -std::numeric_limits<float>::infinity()); };
    const auto above = [](const float x) { return std::nextafter(x, std::numeric_limits<float>::infinity()); };
    const auto rgba = [](const float x) { return Json::array({x, 0.5, 0.5, 1.0}); };
    const float tiny = std::numeric_limits<float>::denorm_min();
    const std::vector<BoundCase> cases{
        {"length", tiny, true, f(&L::Length, tiny)},
        {"length", 0.0, false, f(&L::Length, 0.0f)},
        {"line_width", 0.5f, true, f(&L::LineWidthPx, 0.5f)},
        {"line_width", 32.0f, true, f(&L::LineWidthPx, 32.0f)},
        {"line_width", below(0.5f), false, f(&L::LineWidthPx, below(0.5f))},
        {"line_width", above(32.0f), false, f(&L::LineWidthPx, above(32.0f))},
        {"color", rgba(0.0f), true, red(0.0f)},
        {"color", rgba(1.0f), true, red(1.0f)},
        {"color", rgba(below(0.0f)), false, red(below(0.0f))},
        {"color", rgba(above(1.0f)), false, red(above(1.0f))},
        {"stride", 1, true, u(&L::Stride, 1u)},
        {"stride", 16777216, true, u(&L::Stride, 16777216u)},
        {"stride", 0, false, u(&L::Stride, 0u)},
        {"stride", 16777217, false, u(&L::Stride, 16777217u)},
        {"max_glyphs", 0, true, u(&L::MaxGlyphs, 0u)},
        {"max_glyphs", 4294967295u, true, u(&L::MaxGlyphs, 4294967295u)},
        // Wire overflow: the recipe cannot hold it, so only the agent side is checked.
        {"max_glyphs", 4294967296u, false, {}},
    };
    for (const BoundCase& c : cases)
    {
        const Json args{{"entity", 1}, {"action", "update"}, {"domain", "MeshVertex"}, {"property", "v"},
            {c.Field, c.Value}};
        const auto violation = R::ValidateAgentArgumentsJson(schema, args.dump());
        EXPECT_EQ(!violation.has_value(), c.Accepted) << "agent " << c.Field << "=" << c.Value.dump()
                                                      << (violation ? ": " + *violation : std::string{});
        if (!c.Set)
            continue;
        R::GeometryVectorFieldLayerRecipe layer = ValidLayer();
        c.Set(layer);
        std::string reason;
        EXPECT_EQ(R::ValidateGeometryVectorFieldLayer(layer, reason), c.Accepted)
            << "command " << c.Field << "=" << c.Value.dump() << ": " << reason;
    }
}

// Every declared row carries its field through the agent and the scene encoding: a fixture that
// differs from the default in every row is read back into a default layer unchanged.
TEST(VectorFieldStyle, EveryFieldRoundTripsOnEverySurface)
{
    const R::GeometryVectorFieldLayerRecipe defaults = ValidLayer();
    R::GeometryVectorFieldLayerRecipe fixture = ValidLayer();
    fixture.LengthMode = R::GeometryVectorFieldLengthMode::Raw;
    fixture.Length = 0.25f;
    fixture.LineWidthPx = 4.5f;
    fixture.Color = {0.25f, 0.5f, 0.75f, 0.5f};
    fixture.DepthTested = false;
    fixture.Stride = 3u;
    fixture.MaxGlyphs = 77u;
    fixture.Enabled = false;
    fixture.LengthProperty = "v:len";
    fixture.ColorProperty = "v:heat";
    fixture.ColorMap = Extrinsic::Graphics::Colormap::Type::Coolwarm;
    fixture.ColorAutoRange = false;
    fixture.ColorRangeMin = -2.0f;
    fixture.ColorRangeMax = 8.0f;
    std::string reason;
    ASSERT_TRUE(R::ValidateGeometryVectorFieldLayer(fixture, reason)) << reason;

    // 13 style fields follow `Vector` in the recipe; the fixture changes each of them.
    ASSERT_EQ(R::GeometryVectorFieldStyleFields().size(), 13u);
    const Json fixtureJson = R::GeometryVectorFieldStyleToJson(fixture, R::GeometryVectorFieldStyleKeys::Agent);
    const Json defaultJson = R::GeometryVectorFieldStyleToJson(defaults, R::GeometryVectorFieldStyleKeys::Agent);
    for (const R::ConfigFieldSpec& field : R::GeometryVectorFieldStyleFields())
    {
        const std::string name{field.Name};
        ASSERT_TRUE(fixtureJson.contains(name)) << name;
        EXPECT_NE(fixtureJson[name], defaultJson[name]) << name;
    }

    for (const auto keys : {R::GeometryVectorFieldStyleKeys::Agent, R::GeometryVectorFieldStyleKeys::Scene})
    {
        R::GeometryVectorFieldLayerRecipe read = defaults;
        const auto error =
            R::ReadGeometryVectorFieldStyleJson(R::GeometryVectorFieldStyleToJson(fixture, keys), read, keys);
        EXPECT_FALSE(error.has_value()) << *error;
        EXPECT_EQ(read, fixture) << static_cast<int>(keys);
    }

    // A refused value leaves the layer unchanged, even when an earlier key was valid.
    R::GeometryVectorFieldLayerRecipe untouched = defaults;
    EXPECT_TRUE(R::ReadGeometryVectorFieldStyleJson(
        Json{{"depth_tested", false}, {"line_width", 100}}, untouched, R::GeometryVectorFieldStyleKeys::Agent));
    EXPECT_EQ(untouched, defaults);
    // Scenes must carry the keys every scene has written; the UI-083 keys stay optional.
    Json scene = R::GeometryVectorFieldStyleToJson(fixture, R::GeometryVectorFieldStyleKeys::Scene);
    scene.erase("colorRange");
    EXPECT_FALSE(R::ReadGeometryVectorFieldStyleJson(scene, untouched, R::GeometryVectorFieldStyleKeys::Scene));
    // Each encoding takes only its own tokens: the agent's lower-case length modes are not scene tokens.
    R::GeometryVectorFieldLayerRecipe probe = defaults;
    for (const char* agentToken : {"raw", "normalized"})
    {
        Json mixed = R::GeometryVectorFieldStyleToJson(fixture, R::GeometryVectorFieldStyleKeys::Scene);
        mixed["lengthMode"] = agentToken;
        EXPECT_TRUE(R::ReadGeometryVectorFieldStyleJson(mixed, probe, R::GeometryVectorFieldStyleKeys::Scene))
            << agentToken;
        const Json agent{{"length_mode", agentToken == std::string("raw") ? "Raw" : "Normalized"}};
        EXPECT_TRUE(R::ReadGeometryVectorFieldStyleJson(agent, probe, R::GeometryVectorFieldStyleKeys::Agent));
    }
    EXPECT_EQ(probe, defaults);
    scene.erase("lineWidthPx");
    EXPECT_TRUE(R::ReadGeometryVectorFieldStyleJson(scene, untouched, R::GeometryVectorFieldStyleKeys::Scene));
}
