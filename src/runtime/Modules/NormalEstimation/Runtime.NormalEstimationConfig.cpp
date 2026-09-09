module;
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
module Extrinsic.Runtime.NormalEstimationConfig;
namespace Extrinsic::Runtime
{
    namespace
    {
        using Json = nlohmann::json;
        Json Ref(const GeometryPropertyRef &r)
        {
            return {{"domain",
                     r.Domain <= GeometryElementDomain::PointCloudPoint ? ToString(r.Domain) : "invalid"},
                    {"name", r.Name},
                    {"kind", r.ValueKind == Geometry::PropertyValueKind::Vec3 ? "vec3" : "invalid"}};
        }
        NormalEstimationConfig Parse(const Json &d)
        {
            NormalEstimationConfig c;
            c.StableEntityId = d.at("entity");
            for (unsigned i = 0; i < 3; ++i)
                if (d.at("method") == ToString(NormalEstimationMethod(i)))
                    c.Method = NormalEstimationMethod(i);
            c.Backend = d.at("backend") == "cpu_lbvh" ? NormalEstimationBackend::CpuLBVH
                                                      : NormalEstimationBackend::CpuKDTree;
            auto readRef = [&](const char *key, GeometryPropertyRef &r) {
                r.Name = d.at(key).at("name");
                for (unsigned i = 0; i <= unsigned(GeometryElementDomain::PointCloudPoint); ++i)
                    if (d.at(key).at("domain") == ToString(GeometryElementDomain(i)))
                        r.Domain = GeometryElementDomain(i);
            };
            readRef("positions", c.Positions);
            readRef("output", c.Output);
            c.KNeighbors = d.at("k_neighbors");
            c.MinimumNeighbors = d.at("minimum_neighbors");
            c.UseRadiusSearch = d.at("use_radius");
            c.Radius = d.at("radius");
            c.Orientation =
                Geometry::PointCloud::Normals::OrientationMode(d.at("orientation").get<unsigned>());
            for (unsigned i = 0; i < 3; ++i)
                c.FallbackNormal[i] = d.at("fallback_normal").at(i);
            c.DegenerateNormalLengthEpsilon = d.at("degenerate_epsilon");
            c.CollinearEigenvalueRatioEpsilon = d.at("collinear_epsilon");
            c.Weighting =
                Geometry::HalfedgeMesh::VertexNormals::AveragingMode(d.at("weighting").get<unsigned>());
            c.OrientTowardFallback = d.at("orient_toward_fallback");
            return c;
        }
        Core::Config::EngineConfigSection Section(const NormalEstimationConfig &c)
        {
            return {.Name = std::string{kNormalEstimationConfigSectionName},
                    .SchemaId = std::string{kNormalEstimationConfigSectionSchemaId},
                    .SchemaVersion = 1,
                    .PayloadJson = SerializeNormalEstimationConfig(c)};
        }
    } // namespace
    const char *ToString(NormalEstimationMethod m) noexcept
    {
        switch (m)
        {
        case NormalEstimationMethod::PointSetPCA:
            return "point_set_pca";
        case NormalEstimationMethod::MeshFaceWeighted:
            return "mesh_face_weighted";
        case NormalEstimationMethod::GraphNeighborhood:
            return "graph_neighborhood";
        }
        return "invalid";
    }
    const char *ToString(NormalEstimationBackend b) noexcept
    {
        switch (b)
        {
        case NormalEstimationBackend::CpuKDTree:
            return "cpu_kdtree";
        case NormalEstimationBackend::CpuLBVH:
            return "cpu_lbvh";
        }
        return "invalid";
    }
    std::string SerializeNormalEstimationConfig(const NormalEstimationConfig &c)
    {
        return Json{{"entity", c.StableEntityId},
                    {"method", ToString(c.Method)},
                    {"backend", ToString(c.Backend)},
                    {"positions", Ref(c.Positions)},
                    {"output", Ref(c.Output)},
                    {"k_neighbors", c.KNeighbors},
                    {"minimum_neighbors", c.MinimumNeighbors},
                    {"use_radius", c.UseRadiusSearch},
                    {"radius", c.Radius},
                    {"orientation", unsigned(c.Orientation)},
                    {"fallback_normal", {c.FallbackNormal.x, c.FallbackNormal.y, c.FallbackNormal.z}},
                    {"degenerate_epsilon", c.DegenerateNormalLengthEpsilon},
                    {"collinear_epsilon", c.CollinearEigenvalueRatioEpsilon},
                    {"weighting", unsigned(c.Weighting)},
                    {"orient_toward_fallback", c.OrientTowardFallback}}
            .dump();
    }
    Core::Config::EngineConfigSectionValidationResult ValidateNormalEstimationConfigSection(
        std::string_view payload, std::string_view, std::string_view subject)
    {
        using namespace Core::Config;
        EngineConfigSectionValidationResult result;
        auto reject = [&](std::string message) {
            result.Diagnostics.push_back({.Code = EngineConfigDiagnosticCode::InvalidValue,
                                          .Subject = std::string(subject),
                                          .Message = std::move(message)});
            return result;
        };
        auto input = Json::parse(payload, nullptr, false),
             d = Json::parse(SerializeNormalEstimationConfig({}));
        if (!input.is_object())
            return reject("Normal estimation config must be an object.");
        for (auto it = input.begin(); it != input.end(); ++it)
        {
            if (!d.contains(it.key()))
                return reject("Unknown normal field: " + it.key());
            d[it.key()] = it.value();
        }
        for (auto key : {"entity", "k_neighbors", "minimum_neighbors", "orientation", "weighting"})
            if (!d[key].is_number_unsigned() ||
                d[key].get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
                return reject(std::string(key) + " must be an unsigned 32-bit integer.");
        if (d["k_neighbors"] == 0 || d["minimum_neighbors"] == 0 || d["orientation"] > 1 ||
            d["weighting"] > 4)
            return reject("Positive neighborhood sizes, orientation 0..1 and weighting 0..4 are required.");
        if (d["method"] != "point_set_pca" && d["method"] != "mesh_face_weighted" &&
            d["method"] != "graph_neighborhood")
            return reject("Unknown normal method.");
        if (d["backend"] != "cpu_kdtree" && d["backend"] != "cpu_lbvh")
            return reject("Normal backend must be cpu_kdtree or cpu_lbvh.");
        if (!d["use_radius"].is_boolean() || !d["orient_toward_fallback"].is_boolean())
            return reject("Normal toggles must be boolean.");
        for (auto key : {"radius", "degenerate_epsilon", "collinear_epsilon"})
            if (!d[key].is_number() || !std::isfinite(d[key].get<double>()))
                return reject(std::string(key) + " must be finite.");
        if (d["radius"].get<double>() < 0 || d["radius"].get<double>() > std::numeric_limits<float>::max() ||
            (d["use_radius"].get<bool>() && d["radius"] <= 0) || d["degenerate_epsilon"] <= 0 ||
            d["collinear_epsilon"] <= 0)
            return reject("Normal radii and numerical epsilons are outside their supported range.");
        auto &fallback = d["fallback_normal"];
        if (!fallback.is_array() || fallback.size() != 3)
            return reject("Fallback normal must have three finite float coordinates.");
        for (auto &x : fallback)
            if (!x.is_number() || !std::isfinite(x.get<double>()) ||
                std::abs(x.get<double>()) > std::numeric_limits<float>::max())
                return reject("Fallback normal coordinates must be finite floats.");
        for (auto key : {"positions", "output"})
        {
            const auto &r = d[key];
            if (!r.is_object() || r.size() != 3 || !r.contains("domain") || !r.contains("name") ||
                !r.contains("kind") || r["kind"] != "vec3" || !r["name"].is_string() ||
                r["name"].get<std::string>().empty())
                return reject(std::string(key) + " requires domain, nonempty name and kind=vec3.");
            bool valid = false;
            for (unsigned i = 0; i <= unsigned(GeometryElementDomain::PointCloudPoint); ++i)
                valid |= r["domain"] == ToString(GeometryElementDomain(i));
            if (!valid)
                return reject(std::string(key) + " has an unknown element domain.");
        }
        if (d["positions"]["domain"] != d["output"]["domain"] ||
            d["positions"]["name"] == d["output"]["name"])
            return reject("Normals must use a distinct output property on the input domain.");
        result.State = EngineConfigState::Valid;
        result.CanonicalPayloadJson = SerializeNormalEstimationConfig(Parse(d));
        result.ParsedFieldCount = input.size();
        return result;
    }
    std::optional<NormalEstimationConfig> GetNormalEstimationConfig(const Core::Config::EngineConfig &c)
    {
        const auto *s =
            Core::Config::FindEngineConfigSection(c.AppSections, kNormalEstimationConfigSectionName);
        if (!s || s->SchemaId != kNormalEstimationConfigSectionSchemaId || s->SchemaVersion != 1)
            return {};
        auto v =
            ValidateNormalEstimationConfigSection(s->PayloadJson, {}, kNormalEstimationConfigSectionName);
        if (!v.Usable())
            return {};
        return Parse(Json::parse(v.CanonicalPayloadJson));
    }
    void SetNormalEstimationConfig(Core::Config::EngineConfig &c, const NormalEstimationConfig &v)
    {
        Core::Config::UpsertEngineConfigSection(c.AppSections, Section(v));
    }
    Core::Config::EngineConfigSectionRegistration MakeNormalEstimationConfigSectionRegistration()
    {
        return {.DefaultSection = Section({}), .Validate = ValidateNormalEstimationConfigSection};
    }
} // namespace Extrinsic::Runtime
