// RUNTIME-276 schema shape and frozen before/after controls for section codecs.
// Original strict schemas keep their omitted-field default contract; new schemas
// preserve their serialized defaults, nested bindings, numeric bounds and tokens.
// Fallback sections are checked by canonical outcomes, not rejection-message prefixes.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
import Extrinsic.Core.Config.Engine;
import Extrinsic.Core.Config.EngineLoad;
import Extrinsic.Sandbox.ConfigSections;
namespace Config = Extrinsic::Core::Config;
using Json = nlohmann::json;
namespace
{
    bool IsOriginalStrictSchema(const std::string_view name)
    {
        return name == "sandbox.gizmo" || name == "sandbox.geodesics" ||
            name == "sandbox.scalar_gradient" || name == "sandbox.property_smoothing" ||
            name == "sandbox.harmonic_field" || name == "sandbox.laplacian_eigenbasis" ||
            name == "sandbox.mesh_curvature" || name == "sandbox.coherent_point_drift" ||
            name == "sandbox.point_sampling";
    }

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

TEST(EngineConfigSectionSchemas, EveryRegisteredSectionPublishesASchema)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    ASSERT_EQ(registry.Entries().size(), 26u);
    for (const auto& registration : registry.Entries())
        EXPECT_FALSE(registration.SchemaJson.empty())
            << registration.DefaultSection.Name << " has no generated schema";
}

// These literals deliberately come from the pre-table validators, not generated schemas.
// They are before/after controls for the migration's accepted numeric and token ranges.
TEST(EngineConfigSectionSchemas, StrictOwnerLiteralRangesRemainUnchanged)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    const double u32 = std::numeric_limits<std::uint32_t>::max();
    const double f32 = std::numeric_limits<float>::max();
    struct Row { const char* Section; const char* Field; double Min; double Max; bool Integer; bool ExclusiveMin = false; };
    const Row rows[] = {
        {"normal_estimation", "entity", 0, u32, true},
        {"normal_estimation", "k_neighbors", 1, u32, true},
        {"normal_estimation", "minimum_neighbors", 1, u32, true},
        {"normal_estimation", "orientation", 0, 1, true},
        {"normal_estimation", "weighting", 0, 4, true},
        {"normal_estimation", "gpu_query_batch_size", 1, 16384, true},
        {"normal_estimation", "radius", 0, f32, false},
        {"normal_estimation", "degenerate_epsilon", 0, std::numeric_limits<double>::max(), false, true},
        {"normal_estimation", "collinear_epsilon", 0, std::numeric_limits<double>::max(), false, true},
        {"registration", "source_entity", 0, u32, true},
        {"registration", "target_entity", 0, u32, true},
        {"registration", "max_iterations", 1, u32, true},
        {"registration", "trajectory_step", 0, u32, true},
        {"registration", "inlier_ratio", 0, 1, false, true},
        {"registration", "convergence_threshold", 0, std::numeric_limits<double>::max(), false},
        {"outlier_analysis", "entity", 0, u32, true},
        {"outlier_analysis", "k_neighbors", 1, u32, true},
        {"outlier_analysis", "minimum_neighbors", 0, u32, true},
        {"outlier_analysis", "gpu_query_batch_size", 1, 16384, true},
        {"outlier_analysis", "radius", 0, f32, false},
        {"outlier_analysis", "stddev_multiplier", 0, f32, false},
        {"outlier_analysis", "score_threshold", 0, f32, false},
        {"kernel_density", "entity", 0, u32, true},
        {"kernel_density", "k_neighbors", 0, u32, true},
        {"kernel_density", "gpu_query_batch_size", 1, 16384, true},
        {"kernel_density", "bandwidth", 0, f32, false},
        {"point_spacing", "entity", 0, u32, true},
        {"point_spacing", "k_neighbors", 0, u32, true},
        {"point_spacing", "gpu_query_batch_size", 1, 16384, true},
        {"point_spacing", "scale_factor", 0, f32, false},
        {"density_weights", "entity", 0, u32, true},
        {"density_weights", "gpu_query_batch_size", 1, 16384, true},
        {"density_weights", "gpu_radius_capacity", 1, 1024, true},
        {"density_weights", "support_radius", 0, f32, false},
        {"bilateral_filter", "entity", 0, u32, true},
        {"bilateral_filter", "k_neighbors", 0, u32, true},
        {"bilateral_filter", "gpu_query_batch_size", 1, 16384, true},
        {"bilateral_filter", "iterations", 0, 100, true},
        {"bilateral_filter", "spatial_sigma", 0, f32, false},
        {"bilateral_filter", "normal_sigma", 0, f32, false},
        {"keypoint_analysis", "entity", 0, u32, true},
        {"keypoint_analysis", "minimum_neighbors", 0, u32, true},
        {"keypoint_analysis", "gpu_query_batch_size", 1, 16384, true},
        {"keypoint_analysis", "gpu_radius_capacity", 1, 1024, true},
        {"keypoint_analysis", "salient_radius", 0, f32, false},
        {"keypoint_analysis", "nonmax_radius", 0, f32, false},
        {"keypoint_analysis", "gamma21", 0, 1, false},
        {"keypoint_analysis", "gamma32", 0, 1, false},
        {"descriptor_analysis", "entity", 0, u32, true},
        {"descriptor_analysis", "max_neighbors", 0, u32, true},
        {"descriptor_analysis", "gpu_query_batch_size", 1, 16384, true},
        {"descriptor_analysis", "gpu_radius_capacity", 1, 1024, true},
        {"descriptor_analysis", "feature_radius", 0, f32, false},
        {"point_construction", "entity", 0, u32, true},
        {"point_construction", "resolution", 1, 512, true},
        {"point_construction", "k_neighbors", 1, 63, true},
        {"point_construction", "normal_k_neighbors", 3, 1024, true},
        {"point_construction", "gpu_query_batch_size", 1, 16384, true},
        {"point_construction", "max_grid_vertices", 8, 16777216, true},
        {"point_construction", "bounding_box_padding", 0, f32, false},
        {"point_construction", "normal_agreement_power", 0, f32, false},
        {"point_construction", "kernel_sigma_scale", 0, f32, false, true},
        {"point_construction", "min_distance_epsilon", 0, f32, false},
        {"selection", "point_radius", 1.e-6, 1.e6, false},
    };
    for (const auto& row : rows)
    {
        SCOPED_TRACE(std::string(row.Section) + "." + row.Field);
        const auto name = std::string{row.Section == std::string_view{"selection"} ? "runtime." : "sandbox."} + row.Section;
        const Config::EngineConfigSectionRegistration* registration = nullptr;
        for (const auto& candidate : registry.Entries())
            if (candidate.DefaultSection.Name == name) registration = &candidate;
        ASSERT_NE(registration, nullptr);
        const auto probe = [&](double value, bool expected) {
            const Json number = row.Integer ? Json(static_cast<std::int64_t>(value)) : Json(value);
            const Json payload{{row.Field, number}};
            const auto result = registration->Validate(payload.dump(), registration->DefaultSection.PayloadJson, "literal-range");
            EXPECT_EQ(result.State == Config::EngineConfigState::Valid, expected) << payload.dump();
            if (expected)
            {
                EXPECT_TRUE(result.Diagnostics.empty());
                const Json canonical = Json::parse(result.CanonicalPayloadJson);
                const double stored = canonical.at(row.Field).get<double>();
                EXPECT_TRUE(stored == value || stored == static_cast<double>(static_cast<float>(value)));
            }
            else EXPECT_FALSE(result.Diagnostics.empty());
        };
        const double step = row.Integer ? 1.0 : std::max(1.e-6, std::abs(row.Min) * 1.e-6);
        probe(row.Min, !row.ExclusiveMin);
        if (row.ExclusiveMin) probe(row.Section == std::string_view{"point_construction"}
            ? std::numeric_limits<float>::min() : std::numeric_limits<double>::min(), true);
        probe(row.Min - step, false);
        probe(row.Max, true);
        if (row.Max < std::numeric_limits<double>::max())
            probe(row.Max + (row.Integer ? 1.0 : std::max(1.e-6, std::abs(row.Max) * 1.e-6)), false);
    }
}

TEST(EngineConfigSectionSchemas, StrictOwnerTokensAndKeysRemainUnchanged)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    struct Row { const char* Section; const char* Field; std::initializer_list<const char*> Tokens; };
    const Row rows[] = {
        {"normal_estimation", "method", {"point_set_pca", "mesh_face_weighted", "graph_neighborhood", "mesh_face_normals"}},
        {"normal_estimation", "backend", {"cpu_kdtree", "cpu_lbvh", "vulkan_lbvh", "vulkan"}},
        {"registration", "variant", {"point_to_point", "point_to_plane"}},
        {"registration", "backend", {"cpu_kdtree", "cpu_lbvh", "vulkan_lbvh"}},
        {"outlier_analysis", "method", {"statistical", "radius", "local_distance_ratio"}},
        {"outlier_analysis", "operation", {"analyze", "remove_marked"}},
        {"outlier_analysis", "backend", {"cpu_octree", "cpu_lbvh", "vulkan_lbvh"}},
        {"kernel_density", "backend", {"cpu_octree", "cpu_lbvh", "vulkan_lbvh"}},
        {"point_spacing", "backend", {"cpu_octree", "cpu_lbvh", "vulkan_lbvh"}},
        {"density_weights", "backend", {"cpu_kdtree", "cpu_lbvh", "vulkan_lbvh"}},
        {"density_weights", "kernel", {"gaussian", "theta_lop", "wendland_c2"}},
        {"density_weights", "mode", {"direct", "reciprocal"}},
        {"bilateral_filter", "backend", {"cpu_octree", "cpu_lbvh", "vulkan_lbvh"}},
        {"keypoint_analysis", "backend", {"cpu_kdtree", "cpu_lbvh", "vulkan_lbvh", "vulkan_compute"}},
        {"descriptor_analysis", "backend", {"cpu_kdtree", "cpu_lbvh", "vulkan_lbvh"}},
        {"point_construction", "method", {"hoppe", "knn_graph"}},
        {"point_construction", "backend", {"cpu_reference", "cpu_lbvh", "vulkan_lbvh"}},
        {"selection", "target", {"entity", "vertex", "edge", "face"}},
    };
    for (const auto& row : rows)
    {
        SCOPED_TRACE(std::string(row.Section) + "." + row.Field);
        const Config::EngineConfigSectionRegistration* registration = nullptr;
        for (const auto& candidate : registry.Entries())
            if (candidate.DefaultSection.Name == std::string{row.Section == std::string_view{"selection"} ? "runtime." : "sandbox."} + row.Section) registration = &candidate;
        ASSERT_NE(registration, nullptr);
        for (const auto* token : row.Tokens)
        {
            Json payload{{row.Field, token}};
            if (row.Section == std::string_view{"outlier_analysis"}) payload["radius"] = 1.0;
            const auto result = registration->Validate(payload.dump(), registration->DefaultSection.PayloadJson, "literal-token");
            ASSERT_EQ(result.State, Config::EngineConfigState::Valid) << payload.dump();
            EXPECT_TRUE(result.Diagnostics.empty());
            EXPECT_EQ(Json::parse(result.CanonicalPayloadJson).at(row.Field), token);
        }
        EXPECT_FALSE(registration->Validate(Json{{row.Field, "foreign-token"}}.dump(), registration->DefaultSection.PayloadJson, "literal-token").Usable());
    }
    for (const auto* section : {"normal_estimation", "registration", "outlier_analysis", "kernel_density", "point_spacing",
                                "density_weights", "bilateral_filter", "keypoint_analysis", "descriptor_analysis", "point_construction", "selection"})
    {
        const Config::EngineConfigSectionRegistration* registration = nullptr;
        for (const auto& candidate : registry.Entries())
            if (candidate.DefaultSection.Name == std::string{section == std::string_view{"selection"} ? "runtime." : "sandbox."} + section) registration = &candidate;
        ASSERT_NE(registration, nullptr);
        const auto defaults = Json::parse(registration->DefaultSection.PayloadJson);
        for (const auto& [key, value] : defaults.items())
        {
            const auto result = registration->Validate(Json{{key, value}}.dump(), registration->DefaultSection.PayloadJson, "literal-key");
            EXPECT_EQ(result.State, Config::EngineConfigState::Valid) << section << "." << key;
            EXPECT_TRUE(result.Diagnostics.empty());
            EXPECT_EQ(result.ParsedFieldCount, 1u);
        }
        const auto unknown = registration->Validate(R"({"foreign_field":1})", registration->DefaultSection.PayloadJson, "literal-key");
        EXPECT_FALSE(unknown.Usable()) << section;
        EXPECT_FALSE(unknown.Diagnostics.empty());
    }
}

TEST(EngineConfigSectionSchemas, StrictOwnerArrayAndNameLimitsRemainUnchanged)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    for (const auto& registration : registry.Entries())
    {
        const auto validate = [&](const Json& payload) {
            return registration.Validate(payload.dump(), registration.DefaultSection.PayloadJson, "literal-shape");
        };
        if (registration.DefaultSection.Name == "sandbox.normal_estimation")
        {
            const double f32 = std::numeric_limits<float>::max();
            EXPECT_EQ(validate(Json{{"fallback_normal", {-f32, 0, f32}}}).State, Config::EngineConfigState::Valid);
            for (const Json& value : {Json::array({0, 1}), Json::array({0, 1, 2, 3}), Json::array({0, 0, f32 * 2})})
                EXPECT_FALSE(validate(Json{{"fallback_normal", value}}).Usable());
            EXPECT_FALSE(validate(Json{{"use_radius", true}, {"radius", 0}}).Usable());
            EXPECT_EQ(validate(Json{{"use_radius", true}, {"radius", 1}}).State, Config::EngineConfigState::Valid);
        }
        if (registration.DefaultSection.Name == "sandbox.descriptor_analysis")
        {
            Json outputs = Json::parse(registration.DefaultSection.PayloadJson).at("outputs");
            ASSERT_EQ(outputs.size(), 33u);
            EXPECT_EQ(validate(Json{{"outputs", outputs}}).State, Config::EngineConfigState::Valid);
            outputs.erase(outputs.end() - 1);
            EXPECT_FALSE(validate(Json{{"outputs", outputs}}).Usable());
            outputs.push_back(outputs.front());
            outputs.push_back(outputs.front());
            EXPECT_FALSE(validate(Json{{"outputs", outputs}}).Usable());
        }
        if (registration.DefaultSection.Name == "sandbox.point_construction")
        {
            for (const auto size : {1u, 256u})
                EXPECT_EQ(validate(Json{{"output_name", std::string(size, 'x')}}).State, Config::EngineConfigState::Valid);
            for (const auto size : {0u, 257u})
                EXPECT_FALSE(validate(Json{{"output_name", std::string(size, 'x')}}).Usable());
        }
    }
}

TEST(EngineConfigSectionSchemas, NewSchemasPreserveNestedDefaultsAndBindingEncodings)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    const auto check = [&](auto&& self, const Json& schema, const Json& value, const std::string& path) -> void {
        SCOPED_TRACE(path);
        EXPECT_EQ(schema.at("default"), value);
        if (value.is_object())
        {
            ASSERT_TRUE(schema.contains("properties"));
            EXPECT_EQ(schema.at("properties").size(), value.size());
            if (value.contains("domain") && value.contains("name") && value.contains("kind"))
            {
                const auto& kind = schema.at("properties").at("kind");
                EXPECT_EQ(kind.at("type"), value.at("kind").is_string() ? "string" : "integer");
                EXPECT_NE(std::find(kind.at("enum").begin(), kind.at("enum").end(), value.at("kind")), kind.at("enum").end());
                return;
            }
            for (const auto& [key, child] : value.items())
            {
                ASSERT_TRUE(schema.at("properties").contains(key));
                self(self, schema.at("properties").at(key), child, path + "." + key);
            }
        }
        else if (value.is_null() && schema.contains("properties"))
        {
            for (const auto& [key, property] : schema.at("properties").items())
                EXPECT_FALSE(property.contains("default")) << key << ": null object must not invent child defaults";
        }
    };
    std::size_t checked = 0;
    for (const auto& registration : registry.Entries())
    {
        // Slice A's nine schemas describe omitted-field merge defaults instead.
        if (IsOriginalStrictSchema(registration.DefaultSection.Name)) continue;
        ++checked;
        SCOPED_TRACE(registration.DefaultSection.Name);
        ASSERT_FALSE(registration.SchemaJson.empty());
        Json schema = Json::parse(registration.SchemaJson);
        const Json defaults = Json::parse(registration.DefaultSection.PayloadJson);
        schema["default"] = defaults;
        check(check, schema, defaults, registration.DefaultSection.Name);
    }
    EXPECT_EQ(checked, 17u);
}

TEST(EngineConfigSectionSchemas, GeneratedSchemasAgreeWithTheirValidators)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    std::set<std::string> withSchema;
    for (const auto& registration : registry.Entries())
    {
        if (registration.SchemaJson.empty()) continue;
        const auto& name = registration.DefaultSection.Name;
        // This original strict-only check rejects every malformed field. Fallback owners
        // intentionally accept diagnosed fallback and have separate canonical-outcome controls.
        if (!IsOriginalStrictSchema(name)) continue;
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
                                "sandbox.scalar_gradient", "sandbox.geodesics", "sandbox.mesh_curvature",
                                "sandbox.gizmo"})
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

// Frozen from the pre-migration ReadInteger/ReadNumber call sites. Reference values
// differ from defaults so a fallback-to-default bug cannot pass by coincidence.
TEST(EngineConfigSectionSchemas, FallbackOwnerLiteralRangesRemainUnchanged)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    struct Row { const char* Section; const char* Path; double Min; double Max; bool Integer; };
    const Row rows[]{
        {"clustering", "/cluster_count", 1, 1024, true},
        {"clustering", "/gpu_preview_interval", 1, 1000000, true},
        {"clustering", "/max_iterations", 1, 4096, true},
        {"clustering", "/seed", 0, std::numeric_limits<std::uint32_t>::max(), true},
        {"curvature_segmentation", "/fixed_component_count", 1, 1024, true},
        {"curvature_segmentation", "/feature_base_radius_ratio", 1.0e-12, 1.0, false},
        {"curvature_segmentation", "/hard_dihedral_threshold_degrees", 0.0, 180.0, false},
        {"curvature_segmentation", "/patch_complexity_cost", 0.0, 1.0e12, false},
        {"curvature_segmentation", "/automatic_min_components", 1, 1024, true},
        {"curvature_segmentation", "/automatic_max_components", 1, 1024, true},
        {"curvature_segmentation", "/automatic_fit_tolerance", 1.0e-12, 1.0e12, false},
        {"curvature_segmentation", "/automatic_complexity_weight", 0.0, 1.0e12, false},
        {"curvature_segmentation", "/max_em_iterations", 1, 100000, true},
        {"curvature_segmentation", "/em_relative_tolerance", 0.0, 1.0, false},
        {"curvature_segmentation", "/covariance_floor", 1.0e-15, 1.0e6, false},
        {"curvature_segmentation", "/seed", 0, std::numeric_limits<std::uint32_t>::max(), true},
        {"curvature_segmentation", "/spatial_weight", 0.0, 1.0e12, false},
        {"curvature_segmentation", "/feature_sensitivity", 0.0, 1.0e12, false},
        {"curvature_segmentation", "/max_spatial_iterations", 1, 100000, true},
        {"curvature_segmentation", "/minimum_region_faces", 1, std::numeric_limits<std::uint32_t>::max(), true},
        {"point_cloud_consolidation", "/support_radius", 1.0e-12, 1.0e12, false},
        {"point_cloud_consolidation", "/max_support_neighbors", 1, 1'000'000, true},
        {"point_cloud_consolidation", "/max_predicted_contributions", 1, 1'000'000'000'000LL, true},
        {"point_cloud_consolidation", "/repulsion_weight", 0.0, 0.499999999999, false},
        {"point_cloud_consolidation", "/max_iterations", 1, 4096, true},
        {"point_cloud_consolidation", "/convergence_tolerance", 0.0, 1.0e12, false},
        {"point_cloud_consolidation", "/target_point_count", 0, 1'000'000, true},
        {"point_cloud_consolidation", "/seed", 0, std::numeric_limits<std::uint32_t>::max(), true},
        {"point_cloud_consolidation", "/gpu_preview_interval", 1, 1000000, true},
        {"point_cloud_consolidation", "/gpu_query_batch_size", 1, 16384, true},
        {"point_cloud_consolidation", "/gpu_radius_capacity", 1, 1024, true},
        {"point_cloud_consolidation", "/normal_angle_radians", 1.0e-6, std::numbers::pi_v<double> - 1.0e-6, false},
        {"point_cloud_consolidation", "/normal_refinement_rounds", 1, 4096, true},
        {"point_cloud_consolidation", "/clop_mixture_component_count", 1, 1'000'000, true},
        {"point_cloud_consolidation", "/clop_mixture_max_iterations", 1, 4096, true},
        {"point_cloud_consolidation", "/clop_mixture_relative_tolerance", 0.0, 1.0, false},
        {"point_cloud_consolidation", "/clop_covariance_floor", 1.0e-18, 1.0e12, false},
        {"point_cloud_consolidation", "/ear_edge_sensitivity", 1.0e-12, 1.0e6, false},
        {"progressive_poisson", "/dimension", 2, 3, true},
        {"progressive_poisson", "/grid_width", 1, 4096, true},
        {"progressive_poisson", "/max_levels", 1, 32, true},
        {"progressive_poisson", "/hash_load_factor", 0.01, 16.0, false},
        {"progressive_poisson", "/radius_alpha", -1.0, 0.999, false},
        {"progressive_poisson", "/grid_origin_seed", 0, std::numeric_limits<std::int32_t>::max(), true},
        {"progressive_poisson", "/shuffle_seed", 0, std::numeric_limits<std::int32_t>::max(), true},
        {"progressive_poisson", "/prefix_count", 0, 10'000'000, true},
        {"progressive_poisson", "/debounce_seconds", 0.0, 10.0, false},
        {"parameterization", "/view/split_ratio", 0.2, static_cast<double>(0.8f), false},
        {"parameterization", "/lscm/pin_vertex_0", 0, std::numeric_limits<std::uint32_t>::max(), true},
        {"parameterization", "/lscm/pin_vertex_1", 0, std::numeric_limits<std::uint32_t>::max(), true},
        {"parameterization", "/lscm/solver_tolerance", std::numeric_limits<double>::min(), 1.0e30, false},
        {"parameterization", "/lscm/max_solver_iterations", 1, std::numeric_limits<std::uint32_t>::max(), true},
        {"parameterization", "/bff/angle_sum_tolerance", std::numeric_limits<double>::min(), 1.0e30, false},
        {"parameterization", "/bff/degeneracy_tolerance", std::numeric_limits<double>::min(), 1.0e30, false},
        {"parameterization", "/atlas/region_count", 0, 64, true},
        {"parameterization", "/atlas/resolution", 16, 16384, true},
        {"parameterization", "/atlas/padding", 0, 32, true},
        {"parameterization", "/atlas/max_charts", 1, 1000000, true},
        {"parameterization", "/atlas/max_iterations", 1, 1000, true},
        {"parameterization", "/atlas/texels_per_unit", 0.0, 1.0e12, false},
        {"parameterization", "/atlas/max_conformal_distortion", 1.0, 1.0e6, false},
        {"parameterization", "/atlas/max_area_distortion", 1.0, 1.0e6, false},
    };
    for (const auto& row : rows)
    {
        SCOPED_TRACE(std::string(row.Section) + row.Path);
        const Config::EngineConfigSectionRegistration* registration = nullptr;
        for (const auto& candidate : registry.Entries())
            if (candidate.DefaultSection.Name == std::string{"sandbox."} + row.Section) registration = &candidate;
        ASSERT_NE(registration, nullptr);
        const Json::json_pointer path{row.Path};
        Json reference = Json::parse(registration->DefaultSection.PayloadJson);
        Json companions = Json::object();
        if (row.Section == std::string_view{"curvature_segmentation"})
        {
            companions["automatic_min_components"] = 1;
            companions["automatic_max_components"] = 1024;
            reference.update(companions);
        }
        const double middle = row.Min + (row.Max - row.Min) * 0.5;
        reference[path] = row.Integer ? Json(static_cast<std::int64_t>(middle)) : Json(middle);
        const auto validatedReference = registration->Validate(reference.dump(), registration->DefaultSection.PayloadJson, "literal-reference");
        ASSERT_EQ(validatedReference.State, Config::EngineConfigState::Valid);
        ASSERT_TRUE(validatedReference.Diagnostics.empty());
        reference = Json::parse(validatedReference.CanonicalPayloadJson);
        const auto probe = [&](double value, bool expected) {
            Json payload = companions;
            payload[path] = row.Integer ? Json(static_cast<std::int64_t>(value)) : Json(value);
            const auto result = registration->Validate(payload.dump(), reference.dump(), "literal-fallback-range");
            EXPECT_EQ(result.State == Config::EngineConfigState::Valid, expected) << payload.dump();
            if (expected)
            {
                EXPECT_TRUE(result.Diagnostics.empty());
                const Json canonical = Json::parse(result.CanonicalPayloadJson);
                const double stored = canonical.at(path).get<double>();
                EXPECT_TRUE(stored == value || stored == static_cast<double>(static_cast<float>(value))) << stored << " != " << value;
                // The view reader intentionally does not count split_ratio today.
                EXPECT_EQ(result.ParsedFieldCount, row.Path == std::string_view{"/view/split_ratio"}
                    ? 0u : row.Section == std::string_view{"curvature_segmentation"}
                        ? (payload.contains("automatic_min_components") && payload.contains("automatic_max_components")
                            ? (row.Path == std::string_view{"/automatic_min_components"} || row.Path == std::string_view{"/automatic_max_components"} ? 2u : 3u) : 1u)
                        : 1u);
            }
            else
            {
                EXPECT_FALSE(result.Diagnostics.empty());
                if (result.Usable())
                {
                    EXPECT_EQ(result.State, Config::EngineConfigState::FallbackApplied);
                    EXPECT_EQ(Json::parse(result.CanonicalPayloadJson).at(path), reference.at(path));
                }
            }
        };
        probe(row.Min, true);
        probe(row.Max, true);
        probe(row.Min - (row.Integer ? 1.0 : std::max(1.e-6, std::abs(row.Min) * 1.e-6)), false);
        probe(row.Max + (row.Integer ? 1.0 : std::max(1.e-6, std::abs(row.Max) * 1.e-6)), false);
    }
}

TEST(EngineConfigSectionSchemas, FallbackOwnerTokensRoundTripWithoutOrdinalMappings)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    struct Row { const char* Section; const char* Path; std::initializer_list<const char*> Tokens; };
    const Row rows[]{
        {"clustering", "/initialization", {"Random", "Hierarchical"}},
        {"clustering", "/backend", {"CpuReference", "VulkanCompute"}},
        {"curvature_segmentation", "/method", {"curvature_gmm", "feature_aligned_patches", "feature_boundary_curves_v1"}},
        {"curvature_segmentation", "/selection_mode", {"fixed_count", "automatic"}},
        {"point_cloud_consolidation", "/backend", {"cpu_reference", "gpu_vulkan_compute", "cpu_lbvh", "vulkan_lbvh"}},
        {"point_cloud_consolidation", "/initial_backend", {"cpu", "vulkan"}},
        {"point_cloud_consolidation", "/strategy", {"lop", "wlop", "clop", "ear"}},
        {"point_cloud_consolidation", "/support_radius_mode", {"auto", "manual"}},
        {"point_cloud_consolidation", "/normal_source", {"authored_or_estimate", "require_authored"}},
        {"progressive_poisson", "/channel", {"Level", "Rank", "SplatRadius", "PrefixVisible"}},
        {"progressive_poisson", "/backend", {"CpuReference", "VulkanCompute"}},
        {"parameterization", "/strategy", {"lscm", "harmonic_cotangent", "tutte_uniform", "bff"}},
        {"parameterization", "/view/render_mode", {"cpu_layout", "gpu_shaded"}},
        {"parameterization", "/view/background_mode", {"grid", "checker", "texel_density", "texture"}},
        {"parameterization", "/atlas/method", {"fast_staged", "xatlas"}},
        {"parameterization", "/atlas/distortion", {"none", "angle", "area", "both"}},
        {"parameterization", "/harmonic/boundary", {"circle", "square", "custom"}},
        {"parameterization", "/bff/mode", {"automatic_conformal", "target_lengths", "target_angles"}},
    };
    for (const auto& row : rows)
    {
        SCOPED_TRACE(std::string(row.Section) + row.Path);
        const auto it = std::ranges::find_if(registry.Entries(), [&](const auto& r) {
            return r.DefaultSection.Name == std::string("sandbox.") + row.Section;
        });
        ASSERT_NE(it, registry.Entries().end());
        const Json reference = Json::parse(it->DefaultSection.PayloadJson);
        const Json::json_pointer path{row.Path};
        for (const auto* token : row.Tokens)
        {
            Json payload = Json::object();
            payload[path] = token;
            if (path == Json::json_pointer{"/atlas/method"}) payload["atlas"]["distortion"] = "angle";
            if (path == Json::json_pointer{"/bff/mode"})
            {
                if (token == std::string_view("target_lengths")) payload["bff"]["boundary_data"] = {1., 2., 3.};
                if (token == std::string_view("target_angles")) payload["bff"]["boundary_data"] = {std::numbers::pi, std::numbers::pi};
            }
            const auto result = it->Validate(payload.dump(), reference.dump(), "tokens");
            ASSERT_EQ(result.State, Config::EngineConfigState::Valid) << payload.dump();
            EXPECT_TRUE(result.Diagnostics.empty());
            EXPECT_EQ(Json::parse(result.CanonicalPayloadJson).at(path), token);
        }
        for (const auto* token : {"foreign", "invalid", "none"})
        {
            if (std::ranges::find(row.Tokens, std::string_view(token)) != row.Tokens.end()) continue;
            Json payload = Json::object(); payload[path] = token;
            const auto result = it->Validate(payload.dump(), reference.dump(), "tokens");
            EXPECT_NE(result.State, Config::EngineConfigState::Valid);
            EXPECT_FALSE(result.Diagnostics.empty());
            if (result.Usable()) EXPECT_EQ(Json::parse(result.CanonicalPayloadJson).at(path), reference.at(path));
        }
    }
}

TEST(EngineConfigSectionSchemas, FallbackOwnerKeysPreserveDiagnosedAndSilentUnknownPolicies)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    struct Row { const char* Section; const char* Path; std::initializer_list<const char*> Keys; };
    const Row rows[]{
        {"clustering", "", {"cluster_count", "gpu_preview_interval", "max_iterations", "seed", "initialization", "backend", "properties"}},
        {"curvature_segmentation", "", {"features", "positions", "components", "regions", "region_colors", "boundaries", "boundary_colors", "hard_features", "feature_confidence", "boundary_roles", "feature_colors", "method", "selection_mode", "fixed_component_count", "automatic_min_components", "automatic_max_components", "automatic_fit_tolerance", "automatic_complexity_weight", "max_em_iterations", "em_relative_tolerance", "covariance_floor", "seed", "spatial_weight", "feature_sensitivity", "max_spatial_iterations", "minimum_region_faces", "feature_base_radius_ratio", "hard_dihedral_threshold_degrees", "patch_complexity_cost"}},
        {"point_cloud_consolidation", "", {"backend", "initial_backend", "strategy", "support_radius_mode", "support_radius", "max_support_neighbors", "max_predicted_contributions", "repulsion_weight", "max_iterations", "convergence_tolerance", "target_point_count", "seed", "gpu_preview_interval", "gpu_query_batch_size", "gpu_radius_capacity", "wlop_anisotropic", "normal_source", "normal_angle_radians", "normal_refinement_rounds", "clop_mixture_component_count", "clop_mixture_max_iterations", "clop_mixture_relative_tolerance", "clop_covariance_floor", "ear_edge_sensitivity"}},
        {"progressive_poisson", "", {"positions", "level_property", "rank_property", "splat_radius_property", "prefix_visible_property", "dimension", "grid_width", "max_levels", "hash_load_factor", "radius_alpha", "randomize_grid_origin", "grid_origin_seed", "shuffle_within_levels", "shuffle_seed", "prefix_count", "channel", "backend", "auto_run_on_edit", "debounce_seconds"}},
        {"parameterization", "", {"positions", "texcoords", "corner_texcoords_to_retire", "strategy", "lscm", "harmonic", "bff", "view", "atlas"}},
        {"parameterization", "/view", {"render_mode", "background_mode", "show_distortion_heatmap", "split_enabled", "atlas_on_left", "split_ratio"}},
        {"parameterization", "/atlas", {"method", "distortion", "guide", "region_count", "resolution", "padding", "texels_per_unit", "max_conformal_distortion", "max_area_distortion", "max_charts", "max_iterations", "allow_xatlas_fallback"}},
        {"parameterization", "/lscm", {"auto_pins", "pin_vertex_0", "pin_vertex_1", "pin_uv_0", "pin_uv_1", "solver_tolerance", "max_solver_iterations"}},
        {"parameterization", "/harmonic", {"boundary", "arc_length_spacing", "clamp_non_convex_weights", "pinned_vertices", "pinned_uvs"}},
        {"parameterization", "/bff", {"mode", "boundary_data", "angle_sum_tolerance", "degeneracy_tolerance"}},
    };
    for (const auto& row : rows)
    {
        SCOPED_TRACE(std::string(row.Section) + row.Path);
        const auto it = std::ranges::find_if(registry.Entries(), [&](const auto& r) {
            return r.DefaultSection.Name == std::string("sandbox.") + row.Section;
        });
        ASSERT_NE(it, registry.Entries().end());
        const Json reference = Json::parse(it->DefaultSection.PayloadJson);
        const Json::json_pointer path{row.Path};
        std::set<std::string> expected(row.Keys.begin(), row.Keys.end()), actual;
        for (const auto& [key, value] : reference.at(path).items())
            if (!(row.Section == std::string_view("point_cloud_consolidation") &&
                  key.starts_with("initial_") && key != "initial_backend")) actual.insert(key);
        EXPECT_EQ(actual, expected);
        const auto accepted = it->Validate(reference.dump(), reference.dump(), "keys");
        ASSERT_EQ(accepted.State, Config::EngineConfigState::Valid);
        EXPECT_TRUE(accepted.Diagnostics.empty());
        Json payload = Json::object(); payload[path]["foreign"] = true;
        const auto result = it->Validate(payload.dump(), reference.dump(), "keys");
        ASSERT_EQ(result.State, Config::EngineConfigState::FallbackApplied);
        ASSERT_EQ(result.Diagnostics.size(), 1u);
        EXPECT_EQ(result.Diagnostics[0].Code, Config::EngineConfigDiagnosticCode::UnknownField);
        EXPECT_EQ(Json::parse(result.CanonicalPayloadJson), reference);
    }
    const auto it = std::ranges::find_if(registry.Entries(), [](const auto& r) {
        return r.DefaultSection.Name == "sandbox.clustering";
    });
    ASSERT_NE(it, registry.Entries().end());
    const Json properties{{"positions", {{"domain", "PointCloudPoint"}, {"kind", 8}, {"name", "point_input"}}},
                          {"labels", {{"domain", "PointCloudPoint"}, {"kind", 3}, {"name", "point_labels"}}},
                          {"colors", {{"domain", "PointCloudPoint"}, {"kind", 9}, {"name", "point_colors"}}},
                          {"scalar_labels", nullptr}};
    const auto reference = it->Validate(Json{{"properties", properties}}.dump(), it->DefaultSection.PayloadJson, "keys");
    ASSERT_EQ(reference.State, Config::EngineConfigState::Valid);
    Json withUnknown = properties; withUnknown["foreign"] = 1;
    const auto silent = it->Validate(Json{{"properties", withUnknown}}.dump(), reference.CanonicalPayloadJson, "keys");
    EXPECT_EQ(silent.State, Config::EngineConfigState::Valid);
    EXPECT_TRUE(silent.Diagnostics.empty());
    EXPECT_EQ(silent.CanonicalPayloadJson, reference.CanonicalPayloadJson);
    for (const auto* field : {"positions", "labels", "colors", "scalar_labels"})
    {
        Json empty = properties;
        if (field == std::string_view("scalar_labels"))
            empty[field] = {{"domain", "PointCloudPoint"}, {"kind", 5}, {"name", ""}};
        else empty[field]["name"] = "";
        const auto rejected = it->Validate(Json{{"properties", empty}}.dump(), reference.CanonicalPayloadJson, "empty-name");
        EXPECT_FALSE(rejected.Usable()) << field;
    }
}

namespace
{
    struct FallbackBindingRow
    {
        const char* Section;
        const char* Path;
        std::initializer_list<unsigned> Kinds;
        std::initializer_list<const char*> Domains;
    };
    const FallbackBindingRow kFallbackBindings[]{
        {"clustering", "/properties/positions", {8u}, {"MeshVertex", "MeshEdge", "MeshHalfedge", "MeshFace", "GraphNode", "GraphHalfedge", "GraphEdge", "PointCloudPoint"}},
        {"clustering", "/properties/labels", {1u, 2u, 3u, 4u, 5u, 6u}, {"MeshVertex", "MeshEdge", "MeshHalfedge", "MeshFace", "GraphNode", "GraphHalfedge", "GraphEdge", "PointCloudPoint"}},
        {"clustering", "/properties/colors", {9u}, {"MeshVertex", "MeshEdge", "MeshHalfedge", "MeshFace", "GraphNode", "GraphHalfedge", "GraphEdge", "PointCloudPoint"}},
        {"clustering", "/properties/scalar_labels", {1u, 2u, 3u, 4u, 5u, 6u}, {"MeshVertex", "MeshEdge", "MeshHalfedge", "MeshFace", "GraphNode", "GraphHalfedge", "GraphEdge", "PointCloudPoint"}},
        {"curvature_segmentation", "/positions", {8u}, {"MeshVertex"}},
        {"curvature_segmentation", "/components", {1u, 2u, 3u, 4u, 5u, 6u}, {"MeshFace"}},
        {"curvature_segmentation", "/regions", {1u, 2u, 3u, 4u, 5u, 6u}, {"MeshFace"}},
        {"curvature_segmentation", "/region_colors", {9u}, {"MeshFace"}},
        {"curvature_segmentation", "/boundaries", {1u, 2u, 3u, 4u, 5u, 6u}, {"MeshEdge"}},
        {"curvature_segmentation", "/boundary_colors", {9u}, {"MeshEdge"}},
        {"curvature_segmentation", "/hard_features", {1u, 2u, 3u, 4u, 5u, 6u}, {"MeshEdge"}},
        {"curvature_segmentation", "/feature_confidence", {1u, 2u, 3u, 4u, 5u, 6u}, {"MeshEdge"}},
        {"curvature_segmentation", "/boundary_roles", {1u, 2u, 3u, 4u, 5u, 6u}, {"MeshEdge"}},
        {"curvature_segmentation", "/feature_colors", {9u}, {"MeshEdge"}},
        {"curvature_segmentation", "/features/0", {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u}, {"MeshVertex", "MeshFace"}},
        {"progressive_poisson", "/positions", {8u}, {"Unknown", "MeshVertex", "MeshEdge", "MeshHalfedge", "MeshFace", "GraphNode", "GraphHalfedge", "GraphEdge", "PointCloudPoint"}},
        {"progressive_poisson", "/level_property", {1u, 2u, 3u, 4u, 5u, 6u}, {"Unknown", "MeshVertex", "MeshEdge", "MeshHalfedge", "MeshFace", "GraphNode", "GraphHalfedge", "GraphEdge", "PointCloudPoint"}},
        {"progressive_poisson", "/rank_property", {1u, 2u, 3u, 4u, 5u, 6u}, {"Unknown", "MeshVertex", "MeshEdge", "MeshHalfedge", "MeshFace", "GraphNode", "GraphHalfedge", "GraphEdge", "PointCloudPoint"}},
        {"progressive_poisson", "/splat_radius_property", {1u, 2u, 3u, 4u, 5u, 6u}, {"Unknown", "MeshVertex", "MeshEdge", "MeshHalfedge", "MeshFace", "GraphNode", "GraphHalfedge", "GraphEdge", "PointCloudPoint"}},
        {"progressive_poisson", "/prefix_visible_property", {1u, 2u, 3u, 4u, 5u, 6u}, {"Unknown", "MeshVertex", "MeshEdge", "MeshHalfedge", "MeshFace", "GraphNode", "GraphHalfedge", "GraphEdge", "PointCloudPoint"}},
        {"parameterization", "/positions", {8u}, {"MeshVertex"}},
        {"parameterization", "/texcoords", {7u}, {"MeshVertex"}},
        {"parameterization", "/corner_texcoords_to_retire", {7u}, {"MeshHalfedge"}},
        {"parameterization", "/atlas/guide", {1u, 2u, 3u, 4u, 5u, 6u}, {"MeshVertex", "MeshFace"}},
    };
    Json BindingProbe(const FallbackBindingRow& row, std::string_view domain, unsigned kind)
    {
        Json payload = Json::object();
        if (row.Section == std::string_view("clustering"))
        {
            const auto companionDomain = domain == "Unknown" || domain == "foreign" ? "PointCloudPoint" : domain;
            payload["properties"] = {
                {"positions", {{"domain", companionDomain}, {"kind", 8u}, {"name", "input"}}},
                {"labels", {{"domain", companionDomain}, {"kind", 3u}, {"name", "labels"}}},
                {"colors", {{"domain", companionDomain}, {"kind", 9u}, {"name", "colors"}}},
                {"scalar_labels", nullptr}};
        }
        if (row.Section == std::string_view("progressive_poisson") && domain != "foreign")
            payload["positions"] = {{"domain", domain}, {"kind", 8u}, {"name", "input"}};
        payload[Json::json_pointer{row.Path}] = {{"domain", domain}, {"kind", kind}, {"name", "probe_name"}};
        return payload;
    }
}

TEST(EngineConfigSectionSchemas, FallbackOwnerBindingAcceptanceRemainsUnchanged)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    for (const auto& row : kFallbackBindings)
    {
        const auto it = std::ranges::find_if(registry.Entries(), [&](const auto& r) {
            return r.DefaultSection.Name == std::string("sandbox.") + row.Section;
        });
        ASSERT_NE(it, registry.Entries().end());
        for (const auto* domain : {"Unknown", "MeshVertex", "MeshEdge", "MeshHalfedge", "MeshFace",
                                  "GraphNode", "GraphHalfedge", "GraphEdge", "PointCloudPoint", "foreign"})
            for (unsigned kind = 0; kind <= 10; ++kind)
            {
                SCOPED_TRACE(std::string(row.Section) + row.Path + " " + domain + " " + std::to_string(kind));
                const Json payload = BindingProbe(row, domain, kind);
                const bool expected = std::ranges::find(row.Domains, std::string_view(domain)) != row.Domains.end() &&
                                      std::ranges::find(row.Kinds, kind) != row.Kinds.end();
                const auto result = it->Validate(payload.dump(), it->DefaultSection.PayloadJson, "bindings");
                EXPECT_EQ(result.State == Config::EngineConfigState::Valid, expected) << payload.dump();
                if (expected)
                {
                    EXPECT_TRUE(result.Diagnostics.empty());
                    EXPECT_EQ(Json::parse(result.CanonicalPayloadJson).at(Json::json_pointer{row.Path}),
                              payload.at(Json::json_pointer{row.Path}));
                }
                else EXPECT_FALSE(result.Usable());
            }
    }
}

TEST(EngineConfigSectionSchemas, FallbackBindingSchemasDescribeAcceptedKindsAndDomains)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    for (const auto& row : kFallbackBindings)
    {
        SCOPED_TRACE(std::string(row.Section) + row.Path);
        const auto it = std::ranges::find_if(registry.Entries(), [&](const auto& r) {
            return r.DefaultSection.Name == std::string("sandbox.") + row.Section;
        });
        ASSERT_NE(it, registry.Entries().end());
        ASSERT_FALSE(it->SchemaJson.empty());
        Json property = Json::parse(it->SchemaJson);
        std::string path = row.Path;
        for (std::size_t first = 1; first < path.size();)
        {
            const auto next = path.find('/', first);
            const auto part = path.substr(first, next == std::string::npos ? next : next - first);
            property = part == "0" ? property.at("items") : property.at("properties").at(part);
            if (next == std::string::npos) break;
            first = next + 1;
        }
        const auto& kinds = property.at("properties").at("kind");
        EXPECT_EQ(kinds.at("type"), "integer");
        EXPECT_EQ(kinds.at("enum"), Json(row.Kinds));
        EXPECT_EQ(property.at("properties").at("domain").at("enum"), Json(row.Domains));
    }
}

TEST(EngineConfigSectionSchemas, FallbackGroupsPreserveNondefaultReferenceAndClampOutcomes)
{
    const auto registry = Extrinsic::Sandbox::CreateSandboxConfigSectionRegistry();
    const auto parameterization = std::ranges::find_if(registry.Entries(), [](const auto& r) {
        return r.DefaultSection.Name == "sandbox.parameterization";
    });
    ASSERT_NE(parameterization, registry.Entries().end());
    const Json authored{{"lscm", {{"auto_pins", false}, {"pin_vertex_0", 10}, {"pin_vertex_1", 11}, {"solver_tolerance", 1.e-7}}},
                        {"harmonic", {{"pinned_vertices", {5}}, {"pinned_uvs", {{.25, .75}}}}},
                        {"bff", {{"mode", "target_lengths"}, {"boundary_data", {2., 3., 4.}}}}};
    const auto validated = parameterization->Validate(authored.dump(), parameterization->DefaultSection.PayloadJson, "reference");
    ASSERT_EQ(validated.State, Config::EngineConfigState::Valid);
    const Json reference = Json::parse(validated.CanonicalPayloadJson);
    const std::pair<const char*, Json> groups[]{
        {"lscm", {{"pin_vertex_0", 11}, {"solver_tolerance", 1.e-6}}},
        {"harmonic", {{"pinned_vertices", {8, 9}}}},
        {"bff", {{"mode", "target_angles"}, {"boundary_data", {1.}}}},
    };
    for (const auto& [group, bad] : groups)
    {
        const auto result = parameterization->Validate(Json{{group, bad}}.dump(), reference.dump(), "groups");
        ASSERT_EQ(result.State, Config::EngineConfigState::FallbackApplied) << group;
        ASSERT_EQ(result.Diagnostics.size(), 1u);
        EXPECT_EQ(result.Diagnostics.front().Subject, std::string("groups.") + group);
        EXPECT_EQ(Json::parse(result.CanonicalPayloadJson), reference);
    }
    const auto consolidation = std::ranges::find_if(registry.Entries(), [](const auto& r) {
        return r.DefaultSection.Name == "sandbox.point_cloud_consolidation";
    });
    ASSERT_NE(consolidation, registry.Entries().end());
    const auto configured = consolidation->Validate(
        R"({"strategy":"ear","max_iterations":12,"normal_refinement_rounds":7,"initial_seed":17})",
        consolidation->DefaultSection.PayloadJson, "reference");
    ASSERT_EQ(configured.State, Config::EngineConfigState::Valid);
    const Json consolidationReference = Json::parse(configured.CanonicalPayloadJson);
    const auto initial = consolidation->Validate(R"({"initial_method":3,"initial_eta":1.0,"initial_candidate_cap":8})",
        configured.CanonicalPayloadJson, "groups");
    ASSERT_EQ(initial.State, Config::EngineConfigState::FallbackApplied);
    ASSERT_EQ(initial.Diagnostics.size(), 1u);
    EXPECT_EQ(initial.Diagnostics.front().Subject, "groups.initial_method");
    EXPECT_EQ(Json::parse(initial.CanonicalPayloadJson), consolidationReference);
    const auto clamp = consolidation->Validate(R"({"max_iterations":2,"normal_refinement_rounds":9})",
        configured.CanonicalPayloadJson, "groups");
    ASSERT_EQ(clamp.State, Config::EngineConfigState::FallbackApplied);
    Json clamped = consolidationReference; clamped["max_iterations"] = 2; clamped["normal_refinement_rounds"] = 2;
    EXPECT_EQ(Json::parse(clamp.CanonicalPayloadJson), clamped);
    ASSERT_EQ(clamp.Diagnostics.size(), 1u);
    EXPECT_EQ(clamp.Diagnostics.front().Subject, "groups.normal_refinement_rounds");
    const auto segmentation = std::ranges::find_if(registry.Entries(), [](const auto& r) {
        return r.DefaultSection.Name == "sandbox.curvature_segmentation";
    });
    ASSERT_NE(segmentation, registry.Entries().end());
    const auto bounds = segmentation->Validate(R"({"automatic_min_components":3,"automatic_max_components":7})",
        segmentation->DefaultSection.PayloadJson, "reference");
    ASSERT_EQ(bounds.State, Config::EngineConfigState::Valid);
    const auto reversed = segmentation->Validate(R"({"automatic_min_components":9,"automatic_max_components":2})",
        bounds.CanonicalPayloadJson, "groups");
    ASSERT_EQ(reversed.State, Config::EngineConfigState::FallbackApplied);
    EXPECT_EQ(reversed.CanonicalPayloadJson, bounds.CanonicalPayloadJson);
    ASSERT_EQ(reversed.Diagnostics.size(), 1u);
    EXPECT_EQ(reversed.Diagnostics.front().Subject, "groups.automatic_max_components");
}
