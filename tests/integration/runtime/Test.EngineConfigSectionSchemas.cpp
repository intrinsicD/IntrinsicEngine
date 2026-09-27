// RUNTIME-276: every Sandbox config section with a generated schema agrees with its
// validator. The schema lists exactly the payload keys, every enum value and range bound
// it declares passes the table check, and values past a bound or enum end are rejected.
// Cross-field rules may still reject a boundary value, but never with the table's
// type/range/unknown-value wording for that field.
#include <cmath>
#include <set>
#include <string>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Sandbox.ConfigSections;
namespace Config = Extrinsic::Core::Config;
using Json = nlohmann::json;
namespace
{
    struct Check
    {
        const Config::EngineConfigSectionRegistration& Registration;
        std::string Defaults;

        Config::EngineConfigSectionValidationResult Validate(const Json& payload) const
        {
            return Registration.Validate(payload.dump(), Defaults, "schema-test");
        }
        // Rejected by the declared-field check of `field` (as opposed to a cross-field rule).
        static bool TableRejected(const Config::EngineConfigSectionValidationResult& result, const std::string& field)
        {
            if (result.State == Config::EngineConfigState::Valid || result.Diagnostics.empty()) return false;
            const auto& message = result.Diagnostics.front().Message;
            return message.starts_with(field + " must") || message.starts_with(field + " needs") ||
                   message.starts_with("Unknown " + field + " value") || message.find(" field: ") != std::string::npos;
        }
        void ExpectTableAccepts(const std::string& field, const Json& value) const
        {
            const auto result = Validate(Json{{field, value}});
            EXPECT_FALSE(TableRejected(result, field))
                << Registration.DefaultSection.Name << "." << field << " = " << value.dump() << ": "
                << result.Diagnostics.front().Message;
        }
        void ExpectRejects(const std::string& field, const Json& value) const
        {
            EXPECT_FALSE(Validate(Json{{field, value}}).Usable())
                << Registration.DefaultSection.Name << "." << field << " = " << value.dump() << " was accepted";
        }
    };
}

TEST(EngineConfigSectionSchemas, GeneratedSchemasAgreeWithTheirValidators)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    std::set<std::string> withSchema;
    for (const auto& registration : registry.Entries())
    {
        if (registration.SchemaJson.empty()) continue;
        const auto& name = registration.DefaultSection.Name;
        withSchema.insert(name);
        const Check check{registration, registration.DefaultSection.PayloadJson};
        const Json schema = Json::parse(registration.SchemaJson, nullptr, false);
        const Json defaults = Json::parse(registration.DefaultSection.PayloadJson);
        ASSERT_TRUE(schema.is_object()) << name;
        EXPECT_EQ(schema["$id"], registration.DefaultSection.SchemaId) << name;
        EXPECT_EQ(schema["additionalProperties"], false) << name;
        std::set<std::string> schemaKeys, payloadKeys;
        for (const auto& [key, value] : schema["properties"].items()) schemaKeys.insert(key);
        for (const auto& [key, value] : defaults.items()) payloadKeys.insert(key);
        EXPECT_EQ(schemaKeys, payloadKeys) << name << ": schema properties differ from the payload keys";

        EXPECT_TRUE(check.Validate(defaults).Usable()) << name << ": defaults rejected";
        // Schema defaults are the values omitted fields take in the validator's merge.
        for (const auto& [field, property] : schema["properties"].items())
        {
            Json omitted = defaults, explicitDefault = defaults;
            omitted.erase(field);
            explicitDefault[field] = property["default"];
            const auto implicit = check.Validate(omitted), stated = check.Validate(explicitDefault);
            EXPECT_EQ(implicit.Usable(), stated.Usable()) << name << "." << field;
            EXPECT_EQ(implicit.CanonicalPayloadJson, stated.CanonicalPayloadJson)
                << name << "." << field << ": schema default differs from the omitted-field value";
        }
        EXPECT_FALSE(check.Validate(Json{{"no_such_field", 1}}).Usable()) << name;

        for (const auto& [field, property] : schema["properties"].items())
        {
            EXPECT_TRUE(property.contains("description")) << name << "." << field;
            if (property.contains("enum") && property["type"] == "integer")
            {
                const auto& values = property["enum"];
                ASSERT_EQ(property["x-enum-names"].size(), values.size()) << name << "." << field;
                for (const auto& value : values) check.ExpectTableAccepts(field, value);
                check.ExpectRejects(field, values.back().get<std::int64_t>() + 1);
                if (values.front().get<std::int64_t>() > 0) check.ExpectRejects(field, values.front().get<std::int64_t>() - 1);
                continue;
            }
            const bool integer = property["type"] == "integer";
            if (property["type"] == "number" || integer)
            {
                const double step = integer ? 1.0 : 1e-6;
                if (property.contains("minimum"))
                {
                    const double min = property["minimum"].get<double>();
                    check.ExpectTableAccepts(field, integer ? Json(std::int64_t(min)) : Json(min));
                    check.ExpectRejects(field, integer ? Json(std::int64_t(min) - 1) : Json(min - std::max(step, std::abs(min) * 1e-6)));
                }
                if (property.contains("exclusiveMinimum"))
                {
                    const double min = property["exclusiveMinimum"].get<double>();
                    check.ExpectRejects(field, integer ? Json(std::int64_t(min)) : Json(min));
                }
                if (property.contains("maximum"))
                {
                    const double max = property["maximum"].get<double>();
                    check.ExpectTableAccepts(field, integer ? Json(std::int64_t(max)) : Json(max));
                    check.ExpectRejects(field, integer ? Json(std::int64_t(max) + 1) : Json(max + std::max(step, std::abs(max) * 1e-6)));
                }
                if (property.contains("exclusiveMaximum"))
                {
                    const double max = property["exclusiveMaximum"].get<double>();
                    check.ExpectRejects(field, integer ? Json(std::int64_t(max)) : Json(max));
                }
                check.ExpectRejects(field, "text");
            }
            if (property["type"] == "boolean") check.ExpectRejects(field, 1);
            if (property.contains("x-property-kinds"))
            {
                check.ExpectRejects(field, Json{{"domain", "MeshVertex"}});
                check.ExpectRejects(field, Json{{"domain", "Nowhere"}, {"name", "x"}, {"kind", property["x-property-kinds"][0]}});
            }
        }
    }
    // Slice A of RUNTIME-276: the mesh-field family carries generated schemas.
    for (const auto* section : {"sandbox.property_smoothing", "sandbox.harmonic_field", "sandbox.laplacian_eigenbasis",
                                "sandbox.scalar_gradient", "sandbox.geodesics", "sandbox.mesh_curvature"})
        EXPECT_TRUE(withSchema.contains(section)) << section << " has no generated schema";
}

TEST(EngineConfigSectionSchemas, ExportContainsEverySandboxSection)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    const Json exported = Json::parse(Config::ExportEngineConfigSchema(registry), nullptr, false);
    ASSERT_TRUE(exported.is_object());
    ASSERT_EQ(exported["$defs"].size(), registry.Entries().size());
    for (const auto& registration : registry.Entries())
    {
        const auto& entry = exported["$defs"][registration.DefaultSection.Name];
        EXPECT_EQ(entry["x-schema-id"], registration.DefaultSection.SchemaId);
        EXPECT_EQ(entry["x-schema-version"], registration.DefaultSection.SchemaVersion);
        EXPECT_EQ(entry.contains("x-schema-missing"), registration.SchemaJson.empty());
    }
}
