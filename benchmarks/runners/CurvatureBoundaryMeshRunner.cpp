// Opt-in local OBJ comparison through public geometry APIs; no dataset is copied.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <sstream>
#include <vector>
#include <sys/resource.h>
#include <glm/glm.hpp>
import Extrinsic.Core.Error;
import Geometry;
import Geometry.HalfedgeMesh.CurvatureSegmentation.Multicut;
import Geometry.HalfedgeMesh.CurvatureSegmentation.Patches;

namespace C = Geometry::CurvatureSegmentation;
int main(int argc, char **argv)
{
    if (argc < 4 || argc > 5)
    {
        std::cerr << "usage: IntrinsicCurvatureBoundaryMesh <input.obj> <output-prefix> "
                     "<multicut|regional|contrast|clean|clean_medium|clean_coarse|curves|"
                     "local> "
                     "[feature-weight]\n";
        return 2;
    }
    const std::string mode = argv[3];
    if (mode != "multicut" && mode != "regional" && mode != "contrast" &&
        mode != "clean" && mode != "clean_medium" && mode != "clean_coarse" &&
        mode != "curves" && mode != "local")
        return 2;
    C::BoundaryPartitionParams params;
    if (mode == "regional")
    {
        params.ModelWeight = 0.03;
        params.BoundaryScale = 0.02;
        params.RegionCost = std::numbers::pi * 0.02 * 0.02;
        params.CurvatureRadiusRatio = 0.02;
        params.HardFeatureExclusionRatio = 0.02;
    }
    const bool clean = mode == "clean" || mode == "clean_medium" ||
                       mode == "clean_coarse" || mode == "curves";
    const double radius = (mode == "clean_medium" || mode == "curves") ? 0.04
                          : mode == "clean_coarse"                     ? 0.08
                                                                       : 0.02;
    if (mode == "contrast" || clean)
    {
        params.FeatureWeight = 4;
        params.FeatureExponent = 3;
        params.BoundaryScale = radius;
        params.RegionCost = std::numbers::pi * radius * radius;
        params.HardFeatureExclusionRatio = radius;
    }
    if (clean)
        params.MinimumRegionArea = std::numbers::pi * radius * radius;
    if (mode == "curves")
        params = C::BoundaryCurveCoverageProfileV1();
    if (argc == 5)
    {
        char *end = nullptr;
        params.FeatureWeight = std::strtod(argv[4], &end);
        if (end == argv[4] || *end != '\0')
            return 2;
    }
    using Clock = std::chrono::steady_clock;
    const auto elapsed = [](auto start)
    { return std::chrono::duration<double, std::milli>(Clock::now() - start).count(); };
    const auto total = Clock::now();
    std::ofstream report(std::string(argv[2]) + ".json");
    if (!report)
        return 3;
    std::string failureDetails = "{}";
    const auto failed = [&](const char *stage, const char *status)
    {
        report << "{\"status\":\"error\",\"stage\":" << std::quoted(stage)
               << ",\"diagnostic\":" << std::quoted(status)
               << ",\"input\":" << std::quoted(argv[1])
               << ",\"diagnostics\":" << failureDetails << "}\n";
        return 4;
    };
    auto source = Geometry::MeshIO::LoadOBJ(argv[1]);
    if (!source)
        return failed("load", Extrinsic::Core::Error::ToString(source.error()).data());
    auto positions = source->Vertices.Get<glm::vec3>("v:point");
    auto faces = source->Faces.Get<std::vector<std::uint32_t>>("f:vertices");
    Geometry::HalfedgeMesh::Mesh mesh;
    std::vector<Geometry::VertexHandle> vertices;
    for (auto p : positions.Span())
        vertices.push_back(mesh.AddVertex(p));
    for (const auto &f : faces.Vector())
    {
        if (f.size() != 3)
            return failed("load", "non_triangle_face");
        if (!mesh.AddTriangle(vertices[f[0]], vertices[f[1]], vertices[f[2]]))
            return failed("load", "triangle_insertion_rejected");
    }
    const double loadMs = elapsed(total);
    const auto curvatureStart = Clock::now();
    auto curvature = Geometry::Curvature::ComputeCurvature(mesh);
    const double curvatureMs = elapsed(curvatureStart);
    const auto featureStart = Clock::now();
    auto evidence =
        C::DetectFeatureEvidence(mesh, curvature.MaxPrincipalCurvatureProperty.Vector(),
                                 curvature.MinPrincipalCurvatureProperty.Vector());
    const double featureMs = elapsed(featureStart);
    if (!evidence.Succeeded())
        return failed("features", C::ToString(evidence.Diagnostics.Status));
    std::vector<std::uint32_t> labels;
    std::vector<std::uint8_t> boundary;
    double solveMs = 0, energy = 0, lowerBound = 0, optimizedEnergy = 0;
    std::uint32_t areaMerges = 0, unmergeableSmall = 0;
    std::size_t attenuationCurves = 0, attenuationEdges = 0;
    std::uint64_t attempted = 0, accepted = 0, contractions = 0;
    std::uint32_t sweeps = 0;
    bool exact = false;
    if (mode != "local")
    {
        auto result = C::PartitionFeatureBoundaries(
            mesh, evidence.View(), params,
            curvature.MaxPrincipalCurvatureProperty.Vector(),
            curvature.MinPrincipalCurvatureProperty.Vector());
        if (!result.Succeeded())
        {
            const auto &d = result.Diagnostics;
            std::ostringstream details;
            details << "{\"attempted_moves\":" << d.AttemptedMoves
                    << ",\"flow_edge_visits\":" << d.FlowEdgeVisits
                    << ",\"accepted_moves\":" << d.AcceptedMoves
                    << ",\"contractions\":" << d.Contractions
                    << ",\"sweeps\":" << d.Sweeps
                    << ",\"partition_ms\":" << d.TotalMilliseconds << "}";
            failureDetails = details.str();
            return failed("partition", C::ToString(d.Status));
        }
        labels = std::move(result.FaceRegions);
        boundary = std::move(result.EdgeBoundaries);
        auto &d = result.Diagnostics;
        solveMs = d.TotalMilliseconds;
        energy = d.FinalEnergy;
        optimizedEnergy = d.OptimizedEnergy;
        areaMerges = d.AreaMerges;
        attenuationCurves = d.AttenuationCurveCount;
        attenuationEdges = d.AttenuationHardEdgeCount;
        unmergeableSmall = d.UnmergeableSmallRegions;
        lowerBound = d.LowerBound;
        attempted = d.AttemptedMoves;
        accepted = d.AcceptedMoves;
        contractions = d.Contractions;
        sweeps = d.Sweeps;
        exact = d.Exact;
    }
    else
    {
        C::CurvaturePatchParams p;
        p.PatchComplexityCost = 0.5;
        auto result = C::SegmentFeatureAlignedPatches(
            mesh, curvature.MaxPrincipalCurvatureProperty.Vector(),
            curvature.MinPrincipalCurvatureProperty.Vector(), evidence.View(), p);
        if (!result.Succeeded())
            return failed("partition", C::ToString(result.Diagnostics.Status));
        labels = std::move(result.FaceRegions);
        boundary = std::move(result.EdgeBoundaries);
        auto &d = result.Diagnostics;
        solveMs = d.Timings.TotalMilliseconds;
        energy = d.FinalEnergy;
        optimizedEnergy = energy;
    }
    const auto count = *std::max_element(labels.begin(), labels.end()) + 1;
    std::vector<std::size_t> sizes(count);
    std::vector<double> areas(count);
    std::uint64_t hardMisses = 0, unassigned = 0, boundaryCount = 0, hard = 0, soft = 0,
                  closure = 0;
    double totalArea = 0, featureLength = 0, retainedLength = 0, closureLength = 0,
           boundaryLength = 0;
    for (auto f : mesh.LiveFaces())
    {
        if (labels[f.Index] >= count)
        {
            ++unassigned;
            continue;
        }
        ++sizes[labels[f.Index]];
        std::vector<glm::dvec3> p;
        for (auto v : mesh.VerticesAroundFace(f))
            p.push_back(glm::dvec3(mesh.Position(v)));
        double area = 0.5 * glm::length(glm::cross(p[1] - p[0], p[2] - p[0]));
        areas[labels[f.Index]] += area;
        totalArea += area;
    }
    std::ofstream geometryFile(std::string(argv[2]) + ".geometry.json");
    if (!geometryFile)
        return failed("output", "open_failed");
    geometryFile << std::setprecision(std::numeric_limits<float>::max_digits10)
                 << "{\"positions\":[";
    bool comma = false;
    for (auto v : mesh.LiveVertices())
    {
        const auto p = mesh.Position(v);
        if (comma)
            geometryFile << ',';
        comma = true;
        geometryFile << '[' << p.x << ',' << p.y << ',' << p.z << ']';
    }
    geometryFile << "],\"triangles\":[";
    comma = false;
    for (auto f : mesh.LiveFaces())
    {
        if (comma)
            geometryFile << ',';
        comma = true;
        geometryFile << '[';
        bool vertexComma = false;
        for (auto v : mesh.VerticesAroundFace(f))
        {
            if (vertexComma)
                geometryFile << ',';
            vertexComma = true;
            geometryFile << v.Index;
        }
        geometryFile << ']';
    }
    geometryFile << "]}\n";
    geometryFile.close();
    if (!geometryFile)
        return failed("output", "write_failed");
    std::ofstream labelFile(std::string(argv[2]) + ".labels"),
        edgeFile(std::string(argv[2]) + ".edges");
    if (!labelFile || !edgeFile)
        return failed("output", "open_failed");
    for (auto f : mesh.LiveFaces())
        labelFile << labels[f.Index] << '\n';
    for (auto e : mesh.LiveEdges())
    {
        auto h = mesh.Halfedge(e, 0);
        auto a = mesh.FromVertex(h), b = mesh.ToVertex(h);
        auto f0 = mesh.Face(h), f1 = mesh.Face(mesh.Halfedge(e, 1));
        if (!f0.IsValid() || !f1.IsValid())
            continue;
        const bool hflag = evidence.HardEdgeMask[e.Index] != 0,
                   sflag = evidence.SoftEdgeConfidence[e.Index] > 0;
        const double length =
            glm::length(glm::dvec3(mesh.Position(a)) - glm::dvec3(mesh.Position(b)));
        if (hflag || sflag)
            featureLength += length;
        if (hflag && !boundary[e.Index])
            ++hardMisses;
        if (boundary[e.Index])
        {
            ++boundaryCount;
            boundaryLength += length;
            if (hflag)
                ++hard;
            else if (sflag)
                ++soft;
            else
                ++closure;
            if (hflag || sflag)
                retainedLength += length;
            else
                closureLength += length;
        }
        edgeFile << a.Index << ' ' << b.Index << ' ' << int(hflag) << ' '
                 << evidence.SoftEdgeConfidence[e.Index] << ' ' << int(boundary[e.Index])
                 << '\n';
    }
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const bool valid = hardMisses == 0 && unassigned == 0;
    report
        << std::setprecision(17)
        << "{\n\"status\":" << std::quoted(valid ? "passed" : "failed")
        << ",\n\"benchmark_id\":"
        << std::quoted(std::string("geometry.curvature_segmentation.") +
                       (mode == "multicut" ? "boundary" : mode) + "_mesh.local_cohort")
        << ","
        << "\n\"method\":\"geometry.curvature_segmentation\",\n\"backend\":\"cpu_"
           "reference\","
        << "\n\"dataset\":\"local.method_040.mesh_cohort.v1\",\n\"commit\":\"local-dev\","
        << "\n\"metrics\":{\"runtime_ms\":" << elapsed(total)
        << ",\"memory_peak_bytes\":" << std::uint64_t(usage.ru_maxrss) * 1024
        << ",\"quality_error_l2\":" << double(unassigned) / mesh.FaceCount()
        << ",\"quality_error_linf\":" << hardMisses << ",\"population_count\":" << count
        << "},\n\"diagnostics\":{\"input\":" << std::quoted(argv[1])
        << ",\"implementation\":"
        << std::quoted(
               mode == "multicut"
                   ? "boundary_multicut_isotropic_v2"
                   : (mode == "regional"
                          ? "curvature_region_scale_v2"
                          : (mode == "curves"     ? "boundary_feature_curve_coverage_v1"
                             : clean              ? "boundary_feature_area_cleanup_v1"
                             : mode == "contrast" ? "boundary_feature_contrast_v1"
                                                  : "method_039_local_patch_unadopted")))
        << ",\"feature_weight\":" << params.FeatureWeight
        << ",\"feature_exponent\":" << params.FeatureExponent
        << ",\"model_weight\":" << params.ModelWeight
        << ",\"boundary_scale\":" << params.BoundaryScale
        << ",\"region_cost\":" << params.RegionCost
        << ",\"hard_feature_exclusion_ratio\":" << params.HardFeatureExclusionRatio
        << ",\"faces\":" << mesh.FaceCount() << ",\"vertices\":" << mesh.VertexCount()
        << ",\"load_ms\":" << loadMs << ",\"curvature_ms\":" << curvatureMs
        << ",\"feature_ms\":" << featureMs << ",\"partition_ms\":" << solveMs
        << ",\"minimum_exclusion_curve_length\":" << params.MinimumExclusionCurveLength
        << ",\"minimum_region_area\":" << params.MinimumRegionArea
        << ",\"attenuation_curves\":" << attenuationCurves
        << ",\"attenuation_hard_edges\":" << attenuationEdges
        << ",\"area_merges\":" << areaMerges
        << ",\"unmergeable_small_regions\":" << unmergeableSmall
        << ",\"optimized_energy\":" << optimizedEnergy << ",\"energy\":" << energy
        << ",\"lower_bound\":";
    if (mode == "local")
        report << "null";
    else
        report << lowerBound;
    report << ",\"exact\":" << (exact ? "true" : "false")
           << ",\"attempted_moves\":" << attempted << ",\"accepted_moves\":" << accepted
           << ",\"contractions\":" << contractions << ",\"sweeps\":" << sweeps
           << ",\"source_hard\":" << evidence.Diagnostics.HardFeatureEdgeCount
           << ",\"source_soft\":" << evidence.Diagnostics.RetainedSoftEdgeCount
           << ",\"boundaries\":" << boundaryCount << ",\"hard\":" << hard
           << ",\"soft\":" << soft << ",\"closure\":" << closure
           << ",\"feature_length_recall\":"
           << (featureLength > 0 ? retainedLength / featureLength : 1)
           << ",\"unsupported_length_fraction\":"
           << (boundaryLength > 0 ? closureLength / boundaryLength : 0)
           << ",\"region_sizes\":[";
    for (std::size_t i = 0; i < sizes.size(); ++i)
    {
        if (i)
            report << ',';
        report << sizes[i];
    }
    report << "],\"region_area_fractions\":[";
    for (std::size_t i = 0; i < areas.size(); ++i)
    {
        if (i)
            report << ',';
        report << areas[i] / totalArea;
    }
    report << "]}}\n";
    report.flush();
    labelFile.flush();
    edgeFile.flush();
    return report && labelFile && edgeFile && valid ? 0 : 5;
}
