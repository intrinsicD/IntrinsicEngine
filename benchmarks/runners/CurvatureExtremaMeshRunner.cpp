// Exports CPU-reference extremum curves with exact source-edge provenance for
// local inspection. The OBJ remains untouched and no region partition is made.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <glm/glm.hpp>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <system_error>
#include <vector>
import Extrinsic.Core.Error;
import Geometry;
import Geometry.HalfedgeMesh.CurvatureExtrema;
namespace C = Geometry::CurvatureExtrema;
using Json = nlohmann::json;
int main(int argc, char** argv)
{
    if (argc < 3 || argc > 4)
    {
        std::cerr << "usage: IntrinsicCurvatureExtremaMesh input.obj output.json "
                     "[params.json]\n";
        return 2;
    }
    std::error_code outputError;
    if (std::filesystem::exists(argv[2], outputError) || outputError)
    {
        std::cerr << "Output already exists or cannot be checked; choose a fresh path\n";
        return 2;
    }
    C::Params params;
    Json configuration = Json::object();
    if (argc == 4)
    {
        std::ifstream input(argv[3]);
        if (!input)
        {
            std::cerr << "Cannot read parameter file\n";
            return 2;
        }
        bool duplicate = false;
        std::set<std::string> keys;
        configuration = Json::parse(
            input,
            [&](int, Json::parse_event_t event, Json& parsed)
            {
                if (event == Json::parse_event_t::key &&
                    !keys.insert(parsed.get<std::string>()).second)
                    duplicate = true;
                return true;
            },
            false);
        if (duplicate)
        {
            std::cerr << "Duplicate parameter key\n";
            return 2;
        }
        if (!configuration.is_object())
        {
            std::cerr << "Invalid parameter object\n";
            return 2;
        }
        const std::vector<std::string> allowed = {"radius_ratio",       "scale_factors",
                                                  "minimum_strength",   "minimum_sharpness",
                                                  "minimum_anisotropy", "hard_dihedral_degrees",
                                                  "maximum_neighbors",  "maximum_work_items"};
        for (auto it = configuration.begin(); it != configuration.end(); ++it)
            if (std::find(allowed.begin(), allowed.end(), it.key()) == allowed.end())
            {
                std::cerr << "Unknown parameter: " << it.key() << '\n';
                return 2;
            }
        auto number = [&](const char* key, double& value)
        {
            if (!configuration.contains(key))
                return true;
            if (!configuration[key].is_number())
                return false;
            value = configuration[key].get<double>();
            return std::isfinite(value);
        };
        if (!number("radius_ratio", params.RadiusRatio) ||
            !number("minimum_strength", params.MinimumStrength) ||
            !number("minimum_sharpness", params.MinimumSharpness) ||
            !number("minimum_anisotropy", params.MinimumAnisotropy) ||
            !number("hard_dihedral_degrees", params.HardDihedralDegrees))
        {
            std::cerr << "Parameters require finite numbers\n";
            return 2;
        }
        if (configuration.contains("scale_factors"))
        {
            const auto& factors = configuration["scale_factors"];
            if (!factors.is_array() || factors.size() != 3)
                return 2;
            for (int i = 0; i < 3; ++i)
            {
                if (!factors[i].is_number())
                    return 2;
                params.ScaleFactors[i] = factors[i].get<double>();
            }
        }
        if (configuration.contains("maximum_neighbors"))
        {
            const auto& value = configuration["maximum_neighbors"];
            if (!value.is_number_unsigned() ||
                value.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
                return 2;
            params.MaximumNeighbors = value.get<std::uint32_t>();
        }
        if (configuration.contains("maximum_work_items"))
        {
            const auto& value = configuration["maximum_work_items"];
            if (!value.is_number_unsigned())
                return 2;
            params.MaximumWorkItems = value.get<std::uint64_t>();
        }
    }
    configuration = {{"radius_ratio", params.RadiusRatio},
                     {"scale_factors", params.ScaleFactors},
                     {"minimum_strength", params.MinimumStrength},
                     {"minimum_sharpness", params.MinimumSharpness},
                     {"minimum_anisotropy", params.MinimumAnisotropy},
                     {"hard_dihedral_degrees", params.HardDihedralDegrees},
                     {"maximum_neighbors", params.MaximumNeighbors},
                     {"maximum_work_items", params.MaximumWorkItems}};
    Json report = {{"schema", "intrinsic.curvature-extrema.inspection.v1"},
                   {"implementation", "normal_variation_extrema_reference_v1"},
                   {"input", std::filesystem::absolute(argv[1]).string()},
                   {"parameters", configuration},
                   {"status", "error"}};
    auto write = [&](int code)
    {
        std::ofstream out(argv[2]);
        if (!out)
        {
            std::cerr << "Cannot write output\n";
            return 3;
        }
        out << report.dump() << '\n';
        return out ? code : 3;
    };
    auto source = Geometry::MeshIO::LoadOBJ(argv[1]);
    if (!source)
    {
        report["error"] = Extrinsic::Core::Error::ToString(source.error());
        return write(4);
    }
    auto positions = source->Vertices.Get<glm::vec3>("v:point");
    auto faces = source->Faces.Get<std::vector<std::uint32_t>>("f:vertices");
    Geometry::HalfedgeMesh::Mesh mesh;
    std::vector<Geometry::VertexHandle> vertices;
    for (auto p : positions.Span())
        vertices.push_back(mesh.AddVertex(p));
    for (const auto& f : faces.Vector())
    {
        if (f.size() != 3)
        {
            report["error"] = "non_triangle_face";
            return write(4);
        }
        if (!mesh.AddTriangle(vertices.at(f[0]), vertices.at(f[1]), vertices.at(f[2])))
        {
            report["error"] = "triangle_insertion_rejected";
            return write(4);
        }
    }
    auto result = C::Extract(mesh, params);
    const auto& d = result.Diagnostic;
    report["diagnostics"] = {{"state", C::ToString(d.State)},
                             {"runtime_ms", d.TotalMilliseconds},
                             {"neighborhood_edge_visits", d.EdgeVisits},
                             {"matching_candidate_visits", d.MatchCandidateVisits},
                             {"neighborhood_samples", d.NeighborhoodSamples},
                             {"peak_neighborhood", d.PeakNeighborhood},
                             {"boundary_vertices", d.BoundaryVertices},
                             {"sharp_vertices", d.SharpVertices},
                             {"neighborhood_ms", d.NeighborhoodMilliseconds},
                             {"fields_ms", d.FieldMilliseconds},
                             {"trace_ms", d.TraceMilliseconds},
                             {"matching_ms", d.MatchMilliseconds},
                             {"bounding_box_diagonal", d.BoundingBoxDiagonal}};
    if (!result.Succeeded())
    {
        report["error"] = C::ToString(d.State);
        return write(4);
    }
    report["status"] = "success";
    report["positions"] = Json::array();
    report["triangles"] = Json::array();
    for (auto v : mesh.LiveVertices())
    {
        auto p = mesh.Position(v);
        report["positions"].push_back({p.x, p.y, p.z});
    }
    for (auto f : mesh.LiveFaces())
    {
        std::vector<unsigned> indices;
        for (auto v : mesh.VerticesAroundFace(f))
            indices.push_back(v.Index);
        report["triangles"].push_back(indices);
    }
    report["scales"] = Json::array();
    for (const auto& s : d.Scales)
        report["scales"].push_back({{"radius", s.Radius},
                                    {"supported_vertices", s.SupportedVertices},
                                    {"singular_triangle_evaluations", s.SingularTriangles},
                                    {"plateau_triangle_evaluations", s.PlateauTriangles},
                                    {"segment_counts", s.SegmentCounts}});
    report["points"] = Json::array();
    for (const auto& p : result.Points)
        report["points"].push_back({{"position", {p.Position.x, p.Position.y, p.Position.z}},
                                    {"edge", {p.VertexA, p.VertexB}},
                                    {"fraction", p.Fraction}});
    report["segments"] = Json::array();
    for (const auto& s : result.Segments)
        report["segments"].push_back({{"points", {s.PointA, s.PointB}},
                                      {"face", s.Face},
                                      {"curve", s.Curve},
                                      {"kind", C::ToString(s.Signal)},
                                      {"scale", s.Scale},
                                      {"scale_mask", s.PersistentScaleMask},
                                      {"strength", s.Strength},
                                      {"sharpness", s.Sharpness},
                                      {"confidence", s.Confidence},
                                      {"fit_residual", s.FitResidual}});
    report["curves"] = Json::array();
    for (const auto& c : result.Curves)
        report["curves"].push_back({{"kind", C::ToString(c.Signal)},
                                    {"scale", c.Scale},
                                    {"segments", c.SegmentCount},
                                    {"endpoints", c.Endpoints},
                                    {"junctions", c.Junctions},
                                    {"length", c.Length}});
    std::cout << mesh.VertexCount() << " vertices, " << result.Segments.size() << " segments, "
              << result.Curves.size() << " curves, " << d.TotalMilliseconds << " ms\n";
    return write(0);
}
